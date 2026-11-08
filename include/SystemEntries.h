#ifndef SYSTEM_ENTRIES_H
#define SYSTEM_ENTRIES_H

#include <cstddef>
#include <cstring>
#include <strings.h>

/** @brief Gemeinsame Regel für Karteneinträge, die Dateidialog und Titelsammlung des SD-Players, Datei-Manager des Webportals und FTP-Dienst nicht zeigen: Namen mit führendem Punkt, die Systemnamen aus Names und Einträge mit FAT-Attribut versteckt oder System. Der Punkt erfasst auch die macOS-Einträge „._*“, „.DS_Store“, „.Trashes“, „.Spotlight-V100“, „.fseventsd“ und „.TemporaryItems“ sowie die Staging- und Sicherungsdateien „.rr-ftp-*“ und „.rr-web-*“. Pfade aus Portal und FTP lehnt WebValidation::path ab, sobald ein Bestandteil einen dieser Namen trägt. Das FAT-Attribut kennt nur das Auflisten. */
namespace SystemEntries
{
    constexpr const char *Names[] = {"System Volume Information", "RECYCLER", "$Recycle.Bin", "Thumbs.db", "desktop.ini"}; ///< Systemnamen aus DIYLAB-DE/RetroRadio, Commit ea4229a34fb39faaff23619092cf09cafb8c51cf, `src/FileExplorer.cpp`, `FileExplorer::isSystemDirectory()`. Verglichen ohne Groß- und Kleinschreibung, „$Recycle.Bin“ erfasst so auch „$RECYCLE.BIN“

    /** @brief Prüft einen Namen ohne Pfad auf führenden Punkt (auch „.“ und „..“) oder einen Systemnamen aus Names. @param name Name, muss nicht nullterminiert sein. @param length Länge in Byte. @return true für einen auszublendenden Namen. */
    inline bool systemName(const char *name, size_t length)
    {
        if (length && name[0] == '.')
            return true;
        for (const char *system : Names)
            if (length == strlen(system) && !strncasecmp(name, system, length))
                return true;
        return false;
    }

    /** @brief Wendet die ganze Regel auf einen gelesenen Ordnereintrag an. @param name Nullterminierter Name ohne Pfad. nullptr oder leer gilt als auszublenden. @param fatHidden FAT-Attribut HID oder SYS gesetzt. @return true, wenn der Eintrag nicht erscheint. */
    inline bool hidden(const char *name, bool fatHidden)
    {
        return fatHidden || !name || !name[0] || systemName(name, strlen(name));
    }
}
#endif
