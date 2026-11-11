#ifndef TAB5_CONFIG_H
#define TAB5_CONFIG_H
#include "Settings.h"
#include "RadioPreferences.h"
#include "Thema.h"
#include "FtpProtocol.h"
#include "Tab5Battery.h"

/** @brief Versionierter Tab5-Konfigurationsspeicher, Altprofile bleiben unangetastet. */
namespace Tab5Config
{
    /** @brief Lädt ausschließlich den CRC-geprüften eigenen Radiozustand oder RAM-Vorgaben. */
    RadioPreferences loadRadio();
    /** @brief Speichert geprüfte Radiowerte atomar in einem eigenen NVS-Schlüssel. */
    bool saveRadio(const RadioPreferences &value);
    /** @brief Liest die an einer Ladung gemessenen Kennwerte des Akkus aus rr_tab5/akku_v1. @param value Erhält Akkugröße und Innenwiderstand, bleibt ohne gültigen Eintrag unverändert. @return true bei einem CRC-geprüften Eintrag. */
    bool loadBattery(Tab5Battery::Calibration &value);
    /** @brief Schreibt die gemessenen Kennwerte des Akkus nach rr_tab5/akku_v1, einen einzelnen NVS-Eintrag. Blockiert für den Flash-Zugriff, darum nur aus dem Dienst-Task. @return true nach bestätigtem Schreiben. */
    bool saveBattery(const Tab5Battery::Calibration &value);

    /** @brief Zusätzliche eigene Einstellungen, die bestehende WLAN-Struktur bleibt unverändert. */
    struct Extras
    {
        Settings::Sound sound;
        char pin[5] = "0000";
    };

    /** @brief Prüft Klanggrenzen und eine vierstellige PIN. @param extras Zusätzliche Einstellungen. @return Gültigkeit. */
    bool valid(const Extras &extras);
    /** @brief Erzeugt ausschließlich RAM-Vorgaben, ohne vorhandene NVS-Daten anzufassen. */
    void defaults(Settings::Configuration &config, Extras &extras);
    /** @brief Prüft vollständig terminierte Felder und Wertebereiche vor der Serialisierung. */
    bool valid(const Settings::Configuration &config);
    /** @brief Lädt den eigenen Datensatz oder reine RAM-Defaults, ohne NVS zu ändern. Die Freigabe der seriellen Logs aus logs_v1 hat Vorrang vor Byte 602 von config_v2. Die PIN-Sperre steht nur in pinLockOn und ist ohne diesen Eintrag ein. Der Sekundenzeiger steht nur in secHand_v1 und schleicht ohne diesen Eintrag. @param report false unterdrückt die Logzeile zum Ergebnis. */
    void load(Settings::Configuration &config, uint64_t &revision, Extras *extras = nullptr, bool report = true);
    /** @brief Liest nur den gespeicherten Themenindex, ohne Logzeile. Ohne gültigen Datensatz 0 (Retro). Für den Start vor dem ersten begin() der Ansichten. */
    uint8_t loadTheme();
    /** @brief Liest nur die gespeicherte Freigabe der seriellen Logs, ohne Logzeile. Ohne gültigen Eintrag aus. Für den Start vor der ersten Logzeile. */
    bool loadSerialLogs();
    /** @brief Schreibt nur die Freigabe der seriellen Logs nach rr_tab5/logs_v1, einen einzelnen NVS-Eintrag wie im Original. config_v2 bleibt unberührt. @return false bei NVS-Fehler. */
    bool saveSerialLogs(bool enabled);
    /** @brief Schreibt nur die PIN-Sperre nach rr_tab5/pinLockOn, einen einzelnen NVS-Eintrag wie im Original. config_v2 und die PIN bleiben unberührt. @return false bei NVS-Fehler. */
    bool savePinLock(bool enabled);
    /** @brief Schreibt nur die Gangart des Sekundenzeigers nach rr_tab5/secHand_v1, einen einzelnen NVS-Eintrag. config_v2 bleibt unberührt. @param sweep true schleichend, false Sekundenschritte. @return false bei NVS-Fehler. */
    bool saveSecondHand(bool sweep);
    /** @brief Schreibt Konfiguration, PIN und Klang atomar nach rr_tab5/config_v2, die Logfreigabe zusätzlich nach logs_v1, die PIN-Sperre nach pinLockOn und den Sekundenzeiger nach secHand_v1. config_v1 bleibt erhalten. */
    bool save(const Settings::Configuration &config, uint64_t revision, const Extras &extras);

    /** @brief Lädt die Grundfarben von Modern aus rr_tab5/modern_v1. Fehlt der Eintrag oder ist er ungültig, gelten die Vorgaben. Gelöscht wird nichts. @param farben Ziel. Grund und Fläche tragen immer ihre Vorgabe. @param report false unterdrückt die Warnung bei einem ungültigen Eintrag. @return true, wenn ein gültiger Eintrag geladen wurde. */
    bool loadModern(ModernFarben &farben, bool report = true);
    /** @brief Prüft, dass jede bearbeitbare Grundfarbe höchstens 0xFFFFFF ist. Grund und Fläche zählen nicht. */
    bool validModern(const ModernFarben &farben);
    /** @brief Schreibt die bearbeitbaren Grundfarben nach rr_tab5/modern_v1. Grund und Fläche werden nicht gespeichert, config_v2 bleibt unberührt. @return false bei ungültigen Werten oder NVS-Fehler. */
    bool saveModern(const ModernFarben &farben);
    /** @brief Löscht nur den Schlüssel rr_tab5/modern_v1, danach gelten die Vorgaben. Ein fehlender Schlüssel gilt als Erfolg. */
    bool resetModern();

    /** @brief Lädt das FTP-Passwort aus rr_tab5/ftp_v1. Gelöscht wird nichts. @param out Ziel, ohne gültigen Eintrag leer. @return true, wenn ein gültiger Eintrag geladen wurde. */
    bool loadFtpPassword(char (&out)[FtpProtocol::PasswordCapacity]);
    /** @brief Schreibt ein FTP-Passwort nach rr_tab5/ftp_v1, config_v2 und die Geräte-PIN bleiben unberührt. @return false bei ungültigem Passwort (FtpProtocol::validPassword) oder NVS-Fehler. */
    bool saveFtpPassword(const char *password);
}
#endif
