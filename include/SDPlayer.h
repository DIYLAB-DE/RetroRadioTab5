#ifndef SD_PLAYER_H
#define SD_PLAYER_H

#include "FileExplorer.h"
#include "SleepTimerDialog.h"
#include "SettingsDialogs.h"
#include "VisualizerView.h"
#include "SdPlayerTab5Layout.h"
#include "Thema.h"
#include "BatteryIcon.h"

class AudioManager;
class VisualizerView;

/** @brief Geometrie und Farben einer der acht Kassettenvorlagen, die Maße beziehen sich auf die Pixel der jeweiligen Vorlage. */
struct CassetteDesign
{
    const unsigned short *bitmap; ///< 428x276-Farbtabelle (RGB565) der Kassette
    uint16_t labelText;           ///< Schriftfarbe des Interpreten auf dem Beschriftungsstreifen
    int16_t labelX;               ///< Interpreten-Zone, linke Kante
    int16_t labelY;               ///< Interpreten-Zone, obere Kante
    int16_t labelW;               ///< Interpreten-Zone, Breite, 0 = die Vorlage hat keinen freien Streifen
    int16_t labelH;               ///< Interpreten-Zone, Höhe (Original fest 20)
    uint16_t tape;                ///< dunklere Lagenringe im Bandwickel
    uint16_t tapeEdge;            ///< Grundfarbe des Bandwickels
    int16_t winX;                 ///< Bandfenster, linke Kante
    int16_t winY;                 ///< Bandfenster, obere Kante
    int16_t winW;                 ///< Bandfenster, Breite
    int16_t winH;                 ///< Bandfenster, Höhe
    int16_t hubY;                 ///< Höhe der Nabenzentren
    int16_t hubXLeft;             ///< Nabenzentrum links. Voll gekachelte Fenster: im Original 82/202 (echter C-Kassetten-Abstand 42,5/100,4 der Breite)
    int16_t hubXRight;            ///< Nabenzentrum rechts, Bullaugen-Designs folgen stattdessen den sichtbaren Vorlagen-Tellern
    float hubCoverR;              ///< Deckradius des Einheitsdorns: groß genug für den Vorlagen-Teller, klein genug für die Nabenöffnung
    float reelRMin;               ///< Wickelradius leer, an die Proportionen der Vorlage angepasst. Im Original mindestens 25, damit der Einheitsdorn immer im Band sitzt
    float reelRMax;               ///< Wickelradius voll: 24,5/42,5 des Nabenabstands, wie bei einer echten C90
    float scale;                  ///< Faktor der Originalpixel dieser Vorlage. Skaliert Dorn, Lagenringe und Wickelversatz
};

/** @brief Tatsächlich verfügbare zentrale Dienste und Zustand des aktuellen Mediums. */
struct SdServiceStatus
{
    /** @brief Zustand der SD-Karte. */
    enum class Media : uint8_t
    {
        Unavailable, ///< Keine Karte oder Dienst nicht vorhanden
        Mounting,    ///< Karte wird eingebunden
        Ready,       ///< Karte lesbar
        Error        ///< Einbinden fehlgeschlagen
    };
    uint32_t revision = 0, mediaGeneration = 0;   ///< Steigt mit jedem Stand, ältere werden verworfen. Zählt Kartenwechsel, Aufträge einer anderen Generation sind ungültig
    Media media = Media::Unavailable;             ///< Zustand der Karte
    bool list = false, collect = false, play = false, pause = false, seek = false, cover = false, retry = false; ///< Verfügbare Dienste: Ordner auflisten, Ordner rekursiv sammeln, Abspielen, Pause, Spulen, Cover laden, Karte erneut einbinden
    uint64_t totalBytes = 0, usedBytes = 0;       ///< Kapazität und Belegung der Karte in Bytes
};

/** @brief Ein Auftrag an die Zentrale, Erfolg der Queue ist kein Wiedergabenachweis. */
struct SdPlayerAction
{
    /** @brief Art des Auftrags, bis einschließlich Mute erwartet der SD-Player eine Bestätigung über acknowledgedRequest. */
    enum class Type : uint8_t
    {
        PlayFile,          ///< Datei path abspielen
        Pause,             ///< Wiedergabe anhalten
        Resume,            ///< Wiedergabe fortsetzen
        Seek,              ///< Zu positionSeconds springen
        Stop,              ///< Wiedergabe beenden
        Mute,              ///< Stummschaltung umschalten
        Home,              ///< Zum MainScreen. Zentral stumm, Stopp und Navigation
        RetryMedia,        ///< Karte erneut einbinden
        Volume,            ///< Lautstärke value setzen, 0..100
        Balance,           ///< Balance value setzen, -16..16
        Tone,              ///< Klangregler control (0 Tiefen, 1 Mitten, 2 Höhen) auf value dB setzen, -12..12
        VisualizerChanged, ///< Der Visualizermodus hat gewechselt
        Reset,             ///< Wiedergabe und Playlist zurücksetzen
        Sleep              ///< Schlaftimer starten
    };
    Type type = Type::Stop;                                       ///< Art des Auftrags
    uint32_t requestId = 0, mediaGeneration = 0, playbackId = 0, trackId = 0; ///< Auftragskennung (nie 0). Kartengeneration. Kennung der Wiedergabe. Kennung des Titels
    uint32_t positionSeconds = 0;                                 ///< Zielposition für Seek in Sekunden
    int value = 0, control = 0;                                   ///< Wert und Regler für Volume, Balance und Tone
    char path[256]{};                                             ///< Pfad der Datei für PlayFile
};

/** @brief Rückrufe in die zentrale Instanz, werden im UI-Kontext aufgerufen. */
struct SdPlayerCallbacks
{
    void *context = nullptr; ///< Erster Parameter beider Rückrufe
    /** @brief Kopiert den begrenzten Befehl vor Rückkehr. Keine reentranten Statusaufrufe, Home bedeutet zentral Stumm + Stopp + Navigation. */
    bool (*action)(void *, const SdPlayerAction &) = nullptr;
    /** @brief Kopiert den Dateiauftrag vor Rückkehr in den zentralen Dienst. */
    bool (*files)(void *, const SdFileRequest &) = nullptr;
};

/** @brief Kohärentes Audioabbild aus der zentralen Instanz, im UI-Kontext zuzustellen. */
struct SdPlaybackStatus
{
    /** @brief Quelle der laufenden Wiedergabe. */
    enum class Source : uint8_t
    {
        None,  ///< Keine
        Radio, ///< Internetradio
        Sd     ///< SD-Karte
    };
    /** @brief Zustand der Wiedergabe. */
    enum class State : uint8_t
    {
        Idle,     ///< Nichts geladen
        Starting, ///< Datei wird geöffnet
        Playing,  ///< Läuft
        Paused,   ///< Angehalten
        Stopped,  ///< Beendet
        Ended,    ///< Datei zu Ende gespielt
        Error     ///< Fehler beim Öffnen oder Lesen
    };
    uint32_t revision = 0, mediaGeneration = 0, playbackId = 0, trackId = 0, acknowledgedRequest = 0; ///< Steigt mit jedem Stand. Kartengeneration. Wiedergabe- und Titelkennung des Auftrags. Kennung des zuletzt bearbeiteten Auftrags
    Source source = Source::None;                                  ///< Quelle
    State state = State::Idle;                                     ///< Zustand
    uint32_t positionSeconds = 0, durationSeconds = 0;             ///< Position und Länge in Sekunden
    bool durationKnown = false, seekable = false, muted = true, fileConfirmed = false; ///< Länge bekannt. Spulen möglich, stumm. Die Datei läuft tatsächlich
    bool commandFailed = false;                                    ///< Der Auftrag acknowledgedRequest ist fehlgeschlagen
    uint32_t bitRate = 0;                                          ///< Gemessene Bitrate in bit/s, 0 = unbekannt
    bool timeValid = false;                                        ///< hour, minute und second sind gültig
    uint8_t hour = 0, minute = 0, second = 0, volume = 10;         ///< Uhrzeit für die Uhr, Lautstärke 0..100
    int8_t balance = 0, tone[3]{};                                 ///< Balance -16..16. Tiefen, Mitten, Höhen in dB, -12..12
    char title[128]{}, artist[128]{}, album[128]{};                ///< Metadaten, UTF-8
    BatteryIcon::State battery;                                    ///< Akkuanzeige, ohne erkannten Akku bleibt die Zeile der Buttons unverändert
};

/** @brief Fertiges Cover. RGB565-Wörter in Hostreihenfolge, 345x330, synchron kopiert. */
struct SdCoverFrame
{
    /** @brief Ergebnis der Coversuche. */
    enum class State : uint8_t
    {
        Ready,   ///< Bild liegt in pixels
        Missing, ///< Titel hat kein Cover
        Error    ///< Laden oder Dekodieren fehlgeschlagen
    };
    uint32_t revision = 0, mediaGeneration = 0, playbackId = 0, trackId = 0; ///< Steigt mit jedem Stand. Identität von Karte, Wiedergabe und Titel
    State state = State::Missing;       ///< Ergebnis
    const uint16_t *pixels = nullptr;   ///< Pixel, nur während des Aufrufs gültig
    size_t pixelCount = 0;              ///< Zahl der Pixel
    uint16_t width = 0, height = 0;     ///< Maße in Pixeln
    bool folder = false;                ///< Bild ist folder.jpg des Titelordners und gilt für alle Titel darin
};

/** @brief Nativer SD-Player mit begrenzter Playlist, ohne eigenen Datei-/Audio-/Touchdienst. */
class SdPlayer
{
public:
    static constexpr size_t MaxTracks = 300; ///< Größe der Playlist
    static constexpr int CoverWidth = SdPlayerTab5Layout::Cover::W, CoverHeight = SdPlayerTab5Layout::Cover::H; ///< Maße des Covers in Pixeln

    /** @brief Zeile der Playlist. */
    struct Track
    {
        uint32_t id;         ///< Eindeutige Kennung, nie 0
        SdFileEntry file;    ///< Datei auf der Karte
        bool played, failed; ///< Wurde gespielt, Wiedergabe schlug fehl
    };

    /** @brief Bereitet eigene Puffer/Schriften vor, Visualizer muss zentral bereits initialisiert sein. @param display Einziger Bildschirm. @param visualizer Gemeinsame Referenz. @param callbacks Zentrale Anforderungen. @return false bei fehlenden Puffern/Schriften. */
    bool begin(lgfx::LGFX_Device &display, VisualizerView &visualizer, SdPlayerCallbacks callbacks);
    /** @brief Gibt ausschließlich eigene Puffer frei. */
    ~SdPlayer();
    SdPlayer() = default;
    SdPlayer(const SdPlayer &) = delete;
    SdPlayer &operator=(const SdPlayer &) = delete;
    /** @brief Aktiviert die native Ansicht und die gemeinsame Visualizerfläche. @param language Zentrale Sprache. @return false bei fehlgeschlagenem begin oder Visualizer-Bounds. */
    bool show(LangIndex language);
    /** @brief Verbirgt die Ansicht und entwertet lokale Anforderungen, die Zentrale stoppt Audio beim Bildschirmwechsel. */
    void hide();
    /** @brief Invalidiert alle eigenen Bereiche nach Überlagerung. */
    void invalidate();
    /** @brief Ändert die übergebene Sprache ohne globale Sprachwahl. @param language Neue Sprache. */
    void setLanguage(LangIndex language);
    /** @brief Verarbeitet bestätigten Dienst-/Medienzustand. @param status Kohärentes Abbild mit aufsteigender Revision. */
    void setServices(const SdServiceStatus &status);
    /** @brief Übernimmt bestätigten Audiozustand, reguläres EOF kann genau einen Folgetitel anfordern. @param status Abbild mit Echo der Auftragskennungen. */
    void setPlayback(const SdPlaybackStatus &status);
    /** @brief Kopiert ein vorbereitetes Cover nur für den aktuellen Titel. @param cover Kurzlebige Daten mit Identität und Revision. @return Übernommen. */
    bool setCover(const SdCoverFrame &cover);
    /** @brief Übernimmt begrenzte zentrale Dateiergebnisse. @param result Aktuelles Batch. @return Vom Dateidialog angenommen. */
    bool acceptFiles(const SdFileResult &result);
    /** @brief Ergänzt eine vollständige Auswahl atomar ohne Dubletten. @param entries Gültige Dateien ohne Ordner. @param count Anzahl. @return false bei Grenzen/ungültigen Daten, ohne Teiländerung. */
    bool addFiles(const SdFileEntry *entries, size_t count);
    /** @brief Aktualisiert nur die aktive Ansicht, keine Datei-/Audioabfragen außer gemeinsamer Visualizer-Fassade. @param now Monotone Millisekunden. @param audio Zentraler PCM-Besitzer für genau eine VisualizerView. */
    void update(uint32_t now, AudioManager &audio);
    /** @brief Verarbeitet Press/Move/Release/Cancel aus dem zentralen Loop. @param touch Physische Displaykoordinaten, deckungsgleich mit der Szene. @return Kontakt verbraucht. */
    bool touch(const SdTouch &touch);

    /** @brief Meldet intern sichtbare Modals für zentrale PCM-/Touchpriorität. @return Dateidialog, Leerbestätigung oder Schlafdialog aktiv. */
    bool modalActive() const
    {
        return m_explorer.active() || m_messageBox.active() || m_sleep.isActive();
    }

    /** @brief Anzahl gespeicherter Playlistzeilen. @return 0..MaxTracks. */
    size_t trackCount() const
    {
        return m_count;
    }

    /** @brief Gibt eine kurzlebige nur lesbare Playlistansicht zurück. @param index Index. @return Zeiger bis nächster Playliständerung, nullptr bei ungültigem Index. */
    const Track *track(size_t index) const
    {
        return index < m_count ? m_tracks + index : nullptr;
    }

    /** @brief Kennung des zuletzt angenommenen Dateistarts. @return 0 nach Entwertung. */
    uint32_t playbackId() const
    {
        return m_playbackId;
    }

private:
    /** @brief Bedienelement unter dem Finger. */
    enum class Hit : uint8_t
    {
        None,       ///< Nichts
        Home,       ///< Zurück zum MainScreen
        Mute,       ///< Stumm
        Browse,     ///< Dateidialog öffnen
        Clear,      ///< Playlist leeren, nach Rückfrage
        Previous,   ///< Vorheriger Titel, gehalten Rücklauf
        Play,       ///< Wiedergabe oder Pause
        Pause,      ///< Wiedergabe anhalten
        Stop,       ///< Stopp
        Next,       ///< Nächster Titel, gehalten Vorlauf
        Progress,   ///< Fortschrittsbalken
        Track,      ///< Zeile der Playlist
        Tape,       ///< Kassette, ein Druck wechselt das Design
        Reset,      ///< Zurücksetzen, nach Rückfrage
        Power,      ///< Schlaftimer
        Knob,       ///< Lautstärkeknopf
        Balance,    ///< Regler Balance
        Low,        ///< Regler Tiefen
        Mid,        ///< Regler Mitten
        High,       ///< Regler Höhen
        Visualizer  ///< Fläche des Visualizers
    };

    /** @brief Verhalten am natürlichen Titelende, gilt bis Stopp, Leeren oder Verlassen des Players und wird nicht gespeichert. */
    enum class RepeatMode : uint8_t
    {
        Off,     ///< Playlist einmal bis zum Ende spielen
        Track,   ///< Aktuellen Titel wiederholen
        Playlist ///< Nach dem letzten Titel wieder beim ersten beginnen
    };

    /** @brief Bits der Bereiche, die neu zu zeichnen sind. */
    enum Dirty : uint16_t
    {
        Header = 1,    ///< Kopfzeile mit Status
        Tape = 2,      ///< Kassette
        Cover = 4,     ///< Cover
        Metadata = 8,  ///< Metadatenzeile
        Progress = 16, ///< Fortschritt
        List = 32,     ///< Playlist
        Controls = 64, ///< Tastenleiste, Knopf und Regler
        Scroll = 128,  ///< Nur die Liste ist verschoben
        All = 255,     ///< Alles
        Knob = 256,    ///< Nur der Lautstärkeknopf (in Controls enthalten)
        Tracks = 512   ///< Nur die Schieberegler (in Controls enthalten)
    };

    /** @brief Rechteck in Displaypixeln. */
    struct Rect
    {
        int x, y, w, h;

        bool contains(int px, int py) const
        {
            return px >= x && py >= y && px < x + w && py < y + h;
        }
    };

    /** @brief Leitet Dateianforderungen ohne Dateizugriff weiter. */
    static bool fileRequest(void *context, const SdFileRequest &request);
    /** @brief Kopiert die bestätigte Dateiauswahl in die Playlist. */
    static bool filesSelected(void *context, const SdFileEntry *entries, size_t count);
    /** @brief Fordert eine zentrale Aktion mit neuer Kennung an. */
    bool request(SdPlayerAction::Type type, uint32_t position = 0, const Track *track = nullptr, int value = 0, int control = 0);
    /** @brief Fordert den ausgewählten Titel an und verwirft alte Ereignisidentitäten erst bei Annahme. */
    bool play(size_t index);
    /** @brief Stoppt bzw. entwertet die aktuelle Wiedergabe und schaltet die Wiederholung aus. */
    bool stop();
    /** @brief Setzt alle Gespielt-Marken zurück und fordert den ersten Titel an. @return Auftrag angenommen. */
    bool restartPlaylist();
    /** @brief Leert nur lokale Playlistdaten nach angenommener Stoppanforderung. */
    void clear();
    /** @brief Setzt titelspezifische Anzeige-/Coverdaten zurück. */
    void resetTrack();
    /** @brief Führt den aktuellen Listeneintrag in den sichtbaren Ausschnitt. */
    void revealSelected();
    /** @brief Prüft die aktuelle Seekfähigkeit einschließlich ausstehender Befehle. */
    bool canSeek() const;
    /** @brief Liefert Rechteck und aktuelle Freigabe eines Bedienelements. */
    Rect bounds(Hit hit) const;
    /** @brief Bestimmt das unter den Koordinaten erreichbare Bedienelement. */
    Hit hitAt(int x, int y, int &row) const;
    /** @brief Führt eine vollständig beendete gültige Berührung aus. */
    void activate(Hit hit, int row);
    /** @brief Zeichnet die gemeinsame Fußzeile mit Freigaben der aktuellen Modalpriorität. */
    void drawControls();
    /** @brief Zeichnet vollständigen Rahmen ausschließlich nach Invalidierung. */
    void drawFrame();
    /** @brief Zeichnet Kopf, Metadaten, Liste, Transport und Fortschritt nach Dirty-Bits. */
    void draw();
    /** @brief Zeichnet die Kassettengrundform und einen vorbereiteten Bandfensterausschnitt. */
    void drawTape(uint32_t now, bool force);
    /** @brief Zeichnet eine Playlistzeile in den Ringpuffer. @param index Titelindex. @param y Oberkante im Playlist-Sprite. */
    void drawPlaylistRow(size_t index, int y);
    /** @brief Zeichnet einen neu sichtbaren Listenstreifen in den Ringpuffer. @param from Erste Inhaltszeile in Pixeln. @param to Ende (exklusiv), höchstens eine Listenhöhe nach from. */
    void renderPlaylistStrip(int from, int to);
    /** @brief Überträgt das sichtbare Fenster des Ringpuffers in zwei Teilen direkt aufs Display, beim Neuaufbau stattdessen in die Szene. */
    void presentPlaylist();
    /** @brief Kopiert das sichtbare Fenster des Ringpuffers in die Szene, bevor ein Dialog sie als Untergrund nutzt. */
    void syncPlaylistToScene();
    /** @brief Zeichnet die Metadatenzeile mit adaptiver Schriftgröße, die Vorgabefarben werden bei jedem Aufruf aus dem aktiven Thema gelesen. */
    void drawMetaText(bool init, const char *first = "", const char *second = "", uint16_t firstColor = farbe(Rolle::GemAkzent), uint16_t secondColor = farbe(Rolle::GemTextSilber), uint16_t separatorColor = farbe(Rolle::GemTextGedaempft), const char *separator = " | ", int reservedPixels = 0, uint16_t backgroundColor = farbe(Rolle::GemFlaeche));
    /** @brief Zeichnet Cover oder sprachabhängigen Platzhalter. */
    void drawCover();
    /** @brief Liefert den Textschlüssel des bestätigten aktuellen Zustands. */
    SdPlayerKey stateText() const;
    /** @brief Anzeigewert der Bitrate wie im Original: verlustbehaftete Formate auf MP3-Standardraten gerundet, FLAC ungerundet. @param path Dateipfad für das Format. @param bitRate Gemessene Bitrate in bit/s. @return kbps, 0 ohne Bitrate. */
    static uint32_t displayKbps(const char *path, uint32_t bitRate);

    lgfx::LGFX_Device *m_display = nullptr;          ///< Ziel der Übertragung
    VisualizerView *m_visualizer = nullptr;          ///< Gemeinsame Visualizerfläche
    FileExplorer m_explorer;                         ///< Dateidialog
    SdPlayerCallbacks m_callbacks;                   ///< Rückrufe in die Zentrale
    SdServiceStatus m_services;                      ///< Letzter Dienst- und Kartenzustand
    SdPlaybackStatus m_playback;                     ///< Zustand des aktuellen Titels, bei Titelwechsel zurückgesetzt
    Track *m_tracks = nullptr;                       ///< Playlist mit MaxTracks Einträgen
    size_t m_count = 0, m_selected = 0;              ///< Zahl der Einträge, gewählter Eintrag
    uint32_t m_nextTrackId = 0, m_sequence = 0, m_playbackId = 0, m_confirmedId = 0, m_terminalId = 0, m_pending = 0; ///< Zuletzt vergebene Titelkennung. Auftragszähler. Kennung des gestarteten Titels (0 = keiner). Kennung, deren Datei die Zentrale bestätigt hat. Kennung, deren Ende oder Fehler schon verarbeitet ist. Auftrag, dessen Bestätigung aussteht
    SdPlayerAction::Type m_pendingType = SdPlayerAction::Type::Stop; ///< Art des ausstehenden Auftrags
    RepeatMode m_repeat = RepeatMode::Off;           ///< Wiederholung, über die Play-Taste gewählt
    uint32_t m_serviceRevision = 0, m_audioRevision = 0, m_coverRevision = 0; ///< Zuletzt übernommene Revisionen von Dienst-, Audio- und Coverstand
    char m_coverFolder[256]{};      ///< Ordner samt Schrägstrich, dessen folder.jpg das gezeigte Cover ist, leer bei anderem Cover
    uint32_t m_coverFolderMedia = 0; ///< Karte, zu der m_coverFolder gehört
    bool m_haveServices = false, m_haveAudio = false, m_ready = false, m_visible = false, m_muted = true; ///< Dienststand und Audiostand empfangen, begin erfolgreich. Ansicht sichtbar, stumm
    bool m_confirmClear = false, m_fullDirty = true, m_coverReady = false; ///< Löschbestätigung offen, nächstes Bild vollständig. Cover übernommen
    bool m_stopping = false, m_localError = false;   ///< Stopp ist angefordert und unbestätigt. Fehler, der nicht von der Zentrale stammt
    SdPlayerKey m_message = SdPlayerKey::Ready, m_coverMessage = SdPlayerKey::NoCover; ///< Statustext der Kopfzeile, Platzhaltertext des Covers
    LangIndex m_language = LangIndex::DE;            ///< Sprache der Anzeige
    uint16_t m_dirty = All;                          ///< Zu zeichnende Bereiche als Dirty-Bits
    UiCanvas m_line, m_cover;                        ///< Zeilenpuffer der Tasten, Cover
    lgfx::PointerWrapper m_fontData, m_smallData;    ///< Daten der großen und kleinen Schrift
    lgfx::VLWfont m_font, m_small;                   ///< Große und kleine Schrift
    Hit m_hit = Hit::None;                           ///< Bedienelement des laufenden Kontakts
    int m_pressRow = -1, m_pressY = 0, m_scroll = 0, m_pressScroll = 0; ///< Playlistzeile unter dem Finger. Berührpunkt Y. Scrollversatz in Pixeln. Dessen Wert beim Aufsetzen
    int m_pressX = 0, m_lastTouchY = 0;              ///< Berührpunkt X, zuletzt gemeldetes Y
    int m_ringBase = 0, m_ringScroll = 0; ///< Ringpuffer der Playlist: Inhaltszeile m_ringBase liegt in Spritezeile 0, m_ringScroll ist der zuletzt gezeichnete Versatz
    float m_scrollVelocity = 0, m_inertiaRemainder = 0; ///< Wischgeschwindigkeit in Pixeln/s, nicht verbrauchter Bruchteil eines Pixels der Trägheit
    uint32_t m_lastTouchTime = 0, m_lastScrollTime = 0, m_lastPulseTime = 0; ///< Zeiten in ms: letzte Positionsänderung, letzter Scrollschritt, letzter Puls des Pauseknopfs
    /** @brief Führt die Playlist-Trägheit und Rückfederung mit 60 Hz aus. */
    void updateScrollPhysics(uint32_t now);
    /** @brief Aktualisiert nur den mit drei Sekunden Periodendauer pulsierenden Pauseknopf. */
    void drawPause();
    bool m_cancelled = false, m_dragged = false, m_held = false; ///< Kontakt verworfen. Finger hat die Wischschwelle überschritten. Taste wurde gehalten und wiederholt
    uint32_t m_pressTime = 0, m_lastSeek = 0, m_seekTarget = 0, m_now = 0; ///< Zeiten in ms: Aufsetzen, letztes Spulen. Zielposition in Sekunden, letzte update-Zeit
    // Maße nativ aus SdPlayerTab5Layout, Originalwerte stehen dort.
    static constexpr int PLAYLIST_W = SdPlayerTab5Layout::Playlist::W;     ///< Breite der Playlist
    static constexpr int COVER_W = SdPlayerTab5Layout::Cover::W;           ///< Breite Cover
    static constexpr int COVER_H = SdPlayerTab5Layout::Cover::H;           ///< Höhe Cover
    static constexpr int COVER_X = SdPlayerTab5Layout::Cover::X;           ///< X-Position Cover
    static constexpr int COVER_Y = SdPlayerTab5Layout::Cover::Y;           ///< Y-Position Cover
    static constexpr int CASSETTE_W = SdPlayerTab5Layout::Cassette::W; ///< Breite des Kassetten-Sprites
    static constexpr int CASSETTE_H = SdPlayerTab5Layout::Cassette::H; ///< Höhe des Kassetten-Sprites
    static constexpr int CASSETTE_X = SdPlayerTab5Layout::Cassette::X; ///< X-Position der Kassette
    static constexpr int CASSETTE_Y = SdPlayerTab5Layout::Cassette::Y; ///< Y-Position der Kassette
    static constexpr int WRAP_SPR_SIZE = SdPlayerTab5Layout::Cassette::WRAP_SPRITE; ///< Kantenlänge des Naben-Sprites, fasst auch die großen Kupfer- und Goldteller
    static constexpr uint32_t WRAP_UPDATE_MS = 25;     ///< kürzester Abstand zweier Naben-Bilder, deckelt die Zeichenlast
    static constexpr int CASSETTE_DESIGN_COUNT = 8;    ///< Anzahl der Kassetten-Designs

    RadioDialogCanvas m_scene;                       ///< Szene 1280x720, wird am Ende von draw übertragen
    UiCanvas *m_tft = &m_scene;                      ///< Zeichenziel der gemeinsamen Zeichner
    UiCanvas m_cassette, m_wrap, m_dorn, m_windows[8], m_grounds[8], m_labels[8], m_knob, m_sliders, m_playlist, m_meta, m_details, m_progress, m_digitalClock, m_analogClock; ///< Sprites: Kassettenkörper, Bandwickel, Dorn. Je Design Bandfenster, Fensterhintergrund und Etikett. Drehknopf, Regler, Playlist (Ringpuffer), Metadaten, Details, Fortschritt, Digital- und Analoguhr
    UiCanvas *m_sprCassette = &m_cassette, *m_sprWrap = &m_wrap, *m_sprDorn = &m_dorn, *m_sprWin = nullptr, *m_sprWinGround = nullptr, *m_sprTapeLabel = nullptr, *m_sprCover = &m_cover, *m_sprPlaylist = &m_playlist, *m_sprRetroKnob = &m_knob, *m_sprRetroTracks = &m_sliders, *m_sprMeta = &m_meta; ///< Zeiger auf die Sprites der Zeichner. Bandfenster, Fensterhintergrund und Etikett zeigen auf die Sprites des gewählten Designs (applyCassetteDesign)
    UiButton m_buttons[9];                           ///< Tasten in der Reihenfolge Previous, Play, Pause, Stop, Next, Browse, Clear, Reset, Power
    SettingsMessageBox m_messageBox;                 ///< Bestätigung zum Leeren und Zurücksetzen
    SleepTimerDialog m_sleep{&m_scene};              ///< Schlaftimer
    SdPlaybackStatus m_state;                        ///< Letzter Audiostand, unabhängig vom Titel. Liefert Lautstärke, Balance, Klang und Uhrzeit
    uint16_t *m_bitmaps[9]{}, *m_emptyFolderIcon = nullptr, *m_muteIcon = nullptr; ///< Dekodierte Bilder, 0..7 die Kassetten, 8 der Coverplatzhalter. Symbol der leeren Playlist. Stummsymbol
    CassetteDesign m_cassetteDesigns[8]{};           ///< Geometrie und Farben der acht Kassetten
    uint8_t m_cassetteDesignIdx = 0;                 ///< Gewähltes Design
    char m_currentArtist[128]{}, m_lastArtistDrawn[128]{}; ///< Interpret des Titels, zuletzt auf der Kassette gezeichneter Interpret
    float m_wrapAngleL = 0, m_wrapAngleR = 0, m_lastWrapAngleL = -999, m_lastWrapAngleR = -999, m_totalDegrees = 0, m_lastAngle = 0; ///< Drehwinkel der Naben und der zuletzt gezeichnete in Grad. Knopfwinkel summiert (36 Grad je Lautstärkestufe), letzter Berührwinkel
    uint32_t m_lastWrapTime = 0, m_lastWrapPushMs = 0, m_dialogTime = 0; ///< Zeiten in ms: letzte Winkelfortschreibung, letzte Übertragung der Naben, Öffnen des Dialogs
    int m_lastReelRadiusL = -1, m_lastReelRadiusR = -1, m_lastDrawnSecond = -1, m_pressed = -1; ///< Zuletzt gezeichnete Wickelradien in Pixeln. Zuletzt gezeichnete Sekunde der Uhr, 7, solange der Drehknopf berührt wird, sonst -1
    bool m_lastCassettePlaying = false, m_bulk = false, m_resetDialog = false; ///< Kassette lief beim letzten Bild. Sammelzeichnen läuft. Der Dialog gehört zum Zurücksetzen statt zum Leeren
    float m_lastCassetteProgress = -1;               ///< Zuletzt gezeichneter Bandanteil der ganzen Playlist, 0..1, -1 vor dem ersten Bild
    VisMode m_drawnMode = static_cast<VisMode>(VIS_MODE_COUNT); ///< Zuletzt gezeichneter Visualizermodus
    UiCanvas m_battery;                 ///< Akkusymbol hochkant
    BatteryIcon::State m_batteryDrawn;  ///< Zuletzt gezeichneter Akkustand
    bool m_batteryPresent = false;      ///< Akku war beim letzten Aufbau der Zeile erkannt
    static constexpr float WRAP_DPS_AT_REF = 140.0f; ///< Drehgeschwindigkeit der Naben in Grad/s beim mittleren Wickelradius, kleinere Wickel drehen schneller
    /** @brief Dekodiert die neun Bildassets und reserviert Kassettenfenster und Dialogpuffer vor Wiedergabe. */
    bool prepareGraphics();
    /** @brief Überträgt einen rechteckigen Ausschnitt der deckungsgleichen Szene ohne Skalierung. */
    void presentRect(int x, int y, int w, int h);
    /** @brief Aktualisiert Sprite und gesicherten Dialoguntergrund. */
    void present(UiCanvas &sprite, int x, int y);
    /** @brief Kürzt Text an UTF-8-Grenzen anhand der Schriftbreite, ergänzt bei Platzmangel Auslassungspunkte. */
    static void truncateWithEllipsis(UiCanvas *spr, const char *in, char *out, int size, int width);
    /** @brief Entfernt führende Zahlen samt Trennzeichen aus dem Titel für Liste und Cover, ohne gültigen Trenner bleibt der Text erhalten. */
    static void cleanupFilename(const char *name, char *out, size_t size);
    /** @brief Aktualisiert Bandwickel und Naben aus bestätigtem Audiozustand. Das Band zeigt die Lage in der ganzen Playlist, jeder Titel belegt denselben Anteil. */
    void updateCassette(bool force = false);
    /** @brief Zeichnet den Titel auf dem Kassettenstreifen. */
    void drawCassetteLabel(bool force = false);
    /** @brief Übernimmt den vorbereiteten Kassettenkörper. */
    void drawCassetteBody();
    /** @brief Bereitet den Kassettendorn vor. */
    void buildWrapSprite();
    /** @brief Rotiert den Dorn über den vorbereiteten Hintergrund. */
    void pushDorn(int x, int y, float angle);
    /** @brief Zeichnet die Bandwickel mit einer zum gespeicherten Bandanteil proportionalen Ringfläche. */
    void drawReels(int left, int right);
    /** @brief Wählt bereits reservierte Kassettenpuffer, ohne Allokation. */
    void applyCassetteDesign();
    /** @brief Zeichnet den Hinweis bei leerer Playlist. */
    void drawEmptyPlaylist();
    /** @brief Zeichnet den Lautstärkedrehknopf aus dem zentralen Pegel. */
    void drawKnob(uint8_t volume);
    /** @brief Zeichnet die vier Schieberegler für Balance und EQ. */
    void drawTracks(int8_t balance, int8_t low, int8_t mid, int8_t high);
    /** @brief Berechnet die Position des Knopfmarkers. */
    void calcMarkerPos(float angle, int center, int offset, int &x, int &y);
    /** @brief Zeichnet die Uhr mit zentraler Zeit. */
    void drawClock();
    /** @brief Zeichnet das Akkusymbol links neben dem Ordner-Button. @param fresh Die Zeile wurde gerade geleert, das Symbol muss auch bei unverändertem Stand neu. */
    void drawBattery(bool fresh);
    /** @brief Zeichnet die analoge Uhr für den Goniometermodus. */
    void drawAnalogClock(UiCanvas *sprite, int &last, int32_t x, int32_t y, int minutes, int seconds);
    /** @brief Zeichnet die Digitaluhr für die übrigen Visualizermodi. */
    void drawDigitalClock(UiCanvas *sprite, int &last, int32_t x, int32_t y, int minutes, int seconds);
    /** @brief Öffnet die Löschbestätigung oder den Schlafdialog. */
    void openDialog(Hit hit);
};
#endif
