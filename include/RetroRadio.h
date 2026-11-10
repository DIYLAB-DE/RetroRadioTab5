#ifndef RETRO_RADIO_H
#define RETRO_RADIO_H
#include "VisualizerView.h"
#include "UiCanvas.h"
#include "StationManager.h"
#include "Thema.h"
#include "BatteryIcon.h"
#include "RadioTab5Layout.h"
#include "RadioPresetDialog.h"
#include "SleepTimerDialog.h"
#include "SettingsDialogs.h"

/** @brief Radiooberfläche in 1280x720 mit Senderskala, magischem Auge, Infozeile, Drehknopf, Reglern, Uhr und fünf Tasten. Zeichnet in eine displaygroße Szene und überträgt nur geänderte Rechtecke. Audio, Netz und Speicher liegen in der Zentrale: Das Modul bekommt einen Statusabbild und liefert Anforderungen zurück. Läuft im Oberflächentask. */
class RetroRadio
{
public:
    /** @brief Gibt die einmalig vorbereiteten Puffer von Stummsymbol, grauem Skalenlogo und Panelkopie der Skala frei. */
    ~RetroRadio()
    {
        free(m_muteIcon);
        free(m_logoGrau);
        free(m_panelScale);
    }
    /** @brief Anforderung des Radios an die Zentrale. */
    enum class Action : uint8_t
    {
        None,              ///< Keine Anforderung
        Select,            ///< Sender wählen: Stream stoppen, stummschalten, Auswahl merken
        Start,             ///< Stream des gewählten Senders starten, sobald der Zeiger angekommen ist
        Mute,              ///< Stummschaltung umschalten
        Home,              ///< Zurück zum MainScreen
        VisualizerChanged, ///< Visualizermodus oder Stile geändert, Einstellungen sichern
        Volume,            ///< Lautstärke setzen, value 0..100
        Tone,              ///< Klangband setzen, control 0..2 (tief, mittel, hoch), value -12..12 dB
        Balance,           ///< Balance setzen, value -16..16
        EyeChanged,        ///< Bauform des magischen Auges geändert, value 0..3
        Reset,             ///< Lautstärke, Balance, Klang und Visualizermodus nach Bestätigung im Dialog auf die Vorgaben setzen
        Sleep              ///< Sleep-Countdown abgelaufen, Gerät in den Ruhezustand versetzen
    };

    /** @brief Phase eines zentral zugeteilten Touchkontakts. */
    enum class TouchPhase : uint8_t
    {
        Press,   ///< Erster Kontakt
        Move,    ///< Kontakt bewegt sich oder bleibt liegen
        Release, ///< Kontakt gelöst
        Cancel   ///< Kontakt entzogen, etwa durch ein externes Modal
    };

    /** @brief Anforderung samt Kennung, mit der die Zentrale sie bestätigt. */
    struct Input
    {
        Action action = Action::None;                ///< Art der Anforderung
        int station = -1;                            ///< Senderindex bei Select und Start, sonst -1
        uint32_t requestId = 0, stationsRevision = 0; ///< Kennung der laufenden Senderwahl und Revision der Senderliste, auf der sie beruht
        int value = 0, control = 0;                  ///< Zahlenwert der Aktion, Reglerindex bei Tone (Balance: -1)
    };

    /** @brief Konsistentes Statusabbild der Zentrale für Anzeige und Entscheidungen des Radios. */
    struct State
    {
        LangIndex language = LangIndex::DE;                                  ///< Sprache der Benutzertexte
        uint32_t stationsRevision = 0, acknowledgedRequestId = 0, streamRequestId = 0; ///< Revision der Senderliste, zuletzt bestätigte Senderwahl, Kennung des laufenden Streams
        int selectedStation = -1, streamStation = -1;                        ///< Gewählter Sender der Zentrale und Sender des laufenden Streams, -1 ohne
        AudioManager::State streamState = AudioManager::State::Unavailable;  ///< Zustand des Audioworkers
        bool wifiConnected = false, muted = true, requestFailed = false, timeValid = false; ///< WLAN verbunden, stumm, Senderwahl oder Start abgelehnt, Uhrzeit plausibel
        uint8_t wifiSignalPercent = 0;                                       ///< WLAN-Stärke 0..100 wie auf dem MainScreen, 2 x (dBm + 100), 0 ohne gültigen Wert
        uint8_t hour = 0, minute = 0, second = 0, volume = 10;              ///< Uhrzeit, Lautstärke 0..100
        int8_t balance = 0, tone[3]{};                                       ///< Balance -16..16, Klangbänder tief, mittel, hoch in dB
        uint32_t sampleRate = 0, bitRate = 0;                                ///< Abtastrate in Hz, Bitrate in bit/s
        AudioManager::Codec codec = AudioManager::Codec::None;              ///< Decoder des laufenden Streams
        char title[256]{}, stationName[STATION_NAME_SIZE]{};                 ///< Streamtitel und Sendername, UTF-8
        BatteryIcon::State battery;                                          ///< Akkuanzeige, ohne erkannten Akku bleibt die Zeile der Buttons unverändert
    };

    /** @brief Legt Szene und Sprites an und lädt die Senderliste. @param display Display. @param stations Zentrale Senderverwaltung. @param visualizer Bereits initialisierte, gemeinsam genutzte Fassade. @return false bei bereits vorbereitetem Radio, nicht bereitem Visualizer oder fehlendem Speicher. */
    bool begin(lgfx::LGFX_Device &display, StationManager &stations, VisualizerView &visualizer);
    /** @brief Übernimmt das Statusabbild samt Anforderungskennung und stößt bei Änderungen Neuzeichnen an. @param state Wird kopiert. Streamtitel und Bitrate gelten erst, wenn der Stream zur aktuellen Senderwahl gehört. */
    void setState(const State &state);
    /** @brief Aktiviert das Radio nach einem Bildschirmwechsel, setzt die Visualizerfläche und fordert Neuzeichnen an. Der Sender wird beim nächsten update() neu gewählt. @return false, wenn das Radio nicht vorbereitet ist oder der Visualizer die Fläche ablehnt. */
    bool show();
    /** @brief Markiert alle Bereiche zum Neuzeichnen, etwa nach einem externen Modal. Löst weder Touch- noch Audioaufrufe aus. */
    void invalidate();
    /** @brief Bricht beim Verlassen Touchkontakt, Dialoge, laufenden Sleep-Countdown, vorbereiteten Start und Wiederanlauf ab. */
    void hide();
    /** @brief Treibt Zeigerfahrt, Dialoge, Wiederanlauf und Zeichnen an. Nur im aktiven Screen aufrufen. Hinter einem Radiodialog wird nur Audio abgeholt. @param now Monotone Zeit in ms. @param audio Zentraler AudioManager. */
    void update(uint32_t now, AudioManager &audio);
    /** @brief Führt die Zeigerfahrt hinter einem externen Modal ohne Zeichnen weiter. @param now Monotone Zeit in ms. */
    void advance(uint32_t now);
    /** @brief Verarbeitet den zentral zugeteilten Touchkontakt. @param phase Phase des Kontakts. @param x,y Position in Displaypixeln. @param now Monotone Zeit in ms. @return Sofort zu verarbeitende Anforderung oder None. */
    Input touch(TouchPhase phase, int x, int y, uint32_t now);
    /** @brief Wählt einen Sender und fährt den Zeiger dorthin, auch für Senderwahl über Web oder Tasten. @param index Senderindex. @param now Monotone Zeit in ms. @return Select-Anforderung für Stummschaltung und Stopp in der Zentrale, leer bei ungültigem oder leerem Slot. */
    Input selectStation(int index, uint32_t now);
    /** @brief Liefert eine vorgemerkte Anforderung oder, höchstens einmal je Senderwahl, Start nach Ankunft des Zeigers. @return Start nur, wenn die Zentrale die aktuelle Select-Anforderung bestätigt und nicht abgelehnt hat. Sonst None. */
    Input takeRequest();
    /** @brief Entwertet eine vorbereitete Senderwahl und ihren Start, etwa vor Stopp oder Screenwechsel. */
    void cancelPendingStart();

    /** @brief Ein Radiodialog (Favoriten, Sleep-Timer oder Rückfrage) ist offen. */
    bool modal() const
    {
        return m_presets.isActive() || m_sleep.isActive() || m_message.active();
    }

    /** @brief Übernimmt die Bauform des magischen Auges und erzwingt dessen Neuzeichnen. @param style 0..3, größere Werte werden ignoriert. */
    void setEyeStyle(uint8_t style)
    {
        if (style < 4 && m_eyeStyle != style)
        {
            m_eyeStyle = style;
            m_eyeLevel = -1;
            m_eyeStepUs = 0;
        }
    }

    /** @brief Aktueller Modus des Visualizers, VIS_ANALOG ohne Visualizer. */
    VisMode visualizerMode() const
    {
        return m_visualizer ? m_visualizer->mode() : VisMode::VIS_ANALOG;
    }

    /** @brief Wechselt den Visualizermodus für die zentralen Einstellungen. @param direction Richtung, 1 vorwärts, -1 rückwärts. */
    void nextVisualizer(int direction);

private:
    lgfx::LGFX_Device *m_display = nullptr;       ///< Display, auf das die Szene übertragen wird
    StationManager *m_stations = nullptr;         ///< Zentrale Senderverwaltung
    VisualizerView *m_visualizer = nullptr;       ///< Gemeinsam genutzte Visualizerfassade
    RadioDialogCanvas m_scene;                    ///< Displaygroße Szene, aus der alle Rechtecke übertragen werden
    UiCanvas *m_tft = &m_scene;                   ///< Zeichenziel der Szene
    UiCanvas m_scale, m_div, m_footer, m_stereo, m_codec, m_knob, m_tracks, m_eye, m_analogClock, m_digitalClock; ///< Sprites für Skala, Skalenteiler, Infozeile, Stereo-Symbol, Codec-Feld, Drehknopf, Regler, Auge und beide Uhren
    UiCanvas m_strip;                             ///< Zwischenpuffer NEEDLE_STAGING_W x Skalenhöhe, aus dem die Nadelstreifen ans Display gehen. Der Cacheabgleich der Quelle umfasst so nur den Streifen
    UiCanvas *m_sprRetroScale = &m_scale, *m_sprRetroDiv = &m_div, *m_sprRetroFooter = &m_footer, *m_sprRetroStereo = &m_stereo, *m_sprRetroKnob = &m_knob, *m_sprRetroTracks = &m_tracks, *m_sprMagicEye = &m_eye; ///< Zeiger auf die Sprites in den übernommenen Zeichenfunktionen
    UiButton m_buttons[5];                        ///< Tasten: Favoriten, voriger Sender, nächster Sender, Reset, Sleep-Timer/Aus
    RadioPresetDialog m_presets{&m_scene};        ///< Favoritendialog
    SleepTimerDialog m_sleep{&m_scene};           ///< Sleep-Timer-Dialog samt Countdown
    SettingsMessageBox m_message;                 ///< Rückfrage vor dem Zurücksetzen
    Station m_stationList[MAX_STATIONS];          ///< Kopie der Senderliste in Skalenreihenfolge samt Skalenposition
    int m_stationCounter = 0;                     ///< Anzahl gültiger Einträge in m_stationList
    uint16_t *m_muteIcon = nullptr;               ///< Vorbereitetes Stummsymbol für den Drehknopf, im Destruktor freigegeben
    uint16_t *m_logoGrau = nullptr;               ///< Skalenlogo als graue Kopie bei MerkmalGraustufen, sonst nullptr. Im Destruktor freigegeben
    uint16_t *m_panelScale = nullptr;             ///< Skala in Panelzeilen: Zeile x enthält Querformatspalte x mit Scale::HEIGHT Pixeln in Panelrichtung, RGB565 ohne Bytetausch wie im DSI-Framebuffer. nullptr ohne Speicher, im Destruktor freigegeben
    int m_panelRotation = -1;                     ///< Rotation, für die m_panelScale aufgebaut ist, -1 = ungültig, dann zeichnet die Nadel über die Szene
    bool m_panelStale = true;                     ///< Panelkopie passt nicht mehr zur Senderliste. setStations setzt es, drawScale baut die Kopie dann neu
    int m_sceneNeedle = -1, m_sceneNeedleSub = -1; ///< Nadel in der Szene in ganzen Pixeln und Teilschritten. Während einer Fahrt auf dem Panelweg bleibt sie an der Startstelle, -1 = keine
    State m_state{};                              ///< Letztes übernommenes Statusabbild
    Input m_pending{};                            ///< Vorgemerkte Anforderung für takeRequest()
    bool m_ready = false, m_haveState = false, m_dirty = true, m_scaleDirty = true, m_metadataDirty = true, m_toolbarDirty = true, m_knobDirty = true, m_tracksDirty = true, m_bulk = false; ///< begin() erfolgreich, Status übernommen, Szene/Skala/Infozeile/Tasten/Drehknopf/Schieberegler neu zeichnen, Übertragung bis zum Sammelblock zurückgestellt
    bool m_tuning = false, m_allowStart = false, m_arrived = false, m_motionDrawing = true, m_touchActive = false, m_eyeDone = false, m_startOnShow = false; ///< Zeiger fährt, Start nach Ankunft erlaubt, Zeiger angekommen, Zeiger während der Fahrt zeichnen, Touchkontakt aktiv, Augenwechsel dieses Kontakts erledigt, Sender nach show() neu wählen
    int m_target = -1, m_pressed = -1, m_oldNeedle = -1, m_oldNeedleSub = -1, m_lastGoodPosition = -1, m_eyeLevel = -1, m_lastDrawnSecond = -1; ///< Zielsender, gedrückte Fläche (0..4 Tasten, 6 Auge, 7 Knopfring, 8 Knopfmitte), zuletzt gezeichneter Zeiger in ganzen Pixeln und in Teilschritten (NEEDLE_SUBSTEPS je Pixel), Zeigerposition vor der Fahrt, zuletzt gezeichneter Augenwert, zuletzt gezeichnete Uhrsekunde, -1 = ungültig
    float m_needleX = RadioTab5Layout::Scale::STATION_X0, m_needleTarget = RadioTab5Layout::Scale::STATION_X0, m_needleVelocity = 0, m_totalDegrees = 0, m_lastAngle = 0; ///< Zeigerposition und -ziel in Skalenpixeln, Zeigergeschwindigkeit in Pixel/s, Knopfdrehung in Grad (36 je Lautstärkestufe) und letzter Touchwinkel
    uint8_t m_eyeStyle = 0;                       ///< Bauform des magischen Auges 0..3

    /** @brief Zähler der laufenden Nadelfahrt für die Logzeile NADEL bei Ankunft. */
    struct MotionStats
    {
        uint32_t steps = 0;     ///< gezeichnete Schritte
        uint32_t missed = 0;    ///< zwischen zwei Schritten verpasste Displaybilder
        uint32_t maxDtMs = 0;   ///< längster Zeitschritt in ms
        uint32_t lastFrame = 0; ///< Bildzähler beim letzten Schritt, 0 vor dem ersten
        uint32_t maxNeedleUs = 0; ///< längste Dauer von drawNeedle in µs
        uint32_t maxRestUs = 0;   ///< längste Zeit vom Ende eines Schritts bis zum nächsten Aufruf, also der übrige Schleifendurchlauf, in µs
        uint32_t endUs = 0;       ///< micros() am Ende des letzten Schritts, 0 vor dem ersten
        uint32_t maxVisUs = 0;    ///< längste Dauer des Visualizer-Updates in µs
        uint32_t maxClockUs = 0;  ///< längste Dauer von drawClock in µs
        uint32_t maxOutsideUs = 0; ///< längste Zeit zwischen dem Ende von update() und dem nächsten Aufruf, also die übrige Hauptschleife, in µs
        uint32_t leaveUs = 0;     ///< micros() am Ende des letzten update(), 0 vor dem ersten
        uint32_t visSkipped = 0;  ///< Visualizerbilder, die während der Fahrt aus Zeitmangel ausfielen
        uint32_t eyeDrawn = 0;    ///< während der Fahrt fertig gezeichnete Augenbilder
        uint32_t eyeSkipped = 0;  ///< Durchläufe, in denen ein Augenschritt während der Fahrt aus Zeitmangel auf das nächste Displaybild warten musste
    } m_motion;

    uint32_t m_visCostUs = 0, m_clockCostUs = 0; ///< längste gemessene Dauer des Visualizer-Updates seit dem letzten Moduswechsel und von drawClock seit dem Start, für die Restzeitprüfung während der Fahrt. Sie bleiben über show() erhalten, damit die erste Fahrt danach nicht mit 0 rechnet
    uint32_t m_eyeStepUs = 0; ///< längste gemessene Dauer eines Augenschritts (EyeRowsPerStep Zeilen, im ersten mit Löschen und Ring, im letzten mit Übertragung) seit dem letzten Bauformwechsel, für die Restzeitprüfung während der Fahrt
    int m_eyeBuilding = -1, m_eyeRow = 0; ///< Ausschlag des Augenbildes im Aufbau, -1 ohne, und seine nächste Spritezeile
    bool m_visSilencePending = false; ///< Die Fahrt hat aus dem Stand begonnen. Das nächste Visualizerbild zeigt die Ruhestellung, auch ohne Restzeit

    uint32_t m_requestId = 0, m_expectedStreamId = 0, m_lastMotion = 0, m_pressTime = 0, m_dialogTime = 0; ///< Kennung der aktuellen Senderwahl (0 ungültig), Kennung, die der Stream dazu tragen muss, Zeit der letzten Zeigerbewegung, Beginn des Touchkontakts, letzte Dialogbedienung. Zeiten in ms
    char m_footerStation[STATION_NAME_SIZE]{}, m_footerTitle[256]{}, m_footerRate[24]{}; ///< Zuletzt gezeigter Text der Infozeile
    bool m_footerShown = false; ///< false erzwingt das nächste Zeichnen der Infozeile
    int8_t m_stereoShown = -1;  ///< Zuletzt gezeigtes Stereo-Symbol: 1 mono, 0 stereo, -1 unbekannt
    int8_t m_codecShown = -1;   ///< Zuletzt gezeigtes Codec-Feld als Wert von AudioManager::Codec, -1 erzwingt das Zeichnen
    int8_t m_stereoStable = 1;  ///< Entprellter Signalzustand für das Symbol: 1 mono, 0 stereo. Vor dem ersten Signal gedimmt wie im Original
    bool m_monoCandidate = true; ///< Letztes Ergebnis von AudioManager::isMono() vor der Entprellung
    uint32_t m_monoSince = 0;   ///< Seit wann dieses Ergebnis unverändert gilt, in ms
    uint32_t m_retryAt = 0, m_retryDelay = 0; ///< Nächster selbsttätiger Wiederanlauf nach Streamabbruch und dessen wachsender Abstand
    char m_truncatedTitle[256]{};             ///< Arbeitspuffer für den gekürzten Titel der Infozeile
    VisMode m_drawnMode = static_cast<VisMode>(VIS_MODE_COUNT); ///< Zuletzt gezeichneter Visualizermodus, ungültig bis zum ersten Zeichnen
    UiCanvas m_battery;                 ///< Akkusymbol hochkant
    BatteryIcon::State m_batteryDrawn;  ///< Zuletzt gezeichneter Akkustand
    bool m_batteryPresent = false;      ///< Akku war beim letzten Aufbau der Zeile erkannt
    /** @brief Kopiert den zentralen Sendersnapshot in die Skalenreihenfolge. */
    void loadStations();
    /** @brief Überträgt einen vorbereiteten Sprite und aktualisiert den Dialoguntergrund. */
    void present(UiCanvas &sprite, int x, int y);
    /** @brief Überträgt ein Rechteck der displaygroßen Szene unverändert an dieselbe Displayposition. */
    void presentRect(int x, int y, int w, int h);
    /** @brief Baut den statischen Hintergrund auf. */
    void drawScene();
    /** @brief Zeichnet die Senderskala einschließlich Logo und baut die Panelkopie für die Nadel auf. */
    void drawScale();
    /** @brief Setzt den Zeiger auf die aktuelle Position in Teilschritten von 1/NEEDLE_SUBSTEPS px. Mit Panelkopie schreibt er den Streifen aus alter und neuer Stelle direkt in die Panelzeilen des Framebuffers. Im Sammelblock, bei Sprüngen breiter als NEEDLE_STAGING_W und ohne Panelkopie setzt er ihn in der Szene zusammen und überträgt ein Rechteck. Im Stand steht die Nadel auch in der Szene. */
    void drawNeedle();
    /** @brief Schreibt die Skala in den Spalten at bis at + NEEDLE_STRIP - 1 aus m_scale in die Szene zurück, ohne Übertragung. */
    void restoreSceneStrip(int at);
    /** @brief Zeichnet die Nadel bei x mit Bruchteil frac (0 bis NEEDLE_SUBSTEPS - 1) in die Szene, nachdem die bisherige Szenennadel entfernt wurde. Ohne Übertragung. */
    void putSceneNeedle(int x, int frac);
    /** @brief Bringt die Szenennadel auf die Stelle der Displaynadel, damit Übertragungen aus der Szene (Dialoge, Vollbild) keine alte Nadel zeigen. */
    void syncSceneNeedle();
    /** @brief Bewegt die Nadel beschleunigt und gebremst zum bestätigten Ziel, während der sichtbaren Fahrt wartet sie je Schritt auf die Fertigmeldung eines Displaybildes und zeichnet direkt danach. @param now Monotone Zeit in ms, mit Bildsignal gilt stattdessen der Zeitpunkt der Meldung. */
    void advanceNeedle(uint32_t now);
    /** @brief Während der sichtbaren Nadelfahrt: Passt eine Arbeit von neededUs noch vor das nächste Displaybild? Außerhalb der Fahrt und ohne Bildsignal immer wahr. */
    bool frameTimeLeft(uint32_t neededUs) const;
    /** @brief Zeichnet das magische Auge mit dem Ausschlag aus calculateMagicEyeValue(), nur wenn er sich geändert hat. Das Bild entsteht in Zeilenschritten und erscheint nach dem letzten. Während der Nadelfahrt zeichnet es nur so viele Schritte, wie noch vor das nächste Displaybild passen (frameTimeLeft), und setzt im nächsten Durchlauf fort. */
    void drawEye();
    /** @brief Berechnet die WLAN-Anzeige des magischen Auges mit Übergang während der Zeigerfahrt. @return Ausschlag 0..100. An der Ziel- oder vorherigen Senderposition die WLAN-Stärke, 0 bis -85 dBm, 50 bei -70 dBm, 100 ab -55 dBm, dazwischen linear. Beim Ankommen steigt er über 48 Skalenpixel dorthin, beim Wegfahren fällt er über 10 Skalenpixel auf 0. Ohne WLAN oder gültige Zeigerposition 0. */
    int32_t calculateMagicEyeValue();
    /** @brief Zeichnet das magische Auge in der eingestellten Bauform. @param value Ausschlag 0..100 aus calculateMagicEyeValue(). @param rowFrom,rowTo Nur die Spritezeilen rowFrom bis rowTo - 1. Ab Zeile 0 löscht es das Sprite und zeichnet den Ring, bis zur letzten Zeile überträgt es das Sprite. Die Vorgabe zeichnet das ganze Bild. */
    void drawMagicEye(bool init, int32_t x, int32_t y, int32_t value, int rowFrom = 0, int rowTo = RadioTab5Layout::MagicEye::SIZE);
    /** @brief Zeichnet das Stereo-/Mono-Symbol. */
    void updateStereoIndicator(bool mono);
    /** @brief Zeichnet das Codec-Feld links in der Infozeile als Formatmarke wie im SD-Player, ohne Decoder leer. */
    void updateCodecIndicator(AudioManager::Codec codec);
    /** @brief Zeichnet die einzelne Infozeile mit UTF-8-Kürzung. */
    void drawFooterText(bool init, const char *sender = "", const char *title = "", const char *bitrate = "", uint16_t senderColor = farbe(Rolle::GemAkzent), uint16_t titleColor = farbe(Rolle::GemTextSilber), uint16_t bitrateColor = farbe(Rolle::RadBitrate), uint16_t separatorColor = farbe(Rolle::GemTextGedaempft), const char *separator = " | ", int reservedPixels = RadioTab5Layout::Footer::RESERVED, uint16_t backgroundColor = farbe(Rolle::GemFlaeche));
    /** @brief Zeichnet Lautstärkeknopf und Balance-/EQ-Schieberegler aus dem zentralen Status, jeweils nur wenn als geändert markiert (Knopf rund 9 ms, Schieberegler rund 14 ms). */
    void drawControls();
    /** @brief Zeichnet den Lautstärkedrehknopf ohne Dienstzugriff. */
    void drawKnob(uint8_t vol);
    /** @brief Zeichnet die vier Schieberegler für Balance und EQ ohne Dienstzugriff. */
    void drawTracks(int8_t balance, int8_t gainLowPass, int8_t gainBandPass, int8_t gainHighPass);
    /** @brief Berechnet die Markerposition des Drehknopfs. */
    void calcMarkerPos(float angleDeg, int center, int radiusOffset, int &outX, int &outY);
    /** @brief Zeichnet die fünf Bedientasten mit ihrem aktuellen Druckzustand. */
    void drawButtons();
    /** @brief Zeichnet die vom Visualizermodus abhängige Uhr mit optionalem Timer. */
    void drawClock();
    /** @brief Zeichnet das Akkusymbol links neben dem Favoriten-Button, im Goniometermodus mittig zwischen Pegelsäulen und Favoriten-Button. @param fresh Die Zeile wurde gerade geleert, das Symbol muss auch bei unverändertem Stand neu. */
    void drawBattery(bool fresh);
    /** @brief Zeichnet eine analoge Uhr aus dem zentralen Zeitstatus. */
    void drawAnalogClock(UiCanvas *sprite, int &lastDrawnSecond, int32_t x, int32_t y, int remainingMinutes, int remainingSeconds);
    /** @brief Zeichnet eine digitale Uhr aus dem zentralen Zeitstatus. */
    void drawDigitalClock(UiCanvas *sprite, int &lastDrawnSecond, int32_t x, int32_t y, int remainingMinutes, int remainingSeconds);
    /** @brief Ermittelt den Tastenindex für die logische Trefferposition. */
    int hitButton(int x, int y) const;
    /** @brief Verpackt eine Anforderung mit Revision und Identität. */
    Input command(Action action, int station = -1);
    /** @brief Öffnet den Favoriten-, Reset- oder Schlafdialog. */
    void openDialog(int button, uint32_t now);
    /** @brief Liefert den Text in der zentral gewählten Sprache. */
    const char *text(RetroRadioKey key) const;
};
#endif
