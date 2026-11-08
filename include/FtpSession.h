#ifndef FTP_SESSION_H
#define FTP_SESSION_H

#include "FtpProtocol.h"
#include "WebValidation.h"

/** @brief Umgebung einer FTP-Sitzung: Steuer- und Datenkanal, Kartensperre, Kartenaufträge und Zeit. Im Gerät ist das FtpService, in Hosttests eine Attrappe. Alle Aufrufe kommen aus dem Task der Sitzung. */
class FtpEnvironment
{
public:
    /** @brief Ergebnis einer Anforderung der Kartensperre. */
    enum class Lease : uint8_t
    {
        Granted, ///< Sperre erhalten, gilt bis release()
        Playing, ///< SD-Wiedergabe läuft oder ein SD-Auftrag wartet
        Busy,    ///< Das Webportal hält die Karte
        Timeout  ///< Der UI-Koordinator hat nicht rechtzeitig geantwortet
    };

    /** @brief Ergebnis eines Vorgangs auf dem Datenkanal. */
    enum class Data : uint8_t
    {
        Ok,      ///< Übertragen. Bei receive() mit 0 Byte das Ende des Datenstroms
        Control, ///< Der Datenkanal stockt und auf dem Steuerkanal liegt Eingang, etwa ABOR. Die Sitzung prüft ihn und setzt fort
        Closed,  ///< Gegenstelle hat die Verbindung abgebrochen
        Timeout, ///< Kein Fortschritt innerhalb von FtpProtocol::DataStallTimeoutMs
        Memory,  ///< Nur receive(): zu wenig freier interner DMA-Speicher für weitere Uploaddaten, siehe uploadAllowed()
        Radio,   ///< Nur receive(): ein Radiostream hat begonnen, siehe uploadAllowed()
        Shutdown ///< Der Dienst wird abgeschaltet
    };

    /** @brief Ob Uploaddaten angenommen werden können. */
    enum class Upload : uint8_t
    {
        Ok,    ///< Ja
        Radio, ///< Ein Radiostream läuft, auch stumm
        Memory ///< Zu wenig zusammenhängender interner DMA-Speicher
    };

    /** @brief Zustand des Steuerkanals beim Abfragen ohne Warten. */
    enum class Control : uint8_t
    {
        None,    ///< Keine vollständige Zeile
        Line,    ///< Eine Zeile liegt im Ziel
        Invalid, ///< Zeile zu lang oder mit Nullbyte, verworfen
        Closed   ///< Steuerkanal getrennt
    };

    virtual ~FtpEnvironment() = default;
    /** @brief Liefert eine monotone Zeit in ms. */
    virtual uint64_t now() = 0;
    /** @brief Liefert die Unix-Zeit für die Datumsangaben von LIST. */
    virtual int64_t wallClock() = 0;
    /** @brief Sendet eine oder mehrere vollständige Antwortzeilen samt CRLF auf dem Steuerkanal. */
    virtual void reply(const char *text) = 0;
    /** @brief Fordert einmal die Kartensperre an und wartet begrenzt auf die Antwort. */
    virtual Lease lease() = 0;
    /** @brief Gibt die Kartensperre frei. */
    virtual void release() = 0;
    /** @brief Führt einen Kartenauftrag synchron aus, das Ergebnis steht in job.status. */
    virtual void file(FtpProtocol::Job &job) = 0;
    /** @brief Öffnet einen passiven Datenport und schließt einen vorherigen samt Datenverbindung. @param ip Eigene Adresse des Steuerkanals. @param port Geöffneter Port. @return false, wenn kein Port aus dem Bereich frei ist. */
    virtual bool passive(uint8_t (&ip)[4], uint16_t &port) = 0;
    /** @brief Wartet höchstens FtpProtocol::DataConnectTimeoutMs auf die Datenverbindung derselben Gegenstelle wie beim Steuerkanal. Andere Adressen werden abgewiesen. @return false bei Zeitüberschreitung, Fehler oder Abschaltung. */
    virtual bool accept() = 0;
    /** @brief Sendet length Byte gedrosselt auf dem Datenkanal. @param sent Gesendete Byte, bei Ok length. @param interruptible Bei stockendem Datenkanal und Eingang auf dem Steuerkanal mit Control zurückkehren. */
    virtual Data send(const uint8_t *data, size_t length, size_t &sent, bool interruptible) = 0;
    /** @brief Liest bis capacity Byte vom Datenkanal. @param received Gelesene Byte, 0 mit Ok heißt Ende des Datenstroms. @param interruptible Wie bei send(). @return Radio, sobald uploadAllowed() Radio meldet. Memory, wenn uploadAllowed() zu lange Memory meldet. */
    virtual Data receive(uint8_t *data, size_t capacity, size_t &received, bool interruptible) = 0;
    /** @brief Meldet, ob Uploaddaten angenommen werden können. STOR beginnt sonst nicht. Ein Radiostream schließt Uploads aus: Gleichzeitig mit Kartenschreiben ließ er die Empfangspuffer von esp_hosted so wachsen, dass im internen DMA-Speicher nach 18 bis 100 MB kein zusammenhängender Block für den nächsten Wachstumsschritt mehr frei war. Ohne Radio seit dem Start reichte der Block nach 150 MB noch. */
    virtual Upload uploadAllowed() = 0;
    /** @brief Schließt Datenverbindung und passiven Port. */
    virtual void closeData() = 0;
    /** @brief Liest ohne Warten die nächste Zeile des Steuerkanals. @param line Ziel mit FtpProtocol::MaxLine + 1 Byte. */
    virtual Control poll(char *line) = 0;
    /** @brief Kopiert das gültige Passwort. @return false, solange keines erzeugt ist. */
    virtual bool password(char (&out)[FtpProtocol::PasswordCapacity]) = 0;
    /** @brief Wartet ms Millisekunden, ohne andere Tasks zu blockieren. */
    virtual void wait(uint32_t ms) = 0;
    /** @brief Meldet das Ende einer Übertragung für das Log. @param command LIST, NLST, MLSD, RETR oder STOR. @param path Kanonischer Kartenpfad. @param reply Erste Antwortzeile ohne CRLF. @param bytes Übertragene Byte. */
    virtual void transferred(const char *command, const char *path, const char *reply, uint64_t bytes) = 0;
};

/** @brief Zustandsautomat einer FTP-Verbindung nach RFC 959, 2428 und 3659 ohne eigenen Netz- oder Kartenzugriff. Jede Kartenarbeit hält für ihre Dauer die Kartensperre. Zwischen zwei Befehlen kann die SD-Wiedergabe starten. Während einer Übertragung werden ABOR und QUIT sofort ausgewertet, andere Befehle danach. */
class FtpSession
{
public:
    /** @brief Laufende Übertragung. */
    enum class Transfer : uint8_t
    {
        None, ///< Keine
        List, ///< LIST, NLST oder MLSD
        Retr, ///< Download
        Stor  ///< Upload
    };

    /** @brief Bindet Umgebung und Puffer. @param environment Umgebung, bleibt gültig. @param job Kartenauftrag mit Datenpuffer und Eintragsliste, gehört danach der Sitzung. @param text Puffer für Listenzeilen. @param textCapacity Größe von text, mindestens 1024. */
    FtpSession(FtpEnvironment &environment, FtpProtocol::Job &job, char *text, size_t textCapacity);
    /** @brief Beginnt eine Verbindung: Begrüßung 220, Arbeitsordner „/“, nicht angemeldet. */
    void start();
    /** @brief Verarbeitet eine Befehlszeile und danach Zeilen, die während einer Übertragung zurückgestellt wurden. @return false, wenn die Verbindung zu schließen ist. Die letzte Antwort ist dann gesendet. */
    bool command(const char *line);
    /** @brief Beantwortet eine verworfene Zeile (zu lang oder mit Nullbyte) mit 500. */
    void rejectLine();
    /** @brief Prüft Anmelde- und Leerlauffrist. @return false nach der Antwort 421, die Verbindung ist dann zu schließen. */
    bool tick();
    /** @brief Beendet die Verbindung von Seiten des Dienstes: schließt den Datenkanal und sendet text. @param text Vollständige Antwort mit Code 421 und CRLF. */
    void close(const char *text);

    /** @brief Meldet eine erfolgreiche Anmeldung in dieser Verbindung. */
    bool loggedIn() const
    {
        return m_loggedIn;
    }

    /** @brief Liefert die laufende Übertragung. */
    Transfer transfer() const
    {
        return m_transfer;
    }

    /** @brief Liefert die Bytes der laufenden oder letzten Übertragung. */
    uint64_t bytes() const
    {
        return m_bytes;
    }

    /** @brief Liefert die fehlgeschlagenen Anmeldungen seit dem Start des Dienstes. */
    uint32_t failedLogins() const
    {
        return m_failedLogins;
    }

    /** @brief Liefert die abgeschlossenen Übertragungen seit dem Start des Dienstes. */
    uint32_t transfers() const
    {
        return m_transfers;
    }

private:
    using Status = FtpProtocol::Status;

    /** @brief Ausgang einer Übertragung. */
    enum class Outcome : uint8_t
    {
        Done,        ///< Vollständig
        Aborted,     ///< ABOR vom Client
        Quit,        ///< QUIT vom Client während der Übertragung
        Lost,        ///< Steuerkanal getrennt
        DataClosed,  ///< Datenverbindung abgebrochen
        DataTimeout, ///< Datenkanal ohne Fortschritt
        Shutdown,    ///< Dienst wird abgeschaltet
        Memory,      ///< Zu wenig interner DMA-Speicher für weitere Uploaddaten
        Radio,       ///< Ein Radiostream hat während des Uploads begonnen
        Card         ///< Kartenfehler, Antwort aus m_job.status
    };

    /** @brief Art einer Liste. */
    enum class Format : uint8_t
    {
        List, ///< LIST im Unix-Format
        Names, ///< NLST nur mit Namen
        Facts ///< MLSD nach RFC 3659
    };

    /** @brief Bearbeiter eines Befehls. @return false, wenn die Verbindung zu schließen ist. */
    using Handler = bool (FtpSession::*)(const char *argument);

    /** @brief Eintrag der Befehlstabelle. */
    struct Command
    {
        const char *verb;  ///< Befehl in Großbuchstaben
        bool login;        ///< Nur nach Anmeldung
        Handler handler;   ///< Bearbeiter
    };

    static const Command Commands[]; ///< Bekannte Befehle, alle übrigen ergeben 502

    FtpEnvironment &m_env;                     ///< Umgebung
    FtpProtocol::Job &m_job;                   ///< Kartenauftrag samt Puffern
    char *m_text;                              ///< Puffer für Listenzeilen
    size_t m_textCapacity;                     ///< Größe von m_text
    size_t m_textUsed = 0;                     ///< Gefüllte Bytes in m_text
    char m_reply[FtpProtocol::MaxPath + 160]{}; ///< Puffer für Antworten mit Pfad
    char m_cwd[FtpProtocol::MaxPath + 1] = "/"; ///< Arbeitsordner
    char m_renameFrom[FtpProtocol::MaxPath + 1]{}; ///< Quelle nach RNFR
    bool m_renamePending = false;              ///< RNFR war der vorige Befehl
    bool m_userGiven = false;                  ///< USER erhalten, PASS erwartet
    bool m_userValid = false;                  ///< Benutzername war FtpProtocol::User
    bool m_loggedIn = false;                   ///< Angemeldet
    bool m_passive = false;                    ///< Passiver Datenport ist offen
    unsigned m_failures = 0;                   ///< Fehlgeschlagene Anmeldungen dieser Verbindung
    uint64_t m_rest = 0;                       ///< Versatz aus REST für den nächsten RETR
    uint64_t m_started = 0, m_lastActivity = 0; ///< Beginn der Verbindung und letzter Befehl in ms
    WebValidation::LoginLock m_lock;           ///< Mindestabstand und wachsende Sperre nach Fehlversuchen, über alle Verbindungen
    Transfer m_transfer = Transfer::None;      ///< Laufende Übertragung
    uint64_t m_bytes = 0;                      ///< Bytes der laufenden oder letzten Übertragung
    uint32_t m_failedLogins = 0, m_transfers = 0; ///< Zähler seit dem Start des Dienstes
    char m_line[FtpProtocol::MaxLine + 1]{};   ///< Arbeitskopie einer zurückgestellten Zeile
    char m_deferred[FtpProtocol::MaxLine + 1]{}; ///< Während einer Übertragung eingegangene Zeile
    bool m_hasDeferred = false;                ///< m_deferred ist belegt
    bool m_deferredInvalid = false;            ///< Während einer Übertragung kam eine verworfene Zeile

    /** @brief Verarbeitet genau eine Zeile. */
    bool execute(const char *line);
    /** @brief Sendet eine feste Antwort. @return true für die Bearbeiter. */
    bool reply(const char *text);
    /** @brief Sendet eine formatierte Antwort über m_reply. @return true für die Bearbeiter. */
    bool replyf(const char *format, ...) __attribute__((format(printf, 2, 3)));
    /** @brief Löst einen Pfad gegen den Arbeitsordner auf. */
    bool resolve(const char *argument, char (&out)[FtpProtocol::MaxPath + 1]) const;
    /** @brief Fordert die Kartensperre an. Solange das Webportal sie hält, bis FtpProtocol::LeaseWaitMs wiederholt. Bei Misserfolg ist 450 gesendet. */
    bool acquire();
    /** @brief Führt m_job mit Kartensperre aus und gibt sie danach frei. @return false, wenn die Sperre fehlte, 450 ist dann gesendet. */
    bool run(FtpProtocol::Kind kind, const char *path, const char *target = nullptr);
    /** @brief Beantwortet einen fehlgeschlagenen Kartenauftrag. */
    bool fail(Status status);
    /** @brief Prüft während einer Übertragung den Steuerkanal: ABOR, QUIT und eine Trennung beenden sie, andere Zeilen werden zurückgestellt. @return Done, solange die Übertragung weiterläuft. */
    Outcome control();
    /** @brief Wandelt ein Ergebnis des Datenkanals um, Ok und Control ergeben Done. */
    static Outcome outcome(FtpEnvironment::Data data);
    /** @brief Sendet length Byte und prüft den Steuerkanal, wenn der Datenkanal stockt. @return Done oder der Abbruchgrund. */
    Outcome transmit(const uint8_t *data, size_t length);

    /** @brief Meldet, ob der Datenkanal für Eingang auf dem Steuerkanal unterbrochen werden darf: nicht, solange eine zurückgestellte Zeile wartet. */
    bool interruptible() const
    {
        return !m_hasDeferred && !m_deferredInvalid;
    }

    /** @brief Hängt Text an m_text und sendet gefüllte Blöcke. @return Done oder der Abbruchgrund. */
    Outcome emit(const char *text, size_t length);
    /** @brief Sendet den Rest von m_text. */
    Outcome flush();
    /** @brief Schließt eine Übertragung ab: offene Datei oder Liste und Datenkanal schließen, Sperre freigeben, Antwort nach Ausgang und Meldung an die Umgebung. @param close Close oder ListClose. @param card Ergebnis des gescheiterten Kartenauftrags bei Outcome::Card. @param command Befehl für die Meldung. @param path Kartenpfad für die Meldung. @return false, wenn die Verbindung zu schließen ist. */
    bool finish(Outcome result, FtpProtocol::Kind close, Status card, const char *command, const char *path);

    /** @brief USER: merkt den Namen und fordert das Passwort an. */
    bool user(const char *argument);
    /** @brief PASS: prüft Benutzer und Passwort, nach drei Fehlversuchen wird getrennt. */
    bool pass(const char *argument);
    /** @brief QUIT: verabschiedet und trennt. */
    bool quit(const char *argument);
    /** @brief FEAT: listet die Erweiterungen. */
    bool feat(const char *argument);
    /** @brief OPTS: UTF8 und MLST. */
    bool opts(const char *argument);
    /** @brief NOOP. */
    bool noop(const char *argument);
    /** @brief SYST. */
    bool syst(const char *argument);
    /** @brief HELP. */
    bool help(const char *argument);
    /** @brief PWD und XPWD. */
    bool pwd(const char *argument);
    /** @brief CWD und XCWD: nur in vorhandene Ordner. */
    bool cwd(const char *argument);
    /** @brief CDUP und XCUP. */
    bool cdup(const char *argument);
    /** @brief TYPE: A und I, beide ohne Umwandlung. */
    bool type(const char *argument);
    /** @brief MODE: nur S. */
    bool mode(const char *argument);
    /** @brief STRU: nur F. */
    bool stru(const char *argument);
    /** @brief PASV. */
    bool pasv(const char *argument);
    /** @brief EPSV. */
    bool epsv(const char *argument);
    /** @brief PORT und EPRT: abgelehnt, nur passiver Betrieb. */
    bool port(const char *argument);
    /** @brief LIST. */
    bool listUnix(const char *argument);
    /** @brief NLST. */
    bool listNames(const char *argument);
    /** @brief MLSD. */
    bool listFacts(const char *argument);
    /** @brief Gemeinsamer Ablauf von LIST, NLST und MLSD. */
    bool list(const char *argument, Format format);
    /** @brief MLST. */
    bool mlst(const char *argument);
    /** @brief SIZE. */
    bool size(const char *argument);
    /** @brief MDTM. */
    bool mdtm(const char *argument);
    /** @brief REST: Versatz für den nächsten RETR. */
    bool rest(const char *argument);
    /** @brief RETR. */
    bool retr(const char *argument);
    /** @brief STOR: über eine Stagingdatei, ein vorhandenes Ziel wird ersetzt. */
    bool stor(const char *argument);
    /** @brief DELE. */
    bool dele(const char *argument);
    /** @brief RMD und XRMD: nur Ordner ohne sichtbare Einträge. Enthaltene versteckte Einträge nach SystemEntries::hidden werden samt Inhalt mit gelöscht, nie sichtbare. */
    bool rmd(const char *argument);
    /** @brief MKD und XMKD. */
    bool mkd(const char *argument);
    /** @brief RNFR. */
    bool rnfr(const char *argument);
    /** @brief RNTO: direkt nach RNFR, ohne vorhandene Ziele zu überschreiben. */
    bool rnto(const char *argument);
    /** @brief ABOR außerhalb einer Übertragung. */
    bool abor(const char *argument);
    /** @brief STAT ohne Parameter. */
    bool stat(const char *argument);
    /** @brief ALLO: nicht nötig. */
    bool allo(const char *argument);
};
#endif
