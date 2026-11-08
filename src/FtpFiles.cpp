#include "AppServices.h"
#include "FatDirectory.h"
#include "FtpStore.h"
#include "Logger.h"
#include <SD_MMC.h>
#include <esp_heap_caps.h>
#include <esp_random.h>
#include <new>

namespace
{
    using Status = FtpProtocol::Status;
}

/** @brief Kartenzustand des FTP-Dienstes im Dienstworker. */
struct FtpWorker
{
    FatDirectory directory;                           ///< Ordnerleser über FATFS
    FtpStore store{"/sdcard", &directory, esp_random, FtpStore::FtpPrefix}; ///< Dateioperationen unter dem Mountpunkt
};

bool AppServices::beginFtp()
{
    void *memory = heap_caps_calloc(1, sizeof(FtpWorker), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!memory)
        return false;
    m_ftp = new (memory) FtpWorker();
    return true;
}

void AppServices::endFtp()
{
    if (!m_ftp)
        return;
    m_ftp->~FtpWorker();
    heap_caps_free(m_ftp);
    m_ftp = nullptr;
}

void AppServices::closeFtpFiles()
{
    if (m_ftp)
        m_ftp->store.closeAll();
}

void AppServices::ftpFile(FtpProtocol::Job &job)
{
    if (!m_task || !m_ftp)
    {
        job.status = Status::NoCard;
        return;
    }
    m_ftpWaiter = xTaskGetCurrentTaskHandle();
    m_ftpDone.store(false);
    m_ftpJob.store(&job, std::memory_order_release);
    // Die Benachrichtigung weckt sofort, die Frist fängt nur eine verlorene Benachrichtigung ab.
    while (!m_ftpDone.load(std::memory_order_acquire))
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(50));
}

void AppServices::executeFtpFile()
{
    using Kind = FtpProtocol::Kind;
    FtpProtocol::Job *job = m_ftpJob.exchange(nullptr, std::memory_order_acquire);
    if (!job)
        return;
    const auto audio = g_audioManager.getStatus();
    const bool cleanup = job->kind == Kind::Close || job->kind == Kind::ListClose || job->kind == Kind::TreeClose;
    const bool opening = job->kind == Kind::ListOpen || job->kind == Kind::ReadOpen || job->kind == Kind::WriteOpen;
    if (cleanup)
        m_ftp->store.execute(*job);
    else if (m_fileLease.owner() != FileLease::Owner::Ftp || (audio.file && (audio.state == AudioManager::State::Starting || audio.state == AudioManager::State::Streaming || audio.state == AudioManager::State::Paused)))
        job->status = Status::Busy;
    else if (m_media.load() != SdServiceStatus::Media::Ready)
        job->status = Status::NoCard;
    else if (opening && !cardAnswers())
    {
        // Wie beim Sammeln im SD-Player: Eine verlorene Karte erscheint sonst als leerer Ordner oder kurze Datei.
        job->status = Status::NoCard;
        m_card.failed();
    }
    else
    {
        m_ftp->directory.setDrive(cardDrive());
        m_ftp->store.execute(*job);
        if (job->status == Status::Io)
            m_card.failed();
    }
    if ((job->status == Status::Ok && (job->kind == Kind::Commit || job->kind == Kind::Mkdir || job->kind == Kind::Rmdir || job->kind == Kind::RmdirHidden || job->kind == Kind::Delete || job->kind == Kind::Rename)) || (job->kind == Kind::TreeStep && job->status != Status::More && m_media.load() == SdServiceStatus::Media::Ready))
    {
        const uint64_t used = SD_MMC.usedBytes();
        portENTER_CRITICAL(&m_configLock);
        m_sdUsed = used;
        portEXIT_CRITICAL(&m_configLock);
        m_mediaRevision.fetch_add(1);
    }
    m_ftpDone.store(true, std::memory_order_release);
    if (m_ftpWaiter)
        xTaskNotifyGive(m_ftpWaiter);
}

bool AppServices::ftpPassword(char (&out)[FtpProtocol::PasswordCapacity])
{
    portENTER_CRITICAL(&m_configLock);
    memcpy(out, m_ftpPassword, sizeof(out));
    portEXIT_CRITICAL(&m_configLock);
    return out[0] != 0;
}

bool AppServices::requestFtpPassword(bool renew)
{
    if (!m_task)
        return false;
    // Erneuern hat Vorrang vor dem bloßen Sicherstellen.
    uint8_t expected = 0;
    if (renew)
        m_ftpPasswordRequest.store(2);
    else
        m_ftpPasswordRequest.compare_exchange_strong(expected, 1);
    return true;
}

void AppServices::executeFtpPassword(bool renew)
{
    char password[FtpProtocol::PasswordCapacity]{}, stored[FtpProtocol::PasswordCapacity]{};
    const bool present = ftpPassword(password);
    FtpProtocol::wipe(password, sizeof(password));
    if (present && !renew)
        return;
    FtpProtocol::generatePassword(password, esp_random);
    // Erst das zurückgelesene Passwort gilt. Scheitert das Speichern, bleibt das bisherige gültig.
    const bool saved = Tab5Config::saveFtpPassword(password) && Tab5Config::loadFtpPassword(stored) && !strcmp(stored, password);
    if (saved)
    {
        portENTER_CRITICAL(&m_configLock);
        memcpy(m_ftpPassword, password, sizeof(m_ftpPassword));
        portEXIT_CRITICAL(&m_configLock);
        m_ftpPasswordRevision.fetch_add(1);
        LOG_INFO_F("FTP Passwort %s und in rr_tab5/ftp_v1 gespeichert", present ? "erneuert" : "erzeugt");
    }
    else
        LOG_ERROR("FTP Passwort nicht gespeichert, das bisherige gilt weiter.");
    FtpProtocol::wipe(password, sizeof(password));
    FtpProtocol::wipe(stored, sizeof(stored));
}
