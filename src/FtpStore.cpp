#include "FtpStore.h"
#include "StringUtils.h"
#include "SystemEntries.h"
#include <cerrno>
#include <fcntl.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

namespace
{
    using Status = FtpProtocol::Status;

    /** @brief Ordnet errno einem Ergebnis zu. FATFS meldet einen nicht leeren Ordner und schreibgeschützte Einträge als EACCES, eine volle Karte bei mkdir ebenfalls. */
    Status fromErrno(int error)
    {
        switch (error)
        {
        case ENOENT:
        case ENOTDIR:
            return Status::NotFound;
        case EEXIST:
            return Status::Exists;
        case EACCES:
        case EPERM:
        case ENOTEMPTY:
            return Status::Denied;
        case ENOSPC:
            return Status::NoSpace;
        case EINVAL:
        case ENAMETOOLONG:
            return Status::Invalid;
        default:
            return Status::Io;
        }
    }

    /** @brief Kopiert den übergeordneten Ordner eines kanonischen Pfads, für „/name“ die Wurzel. */
    void parent(const char *path, char (&out)[FtpProtocol::MaxPath + 1])
    {
        stringutils::safeCopy(out, path);
        char *slash = strrchr(out, '/');
        if (slash == out)
            out[1] = 0;
        else if (slash)
            *slash = 0;
    }
}

FtpStore::FtpStore(const char *root, Directory *directory, uint32_t (*random)(), const char *prefix) : m_root(root), m_directory(directory), m_random(random), m_prefix(prefix)
{
}

bool FtpStore::temporary(const char *name)
{
    if (!name || strlen(name) != 20 || strncmp(name, FtpPrefix, 8) || (strcmp(name + 16, ".tmp") && strcmp(name + 16, ".old")))
        return false;
    for (size_t i = 8; i < 16; ++i)
        if (!((name[i] >= '0' && name[i] <= '9') || (name[i] >= 'a' && name[i] <= 'f')))
            return false;
    return true;
}

bool FtpStore::full(const char *path, char (&out)[FullCapacity]) const
{
    const int n = snprintf(out, sizeof(out), "%s%s", m_root, strcmp(path, "/") ? path : "");
    return n > 0 && static_cast<size_t>(n) < sizeof(out);
}

bool FtpStore::temporaryName(const char *suffix, char (&out)[FullCapacity]) const
{
    // Ein vorhandener Name wird übersprungen, nach acht belegten Zufallsnamen gilt die Karte als nicht beschreibbar.
    for (int attempt = 0; attempt < 8; ++attempt)
    {
        const int n = snprintf(out, sizeof(out), "%s/%s%08lx%s", m_root, m_prefix, static_cast<unsigned long>(m_random()), suffix);
        if (n <= 0 || static_cast<size_t>(n) >= sizeof(out))
            return false;
        struct stat info{};
        if (::stat(out, &info) != 0 && errno == ENOENT)
            return true;
    }
    out[0] = 0;
    return false;
}

FtpStore::Status FtpStore::stat(const char *path, Entry &entry) const
{
    entry = Entry{};
    if (!strcmp(path, "/"))
    {
        entry.directory = true;
        return Status::Ok;
    }
    char name[FullCapacity];
    if (!full(path, name))
        return Status::Invalid;
    struct stat info{};
    if (::stat(name, &info) != 0)
        return fromErrno(errno);
    entry.directory = S_ISDIR(info.st_mode);
    entry.size = entry.directory ? 0 : static_cast<uint64_t>(info.st_size);
    entry.modified = static_cast<int64_t>(info.st_mtime);
    stringutils::safeCopy(entry.name, FtpProtocol::baseName(path));
    return Status::Ok;
}

void FtpStore::closeTransfer(bool discard)
{
    if (m_read >= 0)
        ::close(m_read);
    if (m_write >= 0)
        ::close(m_write);
    m_read = m_write = -1;
    if (discard && m_staging[0])
        ::unlink(m_staging);
    m_staging[0] = m_target[0] = 0;
}

void FtpStore::closeList()
{
    if (m_listing)
        m_directory->close();
    m_listing = false;
    m_listPath[0] = 0;
    m_skipped = 0;
}

void FtpStore::closeTree()
{
    if (m_tree.open)
        m_directory->close();
    m_tree.active = m_tree.open = false;
    m_tree.depth = 0;
    m_tree.path[0] = 0;
    m_tree.files = m_tree.folders = 0;
}

void FtpStore::closeAll()
{
    closeTransfer(false);
    closeList();
    closeTree();
}

void FtpStore::execute(Job &job)
{
    using Kind = FtpProtocol::Kind;
    job.status = Status::Invalid;
    switch (job.kind)
    {
    case Kind::Close:
        closeTransfer(true);
        job.status = Status::Ok;
        return;
    case Kind::ListClose:
        closeList();
        job.status = Status::Ok;
        return;
    case Kind::TreeClose:
        closeTree();
        job.status = Status::Ok;
        return;
    case Kind::ListNext:
        listNext(job);
        return;
    case Kind::TreeStep:
        treeStep(job);
        return;
    case Kind::Read:
    {
        job.count = 0;
        if (m_read < 0)
        {
            job.status = Status::Closed;
            return;
        }
        const ssize_t n = ::read(m_read, job.data, job.capacity);
        if (n < 0)
        {
            job.status = Status::Io;
            closeTransfer(false);
        }
        else if (n == 0)
        {
            job.status = Status::End;
            closeTransfer(false);
        }
        else
        {
            job.count = static_cast<uint32_t>(n);
            job.status = Status::Ok;
        }
        return;
    }
    case Kind::Write:
        write(job);
        return;
    case Kind::Commit:
        commit(job);
        return;
    default:
        break;
    }
    if (!WebValidation::path(job.path, true))
        return;
    char name[FullCapacity];
    if (!full(job.path, name))
        return;
    switch (job.kind)
    {
    case Kind::Stat:
        job.status = stat(job.path, job.entry);
        return;
    case Kind::ListOpen:
        // Liste und Baumarbeit teilen sich den Ordnerleser.
        closeTree();
        closeList();
        job.status = m_directory ? m_directory->open(job.path) : Status::Io;
        job.skipped = 0;
        if (job.status == Status::Ok)
        {
            m_listing = true;
            stringutils::safeCopy(m_listPath, job.path);
        }
        return;
    case Kind::ReadOpen:
    {
        closeTransfer(true);
        job.status = stat(job.path, job.entry);
        if (job.status != Status::Ok)
            return;
        if (job.entry.directory)
        {
            job.status = Status::IsDirectory;
            return;
        }
        if (job.offset > job.entry.size)
        {
            job.status = Status::Invalid;
            return;
        }
        m_read = ::open(name, O_RDONLY);
        if (m_read < 0)
        {
            job.status = fromErrno(errno);
            return;
        }
        if (job.offset && ::lseek(m_read, static_cast<off_t>(job.offset), SEEK_SET) != static_cast<off_t>(job.offset))
        {
            closeTransfer(false);
            job.status = Status::Io;
            return;
        }
        job.status = Status::Ok;
        return;
    }
    case Kind::WriteOpen:
        writeOpen(job);
        return;
    case Kind::Mkdir:
        if (!strcmp(job.path, "/"))
            job.status = Status::Exists;
        else
            job.status = ::mkdir(name, 0777) == 0 ? Status::Ok : fromErrno(errno);
        return;
    case Kind::Rmdir:
    case Kind::RmdirHidden:
        rmdir(job, job.kind == Kind::RmdirHidden);
        return;
    case Kind::TreeCount:
    case Kind::TreeRemove:
        treeBegin(job, job.kind == Kind::TreeRemove);
        return;
    case Kind::Delete:
    {
        Entry entry;
        job.status = stat(job.path, entry);
        if (job.status != Status::Ok)
            return;
        if (entry.directory)
            job.status = Status::IsDirectory;
        else if (::unlink(name) != 0)
            job.status = fromErrno(errno);
        return;
    }
    case Kind::Rename:
        rename(job);
        return;
    default:
        return;
    }
}

void FtpStore::listNext(Job &job)
{
    job.entryCount = 0;
    if (!m_listing)
    {
        job.status = Status::Closed;
        return;
    }
    job.status = Status::Ok;
    for (size_t reads = 0; reads < FtpProtocol::ListReads && job.entryCount < job.entryCapacity; ++reads)
    {
        Entry &entry = job.entries[job.entryCount];
        bool hidden = false;
        const Status status = m_directory->next(entry, hidden);
        if (status != Status::Ok)
        {
            job.status = status;
            break;
        }
        if (SystemEntries::hidden(entry.name, hidden))
            continue;
        // Wie im Webportal: Ein Eintrag, dessen Pfad nicht in 255 Byte passt oder dessen Name ungültig ist, wird gezählt und übersprungen, statt die Liste abzubrechen.
        char path[FtpProtocol::MaxPath + 2];
        const int n = snprintf(path, sizeof(path), "%s/%s", strcmp(m_listPath, "/") ? m_listPath : "", entry.name);
        if (n <= 0 || static_cast<size_t>(n) > FtpProtocol::MaxPath || !WebValidation::path(path, false))
        {
            ++m_skipped;
            continue;
        }
        ++job.entryCount;
    }
    job.skipped = m_skipped;
    if (job.status != Status::Ok)
        closeList();
}

void FtpStore::writeOpen(Job &job)
{
    closeTransfer(true);
    if (!strcmp(job.path, "/"))
        return;
    Entry entry;
    Status status = stat(job.path, entry);
    if (status == Status::Ok && entry.directory)
    {
        job.status = Status::IsDirectory;
        return;
    }
    if (status != Status::Ok && status != Status::NotFound)
    {
        job.status = status;
        return;
    }
    char folder[FtpProtocol::MaxPath + 1];
    parent(job.path, folder);
    status = stat(folder, entry);
    if (status != Status::Ok || !entry.directory)
    {
        job.status = status == Status::Ok ? Status::NotFound : status;
        return;
    }
    if (!full(job.path, m_target) || !temporaryName(".tmp", m_staging))
    {
        m_staging[0] = m_target[0] = 0;
        job.status = Status::Io;
        return;
    }
    m_write = ::open(m_staging, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (m_write < 0)
    {
        job.status = fromErrno(errno);
        m_staging[0] = m_target[0] = 0;
        return;
    }
    job.status = Status::Ok;
}

void FtpStore::write(Job &job)
{
    if (m_write < 0)
    {
        job.status = Status::Closed;
        return;
    }
    if (job.count > job.capacity)
    {
        job.status = Status::Invalid;
        closeTransfer(true);
        return;
    }
    for (uint32_t done = 0; done < job.count;)
    {
        // FATFS meldet eine volle Karte zuerst als kurzen Schreibvorgang, danach mit ENOSPC.
        const ssize_t n = ::write(m_write, job.data + done, job.count - done);
        if (n <= 0)
        {
            job.status = n == 0 || errno == ENOSPC ? Status::NoSpace : Status::Io;
            closeTransfer(true);
            return;
        }
        done += static_cast<uint32_t>(n);
    }
    job.status = Status::Ok;
}

void FtpStore::commit(Job &job)
{
    if (m_write < 0)
    {
        job.status = Status::Closed;
        return;
    }
    const bool closed = ::close(m_write) == 0;
    m_write = -1;
    if (!closed)
    {
        job.status = errno == ENOSPC ? Status::NoSpace : Status::Io;
        closeTransfer(true);
        return;
    }
    struct stat info{};
    if (::stat(m_target, &info) == 0)
    {
        if (S_ISDIR(info.st_mode))
        {
            job.status = Status::IsDirectory;
            closeTransfer(true);
            return;
        }
        // FAT benennt nicht auf einen vorhandenen Namen um: Das Ziel wird erst gesichert und nur bei Erfolg gelöscht.
        char backup[FullCapacity];
        if (!temporaryName(".old", backup) || ::rename(m_target, backup) != 0)
        {
            job.status = Status::Io;
            closeTransfer(true);
            return;
        }
        if (::rename(m_staging, m_target) != 0)
        {
            job.status = fromErrno(errno);
            ::rename(backup, m_target);
            closeTransfer(true);
            return;
        }
        // Bleibt die Sicherung wegen eines Fehlers liegen, entfernt sie das nächste Einbinden.
        ::unlink(backup);
    }
    else if (errno != ENOENT)
    {
        job.status = Status::Io;
        closeTransfer(true);
        return;
    }
    else if (::rename(m_staging, m_target) != 0)
    {
        job.status = fromErrno(errno);
        closeTransfer(true);
        return;
    }
    m_staging[0] = m_target[0] = 0;
    job.status = Status::Ok;
}

void FtpStore::rename(Job &job)
{
    if (!WebValidation::path(job.target, false) || !strcmp(job.path, "/"))
        return;
    const size_t length = strlen(job.path);
    if (!strncmp(job.target, job.path, length) && job.target[length] == '/')
        return;
    Entry entry;
    job.status = stat(job.path, entry);
    if (job.status != Status::Ok)
        return;
    if (!strcmp(job.path, job.target))
        return;
    char from[FullCapacity], to[FullCapacity];
    if (!full(job.path, from) || !full(job.target, to))
    {
        job.status = Status::Invalid;
        return;
    }
    // FAT unterscheidet Groß- und Kleinschreibung nicht: Ein Ziel, das sich nur darin unterscheidet, ist derselbe Eintrag und darf umbenannt werden.
    struct stat info{};
    if (::stat(to, &info) == 0 && strcasecmp(job.path, job.target))
    {
        job.status = Status::Exists;
        return;
    }
    job.status = ::rename(from, to) == 0 ? Status::Ok : fromErrno(errno);
}

void FtpStore::rmdir(Job &job, bool withHidden)
{
    if (!strcmp(job.path, "/"))
    {
        job.status = Status::Denied;
        return;
    }
    Entry entry;
    job.status = stat(job.path, entry);
    if (job.status != Status::Ok)
        return;
    char name[FullCapacity];
    if (!entry.directory)
        job.status = Status::NotDirectory;
    else if (!full(job.path, name))
        job.status = Status::Invalid;
    else if (::rmdir(name) != 0)
        job.status = errno == EEXIST ? Status::Denied : fromErrno(errno);
    if (!withHidden || job.status != Status::Denied || !m_directory)
        return;
    // FATFS meldet einen nicht leeren Ordner als verweigert. Enthält er nur versteckte Einträge (etwa „._*“ und „.DS_Store“ von macOS), werden sie samt Inhalt mit gelöscht.
    closeList();
    closeTree();
    const Status opened = m_directory->open(job.path);
    if (opened != Status::Ok)
    {
        job.status = opened;
        return;
    }
    bool hidden = false, visible = false, any = false;
    Status read;
    while ((read = m_directory->next(entry, hidden)) == Status::Ok)
    {
        if (!SystemEntries::hidden(entry.name, hidden))
        {
            visible = true;
            break;
        }
        any = true;
    }
    m_directory->close();
    if (read != Status::Ok && read != Status::End)
        job.status = read;
    else if (!visible && any)
        treeBegin(job, true);
}

void FtpStore::treeBegin(Job &job, bool remove)
{
    closeList();
    closeTree();
    job.files = job.folders = 0;
    job.target[0] = 0;
    if (!m_directory || !strcmp(job.path, "/"))
    {
        job.status = m_directory ? Status::Invalid : Status::Io;
        return;
    }
    Entry entry;
    job.status = stat(job.path, entry);
    if (job.status != Status::Ok)
        return;
    if (!entry.directory)
    {
        job.status = Status::NotDirectory;
        return;
    }
    m_tree.active = true;
    m_tree.remove = remove;
    m_tree.position[0] = 0;
    stringutils::safeCopy(m_tree.path, job.path);
    job.status = Status::More;
}

void FtpStore::treeFail(Job &job, Status status, const char *path)
{
    // path kann auf m_tree.path zeigen und wird vor dem Beenden kopiert.
    stringutils::safeCopy(job.target, path);
    job.files = m_tree.files;
    job.folders = m_tree.folders;
    closeTree();
    job.status = status == Status::Ok || status == Status::End || status == Status::More ? Status::Io : status;
}

void FtpStore::treeStep(Job &job)
{
    job.target[0] = 0;
    if (!m_tree.active)
    {
        job.status = Status::Closed;
        return;
    }
    Tree &t = m_tree;
    char name[FullCapacity];
    for (size_t work = 0; work < FtpProtocol::TreeStepEntries; ++work)
    {
        Entry entry;
        bool hidden = false;
        if (!t.open)
        {
            const Status opened = m_directory->open(t.path);
            if (opened != Status::Ok)
                return treeFail(job, opened, t.path);
            t.open = true;
            // Beim Zählen ändert sich der Ordner nicht, weil die Kartensperre gehalten wird. Schon gezählte Einträge werden überlesen. Beim Löschen sind sie schon entfernt.
            for (uint32_t i = 0; !t.remove && i < t.position[t.depth]; ++i)
                if (m_directory->next(entry, hidden) != Status::Ok)
                    return treeFail(job, Status::Io, t.path);
        }
        const Status read = m_directory->next(entry, hidden);
        if (read == Status::End)
        {
            m_directory->close();
            t.open = false;
            if (t.remove && (!full(t.path, name) || ::rmdir(name) != 0))
                return treeFail(job, errno == EEXIST ? Status::Denied : fromErrno(errno), t.path);
            if (!t.depth)
            {
                job.files = t.files;
                job.folders = t.folders;
                closeTree();
                job.status = Status::End;
                return;
            }
            if (t.remove)
                ++t.folders;
            // Unterhalb des Startordners hat der Pfad mindestens zwei Bestandteile, der letzte Schrägstrich steht also nicht am Anfang.
            *strrchr(t.path, '/') = 0;
            --t.depth;
            continue;
        }
        if (read != Status::Ok)
            return treeFail(job, read == Status::Closed ? Status::Io : read, t.path);
        if (!t.remove)
            ++t.position[t.depth];
        char child[FtpProtocol::MaxPath + 2];
        const int n = snprintf(child, sizeof(child), "%s/%s", t.path, entry.name);
        if (n <= 0 || static_cast<size_t>(n) > FtpProtocol::MaxPath)
            return treeFail(job, Status::Invalid, t.path);
        if (entry.directory)
        {
            if (t.depth + 1u >= FtpProtocol::TreeDepth)
                return treeFail(job, Status::Invalid, child);
            m_directory->close();
            t.open = false;
            if (!t.remove)
                ++t.folders;
            stringutils::safeCopy(t.path, child);
            t.position[++t.depth] = 0;
            continue;
        }
        if (t.remove && (!full(child, name) || ::unlink(name) != 0))
            return treeFail(job, fromErrno(errno), child);
        ++t.files;
    }
    job.files = t.files;
    job.folders = t.folders;
    job.status = Status::More;
}
