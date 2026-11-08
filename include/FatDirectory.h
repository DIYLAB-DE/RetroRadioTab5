#ifndef FAT_DIRECTORY_H
#define FAT_DIRECTORY_H

#include "FtpStore.h"
#include <ff.h>

/** @brief Liest Kartenordner direkt über FATFS: f_readdir liefert Name, Größe, Zeit und Attribute in einem Durchgang, während readdir mit stat je Eintrag den Ordner erneut durchsucht. Versteckte und Systemeinträge (FAT-Attribute HID/SYS, etwa „System Volume Information“) meldet next() als hidden, die Ausblendung entscheidet SystemEntries::hidden. Gehört dem Dienstworker. FTP-Dienst und Datei-Manager des Webportals haben je eine eigene Instanz im PSRAM, die Ordnerliste des SD-Players eine auf dem Stack des Auftrags. */
class FatDirectory : public FtpStore::Directory
{
public:
    using Status = FtpProtocol::Status;

    /** @brief Setzt vor jedem Auftrag die FATFS-Laufwerksnummer der eingebundenen Karte, -1 ohne Karte. */
    void setDrive(int drive)
    {
        m_drive = drive;
    }

    /** @brief Öffnet den Ordner zu einem kanonischen Kartenpfad auf dem gesetzten Laufwerk. @return Ok, NotFound oder Io, ohne Laufwerk Io. */
    Status open(const char *path) override;
    /** @brief Liest den nächsten Eintrag. FATFS lässt „.“ und „..“ selbst weg, ein Name, der als UTF-8 nicht in 255 Byte passt, kommt als Kurzname. @param hidden true bei FAT-Attribut HID oder SYS. @return Ok, End, Io oder Closed ohne offenen Ordner. */
    Status next(FtpProtocol::Entry &entry, bool &hidden) override;
    /** @brief Schließt den Ordner, mehrfach aufrufbar. */
    void close() override;

private:
    FF_DIR m_directory{}; ///< Offener Ordner
    FILINFO m_info{};     ///< Zuletzt gelesener Eintrag
    int m_drive = -1;     ///< FATFS-Laufwerk der Karte
    bool m_open = false;  ///< m_directory ist offen
};
#endif
