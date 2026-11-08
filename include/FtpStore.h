#ifndef FTP_STORE_H
#define FTP_STORE_H

#include "FtpProtocol.h"

/** @brief Kartenoperationen des FTP-Dienstes und, über WebFileStore, des Datei-Managers im Webportal mit den POSIX-Dateifunktionen unter einem Wurzelpfad (im Gerät „/sdcard“). Läuft im Gerät nur im Dienstworker, der vorher Kartensperre, Medium und Audio prüft. Uploads schreiben in eine Stagingdatei „<Namensanfang><8 Hex>.tmp“ in der Kartenwurzel. Erst Commit legt den Zielnamen an oder ersetzt eine vorhandene Datei über eine Sicherung „<Namensanfang><8 Hex>.old“, die bei einem Fehler zurückbenannt wird. Reste beider Arten entfernt das Einbinden der Karte. Die Baumarbeit (Zählen und Löschen samt Inhalt) läuft in Schritten zu höchstens TreeStepEntries Einträgen ohne Rekursion: Der aktuelle Pfad dient als Stapel, offen ist immer nur ein Ordner. */
class FtpStore
{
public:
    using Job = FtpProtocol::Job;
    using Status = FtpProtocol::Status;
    using Entry = FtpProtocol::Entry;

    /** @brief Liest einen Ordner. Im Gerät über FATFS mit Größe, Zeit und Attributen in einem Durchgang, in Hosttests über opendir. */
    class Directory
    {
    public:
        virtual ~Directory() = default;
        /** @brief Öffnet den Ordner zu einem kanonischen Kartenpfad. @return Ok, NotFound oder Io. */
        virtual Status open(const char *path) = 0;
        /** @brief Liest den nächsten Eintrag ohne „.“ und „..“. @param hidden true bei FAT-Attribut versteckt oder System. @return Ok mit Eintrag, End am Ende oder Io. */
        virtual Status next(Entry &entry, bool &hidden) = 0;
        /** @brief Schließt den Ordner, mehrfach aufrufbar. */
        virtual void close() = 0;
    };

    static constexpr const char *FtpPrefix = ".rr-ftp-"; ///< Namensanfang der Staging- und Sicherungsdateien des FTP-Dienstes

    /** @brief Bindet die Wurzel und den Ordnerleser. @param root Wurzelpfad ohne abschließenden Schrägstrich, höchstens 31 Byte. Bleibt gültig. @param directory Ordnerleser, bleibt gültig. nullptr, wenn über diesen Speicher nicht aufgelistet wird, ListOpen meldet dann Io. @param random Zufallsquelle für die Namen der Staging- und Sicherungsdateien. @param prefix Namensanfang dieser Dateien aus genau acht Zeichen, etwa FtpPrefix. Bleibt gültig. */
    FtpStore(const char *root, Directory *directory, uint32_t (*random)(), const char *prefix);

    /** @brief Führt einen Auftrag aus und setzt job.status. Pfade werden erneut mit WebValidation::path geprüft. Ein fehlgeschlagener Schreibauftrag verwirft die Stagingdatei. */
    void execute(Job &job);
    /** @brief Schließt offene Datei und Ordner ohne weiteren Kartenzugriff, etwa vor dem Aushängen einer verlorenen Karte. Eine Stagingdatei bleibt bis zum nächsten Einbinden liegen. */
    void closeAll();

    /** @brief Meldet eine offene Datei oder Stagingdatei. */
    bool transferOpen() const
    {
        return m_read >= 0 || m_write >= 0;
    }

    /** @brief Meldet eine begonnene, noch nicht beendete Baumarbeit. */
    bool treeActive() const
    {
        return m_tree.active;
    }

    /** @brief Erkennt ausschließlich die Reste des FTP-Dienstes, FtpPrefix + 8 Hexziffern + „.tmp“ oder „.old“, für die Bereinigung beim Einbinden. @param name Dateiname ohne Pfad. */
    static bool temporary(const char *name);

private:
    static constexpr size_t FullCapacity = 32 + FtpProtocol::MaxPath + 1; ///< Wurzel und Kartenpfad samt Nullbyte

    const char *m_root;            ///< Wurzelpfad
    Directory *m_directory;        ///< Ordnerleser, nullptr ohne
    uint32_t (*m_random)();        ///< Zufallsquelle
    const char *m_prefix;          ///< Namensanfang der Staging- und Sicherungsdateien
    int m_read = -1, m_write = -1; ///< Offene Datei zum Lesen und Stagingdatei, -1 ohne
    char m_staging[FullCapacity]{}; ///< Vollständiger Pfad der Stagingdatei, leer ohne
    char m_target[FullCapacity]{};  ///< Vollständiger Zielpfad des Uploads
    bool m_listing = false;        ///< Ein Ordner ist zum Auflisten offen
    char m_listPath[FtpProtocol::MaxPath + 1]{}; ///< Kartenpfad des offenen Ordners
    uint32_t m_skipped = 0;        ///< Übersprungene Einträge des offenen Ordners

    /** @brief Zustand der Baumarbeit. */
    struct Tree
    {
        bool active = false;                       ///< Begonnen und nicht beendet
        bool remove = false;                       ///< Löschen statt Zählen
        bool open = false;                         ///< Der Ordner path ist über m_directory offen
        uint16_t depth = 0;                        ///< Ebene von path unter dem Startordner, 0 für ihn selbst
        char path[FtpProtocol::MaxPath + 1]{};     ///< Aktueller Ordner, seine Bestandteile ab dem Startordner bilden den Stapel
        uint32_t position[FtpProtocol::TreeDepth]{}; ///< Nur beim Zählen: je Ebene schon gelesene Einträge, damit ein wieder geöffneter Ordner dort weiterliest
        uint32_t files = 0, folders = 0;           ///< Gezählte bzw. gelöschte Dateien und Ordner
    } m_tree; ///< Baumarbeit, gehört wie die Liste dem Ordnerleser m_directory

    /** @brief Setzt Wurzel und Kartenpfad zusammen. @return false, wenn das Ergebnis nicht passt. */
    bool full(const char *path, char (&out)[FullCapacity]) const;
    /** @brief Fragt einen Eintrag ab, die Wurzel ist immer ein Ordner ohne Zeit. */
    Status stat(const char *path, Entry &entry) const;
    /** @brief Liest bis zu entryCapacity Einträge, überspringt Einträge nach SystemEntries::hidden und zählt ungültige und zu lange als übersprungen. */
    void listNext(Job &job);
    /** @brief Legt die Stagingdatei für path an, wenn der Ordner existiert und path kein Ordner ist. */
    void writeOpen(Job &job);
    /** @brief Hängt count Byte vollständig an, eine volle Karte ergibt NoSpace. */
    void write(Job &job);
    /** @brief Schließt die Stagingdatei und legt das Ziel an oder ersetzt es. */
    void commit(Job &job);
    /** @brief Benennt um, ohne ein vorhandenes Ziel zu überschreiben. Ein Ordner kann nicht in sich selbst verschoben werden. */
    void rename(Job &job);
    /** @brief Löscht einen leeren Ordner. @param withHidden Enthält der Ordner nur versteckte Einträge nach SystemEntries::hidden, beginnt das Löschen samt Inhalt mit More (RmdirHidden). Sonst Denied. */
    void rmdir(Job &job, bool withHidden);
    /** @brief Beginnt die Baumarbeit am Ordner job.path, nicht an der Wurzel. @param remove Löschen statt Zählen. */
    void treeBegin(Job &job, bool remove);
    /** @brief Zählt oder löscht bis zu TreeStepEntries Einträge. More, End nach dem Löschen bzw. Zählen des Startordners, sonst Fehler mit dem betroffenen Pfad in job.target, danach ist die Baumarbeit beendet. */
    void treeStep(Job &job);
    /** @brief Beendet die Baumarbeit mit status und dem betroffenen Pfad in job.target. */
    void treeFail(Job &job, Status status, const char *path);
    /** @brief Schließt den Ordner der Baumarbeit und beendet sie. */
    void closeTree();
    /** @brief Schließt offene Datei und Stagingdatei. @param discard true löscht die Stagingdatei. */
    void closeTransfer(bool discard);
    /** @brief Schließt einen offenen Ordner. */
    void closeList();
    /** @brief Erzeugt einen freien Namen „<Namensanfang><8 Hex><suffix>“ in der Wurzel. @return false, wenn der Name nicht passt. */
    bool temporaryName(const char *suffix, char (&out)[FullCapacity]) const;
};
#endif
