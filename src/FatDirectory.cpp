#include "FatDirectory.h"
#include "StringUtils.h"
#include <ctime>

namespace
{
    /** @brief Rechnet Datum und Zeit eines FAT-Eintrags (Ortszeit) wie der VFS-Aufruf stat() in Unix-Zeit um. @return 0 ohne Datum. */
    int64_t fatTime(WORD date, WORD time)
    {
        if (!date)
            return 0;
        tm local{};
        local.tm_year = (date >> 9) + 80;
        local.tm_mon = ((date >> 5) & 15) - 1;
        local.tm_mday = date & 31;
        local.tm_hour = time >> 11;
        local.tm_min = (time >> 5) & 63;
        local.tm_sec = (time & 31) * 2;
        local.tm_isdst = -1;
        const time_t value = mktime(&local);
        return value > 0 ? static_cast<int64_t>(value) : 0;
    }
}

FatDirectory::Status FatDirectory::open(const char *path)
{
    close();
    char name[FtpProtocol::MaxPath + 4];
    if (m_drive < 0 || snprintf(name, sizeof(name), "%d:%s", m_drive, path) >= static_cast<int>(sizeof(name)))
        return Status::Io;
    const FRESULT result = f_opendir(&m_directory, name);
    if (result == FR_OK)
    {
        m_open = true;
        return Status::Ok;
    }
    return result == FR_NO_PATH || result == FR_NO_FILE || result == FR_INVALID_NAME ? Status::NotFound : Status::Io;
}

FatDirectory::Status FatDirectory::next(FtpProtocol::Entry &entry, bool &hidden)
{
    if (!m_open)
        return Status::Closed;
    if (f_readdir(&m_directory, &m_info) != FR_OK)
        return Status::Io;
    if (!m_info.fname[0])
        return Status::End;
    entry = FtpProtocol::Entry{};
    stringutils::safeCopy(entry.name, m_info.fname);
    entry.directory = m_info.fattrib & AM_DIR;
    entry.size = entry.directory ? 0 : m_info.fsize;
    entry.modified = fatTime(m_info.fdate, m_info.ftime);
    hidden = m_info.fattrib & (AM_HID | AM_SYS);
    return Status::Ok;
}

void FatDirectory::close()
{
    if (m_open)
        f_closedir(&m_directory);
    m_open = false;
}
