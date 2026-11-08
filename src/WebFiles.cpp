#include "AppServices.h"
#include "FatDirectory.h"
#include "FtpStore.h"
#include "Logger.h"
#include "WebValidation.h"
#include "StringUtils.h"
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <SD_MMC.h>

void AppServices::cleanupUploads()
{
    DIR *dir = opendir("/sdcard");
    if (!dir)
        return;
    unsigned removed = 0, failed = 0;
    while (auto *entry = readdir(dir))
    {
        if (!WebValidation::temporaryUpload(entry->d_name) && !FtpStore::temporary(entry->d_name))
            continue;
        char path[40];
        snprintf(path, sizeof(path), "/sdcard/%s", entry->d_name);
        struct stat info{};
        if (stat(path, &info) == 0 && S_ISREG(info.st_mode))
        {
            if (unlink(path) == 0)
                ++removed;
            else
                ++failed;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    closedir(dir);
    LOG_PLAIN_F("WEB Uploadreste von Webportal und FTP entfernt=%u Fehler=%u", removed, failed);
}

AppServices::WebFileResult AppServices::webFile(const WebFileRequest &request)
{
    WebFileResult result;
    if (!m_webFiles || xQueueSend(m_webFiles, &request, 0) != pdPASS)
    {
        result.status = 503;
        return result;
    }
    // Nur der HTTP-Task wartet. Der Worker besitzt seinen kopierten Auftrag bis zum Abschluss.
    xQueueReceive(m_webResults, &result, portMAX_DELAY);
    return result;
}

void AppServices::executeWebFile()
{
    WebFileResult result;
    result.status = 400; // Vorgabe: jeder Zweig, der keinen anderen Status setzt, meldet eine ungültige Anfrage
    const auto &job = m_webWork;
    // Übernimmt Status und Grund eines Auftrags an den Kartenspeicher.
    auto take = [&](const WebFileStore::Result &answer)
    {
        result.status = answer.status;
        result.reason = answer.reason;
    };
    auto run = [&]()
    {
        using K = WebFileKind;
        if (job.kind == K::Abort)
        {
            m_webStore->abort();
            result.status = 200;
            return;
        }
        if (m_fileLease.owner() != FileLease::Owner::Web)
        {
            result.status = 409;
            return;
        }
        if (m_media.load() != SdServiceStatus::Media::Ready)
        {
            result.status = 503;
            return;
        }
        const auto audio = g_audioManager.getStatus();
        if (audio.file && (audio.state == AudioManager::State::Starting || audio.state == AudioManager::State::Streaming || audio.state == AudioManager::State::Paused))
        {
            result.status = 409;
            result.reason = WebFileStore::Reason::Playing;
            return;
        }
        if (job.kind == K::Chunk)
        {
            take(m_webStore->chunk(m_webWork.data, job.count));
            if (result.status == 200)
                m_webUploadAt = millis();
            return;
        }
        if (job.kind == K::Commit)
        {
            take(m_webStore->commit());
            return;
        }
        if (!WebValidation::path(job.path, job.kind == K::List))
            return;
        if (job.kind == K::List)
        {
            // Wie im FTP-Dienst über FATFS: ohne Einträge nach SystemEntries::hidden, zu lange Pfade und ungültige Namen werden gezählt.
            struct Sink
            {
                SdFileEntry *entries; ///< Ziel, FileExplorer::MaxEntries Plätze
                size_t count;         ///< Belegte Plätze
            } sink{m_webEntries, 0};
            auto add = [](void *context, const char *path, const FtpProtocol::Entry &entry)
            {
                auto &s = *static_cast<Sink *>(context);
                if (s.count >= FileExplorer::MaxEntries)
                    return false;
                auto &out = s.entries[s.count++];
                out = {};
                stringutils::safeCopy(out.path, path);
                stringutils::safeCopyUtf8(out.name, entry.name);
                out.directory = entry.directory;
                out.size = static_cast<uint32_t>(entry.size);
                return true;
            };
            uint32_t skipped = 0;
            m_webDirectory->setDrive(cardDrive());
            take(m_webStore->list(job.path, add, &sink, skipped));
            if (result.status == 200)
            {
                result.count = sink.count;
                result.skipped = skipped;
            }
            return;
        }
        if (job.kind == K::TreeCount || job.kind == K::TreeDelete)
        {
            take(m_webStore->treeBegin(job.path, job.kind == K::TreeDelete));
            if (result.status != 200)
                return;
            portENTER_CRITICAL(&m_configLock);
            const uint32_t id = m_webTree.id + 1;
            m_webTree = WebTreeStatus{};
            m_webTree.id = id;
            m_webTree.running = true;
            m_webTree.remove = job.kind == K::TreeDelete;
            stringutils::safeCopy(m_webTree.path, job.path);
            portEXIT_CRITICAL(&m_configLock);
            return;
        }
        if (job.kind == K::Mkdir)
            take(m_webStore->mkdir(job.path));
        else if (job.kind == K::Delete)
            take(m_webStore->remove(job.path));
        else if (job.kind == K::Rename)
            take(m_webStore->rename(job.path, job.target));
        else if (job.kind == K::Begin)
        {
            take(m_webStore->begin(job.path, job.size, job.overwrite));
            if (result.status == 200)
                m_webUploadAt = millis();
        }
    };
    run();
    if (result.status == 200 && (job.kind == WebFileKind::Commit || job.kind == WebFileKind::Mkdir || job.kind == WebFileKind::Delete || job.kind == WebFileKind::Rename))
    {
        const uint64_t used = SD_MMC.usedBytes();
        portENTER_CRITICAL(&m_configLock);
        m_sdUsed = used;
        portEXIT_CRITICAL(&m_configLock);
        m_mediaRevision.fetch_add(1);
    }
    xQueueSend(m_webResults, &result, portMAX_DELAY);
}

AppServices::WebTreeStatus AppServices::webTree()
{
    portENTER_CRITICAL(&m_configLock);
    const WebTreeStatus copy = m_webTree;
    portEXIT_CRITICAL(&m_configLock);
    return copy;
}

void AppServices::stepWebTree()
{
    if (!m_webStore->treeActive())
        return;
    WebFileStore::TreeProgress progress;
    WebFileStore::Result answer{200, WebFileStore::Reason::None};
    // Die Kartensperre schließt SD-Wiedergabe, FTP und andere Webaufträge bis zum Ende aus. Karte und Sperre können trotzdem verloren gehen.
    if (m_fileLease.owner() != FileLease::Owner::Web)
        answer.status = 409;
    else if (m_media.load() != SdServiceStatus::Media::Ready)
        answer.status = 503;
    else
    {
        m_webDirectory->setDrive(cardDrive());
        answer = m_webStore->treeStep(progress);
        if (answer.status == 500)
            m_card.failed();
    }
    const bool finished = progress.done || answer.status != 200;
    if (finished)
        m_webStore->treeClose();
    portENTER_CRITICAL(&m_configLock);
    const bool remove = m_webTree.remove;
    if (answer.status == 200)
    {
        m_webTree.files = progress.files;
        m_webTree.folders = progress.folders;
    }
    else if (progress.failed[0])
    {
        // Nach einem Fehler meldet der Kartenspeicher den Stand bis dahin und den betroffenen Pfad.
        m_webTree.files = progress.files;
        m_webTree.folders = progress.folders;
        stringutils::safeCopy(m_webTree.failed, progress.failed);
    }
    if (finished)
    {
        m_webTree.running = false;
        m_webTree.status = answer.status;
        m_webTree.reason = answer.reason;
    }
    const uint32_t files = m_webTree.files, folders = m_webTree.folders;
    portEXIT_CRITICAL(&m_configLock);
    if (!finished)
        return;
    if (remove && m_media.load() == SdServiceStatus::Media::Ready)
    {
        const uint64_t used = SD_MMC.usedBytes();
        portENTER_CRITICAL(&m_configLock);
        m_sdUsed = used;
        portEXIT_CRITICAL(&m_configLock);
        m_mediaRevision.fetch_add(1);
    }
    // m_webTree.path schreibt nur dieser Task, Lesen ohne Sperre ist hier sicher.
    LOG_PLAIN_F("WEB Ordner %s %s Status=%d Dateien=%lu Ordner=%lu", remove ? "geloescht" : "gezaehlt", m_webTree.path, answer.status, static_cast<unsigned long>(files), static_cast<unsigned long>(folders));
    m_fileLease.release(FileLease::Owner::Web);
}
