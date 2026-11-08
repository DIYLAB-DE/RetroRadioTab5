#ifndef FILE_EXPLORER_H
#define FILE_EXPLORER_H

#include <M5GFX.h>
#include <cstddef>
#include <cstdint>
#include "Strings_sdplayer.h"
#include "UiButton.h"

/** @brief Bereits zentral auf logische Displaykoordinaten gedrehte Berührung. */
enum class SdTouchPhase : uint8_t
{
    Press,   ///< Finger aufgesetzt
    Move,    ///< Finger bewegt
    Release, ///< Finger gehoben
    Cancel   ///< Kontakt verworfen
};

/** @brief Eine Berührung mit Phase, Position und Zeit. */
struct SdTouch
{
    SdTouchPhase phase; ///< Phase des Kontakts
    int16_t x, y;       ///< Position in Displaypixeln
    uint32_t now;       ///< Zeit in ms
};

/** @brief Kopierbarer Dateieintrag. Pfade sind absolut und mount-relativ, UTF-8 und nullterminiert. */
struct SdFileEntry
{
    char path[256];    ///< Absoluter Pfad ab dem Wurzelverzeichnis der Karte
    char name[128];    ///< Anzeigename ohne Pfad
    bool directory;    ///< Ordner statt Datei
    uint32_t size = 0; ///< Größe der Datei in Bytes, bei Ordnern 0
};

/** @brief Begrenzter Auftrag an den einzigen zentralen Dateidienst. Keine FS-Zugriffe im Dialog. */
struct SdFileRequest
{
    /** @brief Art des Auftrags. */
    enum class Kind : uint8_t
    {
        List,    ///< Ordner path auflisten
        Collect, ///< Die Auswahl samt Unterordnern zu Dateien auflösen
        Cancel   ///< Laufenden Auftrag requestId abbrechen
    };
    static constexpr size_t MaxSelection = 32; ///< Größe der Auswahl
    Kind kind = Kind::List;                    ///< Art des Auftrags
    uint32_t requestId = 0, mediaGeneration = 0; ///< Kennung des Auftrags (nie 0). Kartengeneration, für die er gilt
    char path[256] = "/";                      ///< Zu listender Ordner
    uint16_t selectionCount = 0;               ///< Zahl der Einträge in selection
    SdFileEntry selection[MaxSelection]{};     ///< Markierte Einträge für Collect
};

/** @brief Geordnetes Ergebnisbatch. Einträge werden während acceptResult kopiert, niemals behalten. */
struct SdFileResult
{
    /** @brief Ergebnis des Auftrags. */
    enum class State : uint8_t
    {
        Data,     ///< Einträge liegen vor
        Error,    ///< Lesen fehlgeschlagen
        Limit,    ///< Mehr Einträge als die Liste fasst
        Cancelled ///< Abgebrochen
    };
    uint32_t requestId = 0, mediaGeneration = 0; ///< Kennung des Auftrags, Kartengeneration
    SdFileRequest::Kind kind = SdFileRequest::Kind::List; ///< Art des beantworteten Auftrags
    State state = State::Data;                  ///< Ergebnis
    const SdFileEntry *entries = nullptr;       ///< Einträge des Batches, nur während des Aufrufs gültig
    size_t offset = 0, count = 0;               ///< Position des ersten Eintrags im Gesamtergebnis, Zahl der Einträge des Batches
    bool final = true;                          ///< Letztes Batch des Auftrags
    uint16_t skipped = 0;                       ///< Übersprungene Einträge des ganzen Auftrags: Pfad über 255 Byte, ungültiger Name oder zu tief
};

/** @brief Rückrufe in die zentrale Instanz, werden im UI-Kontext aufgerufen. */
struct FileExplorerCallbacks
{
    void *context = nullptr; ///< Erster Parameter beider Rückrufe
    /** @brief Muss den Auftrag vor Rückkehr kopieren. true bedeutet nur angenommen, nicht abgeschlossen. Keine reentranten Ergebnisaufrufe. */
    bool (*request)(void *, const SdFileRequest &) = nullptr;
    /** @brief Kopiert die vollständige Auswahl synchron in die Playlist. false lässt den Dialog offen. */
    bool (*selected)(void *, const SdFileEntry *, size_t) = nullptr;
};

/** @brief Nativer, asynchron versorgter Dateidialog mit eigener begrenzter Auswahl. */
class FileExplorer
{
public:
    static constexpr size_t MaxEntries = 300; ///< Größe der Eintragsliste
    /** @brief Erstellt Puffer einmalig. @param display Zentraler Bildschirm. @param callbacks Zentrale Dateianforderungen und Playlistübergabe. @return false bei Speicher-/Schriftfehler. */
    bool begin(lgfx::LGFX_Device &display, FileExplorerCallbacks callbacks);
    /** @brief Gibt ausschließlich eigene Puffer frei, keine Dateisystemressourcen. */
    ~FileExplorer();
    FileExplorer() = default;
    FileExplorer(const FileExplorer &) = delete;
    FileExplorer &operator=(const FileExplorer &) = delete;
    /** @brief Öffnet den Dialog und fordert ein Verzeichnis an. @param mediaGeneration Aktuelles Medium. @param language Zentrale Sprache. @param canCollect Rekursive Sammlung verfügbar. @param path Mount-relativer Einstieg. @return false bei ungültigem Pfad oder nicht angenommenem Auftrag. */
    bool open(uint32_t mediaGeneration, LangIndex language, bool canCollect, const char *path = "/");
    /** @brief Entwertet ausstehende Ergebnisse und fordert deren Abbruch an, auch bei voller Queue lokal geschlossen. */
    void cancel();
    /** @brief Übernimmt ein Batch nur für den aktuellen Auftrag. @param result Ergebnis im UI-Kontext. @return true bei gültigem übernommenem Batch, false bei veralteten/ungültigen Daten. */
    bool acceptResult(const SdFileResult &result);
    /** @brief Ändert nur die Darstellungsprache. @param language Zentrale Sprache. */
    void setLanguage(LangIndex language);
    /** @brief Erzwingt neue Darstellung nach Überlagerung. */
    void invalidate();
    /** @brief Zeichnet nur geänderte Bereiche, liest keine Dateien. */
    void update();
    /** @brief Verarbeitet eine zentrale Geste ohne Warteschleife. @param touch Press/Move/Release/Cancel in physischen Displaykoordinaten. @return true, solange der Dialog diesen Kontakt übernimmt. */
    bool touch(const SdTouch &touch);

    /** @brief Meldet die Modalpriorität an die Zentrale. @return Dialog sichtbar. */
    bool active() const
    {
        return m_active;
    }

    /** @brief Prüft einen kanonischen mount-relativen UTF-8-Pfad ohne Abschneiden. @param path Nullterminierter Pfad. @return Gültigkeit innerhalb 255 Bytes. */
    static bool validPath(const char *path);
    /** @brief Prüft vollständiges UTF-8 ohne Steuerzeichen. @param text Textpuffer. @param capacity Maximal lesbare Bytes einschließlich NUL. @return true bei gültiger Terminierung und UTF-8. */
    static bool validText(const char *text, size_t capacity);
    /** @brief Prüft Pfad und Dateiendung ohne Unterscheidung der Groß-/Kleinschreibung. @param path Dateipfad. @return Gültiger Pfad mit MP3/AAC/FLAC/M4A/OGG, ohne Codecgarantie. */
    static bool supportedFile(const char *path);
    /** @brief Prüft nur die Dateiendung eines Namens, auch wenn sein Pfad zu lang wäre. @param name Dateiname oder Pfad. @return MP3/AAC/FLAC/M4A/OGG am Ende des Namens. */
    static bool supportedName(const char *name);

private:
    /** @brief Bedienelement unter dem Finger. */
    enum class Hit : uint8_t
    {
        None,   ///< Nichts
        Cancel, ///< Abbrechen
        Up,     ///< Eine Ebene höher
        Add,    ///< Hinzufügen
        Row     ///< Zeile der Liste
    };
    /** @brief Beginnt einen neuen Verzeichnisauftrag und entwertet alte Ergebnisse. */
    bool navigate(const char *path);
    /** @brief Übergibt markierte Dateien oder fordert rekursive Sammlung an. */
    void submit();
    /** @brief Kopiert markierte Einträge in die begrenzte Auftragsstruktur. */
    void collectSelection();
    /** @brief Entwertet einen Auftrag unabhängig vom Erfolg seiner Abbruchübermittlung. */
    void abortRequest();
    /** @brief Bestimmt sichtbares Bedienelement und Zeilenindex. */
    Hit hitAt(int x, int y, int &row) const;
    /** @brief Zeichnet Rahmen und Dateizeilen aus bereits kopierten Daten. */
    void draw();
    /** @brief Kürzt Text an UTF-8-Grenzen auf reale Schriftbreite. */
    void text(lgfx::LGFX_Sprite &surface, const char *value, int x, int y, int width, uint16_t color, bool large = false);

    lgfx::LGFX_Device *m_display = nullptr; ///< Ziel der Übertragung
    UiCanvas m_line, m_scene, m_view;       ///< Zeilenpuffer, Szene in Dialoggröße, Ausschnitt der Liste
    UiButton m_ok, m_cancel;                ///< Tasten Hinzufügen und Abbrechen
    uint16_t *m_icons[4]{};                 ///< Ordnersymbole: Ordner, Ordner gewählt, höher, höher gewählt
    uint32_t m_pressTime = 0;               ///< Zeit des Aufsetzens in ms
    bool m_held = false;                    ///< Der Finger ruht länger als 500 ms auf einer Zeile
    /** @brief Schaltet die Auswahl um: ein Ordner oder mehrere Dateien. */
    void toggle(int row);
    lgfx::PointerWrapper m_fontData, m_smallData;    ///< Daten der großen und kleinen Schrift
    lgfx::VLWfont m_font, m_small;                   ///< Große und kleine Schrift
    FileExplorerCallbacks m_callbacks;               ///< Rückrufe in die Zentrale
    SdFileEntry *m_entries = nullptr, *m_collected = nullptr; ///< Einträge des geöffneten Ordners. Rekursiv gesammelte Dateien, je MaxEntries
    bool m_selected[MaxEntries]{};                   ///< Markierung je Eintrag
    size_t m_count = 0, m_collectedCount = 0;        ///< Zahl der Einträge, Zahl der gesammelten Dateien
    uint16_t m_skipped = 0;                          ///< Übersprungene Einträge des geöffneten Ordners
    SdFileRequest m_request;                         ///< Laufender oder zuletzt gestellter Auftrag
    uint32_t m_sequence = 0, m_generation = 0;       ///< Auftragszähler, Kartengeneration seit open
    bool m_ready = false, m_active = false, m_busy = false, m_collecting = false, m_canCollect = false, m_dirty = false, m_frameDirty = false; ///< begin erfolgreich. Dialog sichtbar. Auftrag läuft. Auftrag ist Collect. Sammeln möglich. Liste neu zeichnen. Rahmen neu zeichnen
    LangIndex m_language = LangIndex::DE;            ///< Sprache der Anzeige
    SdPlayerKey m_message = SdPlayerKey::SelectPrompt; ///< Statustext unter der Liste
    char m_path[256] = "/";                          ///< Geöffneter Ordner
    int m_scroll = 0, m_pressY = 0, m_pressScroll = 0, m_pressRow = -1; ///< Scrollversatz in Pixeln. Berührpunkt Y und Versatz beim Aufsetzen. Zeile unter dem Finger
    int m_pressX = 0, m_lastTouchY = 0;              ///< Berührpunkt X, zuletzt gemeldetes Y
    float m_scrollPos = 0, m_scrollVelocity = 0;     ///< Scrollversatz mit Überlauf in Pixeln, Geschwindigkeit in Pixeln/s
    uint32_t m_lastTouchTime = 0, m_lastUpdateTime = 0; ///< Zeiten in ms: letzte Positionsänderung, letzter update-Schritt
    /** @brief Aktualisiert Scrollträgheit und Rückfederung anhand der vergangenen Millisekunden. */
    void updateScrollPhysics();
    Hit m_hit = Hit::None;                           ///< Bedienelement des laufenden Kontakts
    bool m_dragged = false, m_cancelled = false;     ///< Wischschwelle überschritten, Kontakt verworfen
};
#endif
