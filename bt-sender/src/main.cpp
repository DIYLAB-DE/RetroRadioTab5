#include "BtAudio.h"
#include "BtFm.h"
#include <Arduino.h>
#include <BluetoothA2DPSource.h>
#include <driver/gpio.h>
#include <driver/i2c_master.h>
#include <driver/i2s_std.h>
#include <driver/pulse_cnt.h>
#include <esp_avrc_api.h>
#include <esp_bt_device.h>
#include <esp_gap_bt_api.h>
#include <esp_mac.h>
#include <esp_system.h>
#include <esp_task_wdt.h>
#include <esp_timer.h>
#include <freertos/queue.h>
#include <nvs.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstring>

#ifndef BT_GIT_STAND
#define BT_GIT_STAND "" ///< git_stand.py setzt „+“ und den Git-Commit des Builds, ohne das Skript bleibt der Stand ohne Commit
#endif

namespace
{
    constexpr uint32_t SerialBaud = 115200;              ///< UART0 an G1/G3, dieselbe Rate wie das Bootlog
    constexpr const char *LocalNamePrefix = "RetroRadio "; ///< Eigener Bluetooth-Name. Dahinter stehen die letzten drei Byte der eigenen Adresse, damit mehrere Sender unterscheidbar bleiben
    constexpr size_t LocalNameSize = 32;                 ///< Puffer für den Namen, die Bibliothek merkt sich nur den Zeiger
    constexpr size_t AddressTextSize = 18;               ///< „AA:BB:CC:DD:EE:FF“ mit Nullbyte
    constexpr int MaxListedBonds = 8;                    ///< So viele Kopplungen werden geloggt und von VERGISS gelöscht, der Pico koppelt nur auf Befehl
    constexpr esp_bd_addr_t NoAddress = {};              ///< Leere Adresse: nichts gespeichert

    constexpr int StartupAttempts = 3;                                                ///< Verbindungsversuche zum gespeicherten Gerät nach dem Start, danach still
    constexpr uint32_t StartupListenMs = 15000;                                       ///< Nach dem Start wartet der Pico so lange ansprechbar darauf, dass das gespeicherte Gerät von sich aus zurückkommt (viele Geräte rufen nach einem Abriss selbst an), bevor er es anwählt
    constexpr int CommandAttempts = 1;                                                ///< Verbindungsversuche je VERBINDE
    constexpr uint32_t RetryPauseMs = 5000;                                           ///< Pause zwischen zwei Versuchen
    constexpr uint32_t AttemptTimeoutMs = 20000;                                      ///< Ohne Ergebnis gilt ein Versuch danach als gescheitert. Bluedroid gibt den Seitenruf nach 5,12 s auf
    constexpr uint8_t InquiryUnits = 7;                                               ///< Suchdauer in Einheiten zu 1,28 s, also 8,96 s
    constexpr uint32_t InquiryUnitMs = 1280;                                          ///< Einheit der Suchdauer laut Bluetooth-Kern
    constexpr uint32_t MediaStartWaitMs = 1000;                                       ///< TRENNE und VERGISS warten höchstens so lange auf das Ende eines laufenden Medienstarts
    constexpr int LibraryMediaStarting = 1;                                           ///< APP_AV_MEDIA_STATE_STARTING aus BluetoothA2DPSource.cpp von ESP32-A2DP 1.8.11, die Aufzählung steht dort nicht im Header
    constexpr uint32_t ForgetWaitMs = 3000;                                           ///< VERGISS wartet nach dem Trennen höchstens so lange auf die Meldung der Trennung, dann löscht es trotzdem
    constexpr uint32_t SearchGraceMs = 1000;                                          ///< Kommt das Suchende nicht bis 1 s nach der Suchdauer, bricht der Pico ab. Höchstens 9,96 s
    constexpr uint16_t AudioServices = ESP_BT_COD_SRVC_RENDERING | ESP_BT_COD_SRVC_AUDIO; ///< Dienstklassen, die neben der Hauptklasse Audio/Video einen Treffer zulassen
    constexpr size_t MaxHits = 16;                                                    ///< Verschiedene Geräte je Suche, weitere werden übergangen
    constexpr size_t NameMaxBytes = 30;                                               ///< Längster Gerätename in Byte UTF-8, an einer Zeichengrenze gekürzt
    constexpr size_t EscapedNameSize = NameMaxBytes * 3 + 1;                          ///< Name im Protokoll, jedes Byte höchstens als %XX
    constexpr size_t EventSlots = 24;                                                 ///< Plätze der Queue von den Bluetooth-Tasks an loop()
    constexpr uint32_t HeartBeatMs = 10000;                                           ///< Takt des Herzschlags der Bibliothek, startet nach einer Verbindung die Medien
    constexpr const char *StoreNamespace = "connected_bda";                           ///< NVS-Bereich der Bibliothek, damit die Kopplung aus Schritt 2a gilt
    constexpr const char *StoreAddressKey = "src_bda";                                ///< Adresse als 6-Byte-Blob, Format der Bibliothek
    constexpr const char *StoreNameKey = "src_name";                                  ///< Name als Zeichenkette, nur von dieser Firmware
    constexpr const char *StoreVolumeKey = "src_volume";                              ///< Gerätelautstärke 0..127 als Byte, nur von dieser Firmware

    constexpr uint32_t SampleRate = BtAudio::OutputRate;                                             ///< Die A2DP-Quelle von Bluedroid verlangt immer 44,1 kHz, 16 Bit Stereo
    constexpr double ToneFrequency = 440.0;                                                          ///< Hz
    constexpr float ToneAmplitude = 0.05f;                                                           ///< Anteil der Vollaussteuerung, höchstens 5 %
    constexpr uint32_t ToneFrames = SampleRate / 2;                                                  ///< 0,5 s Ton
    constexpr uint32_t PauseFrames = SampleRate / 2;                                                 ///< 0,5 s Pause
    constexpr uint32_t CycleFrames = ToneFrames + PauseFrames;                                       ///< Ein Takt aus Ton und Pause
    constexpr uint32_t RampFrames = SampleRate / 100;                                                ///< 10 ms Ein- und Ausblenden je Ton gegen Knacken
    constexpr uint32_t TestToneSeconds = 3;                                                          ///< Dauer des Testtons auf TESTTON: drei Töne, danach wieder Stille
    constexpr uint32_t TestToneFrames = SampleRate * TestToneSeconds;                                ///< Ende des Testtons in Frames
    constexpr uint32_t SineTableBits = 10;                                                           ///< 1024 Stützwerte ohne Interpolation, genug für einen Testton
    constexpr uint32_t SineTableSize = 1u << SineTableBits;                                          ///< Stützwerte einer vollen Periode
    constexpr uint32_t PhaseStep = static_cast<uint32_t>(ToneFrequency * 4294967296.0 / SampleRate); ///< Phasenzuwachs je Frame, volle Periode = 2^32
    constexpr uint8_t ToneNone = 0;                                                                  ///< s_toneCommand: nichts zu tun
    constexpr uint8_t ToneStart = 1;                                                                 ///< s_toneCommand: Testton von vorn
    constexpr uint8_t ToneStop = 2;                                                                  ///< s_toneCommand: Testton abbrechen

    constexpr uint8_t AvrcpMaxVolume = 127;       ///< Größte absolute AVRCP-Lautstärke
    constexpr uint8_t DefaultDeviceVolume = 38; ///< Gerätelautstärke ohne gespeicherten Wert, rund 30 % wie Drehknopf 30. Leise, weil das Tab5 den Ton mit vollem Softwarepegel schickt
    constexpr uint8_t AvrcpLabel = 0;             ///< Transaktionsnummer wie bei set_volume() der Bibliothek
    constexpr uint32_t AvrcpDelayMs = 3000;       ///< Wartezeit nach der A2DP-Verbindung, bis AVRCP aufgebaut ist
    constexpr uint32_t VolumeGapMs = 100;         ///< Kleinster Abstand zweier AVRCP-Lautstärkebefehle, beim Drehen zählt nur der letzte Wert
    constexpr uint32_t VolumeAnswerMs = 500;      ///< So lange wartet ein Lautstärkebefehl auf die Antwort des Geräts, bevor der nächste folgt. Es ist immer höchstens einer unterwegs
    constexpr uint32_t VolumeEchoMs = 1000;       ///< Meldet das Gerät binnen dieser Zeit den zuletzt gesetzten Wert, ist das kein Tastendruck
    constexpr uint32_t VolumeSaveMs = 5000;       ///< Die Lautstärke wird erst gespeichert, wenn sie so lange unverändert bleibt, gegen Flashverschleiß beim Drehen
    constexpr uint32_t LoopPauseMs = 5;     ///< Takt von loop(): UART-Befehle des Tab5 und Logausgaben, das Tab5 wartet bis zu 300 ms auf eine Antwort
    constexpr uint32_t OperationLogMs = 60000; ///< Abstand der BETRIEB-Logzeile mit Heap und Stackreserve, auch in Ruhe
    constexpr int FmSda = 32;                  ///< I2C-Daten zum UKW-Modulator (QN8007), Lötpunkt unten am Pico. Die Pull-ups sitzen auf der Baugruppe
    constexpr int FmScl = 33;                  ///< I2C-Takt zum UKW-Modulator, Lötpunkt daneben
    constexpr uint32_t FmI2cHz = 400000;       ///< Höchste Busrate des Chips laut Datenblatt
    constexpr int FmI2cTimeoutMs = 20;         ///< Wartezeit je I2C-Vorgang, ein hängender Bus hält loop() höchstens so lange auf

    constexpr char FirmwareStand[] = "5.3" BT_GIT_STAND;                   ///< Antwort auf HALLO: Nummer des Stands, dahinter „+“ und der Git-Commit des Builds aus git_stand.py, „m“ bei lokalen Änderungen im Pico-Projekt
    static_assert(sizeof(FirmwareStand) >= 2 && sizeof(FirmwareStand) <= 16, "Der Stand muss 1 bis 15 Zeichen haben, wie BtProtocol::parseHello es verlangt");
    constexpr uint32_t LinkBaud = 115200;                                  ///< UART1 zum Tab5, 8N1 ohne Flusssteuerung. Protokoll in bt-sender/README.md, Gegenstück include/BtProtocol.h im Hauptprojekt
    constexpr int LinkRx = 25;                                             ///< Empfang vom Tab5 G6 (M5-Bus 16)
    constexpr int LinkTx = 22;                                             ///< Senden zum Tab5 G7 (M5-Bus 15), der Ruhepegel High zeigt dem Tab5 einen versorgten Pico
    constexpr size_t LinkMaxLine = 127;                                    ///< Längste Zeile ohne Zeilenende in beide Richtungen, längere oder mit Steuerzeichen werden verworfen
    constexpr size_t CountInputs = 3;                                      ///< Flankenzähler an den späteren I2S-Eingängen
    constexpr int CountPins[CountInputs] = {18, 19, 21};                   ///< BCLK vom Tab5 G16, WS von G17, Daten von G18
    constexpr const char *CountNames[CountInputs] = {"G18", "G19", "G21"}; ///< Namen in der ERGEBNIS-Zeile
    constexpr int CountLimit = 32767;                                      ///< Obergrenze des 16-Bit-Zählers, das Tab5 sendet je Leitung nur 100 Flanken
    constexpr uint32_t CountGlitchNs = 1000;                               ///< Kürzere Pulse gelten als Störung, das Tab5 schaltet im Abstand von mindestens 1 ms

    constexpr int I2sBclk = CountPins[0];               ///< Bittakt vom Tab5 G16, der I2S-Empfang hört an denselben Eingängen wie die Flankenzähler
    constexpr int I2sWs = CountPins[1];                 ///< Wortauswahl vom Tab5 G17
    constexpr int I2sData = CountPins[2];               ///< Daten vom Tab5 G18
    constexpr uint32_t I2sDmaDescriptors = 4;           ///< DMA-Puffer des Empfangs
    constexpr uint32_t I2sDmaFrames = 256;              ///< Frames je DMA-Puffer: 2 KiB, bei 96 kHz rund 2,7 ms. Zusammen deckt das die Neuberechnung der Tabelle beim Ratenwechsel nur bis 48 kHz ab, darüber gehen dann einige Millisekunden verloren (die Ausgabe ist dabei ohnehin stumm)
    constexpr size_t I2sBlockFrames = 256;              ///< Frames je Lesevorgang des I2S-Tasks
    constexpr uint32_t I2sReadTimeoutMs = 50;           ///< Wartezeit eines Lesevorgangs, ohne Takt vom Tab5 kommt nichts
    constexpr uint32_t AudioTaskStack = 4096;           ///< Stack des I2S-Tasks in Byte
    constexpr UBaseType_t AudioTaskPriority = configMAX_PRIORITIES - 9; ///< Über dem Task der Bibliothek (configMAX_PRIORITIES - 10) und loop() (1) auf demselben Kern, damit weder AVRCP-Verkehr noch Log den Empfang verdrängen. Der DMA-Puffer reicht bei 48 kHz nur rund 21 ms
    constexpr BaseType_t AudioTaskCore = 1;             ///< Bluedroid und der Controller laufen auf Kern 0
    constexpr uint32_t AudioStatsMs = 10000;            ///< Abstand der AUDIO-Logzeile, solange Ton ankommt oder verbunden ist
    constexpr uint32_t DefaultInputRate = 44100;        ///< Quellrate bis zur ersten RATE-Zeile des Tab5

    constexpr int OpticalPin = 26;                  ///< S/PDIF-Ausgang zum TOSLINK-Sender, der einzige freie Ausgangspin in der Steckerleiste
    constexpr uint32_t OpticalDmaDescriptors = 4;   ///< DMA-Puffer der S/PDIF-Ausgabe
    constexpr uint32_t OpticalDmaFrames = 256;      ///< I2S-Rahmen zu 8 Byte je DMA-Puffer, also 128 Stereorahmen oder rund 2,7 ms bei 48 kHz. Zusammen überbrücken die Puffer die Ausgabe eines Blocks
    constexpr size_t OpticalBlockFrames = 128;      ///< Stereorahmen je Schreibvorgang des optischen Tasks
    constexpr uint32_t OpticalWriteTimeoutMs = 100; ///< Wartezeit eines Schreibvorgangs, die DMA nimmt alle rund 3 ms einen Puffer ab
    constexpr uint32_t OpticalTaskStack = 3072;     ///< Stack des optischen Tasks in Byte

    constexpr int64_t RateWindowUs = 1000000;                                                                              ///< Messfenster der tatsächlichen Quellrate aus den empfangenen Frames
    constexpr uint32_t StandardRates[] = {8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000, 64000, 88200, 96000}; ///< Raten, die audio-lite ausgibt. Die gemessene Rate muss einer davon bis auf RateTolerance entsprechen
    constexpr float RateTolerance = 0.01f;                                                                                 ///< Erlaubte Abweichung der Messung von einer Standardrate, der Taktunterschied der Geräte liegt weit darunter
    constexpr int RateWindowsToAccept = 2;                                                                                 ///< So viele Fenster hintereinander muss eine abweichende Rate messbar sein, bevor der Pico sie übernimmt

    /** @brief Art eines aufgeschobenen Trennens. */
    enum class StopKind : uint8_t
    {
        None,       ///< Nichts aufgeschoben
        Disconnect, ///< TRENNE
        Forget      ///< VERGISS
    };

    /** @brief Zustand der Steuerung, wie STATUS ihn meldet. */
    enum class LinkState : uint8_t
    {
        Idle,       ///< STILL: weder Suche noch Verbindung, der Pico funkt nicht
        Searching,  ///< SUCHT
        Connecting, ///< VERBINDET, auch in der Pause zwischen zwei Versuchen
        Connected   ///< VERBUNDEN, sendet Stille oder den Testton
    };

    constexpr const char *StateNames[] = {"STILL", "SUCHT", "VERBINDET", "VERBUNDEN"}; ///< Namen im Protokoll in der Reihenfolge von LinkState

    /** @brief Art einer Meldung aus den Bluetooth-Tasks an loop(). */
    enum class EventKind : uint8_t
    {
        Found,      ///< Suchtreffer mit Adresse, RSSI und Name
        SearchEnd,  ///< Bluedroid meldet das Ende der Suche
        Connection, ///< A2DP-Verbindungszustand in state, Adresse der Gegenstelle in address
        Name,       ///< Antwort auf esp_bt_gap_read_remote_name, Name leer bei Fehler
        Volume      ///< Das Gerät meldet eine neue Lautstärke in state
    };

    /** @brief Meldung aus dem BTC-Task von Bluedroid oder dem Task der Bibliothek an loop(), feste Größe für die Queue. */
    struct LinkEvent
    {
        EventKind kind;              ///< Art der Meldung
        uint8_t state;               ///< Bei Connection: esp_a2d_connection_state_t, bei Volume: Lautstärke 0..127
        bool rssiKnown;              ///< Bei Found: rssi gültig
        int8_t rssi;                 ///< Bei Found: Empfangsstärke in dBm
        esp_bd_addr_t address;       ///< Bei Found, Name und Connection
        char name[NameMaxBytes + 1]; ///< Bei Found und Name: UTF-8, leer ohne Namen
    };

    /** @brief Treffer der laufenden oder letzten Suche, nur in loop(). */
    struct Hit
    {
        esp_bd_addr_t address;       ///< Adresse des Geräts
        char name[NameMaxBytes + 1]; ///< Name, leer, solange keiner gemeldet ist
    };

    /**
     * @brief A2DP-Quelle, die nur auf Befehl sucht und verbindet.
     *
     * Die Basisklasse sucht nach dem Start, nach jedem Suchende ohne Treffer und nach erschöpften Wiederverbindungen ohne Ende weiter. Diese Klasse ersetzt die drei Stellen über virtuelle Methoden: Der Start richtet nur ein, das Suchende geht als Meldung an loop(), und ohne Verbindung geschieht nichts von selbst. Suchtreffer, Suchende, Namensantworten und Verbindungszustände (mit Adresse der Gegenstelle) laufen über postEvent().
     */
    class QuietSource : public BluetoothA2DPSource
    {
    public:
        /** @brief Verbindet mit address. Die Bibliothek trennt über last_connection, deshalb steht die Adresse auch dort. Nur aus loop(). @return false, wenn Bluedroid den Auftrag ablehnt. */
        bool connectPeer(const uint8_t *address);

        /** @brief Übernimmt die Adresse einer eingehenden Verbindung als Gegenstelle, damit disconnect() sie trifft. Verbindet nichts. Nur aus loop(). */
        void adoptPeer(const uint8_t *address);

        /** @brief Prüft, ob der Herzschlag der Bibliothek gerade den Medienstart beim Gerät angefordert hat und auf dessen Quittung wartet. Liest den Zustand des Bibliothekstasks ohne Sperre. Ein int wird dabei ganz gelesen. */
        bool mediaStarting() const
        {
            return s_media_state == LibraryMediaStarting;
        }

    protected:
        /** @brief Richtet GAP, AVRCP-Steuerung und A2DP-Quelle ein, schaltet unsichtbar und nicht verbindbar und startet den Herzschlag. Ohne Wartezeit, Suche oder Verbindung. Im Task der Bibliothek. */
        void av_hdl_stack_evt(uint16_t event, void *param) override;
        /** @brief Gibt Suchtreffer, Suchende und Namensantworten an loop() weiter, alle übrigen GAP-Ereignisse an die Basisklasse. Im BTC-Task. */
        void app_gap_callback(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param) override;
        /** @brief Gibt jede vom Gerät gemeldete Lautstärke an loop() und meldet die Benachrichtigung neu an. Die Basisklasse schickt den Wert zusätzlich als Befehl an das Gerät zurück, das entfällt hier. Im Task der Bibliothek. */
        void bt_av_notify_evt_handler(uint8_t event, esp_avrc_rn_param_t *param) override;
        /** @brief Die Bibliothek ruft das nur mit der Antwort des Geräts auf einen Lautstärkebefehl auf, vermerkt sie für sendPendingVolume(). Die Basisklasse schickt dabei jedes Mal einen neuen Befehl, mit mehreren Werten unterwegs kreisen sie dann endlos und verdrängen den I2S-Empfang. Im Task der Bibliothek. */
        void set_volume(uint8_t volume) override;
        /** @brief Ohne Verbindung geschieht nichts von selbst. Eine eingehende oder verspätet zustande gekommene Verbindung übernimmt sie wie im Zustand „verbindet“, damit der Herzschlag die Medien startet. Ob sie bleibt, entscheidet loop() anhand der Adresse aus der Meldung Connection. Im Task der Bibliothek. */
        void bt_app_av_state_unconnected_hdlr(uint16_t event, void *param) override;
        /** @brief Meldet jeden A2DP-Verbindungszustand mit der Adresse der Gegenstelle an loop(), bevor die Bibliothek ihn verarbeitet. Die Meldung liegt so vor allen Folgen in der Queue. Im BTC-Task von Bluedroid. */
        void app_a2d_callback(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *param) override;
    };

    /**
     * @brief I2C-Zugriff auf den UKW-Modulator über den I2C-Master-Treiber von ESP-IDF, nicht über Wire: Wire loggt jede unbeantwortete Adresse als Fehler, der Treiber antwortet still.
     *
     * select() prüft die Adresse erst mit i2c_master_probe (ein Adressbyte ohne Log) und gibt die Zugriffe nur frei, wenn jemand antwortet. Ohne Modulator bleibt es so bei einem Adressbyte je Kandidat alle 10 s. Jede Übertragung ist wenige Byte lang und kehrt spätestens nach FmI2cTimeoutMs zurück. Nur aus loop().
     */
    class FmBus : public BtFm::Bus
    {
    public:
        /** @brief Legt Bus und je Adresskandidat ein Gerät an. Interne Pull-ups zusätzlich zu denen der Baugruppe, damit offene Leitungen ohne Baugruppe in Ruhe liegen. @return false, wenn der Treiber scheitert. UKW bleibt dann aus. */
        bool begin()
        {
            i2c_master_bus_config_t bus = {};
            bus.i2c_port = I2C_NUM_0;
            bus.sda_io_num = static_cast<gpio_num_t>(FmSda);
            bus.scl_io_num = static_cast<gpio_num_t>(FmScl);
            bus.clk_source = I2C_CLK_SRC_DEFAULT;
            bus.glitch_ignore_cnt = 7;
            bus.flags.enable_internal_pullup = true;
            if (i2c_new_master_bus(&bus, &m_bus) != ESP_OK)
                return false;
            for (size_t i = 0; i < BtFm::AddressCount; ++i)
            {
                i2c_device_config_t device = {};
                device.dev_addr_length = I2C_ADDR_BIT_LEN_7;
                device.device_address = BtFm::Addresses[i];
                device.scl_speed_hz = FmI2cHz;
                if (i2c_master_bus_add_device(m_bus, &device, &m_devices[i]) != ESP_OK)
                    return false;
            }
            return true;
        }

        void select(uint8_t address) override
        {
            m_device = nullptr;
            if (!m_bus)
                return;
            for (size_t i = 0; i < BtFm::AddressCount; ++i)
                if (BtFm::Addresses[i] == address && i2c_master_probe(m_bus, address, FmI2cTimeoutMs) == ESP_OK)
                    m_device = m_devices[i];
        }

        bool write(uint8_t reg, const uint8_t *data, size_t count) override
        {
            uint8_t frame[9];
            if (!m_device || count + 1 > sizeof(frame))
                return false;
            frame[0] = reg;
            memcpy(frame + 1, data, count);
            return i2c_master_transmit(m_device, frame, count + 1, FmI2cTimeoutMs) == ESP_OK;
        }

        bool read(uint8_t reg, uint8_t &value) override
        {
            return m_device && i2c_master_transmit_receive(m_device, &reg, 1, &value, 1, FmI2cTimeoutMs) == ESP_OK;
        }

    private:
        i2c_master_bus_handle_t m_bus = nullptr;                       ///< Bus an FmSda/FmScl, nullptr ohne Treiber
        i2c_master_dev_handle_t m_devices[BtFm::AddressCount] = {};   ///< Ein Gerät je Adresskandidat
        i2c_master_dev_handle_t m_device = nullptr;                    ///< Gewähltes Gerät, nullptr ohne Antwort
    };

    /** @brief Logzeile der UKW-Zustandsmaschine. */
    void logFm(const char *line)
    {
        log_i("%s", line);
    }

    QuietSource s_source;              ///< Einzige A2DP-Quelle, die Bibliothek leitet ihre Callbacks über einen globalen Zeiger auf das zuletzt erzeugte Objekt
    FmBus s_fmBus;                     ///< I2C zum UKW-Modulator
    BtFm::Modulator s_fm(s_fmBus, logFm); ///< UKW-Modulator, nur in loop()
    char s_fmText[BtFm::TextBytes + 1];   ///< Dekodierter RDS-Text der letzten RDS-Zeile, nur in loop()
    char s_fmState[8] = "FEHLT";          ///< Zuletzt als UKW-Zeile gemeldeter Zustand, ohne Modulator ändert er sich nie
    A2DPNoVolumeControl s_fixedVolume; ///< Lässt die Abtastwerte unverändert, die Lautstärke regelt allein das Gerät über AVRCP
    int16_t s_sine[SineTableSize];     ///< Sinus mit der Testamplitude, beim Start berechnet
    char s_localName[LocalNameSize] = {}; ///< Eigener Name aus LocalNamePrefix und Adresse, in setup() gebildet
    TaskHandle_t s_audioTask = nullptr;   ///< I2S-Task, für seine Stackreserve im Log
    uint32_t s_operationAtMs = 0;         ///< millis() der letzten BETRIEB-Zeile, nur in loop()

    QueueHandle_t s_events = nullptr;       ///< Meldungen an loop(), in setup() angelegt
    std::atomic<bool> s_stackReady{false};  ///< Bluedroid und A2DP-Quelle eingerichtet
    std::atomic<uint32_t> s_eventsLost{0};  ///< Wegen voller Queue verworfene Meldungen
    std::atomic<uint8_t> s_toneCommand{0};  ///< ToneStart oder ToneStop für den Datencallback, der es auf ToneNone zurücksetzt
    std::atomic<uint32_t> s_tonePosition{TestToneFrames}; ///< Frames seit Beginn des Testtons, TestToneFrames ohne Ton. Schreibt nur der Datencallback
    uint32_t s_phase = 0;                                 ///< Phase des Sinus, nur im Datencallback

    i2s_chan_handle_t s_i2s = nullptr;                          ///< I2S-Empfang als Slave, nullptr, wenn das Einrichten scheiterte
    int32_t s_i2sBlock[2 * I2sBlockFrames];                     ///< Lesepuffer des I2S-Tasks
    BtAudio::FrameRing<BtAudio::RingFrames> s_ring;             ///< Umgerechneter Ton vom I2S-Task an den Datencallback
    BtAudio::RateConverter<> s_converter;                       ///< Nur im I2S-Task
    BtAudio::DriftControl s_drift;                              ///< Nur im I2S-Task
    BtAudio::OutputStage s_output;                              ///< Nur im Datencallback, playing() aus dem I2S-Task
    std::atomic<bool> s_muted{true};                            ///< STUMM des Tab5, nach jeder Verbindung und Trennung wieder stumm
    std::atomic<uint32_t> s_rateRequest{DefaultInputRate};      ///< Quellrate laut RATE, geprüft in loop(), übernommen im I2S-Task
    std::atomic<uint32_t> s_generation{0};                      ///< Zählt die Quellraten des I2S-Tasks, die Ausgabe verwirft bei jeder Änderung den Ring
    std::atomic<bool> s_overflow{false};                        ///< Der I2S-Task fand den Ring voll
    std::atomic<uint32_t> s_inFrames{0};                        ///< Empfangene Frames seit dem Start, für die gemessene Rate
    std::atomic<uint32_t> s_convertUs{0};                       ///< Rechenzeit des I2S-Tasks seit dem Start in µs
    std::atomic<uint32_t> s_renderUs{0};                        ///< Rechenzeit des Datencallbacks seit dem Start in µs
    std::atomic<uint32_t> s_requestMax{0};                      ///< Größte Abfrage des Datencallbacks seit der letzten AUDIO-Zeile in Frames
    std::atomic<uint32_t> s_fillLow{UINT32_MAX};                ///< Kleinster Füllstand seit der letzten AUDIO-Zeile
    std::atomic<uint32_t> s_fillHigh{0};                        ///< Größter Füllstand seit der letzten AUDIO-Zeile
    std::atomic<int32_t> s_peakLeft{0};                         ///< Größter Eingangswert links seit der letzten AUDIO-Zeile, 16-Bit-Skala
    std::atomic<int32_t> s_peakRight{0};                        ///< Ebenso rechts
    std::atomic<int32_t> s_correctionPpm{0};                    ///< Zuletzt angewandte Korrektur der Schrittweite
    std::atomic<bool> s_volumeAnswered{false};                  ///< Das Gerät hat den letzten Lautstärkebefehl beantwortet
    std::atomic<bool> s_btActive{false};                        ///< Gerät verbunden: der I2S-Task rechnet auch für den Bluetooth-Ring, schreibt loop()

    i2s_chan_handle_t s_optical = nullptr;                                                 ///< I2S1 als Master für den S/PDIF-Strom, nullptr, wenn das Einrichten scheiterte
    uint32_t s_opticalWords[BtAudio::SpdifEncoder::WordsPerFrame * OpticalBlockFrames];   ///< Schreibpuffer des optischen Tasks
    BtAudio::FrameRing<BtAudio::OpticalRingFrames> s_opticalRing;                          ///< Umgerechneter Ton vom I2S-Task an den optischen Task
    BtAudio::RateConverter<BtAudio::OpticalTaps> s_opticalConverter;                       ///< Quellrate auf die optische Rate, nur im I2S-Task
    BtAudio::DriftControl s_opticalDrift{BtAudio::OpticalTargetFrames};                    ///< Nur im I2S-Task
    BtAudio::OutputStage s_opticalOutput{BtAudio::OpticalTargetFrames};                    ///< Nur im optischen Task, playing() aus dem I2S-Task
    BtAudio::SpdifEncoder s_spdif;                                                         ///< Nur im optischen Task
    std::atomic<uint32_t> s_opticalRate{0};                                                ///< Rate am optischen Ausgang laut I2S-Task, der optische Task stellt den Takt darauf ein
    std::atomic<uint32_t> s_opticalGeneration{0};                                          ///< Zählt die optischen Raten, die Ausgabe verwirft bei jeder Änderung den Ring
    std::atomic<bool> s_opticalOverflow{false};                                            ///< Der I2S-Task fand den optischen Ring voll
    std::atomic<uint32_t> s_opticalConvertUs{0};                                           ///< Rechenzeit der optischen Umrechnung im I2S-Task seit dem Start in µs
    std::atomic<uint32_t> s_opticalRenderUs{0};                                            ///< Rechenzeit von Ausgabe und Kodierung im optischen Task seit dem Start in µs
    std::atomic<uint32_t> s_opticalWriteErrors{0};                                         ///< Schreibvorgänge, die die DMA nicht binnen OpticalWriteTimeoutMs abnahm
    std::atomic<uint32_t> s_opticalFillLow{UINT32_MAX};                                    ///< Kleinster Füllstand des optischen Rings seit der letzten OPTISCH-Zeile
    std::atomic<uint32_t> s_opticalFillHigh{0};                                            ///< Größter Füllstand seit der letzten OPTISCH-Zeile
    std::atomic<int32_t> s_opticalCorrectionPpm{0};                                        ///< Zuletzt angewandte Korrektur des optischen Wegs
    TaskHandle_t s_opticalTask = nullptr;                                                  ///< Optischer Task, für seine Stackreserve im Log

    LinkState s_state = LinkState::Idle;        ///< Nur in loop()
    esp_bd_addr_t s_peer = {};                  ///< Gegenstelle bei VERBINDET und VERBUNDEN, bleibt nach der Trennung stehen (verspätete Verbindung). Nur in loop()
    char s_peerName[NameMaxBytes + 1] = {};     ///< Name der Gegenstelle, leer, solange unbekannt. Nur in loop()
    int s_attemptsLeft = 0;                     ///< Noch nicht begonnene Versuche, nur in loop()
    bool s_attemptActive = false;               ///< Ein Versuch läuft und wartet auf sein Ergebnis, nur in loop()
    uint32_t s_attemptAtMs = 0;                 ///< Beginn des laufenden oder des nächsten Versuchs, nur in loop()
    bool s_connectAfterSearch = false;          ///< VERBINDE während der Suche: nach deren Ende verbinden, nur in loop()
    esp_bd_addr_t s_afterSearchAddress = {};    ///< Adresse dazu, nur in loop()
    uint32_t s_searchEndMs = 0;                 ///< Späteste Endzeit der laufenden Suche, nur in loop()
    Hit s_hits[MaxHits] = {};                   ///< Treffer der laufenden oder letzten Suche, nur in loop()
    size_t s_hitCount = 0;                      ///< Belegte Plätze in s_hits, nur in loop()
    bool s_storedValid = false;                 ///< Ein Gerät ist gespeichert, nur in loop() und setup()
    esp_bd_addr_t s_storedAddress = {};         ///< Seine Adresse, nur in loop() und setup()
    char s_storedName[NameMaxBytes + 1] = {};   ///< Sein Name, leer, solange unbekannt. Nur in loop() und setup()
    bool s_started = false;                     ///< Startablauf nach s_stackReady erledigt, nur in loop()
    bool s_startupPending = false;              ///< Nach dem Start steht die Anwahl des gespeicherten Geräts noch aus, nur in loop()
    uint32_t s_startupDueMs = 0;                ///< millis(), ab dem der Pico das gespeicherte Gerät selbst anwählt. Nur in loop()
    bool s_listenAllowed = true;                ///< Der Pico darf ansprechbar sein, TRENNE nimmt das bis zum nächsten VERBINDE oder Neustart zurück, nur in loop()
    bool s_connectable = false;                 ///< Zuletzt eingestellte Ansprechbarkeit, connect_to() der Bibliothek schaltet sie selbst aus, nur in loop()
    bool s_volumePending = false;               ///< s_deviceVolume per AVRCP senden, frühestens AvrcpDelayMs nach der Verbindung. Nur in loop()
    uint8_t s_deviceVolume = DefaultDeviceVolume; ///< Gerätelautstärke 0..127 für die nächste Verbindung und das Tab5, nur in loop() und setup()
    bool s_volumeDirty = false;                 ///< s_deviceVolume noch nicht gespeichert, nur in loop()
    uint32_t s_volumeChangedAtMs = 0;           ///< millis() der letzten Änderung, nur in loop()
    uint32_t s_volumeSentAtMs = 0;              ///< millis() des letzten AVRCP-Lautstärkebefehls, nur in loop()
    bool s_volumeOutstanding = false;           ///< Ein Lautstärkebefehl wartet auf seine Antwort, nur in loop()
    int s_lastSetVolume = -1;                   ///< Zuletzt per LAUTSTAERKE gesetzter Wert, -1 ohne. Nur in loop()
    uint32_t s_lastSetAtMs = 0;                 ///< millis() dazu, nur in loop()
    uint32_t s_connectedAtMs = 0;               ///< millis() bei der letzten Verbindung, nur in loop()
    bool s_toneRunning = false;                 ///< TESTTON angenommen, Ende noch nicht geloggt. Nur in loop()
    uint32_t s_toneStartMs = 0;                 ///< millis() beim letzten TESTTON, nur in loop()
    bool s_forgetPending = false;               ///< VERGISS wartet auf die Meldung der Trennung, nur in loop()
    StopKind s_stopPending = StopKind::None;    ///< TRENNE oder VERGISS wartet auf das Ende eines Medienstarts, nur in loop()
    uint32_t s_stopAtMs = 0;                    ///< millis() beim Aufschieben, nur in loop()
    uint32_t s_forgetAtMs = 0;                  ///< millis() beim wartenden VERGISS, nur in loop()
    uint32_t s_statsAtMs = 0;                   ///< millis() der letzten AUDIO-Zeile, nur in loop()
    uint32_t s_statsFrames = 0;                 ///< s_inFrames bei der letzten AUDIO-Zeile, nur in loop()
    uint32_t s_statsConvertUs = 0;              ///< s_convertUs dazu, nur in loop()
    uint32_t s_statsRenderUs = 0;               ///< s_renderUs dazu, nur in loop()

    pcnt_unit_handle_t s_counters[CountInputs] = {}; ///< Ein Zähler je Eingang, nullptr, wenn das Anlegen scheiterte
    char s_linkLine[LinkMaxLine + 1] = {};           ///< Empfangene Befehlszeile, nur in loop()
    size_t s_linkUsed = 0;                           ///< Belegte Zeichen in s_linkLine, nur in loop()
    bool s_linkInvalid = false;                      ///< Laufende Zeile zu lang oder mit Steuerzeichen, nur in loop()

    /** @brief Schreibt eine Bluetooth-Adresse als „AA:BB:CC:DD:EE:FF“, text muss AddressTextSize Byte fassen. */
    void formatAddress(const uint8_t *address, char *text)
    {
        snprintf(text, AddressTextSize, "%02X:%02X:%02X:%02X:%02X:%02X", address[0], address[1], address[2], address[3], address[4], address[5]);
    }

    /** @brief Wert einer Hexziffer in Groß- oder Kleinschreibung, -1 für jedes andere Zeichen. */
    int hexValue(char digit)
    {
        if (digit >= '0' && digit <= '9')
            return digit - '0';
        if (digit >= 'A' && digit <= 'F')
            return digit - 'A' + 10;
        if (digit >= 'a' && digit <= 'f')
            return digit - 'a' + 10;
        return -1;
    }

    /** @brief Liest „AA:BB:CC:DD:EE:FF“ mit großen oder kleinen Hexziffern. @return false bei jedem anderen Text, address ist dann unbestimmt. */
    bool parseAddress(const char *text, uint8_t *address)
    {
        if (strlen(text) != AddressTextSize - 1)
            return false;
        for (size_t i = 0; i < ESP_BD_ADDR_LEN; ++i)
        {
            const char *field = text + i * 3;
            const int high = hexValue(field[0]), low = hexValue(field[1]);
            if (high < 0 || low < 0 || (i + 1 < ESP_BD_ADDR_LEN && field[2] != ':'))
                return false;
            address[i] = static_cast<uint8_t>(high << 4 | low);
        }
        return true;
    }

    /** @brief Übernimmt einen Gerätenamen aus Bluedroid (UTF-8, ohne abschließendes Nullbyte) und kürzt ihn an einer Zeichengrenze auf NameMaxBytes. Ein Nullbyte beendet ihn vorher. */
    void copyName(const uint8_t *raw, size_t length, char (&name)[NameMaxBytes + 1])
    {
        const void *zero = memchr(raw, 0, length);
        if (zero)
            length = static_cast<size_t>(static_cast<const uint8_t *>(zero) - raw);
        size_t used = std::min(length, NameMaxBytes);
        while (used > 0 && used < length && (raw[used] & 0xC0) == 0x80)
            --used;
        memcpy(name, raw, used);
        name[used] = 0;
    }

    /** @brief Schreibt name für das Protokoll: sichtbares ASCII und Leerzeichen außer % bleiben, jedes andere Byte wird %XX. */
    void escapeName(const char *name, char (&text)[EscapedNameSize])
    {
        size_t used = 0;
        for (const char *p = name; *p && used + 3 < EscapedNameSize; ++p)
        {
            const uint8_t byte = static_cast<uint8_t>(*p);
            if (byte >= 0x20 && byte <= 0x7e && byte != '%')
                text[used++] = static_cast<char>(byte);
            else
                used += static_cast<size_t>(snprintf(text + used, EscapedNameSize - used, "%%%02X", byte));
        }
        text[used] = 0;
    }

    /** @brief Liest einen Text des Protokolls: %XX steht für ein Byte, sonst sichtbares ASCII oder Leerzeichen. Höchstens BtFm::TextBytes Byte, keine Steuerzeichen. Ein leerer Text ist gültig. @return false bei fehlerhaftem oder zu langem Text, out ist dann unbestimmt. */
    bool decodeText(const char *text, char (&out)[BtFm::TextBytes + 1])
    {
        size_t used = 0;
        for (const char *p = text; *p; ++p)
        {
            int byte = static_cast<unsigned char>(*p);
            if (byte == '%')
            {
                const int high = hexValue(p[1]), low = high < 0 ? -1 : hexValue(p[2]);
                if (low < 0)
                    return false;
                byte = high << 4 | low;
                p += 2;
            }
            if (byte < 0x20 || byte == 0x7f || used == BtFm::TextBytes)
                return false;
            out[used++] = static_cast<char>(byte);
        }
        out[used] = 0;
        return true;
    }

    /** @brief Liest eine Dezimalzahl ohne Vorzeichen mit 1 bis digits Ziffern bis zum Zeilenende. @return false bei jedem anderen Text oder einem Wert über maximum. */
    bool readNumber(const char *text, size_t digits, uint32_t maximum, uint32_t &value)
    {
        value = 0;
        size_t used = 0;
        for (; *text >= '0' && *text <= '9'; ++text)
        {
            if (++used > digits)
                return false;
            value = value * 10 + static_cast<uint32_t>(*text - '0');
        }
        return used > 0 && *text == 0 && value <= maximum;
    }

    /** @brief Sendet eine Zeile mit Zeilenende an das Tab5 und loggt sie, eine Zeile über LinkMaxLine wird nur als Fehler geloggt. */
    __attribute__((format(printf, 1, 2))) void sendLine(const char *format, ...)
    {
        char line[LinkMaxLine + 2];
        va_list args;
        va_start(args, format);
        const int length = vsnprintf(line, LinkMaxLine + 1, format, args);
        va_end(args);
        if (length < 0 || static_cast<size_t>(length) > LinkMaxLine)
        {
            log_e("UART Zeile zu lang, nicht gesendet");
            return;
        }
        log_i("UART -> %s", line);
        line[length] = '\n';
        Serial1.write(reinterpret_cast<const uint8_t *>(line), static_cast<size_t>(length) + 1);
    }

    /** @brief Reicht event an loop() weiter, ohne zu warten. Bei voller Queue zählt s_eventsLost. Aus den Bluetooth-Tasks. */
    void postEvent(const LinkEvent &event)
    {
        if (!s_events || xQueueSend(s_events, &event, 0) != pdTRUE)
            s_eventsLost.fetch_add(1, std::memory_order_relaxed);
    }

    /** @brief Berechnet die Sinustabelle mit der Testamplitude. */
    void fillSineTable()
    {
        const float peak = ToneAmplitude * 32767.0f;
        for (uint32_t i = 0; i < SineTableSize; ++i)
            s_sine[i] = static_cast<int16_t>(lroundf(peak * sinf(2.0f * static_cast<float>(M_PI) * i / SineTableSize)));
    }

    /** @brief Hebt ein Maximum für die AUDIO-Zeile an, ein gleichzeitiges Zurücksetzen durch loop() darf einen Wert verlieren. */
    template <typename T>
    void keepMax(std::atomic<T> &maximum, T value)
    {
        if (value > maximum.load(std::memory_order_relaxed))
            maximum.store(value, std::memory_order_relaxed);
    }

    /** @brief Senkt ein Minimum für die AUDIO-Zeile, wie keepMax(). */
    void keepMin(std::atomic<uint32_t> &minimum, uint32_t value)
    {
        if (value < minimum.load(std::memory_order_relaxed))
            minimum.store(value, std::memory_order_relaxed);
    }

    /**
     * @brief Liefert den Ton des Tab5 aus dem Ring an die A2DP-Quelle (BtAudio::OutputStage), nach TESTTON stattdessen für TestToneSeconds den Testton: 0,5 s Ton und 0,5 s Pause im Wechsel.
     *
     * Läuft im Datentask der A2DP-Quelle von Bluedroid. Ohne Log, ohne Warten und ohne Allokation.
     * @param frames Ziel für count Stereo-Frames
     * @return Anzahl gelieferter Frames, immer count
     */
    int32_t fillFrames(Frame *frames, int32_t count)
    {
        const int64_t begun = esp_timer_get_time();
        s_output.render(s_ring, static_cast<size_t>(count), s_muted.load(std::memory_order_relaxed), s_generation.load(std::memory_order_relaxed), s_overflow.exchange(false), [frames](size_t i, int16_t left, int16_t right)
        {
            frames[i].channel1 = left;
            frames[i].channel2 = right;
        });
        uint32_t position = s_tonePosition.load(std::memory_order_relaxed);
        const uint8_t command = s_toneCommand.exchange(ToneNone);
        if (command == ToneStart)
            position = 0;
        else if (command == ToneStop)
            position = TestToneFrames;
        for (int32_t i = 0; i < count && position < TestToneFrames; ++i, ++position)
        {
            int16_t sample = 0;
            const uint32_t inCycle = position % CycleFrames;
            if (inCycle == 0)
                s_phase = 0;
            if (inCycle < ToneFrames)
            {
                const uint32_t fromEnd = ToneFrames - 1 - inCycle;
                const uint32_t ramp = std::min(std::min(inCycle, fromEnd), RampFrames);
                sample = static_cast<int16_t>(static_cast<int32_t>(s_sine[s_phase >> (32 - SineTableBits)]) * static_cast<int32_t>(ramp) / static_cast<int32_t>(RampFrames));
                s_phase += PhaseStep;
            }
            frames[i].channel1 = sample;
            frames[i].channel2 = sample;
        }
        s_tonePosition.store(position, std::memory_order_relaxed);
        keepMax(s_requestMax, static_cast<uint32_t>(count));
        s_renderUs.fetch_add(static_cast<uint32_t>(esp_timer_get_time() - begun), std::memory_order_relaxed);
        return count;
    }

    /**
     * @brief I2S-Task: liest den Ton des Tab5, rechnet ihn mit der Quellrate aus RATE auf 44,1 kHz um und legt ihn in den Ring. Die Korrektur der Schrittweite folgt dem Füllstand, solange die Ausgabe den Ring liest.
     *
     * Eine neue Quellrate rechnet die Filtertabelle neu und zählt s_generation hoch, die Ausgabe blendet dann aus und verwirft den Ring. Ohne Takt vom Tab5 wartet der Task im Lesevorgang. Ohne Allokation, Log nur beim Ratenwechsel.
     */
    void audioTask(void *)
    {
        // Der Wachhund des Kerns (CONFIG_ESP_TASK_WDT_TIMEOUT_S, Neustart bei Ablauf) überwacht auch diesen Task. Ohne Takt vom Tab5 kehrt der Lesevorgang nach I2sReadTimeoutMs zurück.
        if (esp_task_wdt_add(nullptr) != ESP_OK)
            log_w("WACHHUND I2S-Task nicht angemeldet");
        bool btWasActive = false;
        int64_t windowStart = esp_timer_get_time();
        uint32_t windowFrames = 0, candidate = 0;
        int candidateWindows = 0;
        for (;;)
        {
            esp_task_wdt_reset();
            const uint32_t wanted = s_rateRequest.load();
            if (wanted != s_converter.inputRate())
            {
                const int64_t begun = esp_timer_get_time();
                const uint32_t optical = BtAudio::opticalRate(wanted);
                if (s_converter.configure(wanted) && s_opticalConverter.configure(wanted, optical))
                {
                    s_drift.reset();
                    s_opticalDrift.reset();
                    s_generation.fetch_add(1);
                    s_opticalRate.store(optical);
                    s_opticalGeneration.fetch_add(1);
                    log_i("AUDIO Quellrate %lu Hz, optisch %lu Hz, Filtertabellen in %ld µs", static_cast<unsigned long>(wanted), static_cast<unsigned long>(optical), static_cast<long>(esp_timer_get_time() - begun));
                }
            }
            size_t bytes = 0;
            i2s_channel_read(s_i2s, s_i2sBlock, sizeof(s_i2sBlock), &bytes, pdMS_TO_TICKS(I2sReadTimeoutMs));
            const size_t frames = bytes / (2 * sizeof(int32_t));
            if (frames == 0)
                continue;
            const float seconds = static_cast<float>(frames) / static_cast<float>(s_converter.inputRate());
            const int64_t begun = esp_timer_get_time();
            // Der Bluetooth-Ring wird nur mit verbundenem Gerät gefüllt, beim Verbinden beginnt der Verlauf neu und die Ausgabe verwirft den alten Inhalt des Rings.
            const bool btActive = s_btActive.load(std::memory_order_relaxed);
            if (btActive && !btWasActive)
            {
                s_converter.reset();
                s_generation.fetch_add(1);
            }
            btWasActive = btActive;
            const uint32_t fill = s_ring.fill();
            const float correction = btActive && s_output.playing() ? s_drift.update(fill, seconds) : s_drift.idle();
            if (btActive)
            {
                s_converter.setCorrection(correction);
                bool full = false;
                s_converter.process(s_i2sBlock, frames, [&full](int16_t left, int16_t right)
                {
                    if (!s_ring.push({left, right}))
                        full = true;
                });
                s_ring.commit();
                if (full)
                    s_overflow.store(true);
            }
            const int64_t opticalBegun = esp_timer_get_time();
            const uint32_t opticalFill = s_opticalRing.fill();
            const float opticalCorrection = s_opticalOutput.playing() ? s_opticalDrift.update(opticalFill, seconds) : s_opticalDrift.idle();
            s_opticalConverter.setCorrection(opticalCorrection);
            bool opticalFull = false;
            s_opticalConverter.process(s_i2sBlock, frames, [&opticalFull](int16_t left, int16_t right)
            {
                if (!s_opticalRing.push({left, right}))
                    opticalFull = true;
            });
            s_opticalRing.commit();
            if (opticalFull)
                s_opticalOverflow.store(true);
            keepMin(s_opticalFillLow, opticalFill);
            keepMax(s_opticalFillHigh, opticalFill);
            s_opticalCorrectionPpm.store(static_cast<int32_t>(lroundf(opticalCorrection)), std::memory_order_relaxed);
            s_opticalConvertUs.fetch_add(static_cast<uint32_t>(esp_timer_get_time() - opticalBegun), std::memory_order_relaxed);
            int32_t peakLeft = 0, peakRight = 0;
            for (size_t i = 0; i < frames; ++i)
            {
                peakLeft = std::max(peakLeft, std::abs(s_i2sBlock[2 * i] >> 16));
                peakRight = std::max(peakRight, std::abs(s_i2sBlock[2 * i + 1] >> 16));
            }
            keepMax(s_peakLeft, peakLeft);
            keepMax(s_peakRight, peakRight);
            keepMin(s_fillLow, fill);
            keepMax(s_fillHigh, fill);
            s_correctionPpm.store(static_cast<int32_t>(lroundf(correction)), std::memory_order_relaxed);
            s_inFrames.fetch_add(static_cast<uint32_t>(frames), std::memory_order_relaxed);
            s_convertUs.fetch_add(static_cast<uint32_t>(opticalBegun - begun), std::memory_order_relaxed);
            // Die Quellrate wird auch gemessen: Das Tab5 taktet im Leerlauf mit der Rate des letzten Stroms und meldet RATE erst beim Abspielen, und nach einem Neustart des Pico gilt erst DefaultInputRate. Weicht die Messung über zwei Fenster klar auf eine andere Standardrate ab, übernimmt der Pico sie. Ein späteres RATE des Tab5 gilt wieder.
            windowFrames += static_cast<uint32_t>(frames);
            const int64_t now = esp_timer_get_time();
            if (now - windowStart > 2 * RateWindowUs)
            {
                // Nach einer Pause ohne Takt beginnt die Messung neu.
                windowStart = now;
                windowFrames = 0;
            }
            else if (now - windowStart >= RateWindowUs)
            {
                const float measured = static_cast<float>(windowFrames) * 1e6f / static_cast<float>(now - windowStart);
                windowStart = now;
                windowFrames = 0;
                uint32_t nearest = 0;
                for (uint32_t rate : StandardRates)
                    if (std::fabs(measured - static_cast<float>(rate)) <= RateTolerance * static_cast<float>(rate))
                        nearest = rate;
                if (nearest == 0 || nearest == s_converter.inputRate())
                    candidateWindows = 0;
                else if (nearest != candidate || ++candidateWindows < RateWindowsToAccept)
                {
                    if (nearest != candidate)
                        candidateWindows = 1;
                    candidate = nearest;
                }
                else
                {
                    log_i("AUDIO Quellrate gemessen %lu Hz statt %lu Hz, übernommen", static_cast<unsigned long>(nearest), static_cast<unsigned long>(s_converter.inputRate()));
                    s_rateRequest.store(nearest);
                    candidateWindows = 0;
                }
            }
        }
    }

    /**
     * @brief Optischer Task: liest den optischen Ring über BtAudio::OutputStage, kodiert die Rahmen nach S/PDIF und schreibt sie an I2S1. Bei einer neuen Rate stellt er zuerst den Takt um.
     *
     * I2S1 läuft als Master mit APLL-Takt bei der doppelten Rate und 32 Bit Stereo, sodass je Stereorahmen 128 Zellen lückenlos hinausgehen. Ohne Ton vom Tab5 sendet er Stille als gültige Rahmen, damit der Empfänger eingerastet bleibt. Ohne Allokation. Log nur beim Ratenwechsel.
     */
    void opticalTask(void *)
    {
        if (esp_task_wdt_add(nullptr) != ESP_OK)
            log_w("WACHHUND optischer Task nicht angemeldet");
        uint32_t rate = 0;
        for (;;)
        {
            esp_task_wdt_reset();
            const uint32_t wanted = s_opticalRate.load();
            if (wanted != rate && wanted != 0)
            {
                i2s_std_clk_config_t clock = I2S_STD_CLK_DEFAULT_CONFIG(2 * wanted);
                clock.clk_src = I2S_CLK_SRC_APLL;
                if (i2s_channel_disable(s_optical) != ESP_OK || i2s_channel_reconfig_std_clock(s_optical, &clock) != ESP_OK || i2s_channel_enable(s_optical) != ESP_OK)
                    log_e("OPTISCH Takt für %lu Hz nicht eingestellt", static_cast<unsigned long>(wanted));
                else
                {
                    rate = wanted;
                    s_spdif.setRate(rate);
                    log_i("OPTISCH %lu Hz, Bittakt %lu Hz", static_cast<unsigned long>(rate), static_cast<unsigned long>(128 * rate));
                }
            }
            const int64_t begun = esp_timer_get_time();
            s_opticalOutput.render(s_opticalRing, OpticalBlockFrames, s_muted.load(std::memory_order_relaxed), s_opticalGeneration.load(std::memory_order_relaxed), s_opticalOverflow.exchange(false), [](size_t i, int16_t left, int16_t right)
            {
                s_spdif.encode(left, right, s_opticalWords + i * BtAudio::SpdifEncoder::WordsPerFrame);
            });
            s_opticalRenderUs.fetch_add(static_cast<uint32_t>(esp_timer_get_time() - begun), std::memory_order_relaxed);
            size_t written = 0;
            if (i2s_channel_write(s_optical, s_opticalWords, sizeof(s_opticalWords), &written, pdMS_TO_TICKS(OpticalWriteTimeoutMs)) != ESP_OK || written != sizeof(s_opticalWords))
                s_opticalWriteErrors.fetch_add(1, std::memory_order_relaxed);
        }
    }

    /** @brief Richtet I2S1 als Master für den S/PDIF-Strom an OpticalPin ein: APLL-Takt, doppelte Rate, 32 Bit Stereo, nur die Datenleitung. @return false, wenn der Treiber scheitert. Bluetooth bleibt dann nutzbar. */
    bool setupOptical(uint32_t rate)
    {
        i2s_chan_config_t channel = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
        channel.dma_desc_num = OpticalDmaDescriptors;
        channel.dma_frame_num = OpticalDmaFrames;
        // Bleibt der Task einmal aus, sendet die DMA Nullen statt eines alten Puffers. Der Empfänger verliert dann kurz den Takt, statt alten Ton zu wiederholen.
        channel.auto_clear = true;
        if (i2s_new_channel(&channel, &s_optical, nullptr) != ESP_OK)
        {
            s_optical = nullptr;
            return false;
        }
        i2s_std_config_t config = {};
        config.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(2 * rate);
        config.clk_cfg.clk_src = I2S_CLK_SRC_APLL;
        config.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO);
        config.gpio_cfg.mclk = I2S_GPIO_UNUSED;
        config.gpio_cfg.bclk = I2S_GPIO_UNUSED;
        config.gpio_cfg.ws = I2S_GPIO_UNUSED;
        config.gpio_cfg.dout = static_cast<gpio_num_t>(OpticalPin);
        config.gpio_cfg.din = I2S_GPIO_UNUSED;
        if (i2s_channel_init_std_mode(s_optical, &config) != ESP_OK || i2s_channel_enable(s_optical) != ESP_OK)
        {
            i2s_del_channel(s_optical);
            s_optical = nullptr;
            return false;
        }
        return true;
    }

    /** @brief Richtet den I2S-Empfang als Slave im Format von I2S0 des Tab5 ein: Philips, 32-Bit-Slots, Stereo. Der Modultakt ist für MaxInputRate bemessen und reicht damit für jede kleinere Rate. @return false, wenn der Treiber scheitert. Steuerung und Testton bleiben dann nutzbar. */
    bool setupI2s()
    {
        i2s_chan_config_t channel = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_SLAVE);
        channel.dma_desc_num = I2sDmaDescriptors;
        channel.dma_frame_num = I2sDmaFrames;
        if (i2s_new_channel(&channel, nullptr, &s_i2s) != ESP_OK)
        {
            s_i2s = nullptr;
            return false;
        }
        i2s_std_config_t config = {};
        config.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(BtAudio::MaxInputRate);
        config.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO);
        config.gpio_cfg.mclk = I2S_GPIO_UNUSED;
        config.gpio_cfg.bclk = static_cast<gpio_num_t>(I2sBclk);
        config.gpio_cfg.ws = static_cast<gpio_num_t>(I2sWs);
        config.gpio_cfg.dout = I2S_GPIO_UNUSED;
        config.gpio_cfg.din = static_cast<gpio_num_t>(I2sData);
        if (i2s_channel_init_std_mode(s_i2s, &config) != ESP_OK || i2s_channel_enable(s_i2s) != ESP_OK)
        {
            i2s_del_channel(s_i2s);
            s_i2s = nullptr;
            return false;
        }
        // Wie bei den Flankenzählern: Ohne Takt vom Tab5 liegen die Eingänge definiert Low und speisen ein ausgeschaltetes Tab5 nicht.
        for (int pin : CountPins)
            gpio_set_pull_mode(static_cast<gpio_num_t>(pin), GPIO_PULLDOWN_ONLY);
        return true;
    }

    /**
     * @brief Meldet ein Gerät der Suche an loop(), wenn seine Geräteklasse Audio nennt: Hauptklasse Audio/Video oder Dienst Wiedergabe bzw. Audio.
     *
     * Im BTC-Task. Den Namen liefert meist die erweiterte Suchantwort (EIR), sonst die Namenseigenschaft. Ohne beide bleibt er leer.
     */
    void reportDevice(const esp_bt_gap_cb_param_t::disc_res_param &result)
    {
        LinkEvent event = {};
        event.kind = EventKind::Found;
        uint32_t cod = 0;
        uint8_t *eir = nullptr;
        const uint8_t *property = nullptr;
        size_t propertyLength = 0;
        for (int i = 0; i < result.num_prop; ++i)
        {
            const esp_bt_gap_dev_prop_t &prop = result.prop[i];
            if (prop.type == ESP_BT_GAP_DEV_PROP_COD)
                cod = *static_cast<const uint32_t *>(prop.val);
            else if (prop.type == ESP_BT_GAP_DEV_PROP_RSSI)
            {
                event.rssi = *static_cast<const int8_t *>(prop.val);
                event.rssiKnown = true;
            }
            else if (prop.type == ESP_BT_GAP_DEV_PROP_EIR)
                eir = static_cast<uint8_t *>(prop.val);
            else if (prop.type == ESP_BT_GAP_DEV_PROP_BDNAME && prop.len > 0)
            {
                property = static_cast<const uint8_t *>(prop.val);
                propertyLength = static_cast<size_t>(prop.len);
            }
        }
        if (!esp_bt_gap_is_valid_cod(cod) || (esp_bt_gap_get_cod_major_dev(cod) != ESP_BT_COD_MAJOR_DEV_AV && !(esp_bt_gap_get_cod_srvc(cod) & AudioServices)))
            return;
        uint8_t nameLength = 0;
        const uint8_t *name = nullptr;
        if (eir)
        {
            name = esp_bt_gap_resolve_eir_data(eir, ESP_BT_EIR_TYPE_CMPL_LOCAL_NAME, &nameLength);
            if (!name)
                name = esp_bt_gap_resolve_eir_data(eir, ESP_BT_EIR_TYPE_SHORT_LOCAL_NAME, &nameLength);
        }
        if (name)
            copyName(name, nameLength, event.name);
        else if (property)
            copyName(property, propertyLength, event.name);
        memcpy(event.address, result.bda, ESP_BD_ADDR_LEN);
        postEvent(event);
    }

    bool QuietSource::connectPeer(const uint8_t *address)
    {
        memcpy(last_connection, address, ESP_BD_ADDR_LEN);
        memcpy(peer_bd_addr, address, ESP_BD_ADDR_LEN);
        return connect_to(last_connection);
    }

    void QuietSource::av_hdl_stack_evt(uint16_t, void *)
    {
        // Die Basisklasse wartet hier 10 s und sucht oder verbindet dann, die AVRCP-Zielrolle für Tastenbefehle des Geräts bleibt aus.
        esp_bt_gap_set_device_name(dev_name);
        esp_bt_gap_register_callback(ccall_app_gap_callback);
        esp_avrc_ct_init();
        esp_avrc_ct_register_callback(ccall_app_rc_ct_callback);
        esp_a2d_source_init();
        esp_a2d_register_callback(&ccall_app_a2d_callback);
        esp_a2d_source_register_data_callback(&ccall_bt_app_a2d_data_cb);
        set_scan_mode_connectable(false);
        s_a2d_state = APP_AV_STATE_UNCONNECTED;
        s_tmr = xTimerCreate("connTmr", pdMS_TO_TICKS(HeartBeatMs), pdTRUE, nullptr, ccall_a2d_app_heart_beat);
        if (!s_tmr || xTimerStart(s_tmr, portMAX_DELAY) != pdPASS)
            log_e("START Herzschlag nicht gestartet, Medien starten nach einer Verbindung nicht");
        s_stackReady.store(true);
    }

    void QuietSource::app_gap_callback(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param)
    {
        if (event == ESP_BT_GAP_DISC_RES_EVT)
            reportDevice(param->disc_res);
        else if (event == ESP_BT_GAP_DISC_STATE_CHANGED_EVT)
        {
            // Die Basisklasse startet hier nach jedem Ende ohne Treffer sofort die nächste Suche.
            discovery_active = param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STARTED;
            if (!discovery_active)
            {
                LinkEvent ended = {};
                ended.kind = EventKind::SearchEnd;
                postEvent(ended);
            }
        }
        else if (event == ESP_BT_GAP_READ_REMOTE_NAME_EVT)
        {
            LinkEvent named = {};
            named.kind = EventKind::Name;
            memcpy(named.address, param->read_rmt_name.bda, ESP_BD_ADDR_LEN);
            if (param->read_rmt_name.stat == ESP_BT_STATUS_SUCCESS)
                copyName(param->read_rmt_name.rmt_name, strnlen(reinterpret_cast<const char *>(param->read_rmt_name.rmt_name), ESP_BT_GAP_MAX_BDNAME_LEN), named.name);
            postEvent(named);
        }
        else
            BluetoothA2DPSource::app_gap_callback(event, param);
    }

    void QuietSource::bt_av_notify_evt_handler(uint8_t event, esp_avrc_rn_param_t *param)
    {
        if (event != ESP_AVRC_RN_VOLUME_CHANGE || !param)
            return;
        LinkEvent changed = {};
        changed.kind = EventKind::Volume;
        changed.state = std::min(param->volume, AvrcpMaxVolume);
        postEvent(changed);
        // AVRCP meldet jede Änderung nur einmal, für die nächste Taste neu anmelden.
        bt_av_volume_changed();
    }

    void QuietSource::set_volume(uint8_t)
    {
        s_volumeAnswered.store(true);
    }

    void QuietSource::adoptPeer(const uint8_t *address)
    {
        memcpy(last_connection, address, ESP_BD_ADDR_LEN);
        memcpy(peer_bd_addr, address, ESP_BD_ADDR_LEN);
    }

    void QuietSource::bt_app_av_state_unconnected_hdlr(uint16_t event, void *param)
    {
        // Die Basisklasse verbindet hier bei jeder Trennung und jedem Herzschlag erneut oder sucht. Eine Verbindung, die das Gerät aufbaut (Pico ansprechbar) oder die erst nach dem Zeitlimit der Bibliothek zustande kommt, braucht den Zustand „verbunden“, damit der Herzschlag die Medien startet.
        const auto *a2d = static_cast<const esp_a2d_cb_param_t *>(param);
        if (event == ESP_A2D_CONNECTION_STATE_EVT && a2d && a2d->conn_stat.state == ESP_A2D_CONNECTION_STATE_CONNECTED)
            bt_app_av_state_connecting_hdlr(event, param);
    }

    void QuietSource::app_a2d_callback(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *param)
    {
        if (event == ESP_A2D_CONNECTION_STATE_EVT && param)
        {
            LinkEvent changed = {};
            changed.kind = EventKind::Connection;
            changed.state = static_cast<uint8_t>(param->conn_stat.state);
            memcpy(changed.address, param->conn_stat.remote_bda, ESP_BD_ADDR_LEN);
            postEvent(changed);
        }
        BluetoothA2DPSource::app_a2d_callback(event, param);
    }

    /** @brief Loggt Start und Stopp des Audiostroms, läuft im Task der Bibliothek. */
    void onAudioState(esp_a2d_audio_state_t state, void *)
    {
        log_i("AUDIO %s", s_source.to_str(state));
    }

    /** @brief Liest das gespeicherte Gerät aus dem NVS. Erst nach start(), das den NVS einrichtet. */
    void loadStored()
    {
        s_storedValid = false;
        s_storedName[0] = 0;
        nvs_handle_t handle;
        if (nvs_open(StoreNamespace, NVS_READONLY, &handle) != ESP_OK)
            return;
        size_t size = ESP_BD_ADDR_LEN;
        s_storedValid = nvs_get_blob(handle, StoreAddressKey, s_storedAddress, &size) == ESP_OK && size == ESP_BD_ADDR_LEN && memcmp(s_storedAddress, NoAddress, ESP_BD_ADDR_LEN) != 0;
        size = sizeof(s_storedName);
        if (!s_storedValid || nvs_get_str(handle, StoreNameKey, s_storedName, &size) != ESP_OK)
            s_storedName[0] = 0;
        uint8_t volume = DefaultDeviceVolume;
        if (nvs_get_u8(handle, StoreVolumeKey, &volume) == ESP_OK && volume <= AvrcpMaxVolume)
            s_deviceVolume = volume;
        nvs_close(handle);
    }

    /** @brief Speichert s_deviceVolume für die nächste Verbindung. @return false bei einem NVS-Fehler. */
    bool saveVolume()
    {
        s_volumeDirty = false;
        nvs_handle_t handle;
        if (nvs_open(StoreNamespace, NVS_READWRITE, &handle) != ESP_OK)
            return false;
        const bool saved = nvs_set_u8(handle, StoreVolumeKey, s_deviceVolume) == ESP_OK && nvs_commit(handle) == ESP_OK;
        nvs_close(handle);
        return saved;
    }

    /** @brief Übernimmt eine neue Gerätelautstärke und merkt sie zum Speichern vor. */
    void changeVolume(uint8_t volume)
    {
        if (volume == s_deviceVolume)
            return;
        s_deviceVolume = volume;
        s_volumeDirty = true;
        s_volumeChangedAtMs = millis();
    }

    /** @brief Speichert address und name als Gerät für den nächsten Start, nur bei einer Änderung. @return false bei einem NVS-Fehler. */
    bool saveStored(const uint8_t *address, const char *name)
    {
        if (s_storedValid && memcmp(address, s_storedAddress, ESP_BD_ADDR_LEN) == 0 && strcmp(name, s_storedName) == 0)
            return true;
        nvs_handle_t handle;
        if (nvs_open(StoreNamespace, NVS_READWRITE, &handle) != ESP_OK)
            return false;
        const bool saved = nvs_set_blob(handle, StoreAddressKey, address, ESP_BD_ADDR_LEN) == ESP_OK && nvs_set_str(handle, StoreNameKey, name) == ESP_OK && nvs_commit(handle) == ESP_OK;
        nvs_close(handle);
        if (saved)
        {
            memcpy(s_storedAddress, address, ESP_BD_ADDR_LEN);
            snprintf(s_storedName, sizeof(s_storedName), "%s", name);
            s_storedValid = true;
        }
        return saved;
    }

    /** @brief Löscht Adresse, Namen und Lautstärke des gespeicherten Geräts. @return false bei einem NVS-Fehler. */
    bool eraseStored()
    {
        s_storedValid = false;
        s_storedName[0] = 0;
        nvs_handle_t handle;
        if (nvs_open(StoreNamespace, NVS_READWRITE, &handle) != ESP_OK)
            return false;
        s_deviceVolume = DefaultDeviceVolume;
        s_volumeDirty = false;
        const esp_err_t address = nvs_erase_key(handle, StoreAddressKey), name = nvs_erase_key(handle, StoreNameKey), volume = nvs_erase_key(handle, StoreVolumeKey);
        const bool erased = (address == ESP_OK || address == ESP_ERR_NVS_NOT_FOUND) && (name == ESP_OK || name == ESP_ERR_NVS_NOT_FOUND) && (volume == ESP_OK || volume == ESP_ERR_NVS_NOT_FOUND) && nvs_commit(handle) == ESP_OK;
        nvs_close(handle);
        return erased;
    }

    /** @brief Löscht die Kopplungen aus Bluedroid. Die Liste fasst MaxListedBonds Einträge, weitere holt die Schleife nach. @return Anzahl gelöschter Kopplungen, -1, wenn die Liste nicht lesbar ist. */
    int removeBonds()
    {
        int removed = 0;
        for (int round = 0; round < 4; ++round)
        {
            esp_bd_addr_t bonds[MaxListedBonds];
            int count = MaxListedBonds;
            if (esp_bt_gap_get_bond_device_list(&count, bonds) != ESP_OK)
                return -1;
            for (int i = 0; i < count; ++i)
                if (esp_bt_gap_remove_bond_device(bonds[i]) == ESP_OK)
                    ++removed;
            if (count < MaxListedBonds)
                break;
        }
        return removed;
    }

    /** @brief Grund des letzten Neustarts für das Startlog, ein Wachhund oder Absturz im Dauerbetrieb wird so sichtbar. */
    const char *resetReasonText()
    {
        switch (esp_reset_reason())
        {
        case ESP_RST_POWERON:
            return "Einschalten";
        case ESP_RST_EXT:
            return "Reset-Pin";
        case ESP_RST_SW:
            return "Software";
        case ESP_RST_PANIC:
            return "Absturz";
        case ESP_RST_INT_WDT:
            return "Interrupt-Wachhund";
        case ESP_RST_TASK_WDT:
            return "Task-Wachhund";
        case ESP_RST_WDT:
            return "Wachhund";
        case ESP_RST_BROWNOUT:
            return "Unterspannung";
        case ESP_RST_DEEPSLEEP:
            return "Tiefschlaf";
        case ESP_RST_SDIO:
            return "SDIO";
        default:
            return "unbekannt";
        }
    }

    /** @brief Loggt die eigene Adresse, das gespeicherte Gerät und die Kopplungen von Bluedroid. Erst nach start() und loadStored(). */
    void logStoredPairing()
    {
        char addressText[AddressTextSize];
        const uint8_t *own = esp_bt_dev_get_address();
        if (own != nullptr)
        {
            formatAddress(own, addressText);
            log_i("EIGEN Name=\"%s\" Adresse=%s", s_localName, addressText);
        }
        if (s_storedValid)
        {
            formatAddress(s_storedAddress, addressText);
            log_i("GESPEICHERT Adresse=%s Name=\"%s\" Lautstärke=%u, nach dem Start %lu s ansprechbar, dann bis zu %d Versuche", addressText, s_storedName, static_cast<unsigned>(s_deviceVolume), static_cast<unsigned long>(StartupListenMs / 1000), StartupAttempts);
        }
        else
            log_i("GESPEICHERT kein Gerät, still bis zum ersten Befehl");

        esp_bd_addr_t bonds[MaxListedBonds];
        int count = MaxListedBonds;
        if (esp_bt_gap_get_bond_device_list(&count, bonds) != ESP_OK)
        {
            log_w("GESPEICHERT Kopplungsliste nicht lesbar");
            return;
        }
        log_i("GESPEICHERT Kopplungen=%d", esp_bt_gap_get_bond_device_num());
        for (int i = 0; i < count; ++i)
        {
            formatAddress(bonds[i], addressText);
            log_i("GESPEICHERT Kopplung %d Adresse=%s", i + 1, addressText);
        }
    }

    /** @brief Setzt den Zustand der Steuerung und loggt einen Wechsel. */
    void setState(LinkState state)
    {
        if (state != s_state)
            log_i("ZUSTAND %s -> %s", StateNames[static_cast<size_t>(s_state)], StateNames[static_cast<size_t>(state)]);
        s_state = state;
    }

    /** @brief Name zu address aus den Treffern der letzten Suche oder vom gespeicherten Gerät, sonst leer. */
    const char *knownName(const uint8_t *address)
    {
        for (size_t i = 0; i < s_hitCount; ++i)
            if (memcmp(address, s_hits[i].address, ESP_BD_ADDR_LEN) == 0 && s_hits[i].name[0])
                return s_hits[i].name;
        if (s_storedValid && memcmp(address, s_storedAddress, ESP_BD_ADDR_LEN) == 0)
            return s_storedName;
        return "";
    }

    /** @brief Schaltet den Pico für eingehende Verbindungen ansprechbar (Page-Scan, für Suchen unsichtbar) oder ganz still. Loggt nur Wechsel. Ansprechbar heißt zuhören: Der Pico sendet erst, wenn ein Gerät ihn anruft. */
    void setConnectable(bool connectable)
    {
        if (connectable == s_connectable)
            return;
        const esp_err_t result = esp_bt_gap_set_scan_mode(connectable ? ESP_BT_CONNECTABLE : ESP_BT_NON_CONNECTABLE, ESP_BT_NON_DISCOVERABLE);
        if (result != ESP_OK)
        {
            log_w("ANSPRECHBAR nicht geschaltet: %s", esp_err_to_name(result));
            return;
        }
        s_connectable = connectable;
        log_i("ANSPRECHBAR %s", connectable ? "ja, für das gespeicherte Gerät" : "nein");
    }

    /** @brief Hält den Pico ansprechbar, solange ein Gerät gespeichert ist, weder Suche noch Verbindung läuft und kein TRENNE es untersagt hat. Sonst still. Jeden loop()-Durchlauf. */
    void updateListening()
    {
        setConnectable(s_started && s_state == LinkState::Idle && s_storedValid && s_listenAllowed && !s_forgetPending && s_stopPending == StopKind::None);
    }

    /** @brief Beginnt bis zu attempts Verbindungsversuche zu address, der erste startet im nächsten loop(). */
    void beginConnect(const uint8_t *address, const char *name, int attempts)
    {
        memcpy(s_peer, address, ESP_BD_ADDR_LEN);
        snprintf(s_peerName, sizeof(s_peerName), "%s", name);
        s_attemptsLeft = attempts;
        s_attemptActive = false;
        s_attemptAtMs = millis();
        setState(LinkState::Connecting);
    }

    /** @brief Sendet VERBUNDEN mit Adresse und, falls bekannt, Namen der Gegenstelle. */
    void announceConnected()
    {
        char addressText[AddressTextSize], escaped[EscapedNameSize];
        formatAddress(s_peer, addressText);
        escapeName(s_peerName, escaped);
        sendLine("VERBUNDEN %s%s%s", addressText, escaped[0] ? " " : "", escaped);
    }

    /** @brief Beendet Versuche oder Verbindung, bricht einen Testton ab und meldet GETRENNT. */
    void endLink(const char *reason)
    {
        char addressText[AddressTextSize];
        formatAddress(s_peer, addressText);
        log_i("VERBINDUNG %s beendet: %s", addressText, reason);
        s_attemptsLeft = 0;
        s_attemptActive = false;
        s_volumePending = false;
        s_toneCommand.store(ToneStop);
        s_toneRunning = false;
        s_muted.store(true);
        s_btActive.store(false);
        if (s_volumeDirty && !saveVolume())
            log_w("LAUTSTAERKE nicht gespeichert");
        setState(LinkState::Idle);
        sendLine("GETRENNT %s", addressText);
    }

    /** @brief Wertet einen gescheiterten Versuch: der nächste folgt nach RetryPauseMs, nach dem letzten GETRENNT. */
    void failAttempt(const char *reason)
    {
        s_attemptActive = false;
        if (s_attemptsLeft <= 0)
        {
            endLink(reason);
            return;
        }
        log_i("VERBINDUNG Versuch gescheitert (%s), nächster in %lu s, danach noch %d", reason, static_cast<unsigned long>(RetryPauseMs / 1000), s_attemptsLeft - 1);
        s_attemptAtMs = millis() + RetryPauseMs;
    }

    /** @brief Startet fällige Verbindungsversuche und wertet einen Versuch ohne Ergebnis nach AttemptTimeoutMs als gescheitert. */
    void serviceConnect(uint32_t now)
    {
        if (s_state != LinkState::Connecting)
            return;
        if (s_attemptActive)
        {
            if (now - s_attemptAtMs >= AttemptTimeoutMs)
                failAttempt("Zeitlimit");
            return;
        }
        if (static_cast<int32_t>(now - s_attemptAtMs) < 0)
            return;
        --s_attemptsLeft;
        s_attemptActive = true;
        s_attemptAtMs = now;
        char addressText[AddressTextSize];
        formatAddress(s_peer, addressText);
        log_i("VERBINDUNG Versuch zu %s, danach noch %d", addressText, s_attemptsLeft);
        // connect_to() der Bibliothek schaltet die Ansprechbarkeit aus, updateListening() stellt sie nach dem Ende der Versuche wieder her.
        s_connectable = false;
        if (!s_source.connectPeer(s_peer))
            failAttempt("abgelehnt");
    }

    /** @brief Startet eine Suche mit fester Dauer. Treffer folgen als TREFFER, das Ende als SUCHE ENDE. @return false, wenn Bluedroid ablehnt. */
    bool startSearch()
    {
        s_hitCount = 0;
        s_connectAfterSearch = false;
        // Während der Suche nimmt der Pico keine eingehende Verbindung an.
        setConnectable(false);
        const esp_err_t result = esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, InquiryUnits, 0);
        if (result != ESP_OK)
        {
            log_w("SUCHE nicht gestartet: %s", esp_err_to_name(result));
            return false;
        }
        s_searchEndMs = millis() + InquiryUnits * InquiryUnitMs + SearchGraceMs;
        setState(LinkState::Searching);
        return true;
    }

    /** @brief Schließt die Suche ab: SUCHE ENDE mit der Trefferzahl, danach eine während der Suche befohlene Verbindung. */
    void finishSearch(const char *reason)
    {
        log_i("SUCHE beendet (%s), %u Treffer", reason, static_cast<unsigned>(s_hitCount));
        setState(LinkState::Idle);
        sendLine("SUCHE ENDE %u", static_cast<unsigned>(s_hitCount));
        if (!s_connectAfterSearch)
            return;
        s_connectAfterSearch = false;
        beginConnect(s_afterSearchAddress, knownName(s_afterSearchAddress), CommandAttempts);
    }

    /** @brief Nimmt einen Suchtreffer auf und meldet ihn als TREFFER. Dasselbe Gerät erneut nur, wenn es erst jetzt einen Namen hat. */
    void handleFound(const LinkEvent &event)
    {
        if (s_state != LinkState::Searching)
            return;
        Hit *hit = nullptr;
        for (size_t i = 0; i < s_hitCount && !hit; ++i)
            if (memcmp(event.address, s_hits[i].address, ESP_BD_ADDR_LEN) == 0)
                hit = &s_hits[i];
        if (hit)
        {
            if (hit->name[0] || !event.name[0])
                return;
        }
        else
        {
            if (s_hitCount == MaxHits)
            {
                log_w("SUCHE Trefferliste voll, Gerät übergangen");
                return;
            }
            hit = &s_hits[s_hitCount++];
            memcpy(hit->address, event.address, ESP_BD_ADDR_LEN);
        }
        memcpy(hit->name, event.name, sizeof(hit->name));
        char addressText[AddressTextSize], rssiText[8] = "?", escaped[EscapedNameSize];
        formatAddress(hit->address, addressText);
        if (event.rssiKnown)
            snprintf(rssiText, sizeof(rssiText), "%d", event.rssi);
        escapeName(hit->name, escaped);
        sendLine("TREFFER %s %s%s%s", addressText, rssiText, escaped[0] ? " " : "", escaped);
    }

    /** @brief Löscht Kopplungen und gespeicherte Gerät und beantwortet ein wartendes oder sofortiges VERGISS. @param reason Anlass für das Log. */
    void finishForget(const char *reason)
    {
        s_forgetPending = false;
        const int bonds = removeBonds();
        const bool erased = eraseStored();
        log_i("VERGISS (%s) Kopplungen gelöscht=%d, Speicher %s", reason, bonds, erased ? "gelöscht" : "FEHLER");
        if (bonds >= 0 && erased)
            sendLine("VERGISS OK");
        else
            sendLine("FEHLER SPEICHER");
    }

    /** @brief Wertet einen A2DP-Verbindungszustand aus. Eine Verbindung im Zustand STILL vom gespeicherten Gerät oder vom zuletzt angewählten Gerät (Pico ansprechbar bzw. verspätet zustande gekommen) wird übernommen, jede andere ohne laufenden Auftrag wird sofort wieder getrennt. @param address Adresse der Gegenstelle aus der Meldung. */
    void handleConnection(esp_a2d_connection_state_t state, const uint8_t *address)
    {
        char addressText[AddressTextSize];
        formatAddress(address, addressText);
        log_i("VERBINDUNG %s %s", s_source.to_str(state), addressText);
        if (state == ESP_A2D_CONNECTION_STATE_CONNECTED)
        {
            const bool stored = s_storedValid && memcmp(address, s_storedAddress, ESP_BD_ADDR_LEN) == 0;
            if (s_state == LinkState::Idle && (stored || memcmp(address, s_peer, ESP_BD_ADDR_LEN) == 0))
            {
                log_i("VERBINDUNG eingehend von %s, %s", addressText, stored ? "gespeichertes Gerät" : "zuletzt angewähltes Gerät");
                s_startupPending = false;
                s_source.adoptPeer(address);
                beginConnect(address, stored ? s_storedName : knownName(address), 0);
            }
            if (s_state != LinkState::Connecting)
            {
                log_w("VERBINDUNG von %s im Zustand %s nicht erlaubt, trenne", addressText, StateNames[static_cast<size_t>(s_state)]);
                esp_bd_addr_t peer;
                memcpy(peer, address, ESP_BD_ADDR_LEN);
                esp_a2d_source_disconnect(peer);
                return;
            }
            s_attemptsLeft = 0;
            s_attemptActive = false;
            // Jede Verbindung beginnt stumm, das Tab5 gibt den Ton nach VERBUNDEN mit STUMM AUS frei.
            s_muted.store(true);
            s_btActive.store(true);
            setState(LinkState::Connected);
            s_connectedAtMs = millis();
            s_volumePending = true;
            if (!saveStored(s_peer, s_peerName))
                log_w("GESPEICHERT Gerät nicht gespeichert");
            announceConnected();
            // Das Tab5 stellt seinen Drehknopf auf die gespeicherte Lautstärke, die der Pico nach AvrcpDelayMs auch dem Gerät schickt. KOPFHOERER ist der Name der Zeile für jedes Gerät.
            sendLine("KOPFHOERER %u", static_cast<unsigned>(s_deviceVolume));
            if (!s_peerName[0] && esp_bt_gap_read_remote_name(s_peer) != ESP_OK)
                log_w("NAME Abfrage nicht gestartet");
        }
        else if (state == ESP_A2D_CONNECTION_STATE_DISCONNECTED)
        {
            if (s_forgetPending)
                finishForget("getrennt");
            // Bluedroid meldet eine Trennung oft zweimal (Funkverbindung und A2DP). Die zweite trifft auf den Zustand still oder einen Versuch, der noch nicht läuft.
            if (s_state == LinkState::Connected)
                endLink("getrennt");
            else if (s_state == LinkState::Connecting && s_attemptActive)
                failAttempt("abgewiesen");
        }
    }

    /** @brief Übernimmt den nachgefragten Namen der Gegenstelle, speichert ihn und meldet VERBUNDEN erneut mit Namen. */
    void handleName(const LinkEvent &event)
    {
        if (s_state != LinkState::Connected || memcmp(event.address, s_peer, ESP_BD_ADDR_LEN) != 0)
            return;
        if (!event.name[0])
        {
            log_w("NAME der Gegenstelle nicht lesbar");
            return;
        }
        memcpy(s_peerName, event.name, sizeof(s_peerName));
        log_i("NAME \"%s\"", s_peerName);
        if (!saveStored(s_peer, s_peerName))
            log_w("GESPEICHERT Name nicht gespeichert");
        announceConnected();
    }

    /** @brief Übernimmt eine vom Gerät gemeldete Lautstärke, meist nach dessen Tasten, und meldet sie dem Tab5 als KOPFHOERER (Name aus der Zeit, als nur Kopfhörer vorgesehen waren, gilt für jedes Gerät). Das Echo eines eben per LAUTSTAERKE gesetzten Werts geht nicht zurück, damit der Drehknopf beim Drehen nicht springt. */
    void handleVolume(uint8_t volume)
    {
        if (s_state != LinkState::Connected)
            return;
        if (volume == s_lastSetVolume && millis() - s_lastSetAtMs < VolumeEchoMs)
            return;
        log_i("LAUTSTAERKE vom Gerät %u von %u", static_cast<unsigned>(volume), static_cast<unsigned>(AvrcpMaxVolume));
        changeVolume(volume);
        sendLine("KOPFHOERER %u", static_cast<unsigned>(volume));
    }

    /** @brief Verarbeitet alle Meldungen der Bluetooth-Tasks. */
    void serviceEvents()
    {
        LinkEvent event;
        while (xQueueReceive(s_events, &event, 0) == pdTRUE)
        {
            if (event.kind == EventKind::Found)
                handleFound(event);
            else if (event.kind == EventKind::SearchEnd)
            {
                if (s_state == LinkState::Searching)
                    finishSearch("gemeldet");
            }
            else if (event.kind == EventKind::Connection)
                handleConnection(static_cast<esp_a2d_connection_state_t>(event.state), event.address);
            else if (event.kind == EventKind::Volume)
                handleVolume(event.state);
            else
                handleName(event);
        }
        const uint32_t lost = s_eventsLost.exchange(0);
        if (lost)
            log_w("MELDUNGEN %lu verworfen, Queue voll", static_cast<unsigned long>(lost));
    }

    /** @brief Beendet Suche, Versuche und Verbindung. Meldet SUCHE ENDE bzw. GETRENNT, wenn etwas lief. */
    void stopActivity()
    {
        s_connectAfterSearch = false;
        if (s_state == LinkState::Searching)
        {
            esp_bt_gap_cancel_discovery();
            finishSearch("Befehl");
        }
        if (s_state == LinkState::Connected || s_attemptActive)
            s_source.disconnect();
        if (s_state == LinkState::Connecting || s_state == LinkState::Connected)
            endLink("Befehl");
    }

    /** @brief Führt TRENNE oder VERGISS aus. VERGISS löscht bei bestehender Verbindung oder laufendem Versuch erst nach der Meldung der Trennung, damit Bluedroid die Kopplung nicht während des Abbaus entfernt. */
    void performStop(StopKind kind)
    {
        const bool linked = s_state == LinkState::Connected || s_attemptActive;
        // Nach einem gewollten Trennen soll das Gerät nicht von selbst zurückkommen, bis VERBINDE oder ein Neustart es wieder erlaubt.
        s_listenAllowed = false;
        s_startupPending = false;
        stopActivity();
        if (kind != StopKind::Forget)
            return;
        if (!linked)
            finishForget("ohne Verbindung");
        else
        {
            s_forgetPending = true;
            s_forgetAtMs = millis();
            log_i("VERGISS wartet auf die Trennung");
        }
    }

    /** @brief Nimmt TRENNE oder VERGISS an. Läuft gerade ein Medienstart, wartet das Trennen auf dessen Quittung, höchstens MediaStartWaitMs, damit sich Trennbefehl und Medienstart beim Gerät nicht überschneiden. */
    void requestStop(StopKind kind)
    {
        if (s_stopPending != StopKind::None)
        {
            if (kind == StopKind::Forget)
                s_stopPending = kind;
            return;
        }
        if (s_state == LinkState::Connected && s_source.mediaStarting())
        {
            s_stopPending = kind;
            s_stopAtMs = millis();
            log_i("TRENNEN wartet auf den laufenden Medienstart");
            return;
        }
        performStop(kind);
    }

    /** @brief Sendet STATUS: Zustand, dann die Adresse der Gegenstelle bei VERBINDET und VERBUNDEN, sonst des gespeicherten Geräts oder „-“, zuletzt der Name, falls bekannt. */
    void sendStatus()
    {
        const bool peer = s_state == LinkState::Connecting || s_state == LinkState::Connected;
        const uint8_t *address = peer ? s_peer : (s_storedValid ? s_storedAddress : nullptr);
        char addressText[AddressTextSize] = "-", escaped[EscapedNameSize];
        if (address)
            formatAddress(address, addressText);
        const char *name = !address ? "" : (peer ? s_peerName : s_storedName);
        escapeName(name, escaped);
        sendLine("STATUS %s %s%s%s", StateNames[static_cast<size_t>(s_state)], addressText, escaped[0] ? " " : "", escaped);
        // Nach einem Neustart des Tab5 bei bestehender Verbindung erfährt der Drehknopf so die Lautstärke.
        if (s_state == LinkState::Connected)
            sendLine("KOPFHOERER %u", static_cast<unsigned>(s_deviceVolume));
    }

    /**
     * @brief Legt je Eingang aus CountPins einen Flankenzähler an, der steigende und fallende Flanken zählt, und hängt die Eingänge auf Pull-down.
     *
     * Der Pull-down hält die Leitungen auf Low, solange das Tab5 sie nicht treibt, und speist ein ausgeschaltetes Tab5 nicht über dessen Schutzdioden. Der Zählertreiber setzt beim Anlegen einen Pull-up, deshalb folgt der Pull-down danach.
     * @return false, wenn ein Zähler fehlt. Die übrigen bleiben nutzbar
     */
    bool setupCounters()
    {
        bool complete = true;
        for (size_t i = 0; i < CountInputs; ++i)
        {
            pcnt_unit_config_t unitConfig = {};
            unitConfig.low_limit = -1;
            unitConfig.high_limit = CountLimit;
            pcnt_glitch_filter_config_t filter = {};
            filter.max_glitch_ns = CountGlitchNs;
            pcnt_chan_config_t channelConfig = {};
            channelConfig.edge_gpio_num = CountPins[i];
            channelConfig.level_gpio_num = -1;
            pcnt_unit_handle_t unit = nullptr;
            pcnt_channel_handle_t channel = nullptr;
            esp_err_t result = pcnt_new_unit(&unitConfig, &unit);
            if (result == ESP_OK)
                result = pcnt_unit_set_glitch_filter(unit, &filter);
            if (result == ESP_OK)
                result = pcnt_new_channel(unit, &channelConfig, &channel);
            if (result == ESP_OK)
                result = pcnt_channel_set_edge_action(channel, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_INCREASE);
            if (result == ESP_OK)
                result = pcnt_channel_set_level_action(channel, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_KEEP);
            if (result == ESP_OK)
                result = gpio_set_pull_mode(static_cast<gpio_num_t>(CountPins[i]), GPIO_PULLDOWN_ONLY);
            if (result == ESP_OK)
                result = pcnt_unit_enable(unit);
            if (result == ESP_OK)
            {
                s_counters[i] = unit;
                continue;
            }
            log_e("ZAEHLER %s nicht angelegt: %s", CountNames[i], esp_err_to_name(result));
            complete = false;
            if (channel)
                pcnt_del_channel(channel);
            if (unit)
                pcnt_del_unit(unit);
        }
        return complete;
    }

    /** @brief Setzt alle Flankenzähler auf null und startet sie. */
    void startCounting()
    {
        for (pcnt_unit_handle_t unit : s_counters)
        {
            if (!unit)
                continue;
            pcnt_unit_stop(unit);
            pcnt_unit_clear_count(unit);
            pcnt_unit_start(unit);
        }
    }

    /** @brief Hält die Flankenzähler an und liest sie. Ein fehlender oder nicht lesbarer Zähler meldet -1, was das Tab5 als ungültige Antwort wertet. */
    void stopCounting(int (&counts)[CountInputs])
    {
        for (size_t i = 0; i < CountInputs; ++i)
        {
            counts[i] = -1;
            if (s_counters[i] && (pcnt_unit_stop(s_counters[i]) != ESP_OK || pcnt_unit_get_count(s_counters[i], &counts[i]) != ESP_OK))
                counts[i] = -1;
        }
    }

    /** @brief Sendet die UKW-Zeile: Zustand (FEHLT, AUS, START, AN, STUMM), Frequenz in kHz, Leistungsstufe, Rate am Modulator oder „-“, Chip oder „-“. */
    void sendUkwStatus()
    {
        char rate[8] = "-";
        if (s_fm.present() && s_fm.rateSupported())
            snprintf(rate, sizeof(rate), "%lu", static_cast<unsigned long>(s_fm.rate()));
        sendLine("UKW %s %lu %u %s %s", s_fm.stateName(), static_cast<unsigned long>(s_fm.frequency()), static_cast<unsigned>(s_fm.power()), rate, BtFm::name(s_fm.chip()));
    }

    /** @brief Führt den UKW-Modulator nach: Stummwunsch und Quellrate des Tonwegs, dann ein Schritt seiner Zustandsmaschine. Jeder Wechsel des UKW-Zustands (erkannt, verschwunden, an, aus, stumm) geht unaufgefordert als UKW-Zeile an das Tab5. Ohne Modulator bleibt davon nur eine I2C-Abfrage alle 10 s. */
    void serviceFm()
    {
        s_fm.setMuted(s_muted.load(std::memory_order_relaxed));
        s_fm.setRate(s_rateRequest.load());
        s_fm.service(millis());
        const bool changed = s_fm.takePresenceChange();
        if (!changed && strcmp(s_fmState, s_fm.stateName()) == 0)
            return;
        snprintf(s_fmState, sizeof(s_fmState), "%s", s_fm.stateName());
        sendUkwStatus();
    }

    /** @brief Beantwortet eine Befehlszeile des Tab5 (Protokoll in bt-sender/README.md). Jede unbekannte Zeile ergibt FEHLER UNBEKANNT. Suche und Verbindung erst nach dem Startablauf und nicht, solange VERGISS auf die Trennung wartet. UKW- und RDS-Zeilen gelten nur mit erkanntem Modulator, sonst FEHLER KEIN_UKW. */
    void handleLinkLine(const char *line)
    {
        log_i("UART <- %s", line);
        const bool ready = s_started;
        if (strcmp(line, "HALLO") == 0)
            sendLine("HALLO STAND=%s", FirmwareStand);
        else if (strcmp(line, "STATUS") == 0)
            sendStatus();
        else if (strcmp(line, "SUCHE") == 0)
        {
            if (s_state == LinkState::Searching)
                sendLine("SUCHE LAEUFT");
            else if (!ready || s_forgetPending || s_state != LinkState::Idle || !startSearch())
                sendLine("FEHLER BELEGT");
            else
                sendLine("SUCHE LAEUFT");
        }
        else if (strncmp(line, "VERBINDE ", 9) == 0)
        {
            esp_bd_addr_t address;
            if (!parseAddress(line + 9, address))
                sendLine("FEHLER ADRESSE");
            else if (!ready || s_forgetPending || s_state == LinkState::Connecting || s_state == LinkState::Connected)
                sendLine("FEHLER BELEGT");
            else if (s_state == LinkState::Searching)
            {
                s_listenAllowed = true;
                s_startupPending = false;
                s_connectAfterSearch = true;
                memcpy(s_afterSearchAddress, address, ESP_BD_ADDR_LEN);
                esp_bt_gap_cancel_discovery();
                sendLine("VERBINDE LAEUFT");
            }
            else
            {
                s_listenAllowed = true;
                s_startupPending = false;
                beginConnect(address, knownName(address), CommandAttempts);
                sendLine("VERBINDE LAEUFT");
            }
        }
        else if (strcmp(line, "TRENNE") == 0)
        {
            sendLine("TRENNE OK");
            requestStop(StopKind::Disconnect);
        }
        else if (strcmp(line, "VERGISS") == 0)
        {
            if (s_forgetPending || s_stopPending == StopKind::Forget)
                sendLine("FEHLER BELEGT");
            else
                requestStop(StopKind::Forget);
        }
        else if (strcmp(line, "TESTTON") == 0)
        {
            if (s_state != LinkState::Connected)
                sendLine("FEHLER NICHT_VERBUNDEN");
            else
            {
                s_toneCommand.store(ToneStart);
                s_toneRunning = true;
                s_toneStartMs = millis();
                log_i("TESTTON %.0f Hz, Amplitude %.0f %%, %lu s", ToneFrequency, ToneAmplitude * 100.0f, static_cast<unsigned long>(TestToneSeconds));
                sendLine("TESTTON LAEUFT");
            }
        }
        else if (strcmp(line, "STUMM AN") == 0)
        {
            s_muted.store(true);
            sendLine("STUMM AN");
        }
        else if (strcmp(line, "STUMM AUS") == 0)
        {
            s_muted.store(false);
            sendLine("STUMM AUS");
        }
        else if (strncmp(line, "RATE ", 5) == 0)
        {
            const char *digits = line + 5;
            uint32_t rate = 0;
            size_t used = 0;
            for (; digits[used] >= '0' && digits[used] <= '9' && used < 6; ++used)
                rate = rate * 10 + static_cast<uint32_t>(digits[used] - '0');
            if (used == 0 || digits[used] != 0 || rate < BtAudio::MinInputRate || rate > BtAudio::MaxInputRate)
                sendLine("FEHLER RATE");
            else
            {
                s_rateRequest.store(rate);
                sendLine("RATE %lu", static_cast<unsigned long>(rate));
            }
        }
        else if (strncmp(line, "LAUTSTAERKE ", 12) == 0)
        {
            const char *digits = line + 12;
            unsigned volume = 0;
            size_t used = 0;
            for (; digits[used] >= '0' && digits[used] <= '9' && used < 3; ++used)
                volume = volume * 10 + static_cast<unsigned>(digits[used] - '0');
            if (used == 0 || digits[used] != 0 || volume > AvrcpMaxVolume)
                sendLine("FEHLER LAUTSTAERKE");
            else
            {
                changeVolume(static_cast<uint8_t>(volume));
                s_lastSetVolume = static_cast<int>(volume);
                s_lastSetAtMs = millis();
                s_volumePending = s_state == LinkState::Connected;
                sendLine("LAUTSTAERKE %u", volume);
            }
        }
        else if (strcmp(line, "ZAEHLEN") == 0)
        {
            startCounting();
            sendLine("ZAEHLEN LAEUFT");
        }
        else if (strcmp(line, "ERGEBNIS") == 0)
        {
            int counts[CountInputs];
            stopCounting(counts);
            sendLine("ERGEBNIS %s=%d %s=%d %s=%d", CountNames[0], counts[0], CountNames[1], counts[1], CountNames[2], counts[2]);
        }
        else if (strcmp(line, "UKW") == 0)
            sendUkwStatus();
        else if (strcmp(line, "UKW AN") == 0 || strcmp(line, "UKW AUS") == 0)
        {
            if (!s_fm.present())
                sendLine("FEHLER KEIN_UKW");
            else
            {
                s_fm.setEnabled(strcmp(line, "UKW AN") == 0);
                sendLine("%s", line);
            }
        }
        else if (strncmp(line, "UKW FREQ ", 9) == 0)
        {
            uint32_t khz = 0;
            if (!readNumber(line + 9, 6, BtFm::MaxKhz, khz) || !BtFm::validKhz(khz))
                sendLine("FEHLER FREQ");
            else if (!s_fm.present())
                sendLine("FEHLER KEIN_UKW");
            else
            {
                s_fm.setFrequency(khz);
                sendLine("UKW FREQ %lu", static_cast<unsigned long>(khz));
            }
        }
        else if (strncmp(line, "UKW LEISTUNG ", 13) == 0)
        {
            uint32_t power = 0;
            if (!readNumber(line + 13, 2, BtFm::MaxPower, power))
                sendLine("FEHLER LEISTUNG");
            else if (!s_fm.present())
                sendLine("FEHLER KEIN_UKW");
            else
            {
                s_fm.setPower(static_cast<uint8_t>(power));
                sendLine("UKW LEISTUNG %lu", static_cast<unsigned long>(power));
            }
        }
        else if ((strncmp(line, "RDS PS", 6) == 0 || strncmp(line, "RDS RT", 6) == 0) && (line[6] == 0 || line[6] == ' '))
        {
            // Ohne Text hinter dem Schlüsselwort wird der Text gelöscht.
            const bool ps = line[4] == 'P';
            if (!decodeText(line[6] ? line + 7 : line + 6, s_fmText))
                sendLine("FEHLER TEXT");
            else if (!s_fm.present())
                sendLine("FEHLER KEIN_UKW");
            else
            {
                if (ps ? s_fm.setPs(s_fmText) : s_fm.setRt(s_fmText))
                    log_i("RDS %s \"%s\"", ps ? "PS" : "RT", s_fmText);
                sendLine("RDS %s OK", ps ? "PS" : "RT");
            }
        }
        else
            sendLine("FEHLER UNBEKANNT");
    }

    /**
     * @brief Liest die Bytes des Tab5 und beantwortet jede vollständige Zeile.
     *
     * CR wird übergangen. Leere, zu lange und Zeilen mit Steuerzeichen werden ohne Antwort verworfen. Solange das Tab5 G6 nicht treibt, liegt der Empfang auf Low. Die UART meldet das als Nullbyte, das nur die laufende Zeile verwirft.
     */
    void serviceLink()
    {
        while (Serial1.available() > 0)
        {
            const int byte = Serial1.read();
            if (byte == '\r')
                continue;
            if (byte == '\n')
            {
                s_linkLine[s_linkUsed] = 0;
                if (!s_linkInvalid && s_linkUsed > 0)
                    handleLinkLine(s_linkLine);
                s_linkUsed = 0;
                s_linkInvalid = false;
            }
            else if (byte < 0x20 || byte > 0x7e || s_linkUsed >= LinkMaxLine)
                s_linkInvalid = true;
            else
                s_linkLine[s_linkUsed++] = static_cast<char>(byte);
        }
    }

    /** @brief Startablauf, sobald die Quelle eingerichtet ist, dann Verbindungsversuche und die Höchstdauer der Suche. */
    void serviceTimers()
    {
        const uint32_t now = millis();
        if (!s_started)
        {
            if (!s_stackReady.load())
                return;
            s_started = true;
            if (s_storedValid)
            {
                s_startupPending = true;
                s_startupDueMs = now + StartupListenMs;
                log_i("START %lu s ansprechbar für das gespeicherte Gerät, dann bis zu %d Versuche", static_cast<unsigned long>(StartupListenMs / 1000), StartupAttempts);
            }
            else
                log_i("ZUSTAND STILL, kein gespeichertes Gerät");
        }
        if (s_startupPending && static_cast<int32_t>(now - s_startupDueMs) >= 0)
        {
            s_startupPending = false;
            if (s_state == LinkState::Idle && s_storedValid)
                beginConnect(s_storedAddress, s_storedName, StartupAttempts);
        }
        serviceConnect(now);
        updateListening();
        if (s_stopPending != StopKind::None && (!s_source.mediaStarting() || now - s_stopAtMs >= MediaStartWaitMs))
        {
            const StopKind kind = s_stopPending;
            s_stopPending = StopKind::None;
            log_i("TRENNEN nach %lu ms Warten auf den Medienstart", static_cast<unsigned long>(now - s_stopAtMs));
            performStop(kind);
        }
        if (s_forgetPending && now - s_forgetAtMs >= ForgetWaitMs)
            finishForget("Zeitlimit");
        if (s_state == LinkState::Searching && static_cast<int32_t>(now - s_searchEndMs) >= 0)
        {
            esp_bt_gap_cancel_discovery();
            finishSearch("Höchstdauer");
        }
    }

    /** @brief Loggt das Ende des Testtons aus dem Zähler des Datencallbacks. */
    void logTone()
    {
        if (!s_toneRunning || s_toneCommand.load() != ToneNone || s_tonePosition.load(std::memory_order_relaxed) < TestToneFrames)
            return;
        s_toneRunning = false;
        log_i("TESTTON beendet nach %lu ms", static_cast<unsigned long>(millis() - s_toneStartMs));
    }

    void logOptical(float windowUs);

    /** @brief Pegel in dB bezogen auf die Vollaussteuerung von 16 Bit, -99 für Stille. */
    int peakDb(int32_t peak)
    {
        return peak > 0 ? static_cast<int>(lroundf(20.0f * log10f(static_cast<float>(peak) / 32767.0f))) : -99;
    }

    /** @brief Loggt alle AudioStatsMs eine AUDIO-Zeile, solange Ton ankommt oder ein Gerät verbunden ist: Quellrate laut RATE und gemessen, Füllstand, Korrektur, Unterläufe, Neustarts, größte Abfrage, Rechenlast beider Seiten, Spitzenpegel, Stumm, UKW-Zustand und freier Heap. */
    void logAudio()
    {
        const uint32_t now = millis();
        if (now - s_statsAtMs < AudioStatsMs)
            return;
        const uint32_t elapsed = now - s_statsAtMs, frames = s_inFrames.load(), convertUs = s_convertUs.load(), renderUs = s_renderUs.load();
        const uint32_t received = frames - s_statsFrames;
        const float windowUs = static_cast<float>(elapsed) * 1000.0f;
        s_statsAtMs = now;
        s_statsFrames = frames;
        const float convertLoad = static_cast<float>(convertUs - s_statsConvertUs) * 100.0f / windowUs, renderLoad = static_cast<float>(renderUs - s_statsRenderUs) * 100.0f / windowUs;
        s_statsConvertUs = convertUs;
        s_statsRenderUs = renderUs;
        const uint32_t low = s_fillLow.exchange(UINT32_MAX), high = s_fillHigh.exchange(0), request = s_requestMax.exchange(0);
        const int32_t left = s_peakLeft.exchange(0), right = s_peakRight.exchange(0);
        if (received == 0 && s_state != LinkState::Connected)
        {
            s_opticalConvertUs.store(0);
            s_opticalRenderUs.store(0);
            return;
        }
        log_i("AUDIO Rate=%lu gemessen=%lu Füllstand=%lu..%lu Korrektur=%+ld ppm gelernt=%+ld ppm Unterläufe=%lu Neustarts=%lu Abfrage=%lu Last=%.1f %% Callback=%.1f %% Pegel=%d/%d dB Stumm=%u UKW=%s Heap=%lu/%lu",
              static_cast<unsigned long>(s_rateRequest.load()), static_cast<unsigned long>(static_cast<uint64_t>(received) * 1000 / elapsed), static_cast<unsigned long>(low == UINT32_MAX ? 0 : low), static_cast<unsigned long>(high), static_cast<long>(s_correctionPpm.load()), static_cast<long>(lroundf(s_drift.learned())), static_cast<unsigned long>(s_output.underruns()), static_cast<unsigned long>(s_output.restarts()), static_cast<unsigned long>(request), convertLoad, renderLoad, peakDb(left), peakDb(right), s_muted.load(), s_fm.stateName(), static_cast<unsigned long>(ESP.getFreeHeap()), static_cast<unsigned long>(ESP.getMinFreeHeap()));
        if (s_optical)
            logOptical(windowUs);
    }

    /** @brief Loggt mit der AUDIO-Zeile eine OPTISCH-Zeile: Rate am optischen Ausgang, Füllstand, Korrektur, Unterläufe, Neustarts, Rechenlast der Umrechnung (I2S-Task) und der Ausgabe (optischer Task) sowie Schreibfehler. @param windowUs Länge des Logfensters in µs. */
    void logOptical(float windowUs)
    {
        const uint32_t convertUs = s_opticalConvertUs.exchange(0), renderUs = s_opticalRenderUs.exchange(0);
        const uint32_t low = s_opticalFillLow.exchange(UINT32_MAX), high = s_opticalFillHigh.exchange(0);
        log_i("OPTISCH Rate=%lu Füllstand=%lu..%lu Korrektur=%+ld ppm gelernt=%+ld ppm Unterläufe=%lu Neustarts=%lu Last=%.1f %% Ausgabe=%.1f %% Schreibfehler=%lu",
              static_cast<unsigned long>(s_opticalRate.load()), static_cast<unsigned long>(low == UINT32_MAX ? 0 : low), static_cast<unsigned long>(high), static_cast<long>(s_opticalCorrectionPpm.load()), static_cast<long>(lroundf(s_opticalDrift.learned())), static_cast<unsigned long>(s_opticalOutput.underruns()), static_cast<unsigned long>(s_opticalOutput.restarts()), static_cast<float>(convertUs) * 100.0f / windowUs, static_cast<float>(renderUs) * 100.0f / windowUs, static_cast<unsigned long>(s_opticalWriteErrors.load()));
    }

    /** @brief Loggt alle OperationLogMs eine BETRIEB-Zeile, auch in Ruhe: Laufzeit, Zustand, UKW-Zustand mit RDS-Gruppen, freier Heap mit Minimum und größtem Block sowie die Stackreserve von loop(), I2S-Task und optischem Task in Byte. Ein schleichender Verlust fällt im Dauerbetrieb daran auf. */
    void logOperation()
    {
        const uint32_t now = millis();
        if (s_operationAtMs && now - s_operationAtMs < OperationLogMs)
            return;
        s_operationAtMs = now;
        log_i("BETRIEB Laufzeit=%lld s Zustand=%s UKW=%s RDS=%lu/%lu Heap=%lu/%lu Block=%lu Stack=%u/%u/%u", static_cast<long long>(esp_timer_get_time() / 1000000), StateNames[static_cast<size_t>(s_state)], s_fm.stateName(), static_cast<unsigned long>(s_fm.rdsGroups()), static_cast<unsigned long>(s_fm.rdsTimeouts()), static_cast<unsigned long>(ESP.getFreeHeap()), static_cast<unsigned long>(ESP.getMinFreeHeap()), static_cast<unsigned long>(ESP.getMaxAllocHeap()), static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)), static_cast<unsigned>(s_audioTask ? uxTaskGetStackHighWaterMark(s_audioTask) : 0), static_cast<unsigned>(s_opticalTask ? uxTaskGetStackHighWaterMark(s_opticalTask) : 0));
    }

    /** @brief Sendet s_deviceVolume per AVRCP, nach jeder Verbindung und nach LAUTSTAERKE, frühestens AvrcpDelayMs nach der Verbindung, höchstens alle VolumeGapMs und erst nach der Antwort auf den vorigen Befehl (oder VolumeAnswerMs ohne Antwort). Speichert die Lautstärke, wenn sie VolumeSaveMs unverändert blieb. */
    void sendPendingVolume()
    {
        const uint32_t now = millis();
        if (s_volumeDirty && now - s_volumeChangedAtMs >= VolumeSaveMs && !saveVolume())
            log_w("LAUTSTAERKE nicht gespeichert");
        if (s_volumeOutstanding && (s_volumeAnswered.load() || now - s_volumeSentAtMs >= VolumeAnswerMs))
            s_volumeOutstanding = false;
        if (!s_volumePending || s_volumeOutstanding || now - s_connectedAtMs < AvrcpDelayMs || now - s_volumeSentAtMs < VolumeGapMs)
            return;
        s_volumePending = false;
        s_volumeSentAtMs = now;
        s_volumeOutstanding = true;
        s_volumeAnswered.store(false);
        const esp_err_t result = esp_avrc_ct_send_set_absolute_volume_cmd(AvrcpLabel, s_deviceVolume);
        if (result == ESP_OK)
            log_i("AVRCP Lautstärke %u von %u gesendet, die Antwort des Geräts folgt als \"Set absolute volume response\"", static_cast<unsigned>(s_deviceVolume), static_cast<unsigned>(AvrcpMaxVolume));
        else
            log_w("AVRCP Lautstärke nicht gesendet: %s", esp_err_to_name(result));
    }
}

void setup()
{
    Serial.begin(SerialBaud);
    // ESP-IDF liefert die Revision als MXX, Hauptversion mal 100 plus Unterversion.
    const unsigned revision = ESP.getChipRevision();
    log_i("START RetroRadio Bluetooth-Sender, Stand %s, gebaut %s %s, Neustartgrund %s: Steuerung über UART, Funk nur auf Befehl, Ton vom Tab5 über I2S", FirmwareStand, __DATE__, __TIME__, resetReasonText());
    log_i("CHIP %s v%u.%u, %lu MHz, Flash %u KiB", ESP.getChipModel(), revision / 100, revision % 100, static_cast<unsigned long>(getCpuFrequencyMhz()), static_cast<unsigned>(ESP.getFlashChipSize() / 1024));

    s_events = xQueueCreate(EventSlots, sizeof(LinkEvent));
    if (!s_events)
        log_e("START Queue nicht angelegt, Bluetooth-Meldungen gehen verloren");
    Serial1.begin(LinkBaud, SERIAL_8N1, LinkRx, LinkTx);
    // Pull-down unabhängig von der Vorgabe der UART: Solange das Tab5 G6 nicht treibt, bleibt der Empfang Low, speist ein ausgeschaltetes Tab5 nicht und lässt das Tab5 vertauschte UART-Leitungen am Ruhepegel erkennen.
    gpio_set_pull_mode(static_cast<gpio_num_t>(LinkRx), GPIO_PULLDOWN_ONLY);
    const bool counters = setupCounters();
    log_i("UART %u Baud, RX G%d, TX G%d. Flankenzähler an %s, %s, %s %s", static_cast<unsigned>(LinkBaud), LinkRx, LinkTx, CountNames[0], CountNames[1], CountNames[2], counters ? "bereit" : "unvollständig");
    const uint32_t opticalRate = BtAudio::opticalRate(DefaultInputRate);
    s_converter.configure(DefaultInputRate);
    s_opticalConverter.configure(DefaultInputRate, opticalRate);
    s_opticalRate.store(opticalRate);
    if (!setupI2s())
        log_e("I2S Empfang nicht eingerichtet, Ton vom Tab5 fehlt");
    else if (xTaskCreatePinnedToCore(audioTask, "i2s", AudioTaskStack, nullptr, AudioTaskPriority, &s_audioTask, AudioTaskCore) != pdPASS)
        log_e("I2S Task nicht angelegt, Ton vom Tab5 fehlt");
    else
        log_i("I2S Slave BCLK G%d, WS G%d, Daten G%d, Philips 32 Bit Stereo bis %lu Hz. Ring %lu Frames, Soll %lu, Filter %u Stützstellen", I2sBclk, I2sWs, I2sData, static_cast<unsigned long>(BtAudio::MaxInputRate), static_cast<unsigned long>(BtAudio::RingFrames), static_cast<unsigned long>(BtAudio::TargetFrames), static_cast<unsigned>(BtAudio::Taps));
    if (!setupOptical(opticalRate))
        log_e("OPTISCH I2S1 nicht eingerichtet, kein S/PDIF an G%d", OpticalPin);
    else if (xTaskCreatePinnedToCore(opticalTask, "spdif", OpticalTaskStack, nullptr, AudioTaskPriority, &s_opticalTask, AudioTaskCore) != pdPASS)
        log_e("OPTISCH Task nicht angelegt, kein S/PDIF an G%d", OpticalPin);
    else
        log_i("OPTISCH S/PDIF an G%d, %lu bis %lu Hz. Ring %lu Frames, Soll %lu, Filter %u Stützstellen", OpticalPin, static_cast<unsigned long>(BtAudio::OpticalMinRate), static_cast<unsigned long>(BtAudio::OpticalMaxRate), static_cast<unsigned long>(BtAudio::OpticalRingFrames), static_cast<unsigned long>(BtAudio::OpticalTargetFrames), static_cast<unsigned>(BtAudio::OpticalTaps));

    // Der UKW-Modulator meldet sich im ersten loop()-Durchlauf (BtFm::Modulator::service), ohne Baugruppe bleibt es bei einer Startmeldung und einem Adressbyte je Kandidat alle 10 s.
    if (!s_fmBus.begin())
        log_e("UKW I2C an G%d/G%d nicht eingerichtet", FmSda, FmScl);
    fillSineTable();
    // Die Bluetooth-Adresse ist die Basisadresse des Chips plus 2 und schon vor dem Start des Stacks lesbar.
    uint8_t mac[ESP_BD_ADDR_LEN] = {};
    esp_read_mac(mac, ESP_MAC_BT);
    snprintf(s_localName, sizeof(s_localName), "%s%02X%02X%02X", LocalNamePrefix, mac[3], mac[4], mac[5]);
    s_source.set_local_name(s_localName);
    s_source.set_auto_reconnect(false);
    s_source.set_on_audio_state_changed(onAudioState);
    s_source.set_volume_control(&s_fixedVolume);
    s_source.set_data_callback_in_frames(fillFrames);
    s_source.start();
    loadStored();
    logStoredPairing();
    log_i("SPEICHER Heap frei %u Byte, größter Block %u Byte", static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMaxAllocHeap()));
    // Bleibt loop() hängen, startet der Wachhund des Kerns den Pico neu, statt dass er stumm am Bus hängt. loop() meldet sich alle LoopPauseMs.
    enableLoopWDT();
    log_i("WACHHUND %u s für loop() und I2S-Task", static_cast<unsigned>(CONFIG_ESP_TASK_WDT_TIMEOUT_S));
}

void loop()
{
    feedLoopWDT();
    serviceLink();
    serviceEvents();
    serviceTimers();
    serviceFm();
    logTone();
    sendPendingVolume();
    logAudio();
    logOperation();
    delay(LoopPauseMs);
}
