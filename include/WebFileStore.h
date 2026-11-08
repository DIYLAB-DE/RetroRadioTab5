#ifndef WEB_FILE_STORE_H
#define WEB_FILE_STORE_H

#include "FtpStore.h"

/** @brief Schreibende Kartenarbeit des Datei-Managers im Webportal mit denselben Rechten und Regeln wie der FTP-Dienst, ausgeführt über FtpStore: Hochladen in eine Stagingdatei „.rr-web-<8 Hex>.tmp“ in der Kartenwurzel, eine vorhandene Datei wird nur mit ausdrücklicher Erlaubnis und über die Sicherung „.rr-web-<8 Hex>.old“ ersetzt. Ordner anlegen, Dateien und leere Ordner löschen, Umbenennen ohne Überschreiben und ohne einen Ordner in sich selbst zu verschieben. Zusätzlich zählt die Baumarbeit den Inhalt eines Ordners und löscht ihn samt Inhalt, in Schritten, die der Dienstworker zwischen anderen Aufträgen ausführt. Läuft im Gerät nur im Dienstworker, der vorher Kartensperre, Medium und Audio prüft. Ergebnisse sind HTTP-Statuscodes mit einem Grund für die Meldung im Browser. */
class WebFileStore
{
public:
    /** @brief Grund einer Ablehnung für die Meldung im Browser. */
    enum class Reason : uint8_t
    {
        None,      ///< Kein eigener Grund. Der Browser meldet den Text zum Statuscode
        Exists,    ///< Ziel vorhanden
        Directory, ///< Ein Ordner trägt den Zielnamen
        NotEmpty,  ///< Ordner nicht leer
        Inside,    ///< Ein Ordner soll in sich selbst verschoben werden
        Playing,   ///< SD-Wiedergabe läuft oder wartet
        Ftp        ///< Der FTP-Dienst hält die Kartensperre
    };

    /** @brief Ergebnis eines Auftrags. */
    struct Result
    {
        int status = 500;             ///< HTTP-Statuscode
        Reason reason = Reason::None; ///< Grund einer Ablehnung
    };

    /** @brief Fortschritt der Baumarbeit. */
    struct TreeProgress
    {
        bool done = false;                          ///< Zählen bzw. Löschen abgeschlossen
        uint32_t files = 0, folders = 0;            ///< Bisher gezählte bzw. gelöschte Dateien und Ordner unter dem Startordner, ohne ihn selbst
        char failed[FtpProtocol::MaxPath + 1]{};    ///< Nach einem Fehler der betroffene Pfad, sonst leer
    };

    /** @brief Nimmt einen sichtbaren Eintrag der Ordnerliste entgegen. @param context Zeiger des Aufrufers aus list(). @param path Vollständiger Kartenpfad nach WebValidation::path. @param entry Name, Größe und Art. @return false, wenn kein Platz mehr ist. */
    using ListSink = bool (*)(void *context, const char *path, const FtpProtocol::Entry &entry);

    /** @brief Bindet Wurzel, Ordnerleser und Zufallsquelle. @param root Wurzelpfad ohne abschließenden Schrägstrich, höchstens 31 Byte. Bleibt gültig. @param directory Ordnerleser für list(), bleibt gültig. nullptr, dann antwortet list() mit 500. @param random Zufallsquelle für die Namen der Staging- und Sicherungsdateien. */
    WebFileStore(const char *root, FtpStore::Directory *directory, uint32_t (*random)());

    /** @brief Listet einen Ordner mit derselben Auswahl wie der FTP-Dienst: ohne Einträge nach SystemEntries::hidden (führender Punkt, Systemnamen wie „System Volume Information“ oder „$RECYCLE.BIN“, FAT-Attribut versteckt oder System). Einträge mit einem Pfad über 255 Byte oder ungültigem Namen werden übersprungen und gezählt. Ein laufender Upload bleibt bestehen. @param path Ordner nach WebValidation::path, auch die Wurzel. @param sink Erhält jeden sichtbaren Eintrag in der Reihenfolge des Ordners. @param context Für sink. @param skipped Übersprungene Einträge. @return 200, 413, sobald sink keinen Platz mehr hat, 404 ohne Ordner, 400 bei ungültigem Pfad, 500 bei Kartenfehler oder ohne Ordnerleser. */
    Result list(const char *path, ListSink sink, void *context, uint32_t &skipped);

    /** @brief Beginnt einen Upload in eine neue Stagingdatei. @param path Zielpfad nach WebValidation::path, nicht die Wurzel. @param size Angekündigte Größe in Byte, höchstens WebValidation::MaxUpload. @param overwrite Eine vorhandene Datei darf ersetzt werden. @return 200, 409 Exists ohne Erlaubnis, 409 Directory, 404 ohne übergeordneten Ordner, 400 bei ungültiger Eingabe oder laufendem Upload, 507 bei voller Karte, 500 bei Kartenfehler. */
    Result begin(const char *path, uint32_t size, bool overwrite);
    /** @brief Hängt einen Block an die Stagingdatei an. @param data Inhalt, count Byte. @return 200, 400 ohne Upload, bei leerem Block oder über die angekündigte Größe hinaus (der Upload bleibt bestehen), 507 oder 500 verwerfen den Upload. */
    Result chunk(uint8_t *data, uint32_t count);
    /** @brief Schließt den Upload ab und legt das Ziel an oder ersetzt es. @return 200, 400 ohne Upload oder bei fehlenden Bytes, 409 Exists, wenn ohne Erlaubnis inzwischen eine Datei den Namen trägt, 409 Directory, 507, 500. Jeder Fehler verwirft die Stagingdatei. Das alte Ziel bleibt dann unverändert. */
    Result commit();
    /** @brief Verwirft einen laufenden Upload samt Stagingdatei, ohne Upload wirkungslos. */
    void abort();
    /** @brief Schließt einen laufenden Upload ohne Kartenzugriff, etwa vor dem Aushängen einer verlorenen Karte. Die Stagingdatei entfernt das nächste Einbinden. */
    void closeAll();

    /** @brief Meldet einen laufenden Upload. */
    bool uploading() const
    {
        return m_uploading;
    }

    /** @brief Legt einen Ordner an. @return 200, 409 Exists, 404 ohne übergeordneten Ordner, 400 bei ungültigem Pfad, sonst 500. */
    Result mkdir(const char *path);
    /** @brief Löscht eine Datei oder einen leeren Ordner, nie rekursiv. @return 200, 409 NotEmpty bei einem Ordner mit Inhalt, 404, 400 bei ungültigem Pfad, sonst 500. */
    Result remove(const char *path);
    /** @brief Beginnt die Baumarbeit am Ordner path. Einträge nach SystemEntries::hidden zählen und fallen mit. Ein laufender Upload wird nicht berührt, weil die Kartensperre beides ausschließt. @param path Ordner nach WebValidation::path, nicht die Wurzel. @param remove Den Ordner samt Inhalt löschen statt nur zu zählen. @return 200 begonnen, 404 ohne Eintrag, 400 bei ungültigem Pfad, der Wurzel oder einer Datei, sonst 500. */
    Result treeBegin(const char *path, bool remove);
    /** @brief Zählt oder löscht bis zu FtpProtocol::TreeStepEntries Einträge der begonnenen Baumarbeit. @param progress Stand nach dem Schritt. @return 200 mit progress.done nach dem letzten Schritt, sonst 200 zum Fortsetzen. Ein Fehler (404, 400 bei einem Pfad über 255 Byte, 507, 500, ohne Baumarbeit 409) beendet die Baumarbeit, bereits Gelöschtes bleibt gelöscht. */
    Result treeStep(TreeProgress &progress);
    /** @brief Beendet eine Baumarbeit ohne weiteren Kartenzugriff, ohne Baumarbeit wirkungslos. */
    void treeClose();

    /** @brief Meldet eine begonnene, noch nicht beendete Baumarbeit. */
    bool treeActive() const
    {
        return m_store.treeActive();
    }

    /** @brief Benennt eine Datei oder einen Ordner um oder verschiebt sie, ohne ein vorhandenes Ziel zu überschreiben. Ein Ziel, das sich nur in Groß- und Kleinschreibung unterscheidet, gilt auf FAT als derselbe Eintrag und ist erlaubt. @return 200, 409 Exists, 400 Inside, wenn ein Ordner in sich selbst verschoben würde, 404 ohne Quelle oder Zielordner, 400 bei ungültigem Pfad oder der Wurzel, sonst 500. Ein gleiches Ziel ändert nichts und ergibt 200. */
    Result rename(const char *from, const char *to);

private:
    static constexpr uint16_t ListChunk = 16; ///< Einträge je Leseauftrag von list()

    FtpStore m_store;                        ///< Kartenoperationen mit dem Namensanfang WebValidation::UploadPrefix
    FtpProtocol::Job m_job;                  ///< Auftrag an m_store
    FtpProtocol::Entry m_entries[ListChunk]; ///< Einträge eines Leseauftrags von list()
    char m_path[FtpProtocol::MaxPath + 1]{}; ///< Zielpfad des laufenden Uploads
    uint32_t m_expected = 0, m_written = 0;  ///< Angekündigte und geschriebene Bytes des Uploads
    bool m_overwrite = false;                ///< Der laufende Upload darf eine vorhandene Datei ersetzen
    bool m_uploading = false;                ///< Ein Upload läuft

    /** @brief Führt einen Auftrag an m_store aus. @return Status des Auftrags. */
    FtpProtocol::Status run(FtpProtocol::Kind kind, const char *path, const char *target = "");
    /** @brief Ordnet einen Status des Kartenspeichers dem HTTP-Ergebnis zu. */
    static Result answer(FtpProtocol::Status status);
    /** @brief Setzt den Uploadzustand zurück. */
    void finish();
};
#endif
