#ifndef SETTINGS_H
#define SETTINGS_H
#include "Keyboard.h"
#include "WifiList.h"
#include "MessageBox.h"
#include "UiCanvas.h"
#include "SettingsDialogs.h"
#include "UiButton.h"
#include "Numpad.h"
#include "DotMatrix.h"
#include "SettingsTexts.h"
#include "BtProtocol.h"
#include "BatteryIcon.h"

/** @brief Konfiguration nativ in 1280x720 (Geometrie in SettingsTab5Layout.h). Besitzt nur UI-Entwurf, niemals zentrale Dienste oder Persistenz. */
class Settings
{
public:
    static constexpr size_t ProfileCount = 3;  ///< Anzahl der WLAN-Profile
    static constexpr uint32_t DayMask = 0x00ffffff; ///< Gültige Bits eines Tages der Zeitfenster, Bit n = Stunde n
    /** @brief Konfigurationsseiten in der Reihenfolge des Menüs. */
    enum class Panel : uint8_t
    {
        Wifi,       ///< WLAN-Profile und Netzwerkname
        Bluetooth,  ///< Bluetooth-Gerät am externen Sender. Ändert keine gespeicherte Konfiguration
        Ukw,        ///< UKW-Modulator am externen Sender: Schalter, Frequenz und Leistung, gespeichert in der Konfiguration
        Time,       ///< Zeitzone, NTP-Server, Intervall und manuelle Uhrzeit
        Window,     ///< Zeitfenster je Wochentag und Stunde
        Language,   ///< Allgemein: Sprache und serielle Logs
        Security,   ///< PIN-Sperre und PIN
        Sound,      ///< Klangwerte
        Appearance, ///< Magisches Auge und VU-Meter
        Theme,      ///< Farbthema, wirkt nach dem Neustart
        Screen,     ///< Helligkeit und Drehung
        Info        ///< Geräte- und Netzwerkinformationen
    };
    /** @brief Art eines Auftrags an den zentralen Dienst. */
    enum class RequestType : uint8_t
    {
        None,            ///< Kein Auftrag
        Scan,            ///< WLAN-Suche starten
        CancelScan,      ///< Laufende Suche abbrechen, Ziel in targetId
        TestProfile,     ///< WLAN-Profil probeweise verbinden, Profil und Index im Auftrag
        CancelTest,      ///< Laufenden Verbindungstest abbrechen
        Save,            ///< Entwurf speichern, geänderte Felder in changedFields
        SetTime,         ///< Uhrzeit aus localTime und second setzen
        ChangePin,       ///< PIN setzen oder ändern, neue PIN in pin
        RemovePin,       ///< PIN entfernen, bestätigt mit currentPin
        Mute,            ///< Stummschaltung umschalten
        Home,            ///< Zurück zum MainScreen
        Restart,         ///< Neustart
        PreviewSound,    ///< Klangwerte des Entwurfs hörbar machen, ohne zu speichern
        PreviewPlayback, ///< Hörprobe starten oder stoppen
        SetVolume,       ///< Lautstärke der Hörprobe setzen
        ResetDefaults,   ///< Alle Einstellungen auf die Vorgaben zurücksetzen, baselineRevision nennt den bekannten Stand
        PreviewScreen,   ///< Helligkeit und Drehung des Entwurfs anzeigen, ohne zu speichern
        PreviewUkw,      ///< UKW-Werte des Entwurfs an den Sender geben, ohne zu speichern
        BtSearch,        ///< Bluetooth-Sender: Suche starten
        BtConnect,       ///< Bluetooth-Sender: mit btAddress verbinden
        BtDisconnect,    ///< Bluetooth-Sender: Suche, Versuch oder Verbindung beenden
        BtForget,        ///< Bluetooth-Sender: trennen und Kopplung samt gespeichertem Gerät löschen
        BtTone           ///< Bluetooth-Sender: kurzer leiser Testton am verbundenen Gerät
    };
    static constexpr uint8_t BrightnessMin = 10, BrightnessMax = 100, BrightnessStep = 10, BrightnessDefault = 40; ///< Hintergrundlicht in Prozent
    static constexpr uint8_t RotationNormal = 3, RotationFlipped = 1; ///< Querformat und um 180 Grad gedrehtes Querformat
    static constexpr uint32_t UkwMinKhz = 87500, UkwMaxKhz = 108000, UkwStepKhz = 100, UkwDefaultKhz = 87500; ///< Frequenzbereich der UKW-Seite in kHz: europäisches Band in Schritten von 0,1 MHz. Der Chip könnte ab 76 MHz
    static constexpr uint8_t UkwMaxPower = 15;                       ///< Größte Leistungsstufe des Modulators (124 dBµV), 0 die kleinste (101,5 dBµV)
    /** @brief Stand eines zentral bearbeiteten Auftrags. */
    enum class Outcome : uint8_t
    {
        Running,   ///< Läuft noch
        Success,   ///< Erfolgreich abgeschlossen
        Failed,    ///< Fehlgeschlagen, Grund in Error
        Cancelled  ///< Abgebrochen
    };
    /** @brief Fehlergrund eines Auftrags. */
    enum class Error : uint8_t
    {
        None,           ///< Kein Fehler
        Unavailable,    ///< Funktion nicht vorhanden
        Busy,           ///< Dienst beschäftigt
        Invalid,        ///< Ungültige Eingabe
        Conflict,       ///< Gespeicherter Stand hat sich inzwischen geändert
        Authentication, ///< Anmeldung am Netzwerk abgelehnt
        NoNetwork,      ///< Netzwerk nicht gefunden
        NoDhcp,         ///< Keine Adresse vom DHCP-Server
        Timeout,        ///< Zeitüberschreitung
        Storage,        ///< Speichern fehlgeschlagen
        Service         ///< Sonstiger Fehler des Dienstes
    };

    /** @brief Bitmaske der Konfigurationsbereiche, die ein Auftrag betrifft. */
    enum Field : uint32_t
    {
        Profile0 = 1u,      ///< WLAN-Profil 1
        Profile1 = 2u,      ///< WLAN-Profil 2
        Profile2 = 4u,      ///< WLAN-Profil 3
        Hostname = 8u,      ///< Netzwerkname
        Time = 16u,         ///< Zeitzone, NTP-Server und Intervall
        Windows = 32u,      ///< Zeitfenster
        Language = 64u,     ///< Sprache
        Appearance = 128u,  ///< Magisches Auge und VU-Meter
        Pin = 256u,         ///< PIN
        SoundField = 512u,  ///< Klangwerte
        Screen = 1024u,     ///< Helligkeit und Drehung
        Theme = 2048u,      ///< Themenindex
        Ukw = 4096u,        ///< UKW-Sender: Schalter, Frequenz und Leistung
        SerialLogs = 8192u, ///< Serielle Logs über USB
        PinLock = 16384u,   ///< PIN-Sperre für Display und Webportal
        SecondHand = 32768u ///< Sekundenzeiger der Uhr im MainScreen: schleichend oder in Sekundenschritten
    };

    /** @brief WLAN-Zugangsdaten eines Profils. */
    struct Profile
    {
        char ssid[33] = {};     ///< Netzwerkname, UTF-8, leer = Profil unbelegt
        char password[65] = {}; ///< Passphrase oder 64 Hexziffern, leer = offenes Netz
        bool enabled = false;   ///< Profil ist für die Verbindung freigegeben
    };

    /** @brief Speicherbare Konfiguration, Zeichenketten sind UTF-8 und terminiert. */
    struct Configuration
    {
        Profile profiles[ProfileCount];                                                          ///< WLAN-Profile
        char hostname[33] = {}, ntpServer[64] = {}, timezone[64] = {}, timezonePosix[64] = {};   ///< Netzwerkname, NTP-Server, Zeitzonenname und dessen POSIX-Regel
        uint32_t syncIntervalMs = 0, timeWindows[7] = {};                                        ///< NTP-Intervall in ms, je Wochentag eine Stundenmaske (DayMask)
        LangIndex language = LangIndex::DE;                                                      ///< Bedienungssprache
        uint8_t eyeStyle = 0, vuBallistics = 1, vuSensitivity = 2;                               ///< Röhrenstil 0..3, VU-Trägheit 0..2, VU-Bezugspegel 0..4
        uint8_t brightness = BrightnessDefault, rotation = RotationNormal;                       ///< Hintergrundlicht in Prozent, feste Rotation 3 oder 1
        bool autoRotate = true;                                                                  ///< Drehung nach dem Lagesensor, ohne Sensor gilt die feste Rotation
        uint8_t theme = 0;                                                                       ///< Themenindex nach Thema.h: 0 Retro, 1 Modern, 2 E-Paper. Wirkt nach dem Neustart
        bool ukwEnabled = false;                                                                 ///< UKW-Sender des Pico an
        uint32_t ukwKhz = UkwDefaultKhz;                                                         ///< Seine Frequenz in kHz
        uint8_t ukwPower = 0;                                                                    ///< Seine Leistungsstufe 0..UkwMaxPower
        bool serialLogs = false;                                                                 ///< Serielle Logs über USB, ab Werk aus wie im Original. Antworten auf USB-Befehle kommen immer
        bool pinLock = true;                                                                     ///< Gemeinsame PIN-Sperre für Display und Webportal, ab Werk ein wie im Original. Ausschalten verlangt die aktuelle PIN
        bool secondSweep = true;                                                                 ///< Sekundenzeiger im MainScreen schleichend, ab Werk ein. Aus: Sekundenschritte, wie sie das Original zeigt
    };

    /** @brief Klangwerte getrennt vom binären Konfigurationscontainer. */
    struct Sound
    {
        uint16_t eqLow = 500, eqPeak = 1800, eqHigh = 6000; ///< Eckfrequenzen von Tiefen, Mitten und Höhen in Hz
        uint8_t width = 100, loudness = 0, volume = 40;     ///< Basisbreite 0..200 %, Loudness 0..100 %, Lautstärke 0..maxVolume
        bool speakers = true, playing = false;              ///< Lautsprecher freigegeben, Hörprobe läuft
    };

    /** @brief Zentral vorhandene Funktionen, fehlende bleiben in der Ansicht gesperrt. */
    struct Capabilities
    {
        bool save = false, wifiProfiles = false, wifiScan = false, wifiTest = false;            ///< Speichern, Profile ändern, Suchen, Verbindung testen
        bool time = false, manualTime = false, timeWindows = false, language = false;           ///< Zeiteinstellungen, manuelle Uhrzeit, Zeitfenster, Sprache
        bool pinChange = false;                                                                 ///< PIN ändern
        bool sound = false, appearance = false, restart = false, mute = false, screen = false;  ///< Klang, Anzeige, Neustart, Stumm, Bildschirm
        bool theme = false;                                                                     ///< Thema
        bool ukw = false;                                                                       ///< UKW-Sender
        uint8_t maxVolume = 100;                                                                 ///< Obergrenze der Lautstärke
    };

    /** @brief Lokales Datum mit Uhrzeit ohne Sekunden. */
    struct LocalTime
    {
        uint16_t year = 0;                                ///< Jahr 2000 bis 2099
        uint8_t month = 0, day = 0, hour = 0, minute = 0; ///< Monat 1..12, Tag 1..31, Stunde 0..23, Minute 0..59
    };
    /** @brief Ergebnis des einmaligen I2C-Startscans eines Busses. */
    enum class BusScan : uint8_t
    {
        NotChecked, ///< Noch nicht geprüft
        Done,       ///< Geprüft, Adressen in der Liste
        Blocked     ///< Bus belegt oder gestört
    };

    /** @brief Zentraler Zustand für die Anzeige, Zeichenketten sind terminiert. */
    struct Status
    {
        uint64_t revision = 0;                                                                                                   ///< Steigt mit jedem Stand, ältere werden verworfen
        bool timeValid = false, ntpConfirmed = false, wifiConnected = false, signalValid = false, pinKnown = false, pinSet = false; ///< Uhrzeit plausibel, per NTP bestätigt, WLAN verbunden, Signalwerte gültig, PIN-Zustand bekannt, PIN gesetzt
        LocalTime localTime;                                                                                                     ///< Lokale Uhrzeit
        uint8_t ipv4[4] = {}, gateway[4] = {}, channel = 0, second = 0;                                                          ///< Adresse, Gateway, WLAN-Kanal, Sekunde der Uhrzeit
        Sound sound;                                                                                                             ///< Aktueller Klangzustand
        float rmsDb = -60.0f;                                                                                                    ///< Ausgangspegel in dB
        bool rmsValid = false;                                                                                                   ///< Pegel liegt vor
        uint32_t heapFree = 0, psramFree = 0, firmwareBytes = 0;                                                                 ///< Freier interner Heap und PSRAM, Firmwaregröße in Bytes
        int16_t rssi = 0;                                                                                                        ///< Signalstärke in dBm
        char device[64] = {}, firmware[64] = {}, connectedSsid[33] = {}, mac[18] = {};                                           ///< Gerätename, Firmwareversion, verbundenes Netzwerk, MAC-Adresse als Text
        uint32_t uptimeSeconds = 0, flashBytes = 0, psramBytes = 0;                                                              ///< Laufzeit in s, Größe von Flash und PSRAM in Bytes
        BusScan i2cInternal = BusScan::NotChecked, i2cPortA = BusScan::NotChecked;                                               ///< Startscan des internen Busses und von Port A
        char i2cInternalList[64] = {}, i2cPortAList[48] = {};                                                                    ///< Antwortende Adressen, kommagetrennt
        bool chipTempValid = false;                                                                                              ///< Interner Temperatursensor des ESP32-P4 gelesen
        int16_t chipTempC10 = 0;                                                                                                 ///< Chiptemperatur in Zehntel Grad Celsius
        bool ftpEnabled = false;                                                                                                 ///< FTP-Dienst eingeschaltet, der Schalter gilt nur im RAM
        BatteryIcon::State battery;                                                                                              ///< Akkuanzeige in der Fußzeile, ohne erkannten Akku bleibt die Fußzeile wie im Original
        uint16_t batteryCapacityMah = 0;                                                                                         ///< Akkugröße in mAh für die Info-Seite
        bool batteryLearned = false;                                                                                             ///< Akkugröße stammt aus einer vollständigen Ladung statt aus der Vorgabe
    };

    /** @brief Kopierbarer zentraler Auftrag, nur typzugehörige Felder verwenden und Geheimnisse danach löschen. */
    struct Request
    {
        RequestType type = RequestType::None;                  ///< Art des Auftrags
        uint32_t id = 0, targetId = 0, changedFields = 0;      ///< Kennung (nie 0), Kennung des abzubrechenden Auftrags, Field-Maske des Speicherns
        uint64_t baselineRevision = 0;                         ///< Revision des gespeicherten Standes, auf dem der Entwurf beruht
        Configuration configuration;                           ///< Zu speichernder Entwurf (Save), nur die Bereiche aus changedFields sind gefüllt. Bei PreviewScreen Helligkeit und Drehung
        Sound sound;                                           ///< Klangwerte (Save und alle Vorschauen)
        uint8_t profileIndex = 0, second = 0;                  ///< Profilnummer des Tests, Sekunde für SetTime
        Profile profile;                                       ///< Zu testendes Profil
        LocalTime localTime;                                   ///< Zu setzende Uhrzeit
        char pin[5] = {}, currentPin[5] = {};                  ///< Vierstellige neue und bisherige PIN, nach Verarbeitung zu löschen. currentPin bestätigt auch das Ausschalten der PIN-Sperre
        char btAddress[BtProtocol::AddressSize] = {};          ///< Ziel von BtConnect als „AA:BB:CC:DD:EE:FF“
    };

    /** @brief Meldung des zentralen Dienstes über Fortgang oder Ende eines Auftrags. */
    struct Completion
    {
        RequestType type = RequestType::None;  ///< Art des gemeldeten Auftrags
        uint32_t id = 0;                       ///< Kennung des Auftrags
        Outcome outcome = Outcome::Failed;     ///< Stand
        Error error = Error::Service;          ///< Grund bei Failed
        uint8_t ipv4[4] = {};                  ///< Erhaltene Adresse, beim Verbindungstest Voraussetzung für Erfolg
    };

    Settings() = default;

    /** @brief Gibt reservierte UI-Ressourcen und Eingabepuffer frei. */
    ~Settings()
    {
        end();
    }

    Settings(const Settings &) = delete;
    Settings &operator=(const Settings &) = delete;
    /** @brief Bereitet Szene, Arbeitsbereich und Schriften einmalig vor. @param display Display mit Rotation und 1280x720. @return Initialisierungserfolg. */
    bool begin(lgfx::LGFX_Device &display);
    /** @brief Gibt UI-Ressourcen und Geheimnisse frei, nur nach Abwicklung zentraler Aufträge aufrufen. */
    void end();
    /** @brief Zeigt die Konfiguration. @param language Zentral gültige Sprache. */
    void show(LangIndex language);
    /** @brief Verbirgt die Ansicht und entwertet laufende Kontakte, ohne Speicheraufträge als abgebrochen auszugeben. */
    void hide();
    /** @brief Markiert die ganze Ansicht für erneute Ausgabe. */
    void invalidate();
    /** @brief Zeichnet nur bei Änderungen. Ruft keine Dienste auf. @param now Zentrale Millisekunden, für Cursor, Dialogschutz und Fortschritt. */
    void update(uint32_t now);
    /** @brief Verarbeitet einen zentral gelieferten Kontakt. @param phase Phase. @param x,y Position in Displaypixeln. @return true, wenn die sichtbare Ansicht den Kontakt beansprucht. */
    bool touch(SettingsUi::TouchPhase phase, int x, int y);
    /** @brief Übernimmt tatsächliche Fähigkeiten, unzulässige Aktionen bleiben gesperrt. @param capabilities Zentral vorhandene Dienste. */
    void setCapabilities(const Capabilities &capabilities);
    /** @brief Übernimmt eine gespeicherte Konfiguration oder merkt sie bei lokalem Entwurf als Konflikt vor. @param config Vollständiger kopierbarer Stand. @param revision Monoton wachsende Speicherrevision. @return false bei ungültigen Puffern/veraltetem Stand. */
    bool setConfiguration(const Configuration &config, uint64_t revision);
    /** @brief Übernimmt tatsächliche Statusdaten mit steigender Revision. @param status Zentraler Status. @return false bei ungültigem/veraltetem Stand. */
    bool setStatus(const Status &status);
    /**
     * @brief Übernimmt den Stand des Bluetooth-Senders. Nur bei geändertem Änderungszähler aufrufen. Bewertet laufende Suche und Verbindung der Bluetooth-Seite, zeichnet die UKW-Seite bei geändertem Modulatorstand neu, ändert keine gespeicherte Konfiguration.
     * @param present Pico angeschlossen, UART offen und HALLO beantwortet
     * @param link Zustand, Gerät, Treffer, UKW-Modulator und letzter Fehler laut Pico
     * @param version Änderungszähler von BtSender, eine Fehlerkennung zählt erst ab der zweiten Änderung nach einem Auftrag
     */
    void setBluetooth(bool present, const BtProtocol::LinkView &link, uint32_t version);
    /** @brief Liefert zuerst Stumm, danach FIFO-Aufträge. @param request Empfangspuffer. Nach Verarbeitung sensible Felder löschen. @return true bei kopiertem Auftrag. */
    bool takeRequest(Request &request);
    /** @brief Führt ein passendes Scanergebnis zurück. @param id Scan-ID. @param outcome Ergebnisstatus. @param networks Ergebnisarray. @param count Anzahl <=24. @param more Weitere verworfene Treffer. @param error Zentraler Fehler. @return true bei übernommener passender Meldung. */
    bool completeScan(uint32_t id, Outcome outcome, const WifiList::Network *networks, size_t count, bool more, Error error = Error::None);
    /** @brief Meldet den tatsächlichen Abschluss von Test/Zeit/PIN/Neustart oder letzter Stumm-/Navigationsaktion. @param completion Passende ID und Typ, Test-Erfolg benötigt brauchbare IPv4. @return true bei passender Meldung. */
    bool complete(const Completion &completion);
    /** @brief Bestätigt dauerhaftes Speichern, nicht Queueannahme. @param id Save-ID. @param success Dauerhafter Erfolg. @param stored Zentral validierter gespeicherter Stand. Bei Fehler ignoriert. @param revision Neue Revision. @param error Fehlercode. @param restartRequired Zentrale Anwendungsentscheidung. @return true bei passender Meldung. */
    bool completeSave(uint32_t id, bool success, const Configuration &stored, uint64_t revision, Error error = Error::Storage, bool restartRequired = false);
    /** @brief Meldet lokale Änderungen. @return Kategorien-/Profilbitmaske. */
    uint32_t changedFields() const;

    /** @brief Liefert den UI-Entwurf nur zum Lesen. @return Referenz gültig bis nächster UI-Änderung. */
    const Configuration &draft() const
    {
        return m_draft;
    }

    /** @brief Meldet ein aktives internes Modal. @return Modalstatus. */
    bool modalActive() const;
    /** @brief Akzeptiert leere Passphrasen, 8..63 druckbare ASCII-Zeichen oder 64 Hexziffern. Hexziffern werden kleingeschrieben. @param input Terminierter Text, maximal 64 Bytes. @param output Zielgröße 65 Bytes. @return Gültigkeit. */
    static bool normalizePassword(const char *input, char (&output)[65]);
    /** @brief Prüft ein Kalenderdatum einschließlich Schaltjahr. @param time Lokale Eingabe. @return Gültigkeit 2000-2099. */
    static bool validDate(const LocalTime &time);

private:
    /** @brief Aktiver Dialog über der Konfigurationsseite. */
    enum class Modal : uint8_t
    {
        None,       ///< Kein Dialog
        Keyboard,   ///< Texteingabe
        Networks,   ///< Liste der gefundenen Netzwerke
        Message,    ///< Meldung oder Rückfrage
        Picker,     ///< Auswahlliste mit Wischen
        Progress    ///< Fortschritt des Verbindungstests
    };
    /** @brief Feld, für das die Tastatur geöffnet ist. Die PIN läuft über den Nummernblock. */
    enum class Edit : uint8_t
    {
        None,       ///< Keine Eingabe
        Ssid,       ///< Netzwerkname des gewählten Profils
        Password,   ///< Passwort des gewählten Profils
        Hostname,   ///< Netzwerkname des Geräts
        Ntp,        ///< NTP-Server
        UkwFrequency ///< Frequenz des UKW-Senders in MHz
    };
    /** @brief Anlass eines Meldungsdialogs. Bestimmt Texte, Tasten und die Folge der Bestätigung. */
    enum class Purpose : uint8_t
    {
        ResetDefaults, ///< Alle Einstellungen zurücksetzen
        LanguageSave,  ///< Sprachwechsel verlangt Neustart
        ThemeSave,     ///< Themenwechsel verlangt Neustart
        SavedWifi,     ///< WLAN gespeichert, Neustart anbieten
        Notice,        ///< Hinweis oder Fehler mit einer Taste
        DeleteProfile, ///< WLAN-Profil löschen
        Leave,         ///< Ansicht mit ungespeicherten Änderungen verlassen
        Restart,       ///< Neustart des Geräts
        AllActive,     ///< Alle Zeitfenster aktivieren
        SetTime,       ///< Manuelle Uhrzeit setzen
        BtForget,      ///< Bluetooth-Gerät vergessen
        BtConnected,   ///< Bluetooth-Verbindung hergestellt, Gerät in line1
        BtFailed       ///< Bluetooth-Verbindung gescheitert, Grund in line1
    };
    /** @brief Auswahlliste des Picker-Dialogs. */
    enum class Picker : uint8_t
    {
        Zone,        ///< Zeitzone
        Interval,    ///< Synchronisationsintervall
        Eye,         ///< Röhrenstil
        Ballistics,  ///< VU-Trägheit
        Sensitivity  ///< VU-Bezugspegel
    };
    /** @brief Navigation, die nach erfolgreichem Speichern folgt. */
    enum class AfterSave : uint8_t
    {
        None,    ///< Auf der Seite bleiben
        Home,    ///< Zum MainScreen
        Restart  ///< Neustart
    };

    /** @brief Registriertes Touchziel. */
    struct Hit
    {
        SettingsUi::Rect rect; ///< Fläche in Displaypixeln
        int action;            ///< Zielkennung aus ActionId in Settings.cpp
        bool enabled;          ///< Gesperrte Ziele nehmen keinen Kontakt an
    };

    static constexpr size_t QueueSize = 4, HitCount = 224, DirtyCount = 4; ///< Tiefe der Auftragswarteschlange, Zahl der Touchziele, Zahl vorgemerkter Übertragungsbereiche
    static constexpr int PanelCount = 12; ///< Anzahl der Konfigurationsseiten
    using Rect = SettingsUi::Rect;
    UiCanvas m_painter, m_work, m_clock, m_audioVolume; ///< Szene 1280x720, Arbeitsbereich, Uhr der Fußzeile, Lautstärkezahl der Klangfußzeile
    Rect m_dirtyRects[DirtyCount] = {}; ///< Vorgemerkte Szenenbereiche, die present() ins Display überträgt
    size_t m_dirtyCount = 0;            ///< Belegte Einträge in m_dirtyRects
    Rect m_clip{0, 0, 1280, 720};   ///< Ausschnitt beim Wiederherstellen unter einem Dialog
    Rect m_modalRect{0, 0, 0, 0};   ///< Zuletzt in der Szene gezeichneter Dialog
    Rect m_workDirty{0, 0, 0, 0};   ///< Direkt aus dem Arbeitsbereich zu übertragen, relativ zu dessen Ecke
    bool m_menuDirty = true, m_workInScene = false; ///< Menüspalte neu zeichnen, Arbeitsbereich bereits in die Szene kopiert
    char m_levelText[12] = {};                      ///< Zuletzt gezeichneter Text der Pegelanzeige
    lgfx::LGFX_Device *m_display = nullptr;         ///< Ziel der Übertragung, von begin() übernommen
    SettingsMessageBox m_message;                   ///< Meldungs- und Rückfragedialog
    SettingsProgressBox m_progress;                 ///< Fortschrittsdialog des Verbindungstests
    uint32_t m_now = 0, m_operationStart = 0;       ///< Letzte Zeit aus update() in ms, Beginn des laufenden Verbindungstests
    SettingsKeyboard m_keyboard;                    ///< Tastatur für Texteingaben
    SettingsWifiList m_networks;                    ///< Liste der gefundenen Netzwerke
    SettingsUi::Contact m_contact;                  ///< Kontaktverfolgung der Seite
    Hit m_hits[HitCount] = {};                      ///< Touchziele des aktuellen Bildes, später registrierte haben Vorrang
    size_t m_hitCount = 0, m_panelHitCount = 0;     ///< Belegte Ziele, Zahl der Ziele ohne Dialog und Fußzeile
    Request m_queue[QueueSize] = {};                ///< Ringpuffer ausgehender Aufträge
    size_t m_queueHead = 0, m_queueCount = 0;       ///< Kopf und Füllstand von m_queue
    uint32_t m_nextId = 0, m_muteId = 0, m_scanId = 0, m_pendingId = 0, m_lastMuteId = 0, m_lastNavigationId = 0; ///< Letzte vergebene Kennung. Stummauftrag, laufende Suche, erwarteter Abschluss. Stumm- und Navigationsauftrag mit offener Rückmeldung
    RequestType m_pendingType = RequestType::None, m_lastNavigationType = RequestType::None; ///< Art des erwarteten Abschlusses und des Navigationsauftrags
    Configuration m_baseline, m_draft, m_latest;    ///< Zuletzt gespeicherter Stand der Ansicht, bearbeiteter Entwurf, jüngster zentral gemeldeter Stand
    uint64_t m_revision = 0, m_latestRevision = 0;  ///< Revisionen von m_baseline und m_latest
    Status m_status;                                ///< Letzter zentraler Status
    Capabilities m_capabilities;                    ///< Freigeschaltete Funktionen
    LangIndex m_language = LangIndex::DE;           ///< Sprache der Anzeige
    Panel m_panel = Panel::Wifi;                    ///< Sichtbare Seite
    Modal m_modal = Modal::None;                    ///< Aktiver Dialog
    Edit m_edit = Edit::None;                       ///< Feld der laufenden Eingabe
    Purpose m_purpose = Purpose::Notice;            ///< Anlass des Meldungsdialogs
    Picker m_picker = Picker::Zone;                 ///< Offene Auswahlliste
    AfterSave m_afterSave = AfterSave::None;        ///< Folge des laufenden Speicherauftrags
    UiButton m_btnScan, m_btnPassword, m_btnTest, m_btnDelete; ///< Aktionstasten der WLAN-Seite
    UiButton m_btnBtSearch, m_btnBtTone, m_btnBtForget; ///< Aktionstasten der Bluetooth-Seite
    bool m_btPresent = false;                       ///< Pico angeschlossen laut setBluetooth()
    BtProtocol::LinkView m_btLink;                  ///< Letzter Stand des Pico laut setBluetooth()
    uint32_t m_btVersion = 0, m_btRequestVersion = 0; ///< Änderungszähler des Senders, sein Wert beim letzten Such- oder Verbindungsauftrag
    uint32_t m_btSearchId = 0, m_btSearchSince = 0; ///< Kennung der laufenden Suche, 0 ohne. Deren Beginn in ms
    bool m_btSearchStarted = false, m_btConnectSeen = false; ///< Der Pico hat die angeforderte Suche begonnen, er hat den angeforderten Verbindungsversuch begonnen
    char m_btTarget[BtProtocol::AddressSize] = {};  ///< Adresse des laufenden Verbindungsauftrags
    char m_btListAddress[BtProtocol::MaxHits][BtProtocol::AddressSize] = {}; ///< Adressen zu den Einträgen, die die Geräteliste erhielt, in deren Reihenfolge
    Numpad m_numpad;                                ///< Nummernblock der PIN-Eingabe
    DotMatrix m_audioVolDots;                       ///< Punktmatrix der Lautstärkezahl in der Klangfußzeile
    Sound m_soundDraft, m_soundBaseline;            ///< Bearbeiteter und zuletzt gespeicherter Klangstand
    bool m_soundLoaded = false, m_manualInit = false, m_pinConfirming = false, m_pinMismatch = false, m_numpadLayoutValid = false, m_fullPresent = true; ///< Klangstand übernommen, manuelle Uhrzeit initialisiert. Wiederholung der PIN läuft. Wiederholung oder PIN zum Ausschalten weicht ab. Nummernblock berechnet, nächstes Bild vollständig übertragen
    bool m_confirmPinDisable = false;               ///< Der Nummernblock erwartet die aktuelle PIN zum Ausschalten der PIN-Sperre
    char m_pinEntry[5]{}, m_pinFirst[5]{}, m_pinPending[5]{}; ///< PIN in Eingabe, erste Eingabe zum Vergleich, bestätigte neue PIN bis zum Speichern
    uint8_t m_pinLen = 0;                           ///< Länge von m_pinEntry
    int m_manualYear = 2026, m_manualMonth = 1, m_manualDay = 1, m_manualHour = 12, m_manualMinute = 0; ///< Werte der Schrittgeber der manuellen Uhrzeit
    int m_pickerScroll = 0, m_pickerTouchY = 0, m_pickerStart = 0; ///< Scrollversatz der Auswahlliste in Pixeln, Berührpunkt und Versatz beim Beginn des Wischens
    bool m_pickerDragging = false;                  ///< Wischen der Auswahlliste läuft
    uint32_t m_lastSecond = UINT32_MAX, m_savedFields = 0, m_previewId = 0; ///< Zuletzt gezeichneter Fortschritt in Zehntelsekunden. Field-Maske des letzten Speicherauftrags. Kennung der offenen Vorschau
    RequestType m_previewType = RequestType::None;  ///< Art der offenen Vorschau
    uint8_t m_profile = 0;                          ///< Gewähltes WLAN-Profil
    int m_touchOwner = 0;                           ///< Besitzer des laufenden Kontakts: 0 niemand, 1 Wischen der Auswahlliste, -1 gesperrt bis zum Loslassen
    bool m_modalOnly = false;                       ///< Nächstes Bild zeichnet nur den Dialog
    bool m_ready = false, m_visible = false, m_loaded = false, m_haveStatus = false, m_dirty = true, m_conflict = false, m_footerOnly = false; ///< Ressourcen angelegt. Ansicht sichtbar. Konfiguration übernommen. Status übernommen. Neuzeichnen nötig. Zentral wurde ein neuerer Stand gemeldet, während ein lokaler Entwurf offen ist. Nächstes Bild zeichnet nur die Fußzeile
    bool m_tested[ProfileCount] = {}; ///< Profil seit der letzten Änderung erfolgreich getestet
    SettingsKey m_notice = SettingsKey::Unchanged;  ///< Text der Statusanzeige in der Fußzeile

    /** @brief Liest einen Text in der zentral gültigen Sprache. */
    const char *tr(SettingsKey key) const
    {
        return settingsText(key, m_language);
    }

    /** @brief Liefert eine neue nicht-null Auftragskennung. */
    uint32_t nextId();
    /** @brief Kopiert einen begrenzten Auftrag in die UI-Ausgangsqueue. */
    bool enqueue(Request &request);
    /** @brief Prüft den vollständigen begrenzten Konfigurationscontainer. */
    static bool bounded(const Configuration &configuration);
    /** @brief Prüft einen Netzwerknamen. */
    static bool validHostname(const char *name);
    /** @brief Prüft einen Servernamen ohne URL/Pfad. */
    static bool validServer(const char *name);
    /** @brief Prüft bearbeitete Felder und Verfügbarkeit vor Speichern. */
    bool validateDraft();
    /** @brief Liefert den Speicherumfang der ausgewählten Konfigurationsseite. */
    uint32_t panelFields() const;
    /** @brief Zeichnet die aktive Ansicht in den vorbereiteten Puffer. */
    void draw();
    /** @brief Merkt eine interne Inhaltsänderung für gezielte Neuzeichnung vor. */
    void redraw();
    /** @brief Verwendet beim Dialogupdate die bereits gezeichneten Hintergrundsprites. */
    void redrawModal();
    /** @brief Registriert die Menüziele und zeichnet Menüspalte und Ränder nur bei paint neu. */
    void drawFrame(bool paint);
    /** @brief Merkt einen geänderten Szenenbereich zur Übertragung vor, nahe Bereiche werden zusammengefasst. */
    void markDirty(Rect rect);
    /** @brief Überträgt vorgemerkte Bereiche ins Display, den Arbeitsbereich ohne Dialog direkt aus seinem Sprite. */
    void present();
    /** @brief Merkt einen geänderten Teil des Arbeitsbereichs vor. Liegt der Arbeitsbereich in der Szene, wird der Teil dorthin kopiert. @param rect Bereich relativ zum Arbeitsbereich. */
    void markWork(Rect rect);
    /** @brief Kopiert den Arbeitsbereich in die Szene, bevor ein Dialog darüber entsteht. */
    void ensureWorkInScene();
    /** @brief Zeichnet den aktiven Dialog vollständig über die fertige Szene. */
    void drawModal();
    /** @brief Zeichnet nur die geänderten Teile des aktiven Dialogs. */
    void drawModalChanges();
    /** @brief Stellt Menü, Arbeitsbereich und Fußzeile unter einem Dialogbereich wieder her. */
    void restoreUnder(const Rect &area);
    /** @brief Zeichnet die Pegelanzeige der Anzeige-Seite in den Arbeitsbereich. @param force Auch bei unverändertem Text. @return true bei gezeichneter Anzeige. */
    bool drawAppearanceLevel(bool force);
    /** @brief Zeichnet die aktive Kategorie. */
    void drawPanel();
    /** @brief Liefert ein freigegebenes Touchziel. */
    int hit(int x, int y) const;
    /** @brief Führt ein bestätigtes UI-Ziel aus. */
    void activate(int action);
    /** @brief Markiert eine Änderung und entwertet Auswahlkontakte. */
    void edited();
    /** @brief Markiert ausschließlich die Fußzeile, solange kein größerer Bereich offen ist. */
    void invalidateFooter();
    /** @brief Schließt UI-Modals und löscht lokale PIN-Eingaben. */
    void closeModal();
    /** @brief Öffnet eine konkrete Bestätigung. */
    void ask(Purpose purpose, SettingsKey title, const char *line1, const char *line2 = "");
    /** @brief Zeigt einen lokalisierten Fehler oder Hinweis. */
    void notice(SettingsKey key, const char *detail = "");
    /** @brief Ordnet zentrale Fehler einem Benutzertext zu. */
    static SettingsKey errorText(Error error);
    /** @brief Verarbeitet einen gewählten Bestätigungsknopf. */
    void confirmed(int choice);
    /** @brief Öffnet eine begrenzte Eingabe. */
    void edit(Edit field);
    /** @brief Übernimmt nur validierte Eingaben, ein Fehler hält die Tastatur offen. */
    void acceptInput();
    /** @brief Startet den zentralen Scan und zeigt die Ergebnisansicht. */
    void startScan();
    /** @brief Fordert Scanabbruch an und entwertet nur bei Annahme die ID. */
    bool cancelScan();
    /** @brief Haus-Taste bei offenem Auftrag: bricht einen laufenden WLAN-Verbindungstest oder Bluetooth-Verbindungsversuch ab und verlässt die Ansicht ohne Rückfrage. Der Entwurf bleibt erhalten, andere Aufträge laufen zu Ende. */
    void leavePending();
    /** @brief Verarbeitet eine Auswahl aus der WLAN-Liste. */
    void selectedNetwork(const WifiList::Result &result);
    /** @brief Verarbeitet eine Bedienung der Geräteliste: Abbruch beendet die Suche, scan sucht erneut, ok verbindet mit dem gewählten Gerät. */
    void selectedDevice(const WifiList::Result &result);
    /** @brief Fordert die Bluetooth-Suche an und öffnet die Geräteliste, nur mit Pico im Zustand STILL. */
    void startBtSearch();
    /** @brief Beendet eine laufende Suche am Pico (TRENNE) und vergisst ihre Kennung. */
    void stopBtSearch();
    /** @brief Zeigt in der offenen Geräteliste einen Fehler und beendet die Suche ohne Befehl an den Pico. */
    void failBtSearch(SettingsKey key);
    /** @brief Füllt die Geräteliste mit den Treffern der beendeten Suche: Name, ohne Namen die Adresse, und Signalstärke. */
    void showBtHits();
    /** @brief Fordert die Verbindung mit address an und zeigt den Fortschritt wie beim WLAN-Test. */
    void connectBt(const char *address);
    /** @brief Beendet den Verbindungsauftrag mit einer Meldung. @param reason Grund bei Misserfolg, sonst ungenutzt. */
    void finishBtConnect(bool success, const char *reason = "");
    /** @brief Gibt einen Befehl ohne Fortschrittsanzeige an den Sender. */
    void btCommand(RequestType type);
    /** @brief Meldet abgewiesene Bluetooth-Aufträge zurück. TRENNE bleibt ohne Meldung, weil die Seite den Zustand ohnehin zeigt. */
    bool completeBluetooth(const Completion &completion);
    /** @brief Prüft, ob der Pico verbunden ist oder gerade verbindet. Dann steht der Schalter auf an. */
    bool btLinked() const;
    /** @brief Prüft, ob die Bluetooth-Taste idx (0 suchen, 1 test, 2 vergessen) bedienbar ist. */
    bool btActionEnabled(int idx) const;
    /** @brief Prüft, ob der Schalter der Gerätezeile bedienbar ist: aus bei Verbindung, an mit gespeichertem Gerät im Zustand STILL. */
    bool btToggleEnabled() const;
    /** @brief Prüft, ob die UKW-Seite bedienbar ist: Fähigkeit und Speichern vorhanden. Der Modulator selbst muss dafür nicht da sein, die Werte gelten, sobald er kommt. */
    bool ukwEditable() const;
    /** @brief Schreibt den Zustand der UKW-Zeile: nicht angeschlossen, aus, startet, sendet auf <Frequenz> (stumm). @param text Ziel, mindestens 48 Byte. */
    void ukwStatusText(char *text, size_t size) const;
    /** @brief Schreibt die Frequenz khz als „87,5 MHz“ bzw. „87.5 MHz“ nach Sprache. */
    void formatMhz(char *text, size_t size, uint32_t khz) const;
    /** @brief Öffnet einen paginierten Auswahltyp. */
    void openPicker(Picker picker);
    /** @brief Liefert Anzahl und Beschriftung eines Auswahltyps. */
    size_t pickerCount() const;
    /** @brief Zeichnet die Auswahl, ohne full nur die Listenzeilen. */
    void drawPicker(bool full);
    /** @brief Übernimmt einen ausgewählten Wert. */
    void choose(size_t index);
    /** @brief Speichert den Entwurf zentral, gegebenenfalls mit anschließender Navigation. @param only Beschränkt die gespeicherten Bereiche zusätzlich, etwa auf den sofort gesicherten Log-Schalter. */
    void save(AfterSave after, uint32_t only = UINT32_MAX);
    /** @brief Lädt den jüngsten bekannten gespeicherten Stand ohne Schreibzugriff. */
    void discard();
    /** @brief Gibt eine zentrale Navigation/Systemaktion aus. */
    void action(RequestType type);

    /** @brief Liefert den Seitentext für m_language ohne globale Sprachumschaltung. */
    const char *ot(SettingsTextKey key) const
    {
        return settingsUiText(key, m_language);
    }

    /** @brief Registriert eine logische Trefferfläche, ohne es neu zu zeichnen. */
    void target(Rect rect, int action, bool enabled = true, bool work = true);
    /** @brief Zeichnet Rahmen, Text und Druckzustand einer Schaltfläche auf dem gewählten Sprite. */
    void drawButton(UiCanvas &canvas, Rect rect, const char *label, bool enabled = true, bool pressed = false, bool selected = false);
    /** @brief Liefert die Zeile des WLAN-Profils idx, relativ zum Arbeitsbereich wie alle folgenden Flächen. */
    Rect wifiRowRect(int idx) const;
    /** @brief Liefert den Umschalter des WLAN-Profils idx. */
    Rect wifiToggleRect(int idx) const;
    /** @brief Liefert die Aktionstaste idx (0 Scannen, 1 Passwort, 2 Test, 3 Löschen). */
    Rect wifiActionRect(int idx) const;
    /** @brief Liefert das Feld des Netzwerknamens am unteren Rand der WLAN-Seite, auf der Bluetooth-Seite steht dort das Senderfeld. */
    Rect hostnameFieldRect() const;
    /** @brief Liefert die Bluetooth-Taste idx (0 suchen, 1 test, 2 vergessen) in ihrer Spalte des WLAN-Tastenrasters unter der Gerätezeile. */
    Rect btActionRect(int idx) const;
    /** @brief Liefert Minus- oder Plustaste der UKW-Zeile row (0 Frequenz, 1 Leistung), rechtsbündig wie auf der Klangseite. */
    Rect ukwStepRect(int row, bool plus) const;
    /** @brief Liefert das Wertefeld der UKW-Zeile row zwischen den Tasten, in Zeile 0 öffnet ein Tipp die Tastatur. */
    Rect ukwValueRect(int row) const;
    /** @brief Liefert die Zeile der Sprache idx. */
    Rect langRowRect(int idx) const;
    /** @brief Liefert die Zeile idx der Themenseite, von oben ab 0 gezählt. Welches Thema dort steht, legt ThemeRows in Settings.cpp fest. */
    Rect themeRowRect(int idx) const;
    /** @brief Liefert die Auswahlzeile idx der Zeitseite (Zeitzone, NTP-Server, Intervall). */
    Rect timeFieldRect(int idx) const;
    /** @brief Liefert die Spalte col des Schrittgeberblocks der manuellen Uhrzeit. */
    Rect timeStepperColumnRect(int col) const;
    /** @brief Liefert die Plustaste der Schrittgeberspalte col. */
    Rect timeStepperUpRect(int col) const;
    /** @brief Liefert die Minustaste der Schrittgeberspalte col. */
    Rect timeStepperDownRect(int col) const;
    /** @brief Liefert die Wertanzeige der Schrittgeberspalte col. */
    Rect timeStepperValueRect(int col) const;
    /** @brief Liefert die Taste, die die manuelle Uhrzeit setzt. */
    Rect timeSetButtonRect() const;
    /** @brief Liefert die Zelle des Wochentags day (0..6) und der Stunde hour (0..23) der Zeitfenster. */
    Rect timeWindowCellRect(int day, int hour) const;
    /** @brief Liefert die ganze Zeile row der Klangseite. */
    Rect audioStepperColumnRect(int row) const;
    /** @brief Liefert die Minustaste der Klangzeile row. */
    Rect audioStepperDownRect(int row) const;
    /** @brief Liefert die Wertanzeige der Klangzeile row. */
    Rect audioStepperValueRect(int row) const;
    /** @brief Liefert die Plustaste der Klangzeile row. */
    Rect audioStepperUpRect(int row) const;
    /** @brief Liefert die Kachel index der Röhrenauswahl. */
    Rect appearanceTileRect(int index) const;
    /** @brief Liefert das Segment index von count Segmenten einer Wahlzeile. @param row Obere Kante der Zeile relativ zum Inhalt. */
    Rect appearanceSegmentRect(int row, int index, int count) const;
    /** @brief Zeichnet WLANprofil und Schaltflächen aus dem lokalen Konfigurationsentwurf. */
    void drawWifiRow(UiCanvas *spr, int idx, const Rect &r, bool selected);
    /**
     * @brief Zeichnet eine Zeile im Stil eines WLAN-Profils: Grund, bei selected Akzentstreifen, Zustandspunkt, Name, Zustand und Schalter.
     * @param named Name hell, sonst gedämpft wie ein leeres Profil
     * @param status Zustandstext unter dem Namen, in statusColor wie der Punkt
     * @param on Stellung des Schalters
     */
    void drawProfileRow(UiCanvas *spr, const Rect &r, bool selected, const char *name, bool named, const char *status, uint16_t statusColor, bool on);
    /** @brief Zeichnet ein Feld mit Rahmen, gedämpfter Beschriftung und darunter dem Wert wie das Netzwerknamenfeld. */
    void drawLabelField(UiCanvas *spr, const Rect &r, const char *label, const char *value);
    /** @brief Zeichnet einen Umschalter mit Statusfarbe im Rechteck r. @param background Farbe unter dem Umschalter. */
    void drawToggle(UiCanvas *spr, const Rect &r, bool on, uint16_t background);
    /** @brief Zeichnet die WLAN-Seite und registriert ihre Touchziele. */
    void drawPanelWifi(UiCanvas *spr);
    /** @brief Zeichnet die Bluetooth-Seite: Gerätezeile mit Schalter, Tasten suchen, test und vergessen, Senderfeld. */
    void drawPanelBluetooth(UiCanvas *spr);
    /** @brief Zeichnet die UKW-Seite: Senderzeile mit Schalter, Frequenz und Leistung mit Minus, Wert und Plus, Hinweise, Modulatorfeld. */
    void drawPanelUkw(UiCanvas *spr);
    /** @brief Zeichnet die Seite Allgemein: Rubrik Sprache mit den Sprachzeilen, darunter Rubrik serielle Logs mit Schalter. */
    void drawPanelLanguage(UiCanvas *spr);
    /** @brief Lage des Schalters für serielle Logs auf der Seite Allgemein, rechtsbündig unter der Rubriküberschrift. */
    Rect serialLogsToggleRect() const;
    /** @brief Zeichnet die Themenseite: je Thema eine Zeile mit Minivorschau in der Reihenfolge Retro, E-Paper, Individuell, Auswahl aus dem Entwurf, „aktiv“ am laufenden Thema. */
    void drawPanelTheme(UiCanvas *spr);
    /** @brief Zeichnet die Sicherheitsseite mit Schalter der PIN-Sperre, Hinweis, Punkten und Nummernblock. */
    void drawPanelSecurity(UiCanvas *spr);
    /** @brief Lage des Schalters der PIN-Sperre, rechtsbündig oben auf der Sicherheitsseite. */
    Rect pinLockToggleRect() const;
    /** @brief Zeichnet die Zeitseite: Auswahlzeilen, Schrittgeber links, „zeit setzen“ rechts daneben, darunter die Zeile des Sekundenzeigers mit Schalter. */
    void drawPanelTime(UiCanvas *spr);
    /** @brief Lage der Zeile „Sekundenzeiger“ unten auf der Zeitseite, die ganze Zeile schaltet. */
    Rect secondHandRowRect() const;
    /** @brief Zeichnet die Matrix der Zeitfenster. */
    void drawPanelTimeWindow(UiCanvas *spr);
    /** @brief Zeichnet die Klangseite. */
    void drawPanelAudio(UiCanvas *spr);
    /** @brief Zeichnet die Anzeige-Seite mit Röhrenwahl und VU-Meter. */
    void drawPanelAppearance(UiCanvas *spr);
    /** @brief Zeichnet die Infoseite. */
    void drawPanelInfo(UiCanvas *spr);
    /** @brief Zeichnet die Bildschirmseite mit Helligkeit und Ausrichtung im Stil der Klang- und Anzeige-Seite. */
    void drawPanelScreen(UiCanvas *spr);
    /** @brief Liefert die Minus- oder Plustaste der Helligkeit. */
    Rect screenStepRect(bool plus) const;
    /** @brief Liefert die Wertanzeige der Helligkeit. */
    Rect screenValueRect() const;
    /** @brief Zeichnet die Abschnittsüberschrift mit Trennlinie bei y relativ zum Inhalt. */
    void drawAppearanceSectionTitle(UiCanvas *spr, int y, SettingsTextKey key);
    /** @brief Zeichnet eine einzeilige Hilfezeile bei y relativ zum Inhalt. */
    void drawAppearanceHint(UiCanvas *spr, int y, SettingsTextKey key);
    /** @brief Zeichnet die Kacheln der Röhrenwahl samt Vorschau. */
    void drawAppearanceEyeTiles(UiCanvas *spr);
    /** @brief Zeichnet eine Wahlzeile mit Beschriftung und count Segmenten. @param y Obere Kante relativ zum Inhalt. @param values Segmenttexte. @param selected Gewähltes Segment oder -1. */
    void drawAppearanceSegments(UiCanvas *spr, int y, SettingsTextKey key, const char *const *values, int count, int selected);
    /** @brief Liefert den Textschlüssel für den Zustand eines WLANprofils. */
    SettingsTextKey wifiStatusKey(int index) const;
    /** @brief Liefert die Farbe für den Zustand eines WLANprofils. */
    uint16_t wifiStatusColor(int index) const;
    /** @brief Liefert den Textschlüssel für den Zustand der Bluetooth-Gerätezeile. */
    SettingsTextKey btStatusKey() const;
    /** @brief Liefert die Farbe für den Zustand der Bluetooth-Gerätezeile, aus denselben Rollen wie bei WLAN. */
    uint16_t btStatusColor() const;
    /** @brief Liefert die Farbe für den Zustand der UKW-Senderzeile, aus denselben Rollen wie bei WLAN. */
    uint16_t ukwStatusColor() const;

    /** @brief Prüft, ob ein Profil im Entwurf eine SSID hat. */
    bool isProfileValid(int index) const
    {
        return index >= 0 && index < 3 && m_draft.profiles[index].ssid[0];
    }

    /** @brief Prüft, ob der Netzwerkname im Entwurf vom gespeicherten abweicht. */
    bool hostnamePending() const
    {
        return strcmp(m_draft.hostname, m_baseline.hostname) != 0;
    }

    /** @brief Liefert 0 für Deutsch und 1 für Englisch als Index der Sprachzeilen. */
    int currentLangIndex() const
    {
        return m_language == LangIndex::EN ? 1 : 0;
    }

    /** @brief Prüft, ob eine bestätigte neue PIN auf das Speichern wartet. */
    bool pinPending() const
    {
        return m_pinPending[0];
    }

    /** @brief Löscht die laufende PIN-Eingabe samt Zwischenstand. */
    void resetPinEntry();
    /** @brief Verarbeitet eine Taste des Nummernblocks. @param key Ziffer, 'B' löscht die letzte Stelle, 'C' verwirft die Eingabe. Die vierte Ziffer schließt die erste Eingabe ab, die Wiederholung bestätigt oder verwirft sie. Wartet die Seite auf die PIN zum Ausschalten der Sperre, gibt die vierte Ziffer den Speicherauftrag. */
    void handlePinKey(char key);
    /** @brief Speichert die PIN-Sperre sofort ohne „speichern“, wie der Log-Schalter nur ihren eigenen Eintrag. @param enabled Gewünschter Zustand. Zum Ausschalten trägt m_pinEntry die aktuelle PIN. Kommt kein Auftrag zustande, bleibt der gespeicherte Zustand stehen. */
    void changePinLock(bool enabled);
    /** @brief Begrenzt den manuellen Tag auf die Länge des gewählten Monats. */
    void clampManualDay();
    /** @brief Setzt die Schrittgeber der manuellen Uhrzeit auf die aktuelle lokale Zeit, falls sie gültig ist. */
    void initManualFromRtc();
    /** @brief Ordnet eine Spalte des Schrittgeberblocks dem Datenfeld zu, die Spaltenreihenfolge hängt von der Sprache ab. */
    int stepperColToField(int col) const;
    /** @brief Zeichnet Fußzeile und Uhr in die Szene und merkt sie zur Übertragung vor. */
    void drawFooter();
    /** @brief Zeichnet die Uhr der Fußzeile. */
    void drawClock();
    /** @brief Sendet den aktuellen Klangentwurf als begrenzten asynchronen Auftrag. */
    void preview(RequestType type);
};
#endif
