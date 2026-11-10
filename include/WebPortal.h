#ifndef WEB_PORTAL_H
#define WEB_PORTAL_H

#include "AppServices.h"
#include "StationManager.h"
#include "WebReply.h"
#include "WebValidation.h"
#include <esp_http_server.h>
#include <atomic>

class FtpService;
struct cJSON;

/** @brief Webportal auf dem vorhandenen IDF-HTTP-Server, UI-Befehle laufen ausschließlich über den Koordinator. */
class WebPortal
{
public:
    /** @brief Auftragsart, die der HTTP-Task an den UI-Koordinator übergibt. */
    enum class Action : uint8_t
    {
        Volume,          ///< Pegel setzen
        Mute,            ///< Stummschaltung setzen
        Balance,         ///< Balance setzen
        Tone,            ///< Drei EQ-Werte setzen
        Station,         ///< Sender wählen
        Screenshot,      ///< Display aufnehmen
        Settings,        ///< Konfiguration speichern oder zurücksetzen
        Reboot,          ///< Neustart
        LeaseFiles,      ///< SD-Zugriff für die Dateiverwaltung reservieren
        ReleaseFiles,    ///< Reservierung aufheben
        StationsChanged, ///< Senderliste wurde gespeichert
        Theme            ///< Thema und Grundfarben von Modern speichern oder die Grundfarben auf die Vorgaben zurücksetzen
    };

    /** @brief Auftrag des HTTP-Tasks. id, expires und generation setzt execute(). */
    struct Command
    {
        Action action = Action::Volume;   ///< Auftragsart
        uint32_t id = 0, expires = 0, generation = 0; ///< Kennung, Ablaufzeit in ms und Sitzungsgeneration zum Zeitpunkt der Anfrage
        int values[3]{};                  ///< Parameter je Aktion: Wert, Index oder drei EQ-Werte. Bei StationsChanged ohne reorder drei Masken (je 24 Bit) der geänderten Sender. Bei Theme values[0] = 1, wenn modern auszuführen ist
        uint8_t order[MAX_STATIONS]{};    ///< Bei StationsChanged mit reorder der bisherige Platz je neuem Platz (order[neu] = alt)
        bool reorder = false;             ///< StationsChanged hat die Liste nur umsortiert, der Koordinator setzt Senderwahl und laufenden Stream mit order auf den neuen Platz
        bool restart = false;             ///< Nach Abschluss der Konfiguration, des Themas oder der Grundfarben neu starten
        Settings::Request settings;       ///< Auftrag für den Dienst bei Action::Settings. Bei Action::Theme das Speichern des Themas, sonst type None
        AppServices::ModernRequest modern; ///< Auftrag für den Dienst bei Action::Theme mit values[0] = 1
    };

    /** @brief Vom UI-Task veröffentlichter Zustand für /api/status. */
    struct State
    {
        bool locked = true, radio = false, muted = true, playing = false; ///< PIN-Sperre aktiv, Radiobetrieb statt SD-Player, stumm, Wiedergabe läuft
        bool sdBusy = false;                                               ///< SD-Wiedergabe läuft oder ein SD-Auftrag wartet, solange vergibt der Koordinator keine Kartensperre
        uint8_t volume = 10, eyeStyle = 0;                                 ///< Pegel und gewählter Stil des Magischen Auges
        int8_t balance = 0, tone[3]{};                                     ///< Balance und drei EQ-Werte
        int station = 0;                                                   ///< Gewählter Senderindex
        uint32_t firmwareBytes = 0, appAddress = 0, appCapacity = 0, stationsRevision = 1; ///< Größe der Firmware, Adresse und Größe der App-Partition, Zähler der Senderliste
        char title[256]{}, bitrate[32]{};                                  ///< Aktueller Titel und Bitrate als Text
    };

    /** @brief Reserviert Server und Queues einmalig. Keine neue Bibliothek. @param services Dienste für Konfiguration und Dateien. @param stations Senderverwaltung. @param pixels Gemeinsamer Aufnahmeplatz mit 1280x720 RGB565-Pixeln (gehört dem Aufrufer, nullptr nur ohne Screenshot). @param ftp FTP-Dienst für Schalter, Zustand und Passwort, nullptr ohne (die FTP-Routen antworten dann mit 503). @return false bei fehlendem Speicher oder Serverfehler. */
    bool begin(AppServices &services, StationManager &stations, uint16_t *pixels, FtpService *ftp);
    /** @brief Kopiert ein Statusabbild ohne I/O oder Allokation im UI-Pfad, ein Wechsel auf locked entwertet alle Sitzungen. */
    void publish(const State &state);
    /** @brief Holt einen noch gültigen Auftrag. Abgelaufene (504) und nach einer Sitzungsentwertung gestellte (401) werden nicht ausgeführt, sondern sofort beantwortet. StationsChanged gilt immer, weil die Senderliste dann schon gespeichert ist. @param command Ziel. @return true, wenn ein auszuführender Auftrag vorliegt. */
    bool take(Command &command);
    /** @brief Meldet tatsächliche Ausführung oder Speicherabschluss an den wartenden HTTP-Aufruf, Abschlüsse abgelaufener Aufträge werden verworfen. @param id Kennung aus Command::id. @param status HTTP-Statuscode. */
    void complete(uint32_t id, int status);
    /** @brief Erzeugt die Aufnahme des ganzen 1280x720-Displays ausschließlich im UI-Task. @return false ohne Puffer oder bei anderer Displaygröße. */
    bool capture(lgfx::LGFX_Device &display);

    /** @brief Meldet einen laufenden Web-Screenshot vom Auftrag bis zum Ende der Übertragung, solange bleibt der Aufnahmeplatz belegt. */
    bool shotBusy() const
    {
        return m_shotBusy.load();
    }

    /** @brief Entwertet Websitzungen nach PINänderungen oder einer erneuten Displaysperre. */
    void invalidateSessions()
    {
        m_authGeneration.fetch_add(1);
    }

private:
    AppServices *m_services = nullptr;
    StationManager *m_stations = nullptr;
    FtpService *m_ftp = nullptr;                                 ///< FTP-Dienst, nullptr ohne
    httpd_handle_t m_server = nullptr;
    QueueHandle_t m_commands = nullptr;                          ///< Aufträge an den Koordinator, Tiefe 1
    WebReply m_reply;                                            ///< Antworten des Koordinators an den wartenden HTTP-Task
    portMUX_TYPE m_lock = portMUX_INITIALIZER_UNLOCKED;          ///< Schützt m_state
    State m_state;
    uint16_t *m_pixels = nullptr;                                ///< Aufnahmeplatz, gehört dem Aufrufer
    Station *m_stationScratch = nullptr;                         ///< Arbeitskopie der Senderliste beim Speichern
    uint32_t m_sequence = 0;                                     ///< Zähler der Auftragskennungen
    WebValidation::LoginLock m_login;                            ///< Mindestabstand und Sperre der PIN-Anmeldung, nur im HTTP-Task
    uint32_t m_uploadId = 0, m_uploadSize = 0, m_uploaded = 0, m_uploadGeneration = 0; ///< Laufender Upload: Kennung, erwartete und empfangene Bytes, Sitzungsgeneration
    std::atomic<uint32_t> m_authGeneration{1};                   ///< Steigt bei jeder Entwertung der Sitzungen
    std::atomic<bool> m_shotBusy{false};                         ///< Aufnahmeplatz belegt, siehe shotBusy()

    /** @brief Angemeldete Websitzung. */
    struct Session
    {
        char token[33]{};                    ///< Zufälliges Cookie-Token
        uint32_t generation = 0, created = 0; ///< Gültige Generation und Erstellzeit in ms, Sitzungen gelten höchstens 30 Minuten
    } m_sessions[3];

    /** @brief Verarbeitet HTTP ausschließlich im Server-Task auf Core 0. */
    static esp_err_t handle(httpd_req_t *request);
    /** @brief Verteilt eine Anfrage nach Pfad und Methode auf die Endpunkte, prüft Ursprung und Sitzung. */
    esp_err_t route(httpd_req_t *request);
    /** @brief Kopiert den zuletzt veröffentlichten Zustand. */
    State state();
    /** @brief Ermittelt nach einer verweigerten Kartensperre den Grund für die Meldung im Browser: FTP hält die Sperre, oder die SD-Wiedergabe läuft bzw. wartet. */
    WebFileStore::Reason leaseReason();
    /** @brief Prüft, ob die Anfrage das Cookie einer gültigen Sitzung der aktuellen Generation trägt. Bei ausgeschalteter PIN-Sperre gilt jede Anfrage als angemeldet. */
    bool authenticated(httpd_req_t *request);
    /** @brief Nimmt nur Anfragen an die eigene IPv4, den Hostnamen oder „<hostname>.local“ an. Bei POST zusätzlich Origin beziehungsweise Sec-Fetch-Site gegen fremde Seiten. Abgewiesene Hosts landen im Log. @param post true bei schreibender Anfrage. */
    bool trusted(httpd_req_t *request, bool post);
    /** @brief Übergibt einen Auftrag an den UI-Koordinator und wartet bis zu 16 s auf die Antwort. @param command Auftrag. id, expires und generation werden gesetzt. @return HTTP-Statuscode, 503 bei voller Warteschlange, 504 ohne Antwort. */
    int execute(Command &command);
    /** @brief Bearbeitet /api/settings, /api/settime und /factoryreset: prüft das JSON und gibt die Änderung an den Dienst. GET /api/settings liefert auch pinLockEnabled und secondHandSweep. Ein POST nur mit lockPinCurrent und lockPinNew ändert allein die PIN, ohne Revision und Neustart, die falsche aktuelle PIN ergibt 403. */
    esp_err_t settings(httpd_req_t *request, const char *body);
    /** @brief Bearbeitet POST /api/lock/mode (Formular enabled=true|false, zum Ausschalten zusätzlich pin). Der schon geltende Zustand ist ohne Auftrag erfolgreich. Ausschalten ohne die aktuelle PIN ergibt 403, sonst speichert der Dienst den Schalter. Antwort wie das lock-Ereignis {"enabled":...,"locked":...}. */
    esp_err_t lockMode(httpd_req_t *request, const char *body);
    /** @brief Bearbeitet POST /api/secondhand (Formular sweep=true|false): speichert die Gangart des Sekundenzeigers im MainScreen ohne Neustart und ohne die Sitzungen abzumelden. Der schon geltende Zustand ist ohne Auftrag erfolgreich. Antwort {"sweep":...,"revision":...} mit der neuen Revision der Einstellungen. */
    esp_err_t secondHand(httpd_req_t *request, const char *body);
    /** @brief Bearbeitet /api/theme (GET liefert Thema und Grundfarben von Modern, POST speichert Thema und/oder geänderte Grundfarben mit höchstens einem Neustart) und /api/theme/modern/reset (POST stellt die Vorgaben der Grundfarben wieder her). Den Neustart verlangt wie bei den Einstellungen das Feld restart. */
    esp_err_t theme(httpd_req_t *request, const char *body);
    /** @brief Bearbeitet /api/stations, /api/radio/presets und /api/radio/reset-stations (Senderliste speichern, Favoriten, Zurücksetzen). Ein POST auf /api/stations mit order (70 bisherige Plätze, order[neu] = alt) sortiert nur um: Jeder Eintrag muss dem bisherigen Sender auf order[i] gleichen, sonst 409. Die Antwort trägt dann zusätzlich stationId. */
    esp_err_t stations(httpd_req_t *request, const char *body);
    /** @brief Bearbeitet /api/sdcard/info und /api/files/* der SD-Karte (Auflisten, Anlegen, Löschen, Umbenennen). */
    esp_err_t files(httpd_req_t *request, const char *body);
    /** @brief Bearbeitet /api/files/tree: POST mit path und action (count zählt alle Dateien und Ordner im Ordner, delete löscht ihn samt Inhalt, versteckte und Systemeinträge eingeschlossen) beginnt die Baumarbeit im Dienstworker und antwortet sofort. GET liefert den Stand (id, action, running, path, files, folders, nach dem Ende status samt reason und failed). Der Dienstworker hält die Kartensperre bis zum Ende. */
    esp_err_t tree(httpd_req_t *request, const char *body);
    /** @brief Bearbeitet /api/ftp (GET liefert den Zustand, POST {"enabled":true|false} schaltet den Dienst nur im RAM und liefert den neuen Zustand), /api/ftp/password (POST liefert das Passwort und lässt vorher eines erzeugen, wenn noch keins existiert) und /api/ftp/renew (POST lässt ein neues Passwort erzeugen und speichern und antwortet erst danach, eine angemeldete FTP-Verbindung wird getrennt). Ohne FTP-Dienst 503. Das Passwort steht nur in der Antwort von /api/ftp/password, nie im HTML oder Log. Puffer werden danach gelöscht. */
    esp_err_t ftp(httpd_req_t *request, const char *body);
    /** @brief Hängt den FTP-Zustand an object an: enabled, listening, state (off aus, sd belegt durch SD-Wiedergabe, client verbunden, on an), address („ftp://<IP>“, leer ohne Adresse), port, user, client (Adresse oder leer), loggedIn, transfer (LIST, RETR, STOR oder leer) und password (eines ist erzeugt), nie das Passwort selbst. */
    void ftpJson(cJSON *object);
    /** @brief Nimmt /api/files/upload blockweise entgegen. */
    esp_err_t upload(httpd_req_t *request);
    /** @brief Liefert /screenshot als Datenstrom, belegt dafür den Aufnahmeplatz bis zum Ende der Übertragung. */
    esp_err_t screenshot(httpd_req_t *request);
    /** @brief Erzeugt ausschließlich öffentliche Statusfelder, WLANpasswort und PIN werden nie serialisiert. */
    char *statusJson(bool radio);
};
#endif
