#ifndef APP_SERVICES_H
#define APP_SERVICES_H
#include "Settings.h"
#include "FileLease.h"
#include "FtpProtocol.h"
#include "WebFileStore.h"
#include "Tab5Config.h"
#include "Tab5Rtc.h"
#include "SDPlayer.h"
#include "SdCardWatch.h"
#include "SdFileScan.h"
#include "WifiSequence.h"
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <cstdio>

struct FtpWorker;
class FatDirectory;

/** @brief Zentrale Hintergrundarbeit für Konfiguration, WLAN/Zeit sowie SD-Lese-, Web- und FTP-Dateiaufträge. */
class AppServices
{
public:
    /** @brief Netzabbild des C6-Stationsinterfaces. */
    struct NetworkStatus
    {
        bool connected = false;           ///< Mit einem Zugangspunkt verbunden
        int16_t rssi = 0;                 ///< Empfangsstärke in dBm
        uint8_t ip[4]{}, gateway[4]{}, channel = 0; ///< IPv4-Adresse, Gateway und Funkkanal
        char ssid[33]{}, mac[18]{};       ///< Name des Netzes und eigene MAC im Format AA:BB:CC:DD:EE:FF
    };

    /** @brief Kopiert das im Dienstworker ermittelte Netzabbild ohne C6-RPC im UI-Pfad. */
    NetworkStatus networkStatus();

    /** @brief Abschluss eines Konfigurations- oder Netzauftrags. */
    struct Result
    {
        Settings::RequestType type = Settings::RequestType::None; ///< Art des abgeschlossenen Auftrags
        uint32_t id = 0;                                          ///< Kennung des Auftrags
        Settings::Outcome outcome = Settings::Outcome::Failed;    ///< Erfolg, Abbruch oder Fehlschlag
        Settings::Error error = Settings::Error::Service;         ///< Fehlerursache, None bei Erfolg
        Settings::Configuration config;                           ///< Gespeicherte Konfiguration nach Save und ResetDefaults
        uint64_t revision = 0;                                    ///< Revision dieser Konfiguration
        uint32_t changedFields = 0;                               ///< Geänderte Bereiche (Settings::Field) bei Save, alle bei ResetDefaults
        uint8_t ip[4]{};                                          ///< Erhaltene IPv4-Adresse nach einem Verbindungstest
        WifiList::Network networks[WifiList::MaxNetworks];        ///< Suchergebnis
        size_t count = 0;                                         ///< Anzahl der Netze im Suchergebnis
        bool more = false;                                        ///< Weitere Netze wurden verworfen
    };

    /** @brief Lädt eigene NVS-/RTC-Werte und startet einen Dienstworker. @param defaultStation Ersatzsender bei ungültigem RTC-RAM. @return Vorbereitet. */
    bool begin(uint8_t defaultStation = 0);
    /** @brief Kopiert einen Konfigurationsauftrag ohne I/O. @param request Auftrag. @return Angenommen. */
    bool request(const Settings::Request &request);
    /** @brief Holt einen tatsächlichen Abschluss. @param result Ziel. @return Ergebnis vorhanden. */
    bool takeResult(Result &result);
    /** @brief Kopiert gespeicherte Einstellungen. @param config Ziel. @return Speicherrevision. */
    uint64_t configuration(Settings::Configuration &config);
    /** @brief Liefert ausschließlich gespeicherte Klangwerte ohne NVS-Zugriff. */
    Settings::Sound sound();
    /** @brief Kopiert den zuletzt geladenen Radiozustand ohne I/O. */
    RadioPreferences radioPreferences();
    /** @brief Merkt Regler-/Stiländerungen zum zusammengefassten Speichern im Worker. */
    void saveRadioPreferences(const RadioPreferences &value);
    /** @brief Kopiert bestätigte RTC-Befunde und Schreibzähler ohne Hardwarezugriff. */
    Tab5Rtc::Status rtcStatus();

    /** @brief Auftrag für die Grundfarben von Modern. Wird in den Dienstworker kopiert. Name, Vorgabe und Gruppe jeder Farbe stehen in ModernGrundfarben. */
    struct ModernRequest
    {
        uint32_t id = 0;                     ///< Kennung, kehrt im Ergebnis zurück
        bool reset = false;                  ///< true stellt die Vorgaben wieder her und löscht dazu nur rr_tab5/modern_v1, colors zählt dann nicht
        ModernFarben colors = ModernVorgabe; ///< Zu speichernde Grundfarben als 0xRRGGBB, Grund und Fläche werden nicht gespeichert
    };

    /** @brief Abschluss eines ModernRequest. */
    struct ModernResult
    {
        uint32_t id = 0;                                  ///< Kennung des Auftrags
        Settings::Error error = Settings::Error::Service; ///< None bei Erfolg, Invalid bei einem Wert über 0xFFFFFF, Storage bei NVS-Fehler oder abweichendem Rücklesen
        ModernFarben colors = ModernVorgabe;              ///< Gespeicherte Grundfarben nach dem Auftrag laut Rücklesen, bei Invalid unverändert
        bool stored = false;                              ///< rr_tab5/modern_v1 ist nach dem Auftrag vorhanden
    };

    /** @brief Kopiert die gespeicherten Grundfarben von Modern ohne I/O. Sie gelten ab dem nächsten Neustart, das laufende Thema behält die beim Start geladenen. @param colors Ziel, ohne Eintrag die Vorgaben. @return true, wenn rr_tab5/modern_v1 vorhanden ist. */
    bool modernColors(ModernFarben &colors);
    /** @brief Kopiert einen Auftrag für die Grundfarben ohne I/O in den Dienstworker. @return false, wenn der Dienst fehlt oder noch ein Auftrag wartet. */
    bool requestModern(const ModernRequest &request);
    /** @brief Holt den Abschluss eines Auftrags für die Grundfarben ohne Warten. @param result Ziel. @return Ergebnis vorhanden. */
    bool takeModernResult(ModernResult &result);

    /** @brief Liefert die tatsächlichen NVS-Schreibabschlüsse für Radiowerte seit diesem Boot. */
    uint32_t radioFlashWrites() const
    {
        return m_radioFlashWrites.load();
    }

    /** @brief Prüft genau vier Ziffern gegen die zentrale PIN, ohne sie herauszugeben. */
    bool validatePin(const char *pin);
    /** @brief Liefert den gespeicherten Schalter der PIN-Sperre ohne I/O, aus jedem Task aufrufbar. @return true, wenn Display und Webportal die PIN verlangen. */
    bool pinLockEnabled();
    /** @brief Verbindet temporär, ohne Profile zu speichern. @param ssid Netz. @param password Passwort. @return Angenommen. */
    bool temporaryWifi(const char *ssid, const char *password);
    /** @brief Übernimmt einen begrenzten Dateiauftrag. @param request Auftrag. @return Angenommen. */
    bool requestFiles(const SdFileRequest &request);
    /** @brief Liefert vorbereitete Dateien bis releaseFiles. @param result Ziel. @return Ergebnis vorhanden. */
    bool takeFiles(SdFileResult &result);
    /** @brief Gibt den Ergebnisblock nach synchronem Kopieren wieder an den Worker. */
    void releaseFiles();

    /** @brief Fordert erneutes Mounten an, wenn keine bereite Karte eingebunden ist. Eine verlorene Karte wird vorher ausgehängt. Niemals Formatieren. */
    void retryMedia()
    {
        m_retry.store(true);
    }

    /** @brief Liefert ausschließlich bestätigten Medienstatus. @return Status. */
    SdServiceStatus mediaStatus();

    /** @brief Auftrag für das Cover eines Titels: Ordnerbild folder.jpg, sonst das eingebettete Bild, sofern audio-lite eines gemeldet hat. */
    struct CoverRequest
    {
        uint32_t mediaGeneration = 0, playbackId = 0, trackId = 0; ///< Kennungen von Karte, Wiedergabe und Titel. Ergebnisse veralteter Aufträge verwirft der Aufrufer
        char path[256]{};                                          ///< Dateipfad auf der Karte
        AudioManager::CoverEvent event;                            ///< Von audio-lite gemeldete Coverpositionen, beim Titelstart noch leer
    };

    /** @brief Kopiert eine Bildanforderung in den einzigen Dateiworker. */
    bool requestCover(const CoverRequest &request);
    /** @brief Liefert den Coverabschluss bis releaseCover. Nur bei Ready sind Pixel vorhanden, ihr Puffer bleibt Eigentum des Dienstes. Die Auftragsidentität bleibt erhalten. */
    bool takeCover(SdCoverFrame &frame);

    /** @brief Gibt den synchron kopierten Bildpuffer wieder an den Worker. */
    void releaseCover()
    {
        m_coverReady.store(false, std::memory_order_release);
    }

    /** @brief Meldet eine laufende WLAN-Suche oder einen Verbindungstest. */
    bool wifiBusy() const
    {
        return m_wifiBusy.load();
    }

    /** @brief Meldet, ob der Dienstworker die gespeicherten WLAN-Profile gerade der Reihe nach zu verbinden versucht. */
    bool wifiConnecting() const
    {
        return m_wifiConnecting.load();
    }

    /** @brief Meldet das Verlassen der Einstellungen: Ein dort erfolgreich getestetes, aber nicht als freigegebenes Profil gespeichertes Netz wird gegen die gespeicherten Profile getauscht. */
    void restoreWifi()
    {
        m_restoreWifi.store(true);
    }

    /** @brief Meldet, ob seit dem Start mindestens einmal per NTP synchronisiert wurde. */
    bool ntpSynced() const
    {
        return s_ntpSynced.load();
    }

    /** @brief Liefert einen Zähler, der bei jeder Änderung von Zeitzone oder NTP-Einstellungen steigt. */
    uint32_t timeRevision() const
    {
        return m_timeRevision.load();
    }

    /** @brief Prüft/serialisiert nur das versionierte Konfigurationsformat. */
    static bool validConfiguration(const Settings::Configuration &config);
    /** @brief Art eines Dateiauftrags der Weboberfläche. */
    enum class WebFileKind : uint8_t
    {
        List,   ///< Ordner auflisten
        Mkdir,  ///< Ordner anlegen
        Rename, ///< Datei oder Ordner umbenennen oder verschieben
        Delete, ///< Datei oder leeren Ordner löschen
        Begin,  ///< Upload beginnen. Mit overwrite darf er eine vorhandene Datei ersetzen
        Chunk,  ///< Uploadblock schreiben
        Commit, ///< Upload abschließen und unter dem Zielnamen ablegen
        Abort,  ///< Upload verwerfen
        TreeCount, ///< Zählen des Inhalts eines Ordners als Baumarbeit beginnen. Die Kartensperre geht an den Dienstworker über
        TreeDelete ///< Löschen eines Ordners samt Inhalt als Baumarbeit beginnen, die Kartensperre geht an den Dienstworker über
    };

    /** @brief Dateiauftrag der Weboberfläche, wird in den Dienstworker kopiert. */
    struct WebFileRequest
    {
        WebFileKind kind = WebFileKind::List; ///< Art des Auftrags
        char path[256]{}, target[256]{};      ///< Pfad (bei Begin der Zielpfad des Uploads) und, nur bei Rename, das neue Ziel
        uint32_t size = 0;                    ///< Gesamtgröße der Datei bei Begin
        bool overwrite = false;               ///< Nur bei Begin: Eine vorhandene Datei darf ersetzt werden
        uint16_t count = 0;                   ///< Anzahl gültiger Bytes in data bei Chunk
        uint8_t data[4096]{};                 ///< Inhalt eines Uploadblocks
    };

    /** @brief Ergebnis eines Dateiauftrags der Weboberfläche. */
    struct WebFileResult
    {
        int status = 500;  ///< HTTP-Statuscode für die Antwort
        size_t count = 0;  ///< Anzahl der Einträge einer Auflistung in webEntries()
        size_t skipped = 0; ///< Übersprungene Einträge einer Auflistung: Pfad über 255 Byte oder ungültiger Name
        WebFileStore::Reason reason = WebFileStore::Reason::None; ///< Grund einer Ablehnung für die Meldung im Browser
    };

    /** @brief Führt Dateiarbeit synchron für den HTTP-Task im einzigen Dateiworker aus. Niemals aus dem UI aufrufen. Beginnt TreeCount oder TreeDelete mit 200, hält der Dienstworker die Kartensperre des Webportals bis zum Ende der Baumarbeit und gibt sie dann selbst frei. Den Stand liefert webTree(). */
    WebFileResult webFile(const WebFileRequest &request);

    /** @brief Stand der Baumarbeit des Datei-Managers. */
    struct WebTreeStatus
    {
        uint32_t id = 0;                                          ///< Kennung der zuletzt begonnenen Baumarbeit, 0 vor der ersten
        bool running = false;                                     ///< Läuft noch
        bool remove = false;                                      ///< Löschen samt Inhalt statt Zählen
        int status = 0;                                           ///< Nach dem Ende HTTP-Status: 200 vollständig, sonst Fehler
        WebFileStore::Reason reason = WebFileStore::Reason::None; ///< Grund eines Abbruchs für die Meldung im Browser
        uint32_t files = 0, folders = 0;                          ///< Gezählte bzw. gelöschte Dateien und Ordner unter dem Ordner
        char path[256]{};                                         ///< Ordner der Baumarbeit
        char failed[256]{};                                       ///< Nach einem Fehler der betroffene Pfad, sonst leer
    };

    /** @brief Kopiert den Stand der Baumarbeit ohne I/O, aus jedem Task. */
    WebTreeStatus webTree();

    /** @brief Vergibt die Kartensperre der Dateiverwaltung an Webportal oder FTP. Nur im UI-Koordinator aufrufen. @param owner Anfordernder Besitzer. @param sdBusy SD-Wiedergabe läuft oder ein SD-Auftrag wartet. @return Granted, wenn die Sperre frei war und die Karte nicht spielt. */
    FileLease::Answer grantFileLease(FileLease::Owner owner, bool sdBusy)
    {
        return m_fileLease.grant(owner, sdBusy);
    }

    /** @brief Gibt die Kartensperre frei, wenn owner sie hält. */
    void releaseFileLease(FileLease::Owner owner)
    {
        m_fileLease.release(owner);
    }

    /** @brief Liefert den Besitzer der Kartensperre. */
    FileLease::Owner fileLease() const
    {
        return m_fileLease.owner();
    }

    /** @brief Meldet, ob Webportal oder FTP die Kartensperre halten. Solange startet keine SD-Wiedergabe. */
    bool fileLeased() const
    {
        return m_fileLease.held();
    }

    /** @brief Führt einen FTP-Kartenauftrag im Dienstworker aus und wartet ohne Zeitgrenze auf den Abschluss. Nur aus dem FTP-Task. Außer Close und ListClose verlangt jeder Auftrag die Kartensperre des FTP-Dienstes, eine bereite Karte und keine offene Audiodatei, sonst Busy oder NoCard. @param job Auftrag, gehört bis zur Rückkehr dem Worker. */
    void ftpFile(FtpProtocol::Job &job);

    /** @brief Kopiert das FTP-Passwort ohne I/O. @return false, solange keines erzeugt ist. */
    bool ftpPassword(char (&out)[FtpProtocol::PasswordCapacity]);

    /** @brief Fordert im Dienstworker ein FTP-Passwort an: Ohne gespeichertes oder mit renew wird ein neues erzeugt und in rr_tab5/ftp_v1 gespeichert. Ohne I/O im Aufrufer. @return false, wenn der Dienst fehlt. */
    bool requestFtpPassword(bool renew);

    /** @brief Liefert einen Zähler, der mit jedem bestätigt gespeicherten neuen FTP-Passwort steigt, 0 ohne Passwort. */
    uint32_t ftpPasswordRevision() const
    {
        return m_ftpPasswordRevision.load();
    }

    /** @brief Liefert die unveränderte Webliste bis zum nächsten serialisierten Webauftrag. */
    const SdFileEntry *webEntries() const
    {
        return m_webEntries;
    }

private:
    QueueHandle_t m_webFiles = nullptr, m_webResults = nullptr;
    SdFileEntry *m_webEntries = nullptr;
    WebFileRequest m_webWork;
    uint32_t m_webUploadAt = 0;
    FileLease m_fileLease; ///< Kartensperre der Dateiverwaltung für Webportal und FTP
    WebFileStore *m_webStore = nullptr; ///< Kartenarbeit des Datei-Managers im PSRAM, nur im Dienstworker
    FatDirectory *m_webDirectory = nullptr; ///< Ordnerleser des Datei-Managers im PSRAM, getrennt von dem des FTP-Dienstes. Nur im Dienstworker
    WebTreeStatus m_webTree; ///< Stand der Baumarbeit. Geschützt durch m_configLock, geschrieben nur im Dienstworker
    /** @brief Verarbeitet einen begrenzten Dateimanagerauftrag und entfernt eigene abgebrochene Uploads. */
    void executeWebFile();
    /** @brief Führt einen Schritt der laufenden Baumarbeit aus, am Ende oder bei einem Fehler wird sie beendet und die Kartensperre des Webportals freigegeben. Nur im Dienstworker. */
    void stepWebTree();
    /** @brief Entfernt eigene verwaiste Uploadteile von Webportal und FTP vor der Freigabe einer frisch gemounteten Karte. */
    void cleanupUploads();
    FtpWorker *m_ftp = nullptr;                          ///< Kartenzustand des FTP-Dienstes im PSRAM, nur im Dienstworker
    std::atomic<FtpProtocol::Job *> m_ftpJob{nullptr};   ///< Wartender FTP-Auftrag
    std::atomic<bool> m_ftpDone{false};                  ///< Abschluss des zuletzt übergebenen FTP-Auftrags
    TaskHandle_t m_ftpWaiter = nullptr;                  ///< Wartender FTP-Task, erhält eine Benachrichtigung
    char m_ftpPassword[FtpProtocol::PasswordCapacity]{}; ///< Gültiges FTP-Passwort, leer ohne. Geschützt durch m_configLock
    std::atomic<uint32_t> m_ftpPasswordRevision{0};      ///< Siehe ftpPasswordRevision()
    std::atomic<uint8_t> m_ftpPasswordRequest{0};        ///< 1 Passwort sicherstellen, 2 erneuern. Vom Worker abgeholt
    /** @brief Legt den FTP-Kartenzustand im PSRAM an. @return false ohne Speicher. */
    bool beginFtp();
    /** @brief Gibt den FTP-Kartenzustand nach einem gescheiterten begin() frei. */
    void endFtp();
    /** @brief Bearbeitet den wartenden FTP-Auftrag und weckt den FTP-Task, nur im Dienstworker. */
    void executeFtpFile();
    /** @brief Erzeugt, speichert und bestätigt ein neues FTP-Passwort, wenn renew gilt oder keines vorhanden ist. Nur im Dienstworker. */
    void executeFtpPassword(bool renew);
    /** @brief Schließt offene FTP-Dateien und -Ordner vor dem Aushängen einer verlorenen Karte, nur im Dienstworker. */
    void closeFtpFiles();
    /** @brief Fragt die eingebundene Karte mit CMD13 ab. Eine entnommene Karte antwortet nicht, eine wieder eingesetzte erst nach neuer Initialisierung. Erst drei Fehlschläge in Folge zählen, damit ein einzelner gestörter Befehl die Titelliste nicht verwirft. @return false ohne Antwort oder ohne eingebundene Karte. */
    static bool cardAnswers();
    /** @brief Liefert die FATFS-Laufwerksnummer der eingebundenen Karte, -1 ohne Karte. */
    static int cardDrive();
    QueueHandle_t m_coverRequests = nullptr;
    lgfx::LGFX_Sprite m_coverCanvas;
    uint8_t *m_coverBytes = nullptr;
    uint16_t *m_coverPixels = nullptr;
    SdCoverFrame m_coverFrame;
    uint32_t m_coverRevision = 0;
    CoverRequest m_coverWork;
    std::atomic<bool> m_coverReady{false};
    uint64_t m_sdTotal = 0, m_sdUsed = 0;

    /** @brief Ergebnis des zuletzt gelesenen Ordnerbilds. Weitere Titel desselben Ordners brauchen es nicht erneut zu lesen. */
    struct FolderCover
    {
        char path[256] = {};          ///< Pfad des Ordnerbilds, leer ohne Eintrag
        uint32_t mediaGeneration = 0; ///< Karte, von der es gelesen wurde
        bool usable = false;          ///< Dekodiert. Sonst zu groß oder defekt, dann gilt das eingebettete Bild
        bool inPixels = false;        ///< m_coverPixels enthält noch genau dieses Bild
    } m_folderCover;

    /** @brief Liest und dekodiert begrenzte JPEG-/PNG-Daten ausschließlich im Dienstworker: zuerst folder.jpg im Titelordner, danach das eingebettete Bild. */
    void decodeCover();
    /** @brief Dekodiert das erste JPEG, bei allowPng auch PNG, innerhalb der Daten auf die Coverfläche und kopiert es nach m_coverPixels. @return Bild vollständig dekodiert. */
    bool decodeImage(const uint8_t *data, size_t length, bool allowPng);
    QueueHandle_t m_requests = nullptr, m_results = nullptr, m_files = nullptr;
    TaskHandle_t m_task = nullptr;
    portMUX_TYPE m_configLock = portMUX_INITIALIZER_UNLOCKED;
    NetworkStatus m_network;
    uint32_t m_lastNetwork = 0;
    /** @brief Aktualisiert höchstens einmal je Sekunde das Netzabbild, nur im Dienstworker. */
    void updateNetwork();
    Settings::Configuration m_config;
    Tab5Config::Extras m_extras;
    ModernFarben m_modern = ModernVorgabe;                                ///< Gespeicherte Grundfarben von Modern, geschützt durch m_configLock
    bool m_modernStored = false;                                         ///< rr_tab5/modern_v1 vorhanden, geschützt durch m_configLock
    QueueHandle_t m_modernRequests = nullptr, m_modernResults = nullptr; ///< Aufträge und Abschlüsse für die Grundfarben, Tiefe je 1
    ModernRequest m_modernWork;                                          ///< Kopierter Auftrag im Dienstworker
    /** @brief Speichert oder löscht rr_tab5/modern_v1 nach m_modernWork, liest zur Bestätigung zurück und reiht das Ergebnis ein. Nur im Dienstworker. */
    void executeModern();
    RadioPreferences m_radio;
    RadioPreferences m_radioNvs;
    Tab5Rtc m_rtc;
    Tab5Rtc::Status m_rtcStatus;
    uint32_t m_rtcAttempt = 0, m_rtcTimeAttempt = 0, m_rtcRefresh = 0, m_flashAttempt = 0;
    bool m_rtcRadioFailed = false;
    std::atomic<uint32_t> m_radioFlashWrites{0};
    uint32_t m_radioChanged = 0, m_radioGeneration = 0, m_radioSaved = 0;
    uint64_t m_revision = 1;
    Settings::Request m_work;
    Result m_result;
    SdFileRequest m_fileWork;
    SdFileEntry *m_entries = nullptr;
    SdFileScan::Pending *m_pending = nullptr; ///< Pfadstapel des Sammelns im PSRAM, MaxPending Plätze
    SdFileResult m_fileResult;
    std::atomic<int> m_filesReady{0};
    std::atomic<uint32_t> m_cancel{0}, m_cancelFile{0}, m_timeRevision{0}, m_mediaRevision{1};
    std::atomic<uint32_t> m_mediaGeneration{1}; ///< Kartengeneration aus m_card für den UI-Task
    std::atomic<SdServiceStatus::Media> m_media{SdServiceStatus::Media::Unavailable};
    SdCardWatch m_card; ///< Überwachung der eingebundenen Karte, nur im Dienstworker
    std::atomic<bool> m_retry{true}, m_wifiBusy{false}, m_wifiConnecting{false}, m_restoreWifi{false};
    bool m_wifiReady = false;
    WifiSequence m_sequence; ///< Verbindungsablauf der gespeicherten Profile, nur im Dienstworker
    static std::atomic<uint32_t> s_connections; ///< Zähler der IPv4-Verbindungen aus dem Arduino-Ereignistask
    static std::atomic<bool> s_ntpSynced;
    static std::atomic<bool> s_rtcSyncPending;
    /** @brief Führt ausschließlich hier langsame Dienste aus. */
    static void worker(void *context);
    /** @brief Initialisiert einmalig die C6-Station (SDIO-Pins, Hostname) und prüft die MAC. @return false bei Pin-, Start- oder MAC-Fehler. */
    bool prepareWifi();
    /** @brief Führt den kopierten Auftrag m_work aus (Speichern, Zurücksetzen, Zeit setzen, WLAN-Suche, Verbindungstest), reiht das Ergebnis ein und löscht danach Auftrag und Ergebnis. */
    void execute();
    /** @brief Übernimmt Zeitzone und, bei bestehender WLAN-Verbindung, den NTP-Abgleich. Erhöht timeRevision(). */
    void applyTime();
    /** @brief Führt einen Schritt des Verbindungsablaufs aus, ohne auf das WLAN zu warten: Profil verbinden oder bei neuer Verbindung applyTime(). Nur im Dienstworker. */
    void connectProfiles();
    /** @brief Bindet die SD-Karte ein, räumt eigene Uploadreste auf, meldet das Ergebnis an m_card und aktualisiert den Medienstatus. Formatiert niemals. */
    void mount();
    /** @brief Führt den nächsten Schritt von m_card aus: Statusabfrage (bei Kartenverlust Medium Error mit neuer Generation), Aushängen ohne offene Audiodatei samt eigenem Upload oder Einbinden auf Anforderung. Nur im Dienstworker. */
    void watchCard();
    /** @brief Verarbeitet m_fileWork mit SdFileScan: Ordnerliste oder Sammeln der Auswahl, nur wenn die Karte zuvor auf den Statusbefehl antwortet. Ein gescheiterter Auftrag lässt m_card sofort prüfen. Gibt das Ergebnis über m_filesReady frei. */
    void files();
    /** @brief Lädt Konfiguration, Zusatzeinstellungen und Radiozustand aus dem NVS. */
    void load();
};
#endif
