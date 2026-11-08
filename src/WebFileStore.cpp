#include "WebFileStore.h"
#include "StringUtils.h"

namespace
{
    using Kind = FtpProtocol::Kind;
    using Status = FtpProtocol::Status;
}

WebFileStore::WebFileStore(const char *root, FtpStore::Directory *directory, uint32_t (*random)()) : m_store(root, directory, random, WebValidation::UploadPrefix)
{
}

Status WebFileStore::run(Kind kind, const char *path, const char *target)
{
    m_job.kind = kind;
    stringutils::safeCopy(m_job.path, path);
    stringutils::safeCopy(m_job.target, target);
    m_store.execute(m_job);
    return m_job.status;
}

WebFileStore::Result WebFileStore::answer(Status status)
{
    switch (status)
    {
    case Status::Ok:
        return {200, Reason::None};
    case Status::NotFound:
        return {404, Reason::None};
    case Status::Exists:
        return {409, Reason::Exists};
    case Status::IsDirectory:
        return {409, Reason::Directory};
    case Status::Invalid:
        return {400, Reason::None};
    case Status::NoSpace:
        return {507, Reason::None};
    default:
        return {500, Reason::None};
    }
}

void WebFileStore::finish()
{
    m_uploading = m_overwrite = false;
    m_expected = m_written = 0;
    m_path[0] = 0;
}

WebFileStore::Result WebFileStore::begin(const char *path, uint32_t size, bool overwrite)
{
    if (m_uploading || size > WebValidation::MaxUpload || !WebValidation::path(path, false))
        return {400, Reason::None};
    const Status existing = run(Kind::Stat, path);
    if (existing == Status::Ok)
    {
        if (m_job.entry.directory)
            return {409, Reason::Directory};
        if (!overwrite)
            return {409, Reason::Exists};
    }
    else if (existing != Status::NotFound)
        return answer(existing);
    const Status opened = run(Kind::WriteOpen, path);
    if (opened != Status::Ok)
        return answer(opened);
    stringutils::safeCopy(m_path, path);
    m_expected = size;
    m_written = 0;
    m_overwrite = overwrite;
    m_uploading = true;
    return {200, Reason::None};
}

WebFileStore::Result WebFileStore::chunk(uint8_t *data, uint32_t count)
{
    if (!m_uploading || !data || !count || count > m_expected - m_written)
        return {400, Reason::None};
    m_job.kind = Kind::Write;
    m_job.data = data;
    m_job.capacity = m_job.count = count;
    m_store.execute(m_job);
    m_job.data = nullptr;
    m_job.capacity = m_job.count = 0;
    if (m_job.status != Status::Ok)
    {
        // Der Kartenspeicher hat die Stagingdatei bereits verworfen.
        finish();
        return answer(m_job.status);
    }
    m_written += count;
    return {200, Reason::None};
}

WebFileStore::Result WebFileStore::commit()
{
    if (!m_uploading)
        return {400, Reason::None};
    if (m_written != m_expected)
    {
        abort();
        return {400, Reason::None};
    }
    // Ohne Erlaubnis wird nichts ersetzt, auch wenn der Name erst während des Uploads entstand.
    if (!m_overwrite && run(Kind::Stat, m_path) == Status::Ok)
    {
        const bool directory = m_job.entry.directory;
        abort();
        return {409, directory ? Reason::Directory : Reason::Exists};
    }
    const Status status = run(Kind::Commit, "");
    finish();
    return answer(status);
}

void WebFileStore::abort()
{
    if (m_uploading)
        run(Kind::Close, "");
    finish();
}

void WebFileStore::closeAll()
{
    m_store.closeAll();
    finish();
}

WebFileStore::Result WebFileStore::list(const char *path, ListSink sink, void *context, uint32_t &skipped)
{
    skipped = 0;
    if (!WebValidation::path(path, true))
        return {400, Reason::None};
    const Status opened = run(Kind::ListOpen, path);
    if (opened != Status::Ok)
        return answer(opened);
    m_job.kind = Kind::ListNext;
    m_job.entries = m_entries;
    m_job.entryCapacity = ListChunk;
    Result result{200, Reason::None};
    for (;;)
    {
        // Der Kartenspeicher liefert nur Einträge, deren vollständiger Pfad gültig ist und in MaxPath passt.
        m_store.execute(m_job);
        skipped = m_job.skipped;
        for (uint16_t i = 0; i < m_job.entryCount && result.status == 200; ++i)
        {
            char full[FtpProtocol::MaxPath + 1];
            snprintf(full, sizeof(full), "%s/%s", strcmp(path, "/") ? path : "", m_entries[i].name);
            if (!sink(context, full, m_entries[i]))
                result.status = 413;
        }
        if (result.status != 200 || m_job.status != Status::Ok)
            break;
    }
    if (result.status == 200 && m_job.status != Status::End)
        result = answer(m_job.status);
    m_job.entries = nullptr;
    m_job.entryCapacity = m_job.entryCount = 0;
    // Bei End und Fehlern hat der Kartenspeicher den Ordner schon geschlossen, ListClose schadet dann nicht.
    run(Kind::ListClose, "");
    return result;
}

WebFileStore::Result WebFileStore::mkdir(const char *path)
{
    if (!WebValidation::path(path, false))
        return {400, Reason::None};
    return answer(run(Kind::Mkdir, path));
}

WebFileStore::Result WebFileStore::remove(const char *path)
{
    if (!WebValidation::path(path, false))
        return {400, Reason::None};
    const Status found = run(Kind::Stat, path);
    if (found != Status::Ok)
        return answer(found);
    if (!m_job.entry.directory)
        return answer(run(Kind::Delete, path));
    // FATFS meldet einen Ordner mit Inhalt als verweigert, gelöscht wird nur ein leerer Ordner.
    const Status removed = run(Kind::Rmdir, path);
    return removed == Status::Denied ? Result{409, Reason::NotEmpty} : answer(removed);
}

WebFileStore::Result WebFileStore::rename(const char *from, const char *to)
{
    if (!WebValidation::path(from, false) || !WebValidation::path(to, false))
        return {400, Reason::None};
    const size_t length = strlen(from);
    if (!strncmp(to, from, length) && to[length] == '/')
        return {400, Reason::Inside};
    return answer(run(Kind::Rename, from, to));
}

WebFileStore::Result WebFileStore::treeBegin(const char *path, bool remove)
{
    if (!WebValidation::path(path, false))
        return {400, Reason::None};
    const Status status = run(remove ? Kind::TreeRemove : Kind::TreeCount, path);
    if (status == Status::More)
        return {200, Reason::None};
    return status == Status::NotDirectory ? Result{400, Reason::None} : answer(status);
}

WebFileStore::Result WebFileStore::treeStep(TreeProgress &progress)
{
    progress = TreeProgress{};
    if (!m_store.treeActive())
        return {409, Reason::None};
    m_job.kind = Kind::TreeStep;
    m_store.execute(m_job);
    progress.files = m_job.files;
    progress.folders = m_job.folders;
    stringutils::safeCopy(progress.failed, m_job.target);
    if (m_job.status == Status::More)
        return {200, Reason::None};
    progress.done = m_job.status == Status::End;
    return progress.done ? Result{200, Reason::None} : answer(m_job.status);
}

void WebFileStore::treeClose()
{
    if (m_store.treeActive())
        run(Kind::TreeClose, "");
}
