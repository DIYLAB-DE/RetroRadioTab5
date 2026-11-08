#ifndef STRINGS_SDPLAYER_H
#define STRINGS_SDPLAYER_H

#include "I18n.h"

/** @brief Texte der nativen SD-Ansicht und ihres Dateidialogs. Die Reihenfolge entspricht der Tabelle in Strings_sdplayer.cpp, Count steht am Ende. */
enum class SdPlayerKey : uint8_t
{
    Ready,
    Unavailable,
    Mounting,
    MountError,
    NoTitles,
    NoCover,
    LoadingCover,
    CoverError,
    Starting,
    ReadError,
    CommandError,
    QueueError,
    Limit,
    InvalidPath,
    Scanning,
    Collecting,
    EmptyFolder,
    ScanError,
    SelectPrompt,
    NoService,
    Added,
    Duplicate,
    Skipped,  ///< printf-Format: Zahl der übersprungenen Einträge (%u)
    BrowseButton,  ///< Beschriftung des Knopfs für den Dateidialog
    ClearButton,  ///< Beschriftung des Knopfs zum Leeren der Titelliste
    PlaylistHint1,  ///< erste Zeile des Hinweises bei leerer Titelliste
    PlaylistHint2,  ///< zweite Zeile des Hinweises bei leerer Titelliste
    NoPlayback,
    PlaylistEnd,
    AlbumUnknown,
    TrackInfoFormat,  ///< printf-Format: aktueller Titel, Titelzahl (%d/%d)
    CapacityFormat,  ///< printf-Format: freie und gesamte Kapazität als Ganzzahl und Zehntel (%d, %d, %d, %d)
    NoCard,
    SummaryFormat,  ///< printf-Format: Titelzahl (%d), Ordnername (%s)
    SummaryOneFormat,  ///< printf-Format: Ordnername (%s)
    AddTracksHint,
    ClearTitle,
    ClearMessage,
    ResetTitle,
    ResetMessage,
    SleepTitle,
    SleepMessage,
    ExplorerHeading,  ///< Titel des Dateidialogs
    ColumnName,
    ColumnType,
    ColumnSize,
    DirectoryEmpty,
    StatusOverview,  ///< printf-Format: Dateizahl, Ordnerzahl (%d, %d)
    StatusFolders,  ///< printf-Format: Zahl der gewählten Ordner (%d)
    StatusFiles,  ///< printf-Format: Größentext (%s), Zahl der gewählten Dateien (%d)
    NothingSelected,
    Count
};

namespace I18n
{
    /** @brief Liefert einen SD-Text ohne globale Sprachwahl. @param key Textschlüssel. @param language Übergebene Sprache. @return Statischer UTF-8-Text, bei ungültigem Schlüssel leer. */
    const char *sdPlayer(SdPlayerKey key, LangIndex language);
}
#endif
