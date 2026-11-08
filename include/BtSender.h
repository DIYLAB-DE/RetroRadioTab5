#ifndef BT_SENDER_H
#define BT_SENDER_H

#include "BtProtocol.h"
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

/** @brief Anbindung des externen Bluetooth-Senders (Pico) am M5-Bus: Steuerprotokoll über UART1 an G6/G7, Tonweg und Leitungstest. Für den Ton legt der Task BCLK, WS und Daten von I2S0 über die GPIO-Matrix zusätzlich an G16, G17, G18, wie ein Y-Kabel ohne zweite I2S-Einheit, solange der Pico versorgt ist (BtProtocol::routeAudio). Der Pico gibt den Ton an seinen optischen Ausgang und bei verbundenem Kopfhörer per Bluetooth (BtProtocol::headphoneOutput), sein UKW-Modulator hört an denselben Leitungen mit und bekommt Schalter, Frequenz, Leistung und RDS-Texte über BtProtocol::UkwSync. Ein eigener Task mit Stack im PSRAM bedient alles nacheinander. UI- und USB-Befehle stoßen nur an und lesen den Stand ohne Warten. Die UART ist nur offen, solange der Pico nachweislich versorgt ist (Ruhepegel G7 High, G6 Low, BtProtocol::judgeIdle) und HALLO beantwortet. Sonst sind alle fünf Leitungen Eingänge mit Pull-down. Verdrahtung und Flashregel in docs/Bluetooth-Sender.md. */
class BtSender : private BtProtocol::TestPort
{
public:
    /** @brief Antwort von startTest(). */
    enum class Start : uint8_t
    {
        Started, ///< Test angestoßen, das Ergebnis folgt als Logzeilen „BT TEST ...“
        Busy,    ///< Ein Test läuft noch
        NotReady ///< begin() ist gescheitert oder fehlt
    };

    /** @brief Befehl an den Pico, Zeilen und Wirkung in bt-sender/README.md. */
    enum class Command : uint8_t
    {
        Search,     ///< SUCHE: Suche mit fester Höchstdauer, Treffer in Status::link.hits
        Connect,    ///< VERBINDE mit der Adresse aus request()
        Disconnect, ///< TRENNE
        Forget,     ///< VERGISS
        Status,     ///< STATUS
        Tone,       ///< TESTTON
        Ukw         ///< UKW: UKW-Zeile anfordern
    };

    /** @brief Antwort von request(). */
    enum class Request : uint8_t
    {
        Accepted, ///< An den Task übergeben. Antwort und Ereignisse des Pico folgen in status()
        NoPico,   ///< Kein Pico verbunden oder ein Leitungstest läuft
        Busy,     ///< Befehlsqueue voll
        Invalid,  ///< Adresse fehlt oder ist ungültig
        NotReady  ///< begin() ist gescheitert oder fehlt
    };

    /** @brief Stand für Anzeige und Diagnose. */
    struct Status
    {
        bool present = false;                    ///< UART offen und HALLO beantwortet
        char stand[BtProtocol::StandCapacity]{}; ///< Firmwarestand des Pico, leer ohne
        BtProtocol::LinkView link;               ///< Zustand, Gerät, Treffer und letzter Fehler laut Pico
        uint32_t version = 0;                    ///< Zählt jede Änderung des Stands
    };

    /** @brief Stellt die fünf Leitungen als Eingänge mit Pull-down ein, bevor irgendein Treiber läuft. Ein Software-Neustart (ESP.restart, Absturz, Wachhund) lässt beim ESP32-P4 die GPIO-Matrix stehen: G6 treibt dann weiter UART1-TX und G16 bis G18 die I2S0-Signale, G6 speist den stromlosen Pico über dessen Schutzdiode, und begin() hielte die Leitungen für fremd belegt. Als erste Anweisung des Setups aufrufen. Danach kann nur noch ein anderer Teil der Firmware die Leitungen belegen. Ohne Log, weil der Logger noch nicht läuft. @return Bitmaske über BtProtocol::IdlePins der Leitungen, die noch trieben, 0 nach einem Kaltstart. */
    static uint8_t releaseAfterReset();
    /** @brief Prüft, dass UART1 und die fünf Leitungen frei sind (kein Arduino-Treiber, kein Ausgang, kein Peripheriesignal), stellt die Leitungen als Eingänge mit Pull-down ein und legt Befehlsqueue und Task an. Einmal im Setup nach den übrigen Diensten aufrufen. @return false, wenn etwas belegt ist (Logzeile „BT LEITUNG belegt“, die Leitungen bleiben dann unverändert), eine Leitung danach nicht als Eingang mit Pull-down dasteht (Logzeile „BT LEITUNG kein Eingang“) oder Queue bzw. Task fehlen. */
    bool begin();
    /** @brief Stößt den Leitungstest an. Aus dem UI-Task, ohne Warten. Der Task schließt dafür die UART und öffnet sie danach wieder. */
    Start startTest();
    /**
     * @brief Übergibt einen Befehl an den Task, ohne zu warten. Aus jedem Task außer dem eigenen.
     * @param command Befehl an den Pico
     * @param address Bei Command::Connect „AA:BB:CC:DD:EE:FF“ in Groß- oder Kleinschreibung, sonst ungenutzt
     * @return Accepted oder der Grund, warum nichts gesendet wird
     */
    Request request(Command command, const char *address = nullptr);
    /** @brief Kopiert den Stand nach out. Nur unter einer kurzen Sperre, ohne auf den Task zu warten. */
    void status(Status &out) const;
    /** @brief Änderungszähler des Stands ohne Sperre, die Anzeige kopiert nur bei einem neuen Wert. */
    uint32_t version() const;
    /** @brief Wunsch des Tab5, den Ton am Kopfhörer stumm zu schalten. Der Task sendet STUMM AN bzw. STUMM AUS, sobald der Pico verbunden ist, und nach jeder neuen Verbindung erneut. Aus dem UI-Task, ohne Warten. */
    void setMuted(bool muted);
    /** @brief Abtastrate der Quelle in Hz, die I2S0 gerade ausgibt. Der Task sendet sie als RATE, 0 und Werte außerhalb BtProtocol::MinRate..MaxRate bleiben ohne Wirkung. Aus dem UI-Task, ohne Warten. */
    void setRate(uint32_t rate);
    /** @brief Stellung des Drehknopfs 0..100 als Kopfhörerlautstärke. Der Task sendet sie als LAUTSTAERKE, solange der Kopfhörer verbunden ist. Erst nach der ersten KOPFHOERER-Meldung der Verbindung setzen, sonst überschriebe der Knopf die im Pico gespeicherte Lautstärke. Jeder Verbindungswechsel und jedes Ende des Tonwegs löscht den Wunsch. Aus dem UI-Task, ohne Warten. */
    void setVolume(uint8_t volume);
    /** @brief Wunsch des Tab5 an den UKW-Modulator des Pico: Sender an oder aus, Frequenz in kHz, Leistungsstufe 0..BtProtocol::UkwMaxPower. Der Task sendet nur Änderungen und nur, solange der Pico einen Modulator meldet, nach jedem Öffnen der UART und jedem neu erkannten Modulator alles erneut. Aus dem UI-Task, ohne Warten. */
    void setUkw(bool enabled, uint32_t khz, uint8_t power);
    /** @brief RDS-Texte für den Modulator: Programmname (der Pico kürzt auf 8 Zeichen) und Radiotext (64 Zeichen), UTF-8, jeder bis BtProtocol::RdsTextBytes. Gesendet werden nur Änderungen. Aus dem UI-Task, ohne Warten. */
    void setRds(const char *name, const char *text);
    /** @brief I2S0 liegt zusätzlich an G16, G17 und G18, solange der Pico versorgt ist. Der Ton geht dann an seinen optischen Ausgang und den UKW-Modulator. Ohne Sperre, aus jedem Task. */
    bool routed() const;
    /** @brief Der Ton gehört dem Bluetooth-Kopfhörer: Tonweg angelegt und Kopfhörer verbunden (BtProtocol::headphoneOutput). Ohne Sperre, aus jedem Task. */
    bool headphone() const;
    /** @brief Ob der Pico gerade läuft: G7 liegt bei acht Ablesungen im Abstand von 100 µs mindestens einmal High. Die Pico-TX ruht High, ohne laufenden Pico zieht der Pull-down aus begin() G7 auf Low. Auch bei offener UART gültig. Treibt nichts, ohne Sperre, aus jedem Task. Erst nach begin() verlässlich. Dauert höchstens rund 1 ms. */
    bool picoAwake() const;

private:
    /** @brief Eintrag der Befehlsqueue. */
    struct Order
    {
        Command command;                          ///< Befehl
        char address[BtProtocol::AddressSize]{}; ///< Bei Connect die geprüfte Adresse in Großbuchstaben
    };

    /** @brief Einstieg des Tasks. @param context Das BtSender-Objekt. */
    static void task(void *context);
    /** @brief Schleife des Tasks: Leitungstest auf Anforderung, sonst bei offener UART serviceLink() im Takt von 20 ms, bei geschlossener probe() im Abstand von 1 s. */
    void run();
    /** @brief Öffnet die UART nur, wenn die Leitungen noch wie nach begin() stehen und judgeIdle() den Pico als versorgt erkennt, und fragt dann HALLO und STATUS. Ohne Antwort gibt er die Leitungen wieder frei und versucht es erst nach 5 s erneut. */
    void probe();
    /** @brief Sendet die Befehle der Queue, wertet die Zeilen des Pico aus, führt Tonweg, Stummschaltung und Quellrate nach und schließt die UART, sobald G7 bei zwei Prüfungen im Abstand von 0,5 s dauerhaft Low liegt (Pico ohne Versorgung oder im Reset). */
    void serviceLink();
    /** @brief Legt I2S0 an G16, G17, G18 an oder nimmt es weg, wie BtProtocol::routeAudio es verlangt. Scheitert das Anlegen, folgt der nächste Versuch erst nach dem nächsten Verbindungswechsel. */
    void updateRoute();
    /** @brief Legt die Ausgangssignale BCLK, WS und Daten von I2S0 über die GPIO-Matrix zusätzlich an G16, G17, G18, mit der schwächsten Treiberstufe. Nur, wenn die Codec-Pins G27, G29, G26 genau diese Signale tragen. Prüft danach jede Leitung. @return false ohne Änderung bzw. mit wieder freigegebenen Leitungen. */
    bool routeI2s();
    /** @brief Stellt G16, G17, G18 wieder als Eingänge mit Pull-down ein und löscht den Lautstärkewunsch. @param reason Grund für die Logzeile. */
    void unrouteI2s(const char *reason);
    /** @brief Sendet die fälligen Zeilen aus BtProtocol::OutputSync. */
    void syncOutput();
    /** @brief Sendet die fälligen Zeilen aus BtProtocol::UkwSync, nur solange der Pico einen Modulator meldet. */
    void syncUkw();
    /** @brief Sendet eine Befehlszeile mit Zeilenende und loggt sie als „BT -> ...“. */
    void sendLine(const char *line);
    /** @brief Wertet eine Zeile des Pico aus, loggt sie als „BT PICO ...“, führt den Stand nach und fragt bei Bedarf STATUS. */
    void handleLine(const char *line);
    /** @brief Gibt UART und Leitungen frei (release()), verwirft offene Befehle und setzt den Stand auf „kein Pico“. @param reason Grund für die Logzeile. */
    void closeLink(const char *reason);
    /** @brief Prüft, dass UART1 frei ist und die fünf Leitungen noch Eingänge mit Pull-down ohne Ausgang sind. Loggt jede abweichende Leitung mit prefix, bei nullptr ohne Log. */
    bool linesResting(const char *prefix);
    /** @brief Prüft die Leitungen, führt BtProtocol::runLineTest aus, gibt UART und Leitungen frei und loggt Befunde und Zusammenfassung. Nur im Task bei geschlossener UART. */
    void runTest();
    /** @brief Liest den Pegel mehrmals im Abstand von bis zu 1 ms. */
    int idleLevel(uint8_t pin) override;
    /** @brief Startet den Treiber für UART1 an G6/G7 mit der schwächsten Treiberstufe an G6 und schickt ein einzelnes Zeilenende. */
    bool openUart() override;
    /** @brief Leert den Empfang, sendet command und liest byteweise mit Frist. */
    bool exchange(const char *command, const char *expected, char (&reply)[BtProtocol::MaxLine + 1]) override;
    /** @brief Schaltet pin mit der schwächsten Treiberstufe im Abstand von mindestens 1 ms um. */
    void toggle(uint8_t pin, uint16_t edges) override;
    /** @brief Nimmt den Tonweg samt Lautstärkewunsch weg, löscht den UART-Treiber, falls offen, und stellt alle fünf Leitungen wieder als Eingänge mit Pull-down ein. */
    void release();

    TaskHandle_t m_task = nullptr;                              ///< Task, nullptr ohne begin()
    QueueHandle_t m_orders = nullptr;                           ///< Befehlsqueue vom UI- und USB-Weg an den Task
    std::atomic<bool> m_running{false};                         ///< Ein Leitungstest ist angestoßen und noch nicht beendet
    std::atomic<bool> m_present{false};                         ///< Wie m_status.present, für request() ohne Sperre
    std::atomic<uint32_t> m_version{0};                         ///< Wie m_status.version, für version() ohne Sperre
    std::atomic<bool> m_routed{false};                          ///< I2S0 liegt an G16, G17, G18. Schreibt nur der Task
    std::atomic<bool> m_connected{false};                       ///< Kopfhörer laut Pico verbunden, schreibt nur der Task
    std::atomic<bool> m_wantMuted{true};                        ///< Stummwunsch des UI-Tasks
    std::atomic<uint32_t> m_wantRate{0};                        ///< Quellrate vom UI-Task in Hz, 0 ohne
    std::atomic<int> m_wantVolume{-1};                          ///< Stellung des Drehknopfs vom UI-Task, -1 ohne Wunsch
    BtProtocol::OutputSync m_sync;                              ///< An den Pico gesendeter Stumm- und Ratenstand, nur im Task
    std::atomic<bool> m_wantUkwOn{false};                       ///< UKW-Wunsch des UI-Tasks: Sender an
    std::atomic<uint32_t> m_wantUkwKhz{0};                      ///< UKW-Wunsch: Frequenz in kHz, 0 ohne
    std::atomic<uint8_t> m_wantUkwPower{0};                     ///< UKW-Wunsch: Leistungsstufe
    BtProtocol::UkwSync m_ukwSync;                              ///< An den Pico gesendeter UKW-Stand, nur im Task
    char m_rdsName[BtProtocol::RdsTextBytes] = {};              ///< Programmname für RDS, unter m_lock
    char m_rdsText[BtProtocol::RdsTextBytes] = {};              ///< Radiotext für RDS, unter m_lock
    uint32_t m_rdsNameVersion = 0, m_rdsTextVersion = 0;        ///< Zählen jede Änderung der Texte, unter m_lock, 0 heißt nie gesetzt
    bool m_routeRefused = false;                                ///< routeI2s() scheiterte für die laufende Verbindung, nur im Task
    bool m_uartOpen = false;                                    ///< UART-Treiber installiert, nur im Task
    uint32_t m_nextProbeMs = 0;                                 ///< Frühester nächster HALLO-Versuch, nur im Task
    uint32_t m_nextPresenceMs = 0;                              ///< Nächste Prüfung des Ruhepegels an G7, nur im Task
    uint8_t m_lowChecks = 0;                                    ///< Prüfungen hintereinander mit G7 dauerhaft Low, nur im Task
    BtProtocol::Verdict m_lastIdle = BtProtocol::Verdict::Unchecked; ///< Zuletzt geloggter Ruhebefund, gegen Wiederholungen im Log. Nur im Task
    bool m_quietLogged = false;                                 ///< Abweichung der Leitungen oder fehlende HALLO-Antwort schon geloggt, bis zur nächsten Antwort. Nur im Task
    BtProtocol::LineReader m_reader;                            ///< Empfangszeile bei offener UART, nur im Task
    mutable portMUX_TYPE m_lock = portMUX_INITIALIZER_UNLOCKED; ///< Schützt m_status zwischen Task und Lesern
    Status m_status;                                            ///< Stand, nur unter m_lock
};

#endif
