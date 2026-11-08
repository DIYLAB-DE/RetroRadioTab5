#ifndef FTP_SERVICE_H
#define FTP_SERVICE_H

#include "AppServices.h"
#include "FileLease.h"
#include "FtpSession.h"
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

/** @brief FTP-Dienst auf lwIP-Sockets für genau einen Client im eigenen IPv4-Netz: Steuerkanal Port 21, Datenkanal nur passiv (PASV/EPSV) auf FtpProtocol::PassiveFirst bis PassiveFirst + PassiveCount - 1. Ein eigener Task mit Stack im PSRAM bedient die Sockets. Jede Kartenarbeit geht als Auftrag an den Dienstworker und hält die Kartensperre, die der UI-Koordinator vergibt. Der Schalter gilt nur im RAM und ist nach dem Start aus. Senden ist wie im Webportal gedrosselt. */
class FtpService : private FtpEnvironment
{
public:
    /** @brief Zustand für Diagnose und Webportal, Kopie ohne I/O. */
    struct Status
    {
        bool enabled = false;                                       ///< Schalter
        bool listening = false;                                     ///< Port 21 nimmt Verbindungen an
        bool password = false;                                      ///< Ein Passwort ist erzeugt
        bool client = false;                                        ///< Ein Client ist verbunden
        bool loggedIn = false;                                      ///< Der Client ist angemeldet
        FtpSession::Transfer transfer = FtpSession::Transfer::None; ///< Laufende Übertragung
        uint8_t ip[4]{};                                            ///< Eigene IPv4-Adresse der WLAN-Station, 0.0.0.0 ohne Verbindung
        uint8_t clientIp[4]{};                                      ///< Adresse des verbundenen Clients
        uint16_t port = FtpProtocol::ControlPort;                   ///< Port des Steuerkanals
        uint64_t bytes = 0;                                         ///< Bytes der laufenden oder letzten Übertragung
        uint32_t sessions = 0;                                      ///< Angenommene Verbindungen seit dem Start
        uint32_t rejected = 0;                                      ///< Abgewiesene Verbindungen: zweiter Client oder fremdes Netz
        uint32_t failedLogins = 0;                                  ///< Fehlgeschlagene Anmeldungen
        uint32_t transfers = 0;                                     ///< Abgeschlossene Übertragungen
        uint32_t stackFree = 0;                                     ///< Kleinster freier Stack des FTP-Tasks in Byte
    };

    /** @brief Legt Puffer, Sitzung und Task einmalig im PSRAM an. Der Dienst bleibt aus. @return false ohne Speicher oder Task. */
    bool begin(AppServices &services);
    /** @brief Schaltet den Dienst ein oder aus. Aus jedem Task, ohne Warten. Beim Einschalten ohne Passwort wird eines erzeugt. Beim Ausschalten werden eine laufende Übertragung abgebrochen und die Verbindungen geschlossen. */
    void setEnabled(bool enabled);

    /** @brief Liefert den Schalter. */
    bool enabled() const
    {
        return m_wanted.load();
    }

    /** @brief Kopiert den Zustand, aus jedem Task. */
    Status status();
    /** @brief Kopiert das Passwort für eine angemeldete Websitzung. Nie ins Log oder ins HTML. @return false, solange keines erzeugt ist. */
    bool password(char (&out)[FtpProtocol::PasswordCapacity]) override;
    /** @brief Lässt im Dienstworker ein neues Passwort erzeugen und speichern, eine angemeldete Verbindung wird danach getrennt. @return false ohne Dienst. */
    bool renewPassword();

    /** @brief Zähler, der mit jedem gespeicherten neuen Passwort steigt, 0 ohne Passwort. */
    uint32_t passwordRevision() const
    {
        return m_services ? m_services->ftpPasswordRevision() : 0;
    }

    /** @brief Liefert den Namen einer Übertragung für Diagnose und Anzeige: LIST, RETR, STOR oder „keiner“. */
    static const char *transferName(FtpSession::Transfer transfer);

    /** @brief Briefkasten für die Kartensperre, der UI-Koordinator beantwortet ihn mit pending() und answer(). */
    FileLeaseRequest &leaseRequests()
    {
        return m_leaseRequests;
    }

private:
    struct Memory;

    AppServices *m_services = nullptr;              ///< Dienstworker
    Memory *m_memory = nullptr;                     ///< Puffer und Sitzung im PSRAM
    FtpSession *m_session = nullptr;                ///< Sitzung in m_memory
    TaskHandle_t m_task = nullptr;                  ///< FTP-Task
    std::atomic<bool> m_wanted{false};              ///< Schalter
    FileLeaseRequest m_leaseRequests;               ///< Anforderung der Kartensperre an den UI-Koordinator
    int m_listen = -1, m_control = -1;              ///< Lauschender Socket und Steuerkanal, -1 ohne
    int m_passive = -1, m_data = -1;                ///< Passiver Datenport und Datenverbindung, -1 ohne
    uint16_t m_nextPort = 0;                        ///< Index des nächsten passiven Ports
    uint8_t m_peer[4]{};                            ///< Adresse des Clients
    bool m_dataEnd = false;                         ///< Die Datenverbindung hat ihr Ende gemeldet
    bool m_controlBroken = false;                   ///< Der Steuerkanal ist nicht mehr beschreibbar
    uint32_t m_loginRevision = 0;                   ///< Passwortstand bei der Anmeldung
    uint64_t m_serviceAt = 0;                       ///< Letzte Prüfung auf neue Verbindungen während einer Übertragung in ms
    portMUX_TYPE m_statusLock = portMUX_INITIALIZER_UNLOCKED; ///< Schützt m_status
    Status m_status;                                ///< Zustand für status()

    /** @brief Taskeinstieg. */
    static void task(void *context);
    /** @brief Hauptschleife des FTP-Tasks: Schalter, Lauschen, Verbindung und Fristen. */
    void run();
    /** @brief Öffnet Port 21. @return false bei Socketfehler. */
    bool listen();
    /** @brief Trennt den Client und schließt Port 21. */
    void stopListening();
    /** @brief Nimmt eine wartende Verbindung an: als Sitzung, wenn keine besteht und die Gegenstelle im eigenen Netz liegt, sonst mit 421 abgewiesen. @param busy Eine Sitzung besteht. */
    void acceptClient(bool busy);
    /** @brief Hält den Dienst während einer Übertragung bedienbar: weist neue Verbindungen ab. @return false, wenn der Dienst abgeschaltet wird. */
    bool service();
    /** @brief Verarbeitet alle vollständigen Zeilen des Steuerkanals. */
    void handleControl();
    /** @brief Schließt Steuer- und Datenkanal und protokolliert den Grund. */
    void endSession(const char *reason);
    /** @brief Liefert die nächste Zeile aus bereits empfangenen Bytes oder liest ohne Warten nach. */
    Control readLine(char *out);
    /** @brief Sendet Text vollständig auf dem Steuerkanal, höchstens ReplyTimeoutMs. */
    bool sendControl(int socket, const char *text, size_t length);
    /** @brief Überträgt den Zustand der Sitzung nach m_status. */
    void publish();
    /** @brief Wartet höchstens 50 ms, bis der Datenkanal beschreibbar bzw. lesbar ist. @param write true beim Senden. @param interruptible Auch auf Eingang des Steuerkanals achten. @return true, wenn Eingang auf dem Steuerkanal wartet. */
    bool waitData(bool write, bool interruptible);

    uint64_t now() override;
    int64_t wallClock() override;
    void reply(const char *text) override;
    Lease lease() override;
    void release() override;
    void file(FtpProtocol::Job &job) override;
    bool passive(uint8_t (&ip)[4], uint16_t &port) override;
    bool accept() override;
    Data send(const uint8_t *data, size_t length, size_t &sent, bool interruptible) override;
    Data receive(uint8_t *data, size_t capacity, size_t &received, bool interruptible) override;
    Upload uploadAllowed() override;
    void closeData() override;
    Control poll(char *line) override;
    void wait(uint32_t ms) override;
    void transferred(const char *command, const char *path, const char *reply, uint64_t bytes) override;
};
#endif
