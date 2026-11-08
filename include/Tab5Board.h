#ifndef TAB5_BOARD_H
#define TAB5_BOARD_H

#include <cstddef>
#include <cstdint>

namespace Tab5Board
{
    constexpr uint8_t I2cSda = 31;                 ///< SDA des internen I2C-Busses (GPIO)
    constexpr uint8_t I2cScl = 32;                 ///< SCL des internen I2C-Busses (GPIO)
    constexpr uint8_t CodecAddress = 0x10;         ///< 7-Bit-I2C-Adresse des ES8388
    constexpr uint8_t ExpanderAddress = 0x43;      ///< PI4IOE5V6408 E1 mit dem Lautsprecherverstärker
    constexpr uint8_t Expander2Address = 0x44;     ///< PI4IOE5V6408 E2 mit WLAN-Versorgung und USB-A, die Firmware liest ihn nur zur Diagnose
    constexpr uint8_t SpeakerMask = 1U << 1;       ///< E1.P1: Lautsprecherverstärker, High = freigegeben
    constexpr uint8_t BusPowerMask = 1U << 2;      ///< E1.P2: EXT5V_EN, Lastschalter der 5 V an M5-Bus 28, Port A und der 2x5-Leiste. Eingang, Pull-up = an, Pull-down = aus
    constexpr uint8_t HeadphoneMask = 1U << 7;     ///< E1.P7: Kopfhörer-Erkennung der 3,5-mm-Buchse, Eingang ohne internen Pull, High = Stecker steckt
    constexpr uint8_t ExpanderDirectionRegister = 0x03;     ///< PI4IOE5V6408-Richtung, 1 = Ausgang
    constexpr uint8_t ExpanderOutputRegister = 0x05;        ///< PI4IOE5V6408-Ausgangslatch
    constexpr uint8_t ExpanderHighImpedanceRegister = 0x07; ///< PI4IOE5V6408-Ausgang hochohmig, 1 = hochohmig
    constexpr uint8_t ExpanderPullEnableRegister = 0x0b;    ///< PI4IOE5V6408-Pull-Freigabe, 1 = Pull aktiv
    constexpr uint8_t ExpanderPullSelectRegister = 0x0d;    ///< PI4IOE5V6408-Pull-Auswahl, 1 = Pull-up, 0 = Pull-down
    constexpr uint8_t ExpanderInputRegister = 0x0f;         ///< PI4IOE5V6408-Eingangszustand, nur lesen. Zeigt auch bei Eingängen mit Pull den Pegel am Pin
    constexpr uint32_t HeadphonePollMs = 50;       ///< Abstand der Abfragen von E1.P7 im UI-Task
    constexpr uint8_t HeadphoneStableReads = 3;    ///< So viele gleiche Abfragen in Folge übernehmen einen neuen Steckerzustand
    constexpr uint8_t CodecMuteRegister = 0x19;    ///< ES8388-Register mit dem DAC-Mute-Bit
    constexpr uint8_t DacWordLength = 0x20;        ///< ES8388 DACCONTROL1 (0x17): Philips-I2S, 32-Bit-Wort in den 32-Bit-Slots von audio-lite. 16 Bit (0x18) und 24 Bit (0x00) wurden gegen den stummen Start geprüft und änderten nichts. Entscheidend ist der Reset der DAC-Zustandsmaschine (0x02 Bit 4) am Ende der Startfolge
    constexpr uint8_t CodecMuteMask = 1U << 2;     ///< DAC-Mute im Register 0x19
    constexpr uint32_t I2cFrequency = 400000;      ///< Bustakt für Codec und Expander in Hz
    constexpr uint8_t I2sMclk = 30;                ///< I2S-Mastertakt (GPIO)
    constexpr uint8_t I2sBclk = 27;                ///< I2S-Bittakt (GPIO)
    constexpr uint8_t I2sLrck = 29;                ///< I2S-Wortauswahl (GPIO)
    constexpr uint8_t I2sData = 26;                ///< I2S-Daten zum Codec (GPIO)
    constexpr uint8_t BtUartTx = 6;                ///< UART zum Bluetooth-Sender, M5-Bus 16 -> Pico G25
    constexpr uint8_t BtUartRx = 7;                ///< UART vom Bluetooth-Sender, M5-Bus 15 <- Pico G22
    constexpr uint8_t BtBclk = 16;                 ///< I2S-Bittakt zum Bluetooth-Sender, M5-Bus 2 -> Pico G18
    constexpr uint8_t BtWs = 17;                   ///< I2S-Wortauswahl zum Bluetooth-Sender, M5-Bus 4 -> Pico G19
    constexpr uint8_t BtData = 18;                 ///< I2S-Daten zum Bluetooth-Sender, M5-Bus 7 -> Pico G21

    /** @brief Schaltet vor der Displayinitialisierung den Verstärker aus und den ES8388 stumm. @return true bei bestätigten I2C-Zugriffen. */
    bool silenceBeforeDisplay();

    /** @brief Prüft einen eigenen PSRAM-Puffer mit zwei Mustern und gibt ihn wieder frei. @param bytes Größe des Prüfpuffers in Bytes, durch vier teilbar. @return true bei vollständig erfolgreicher Prüfung. */
    bool testPsram(size_t bytes);

    /** @brief Fährt den ES8388 vollständig herunter und initialisiert ihn für audio-lite mit 32-Bit-I2S und ausgeschaltetem Verstärker. I2S-Takte müssen bereits laufen, M5.Speaker darf nicht laufen. Nur zur Audioinitialisierung aus dem UI-Task aufrufen. @return true bei bestätigter Konfiguration. */
    bool prepareRadioCodec();

    /** @brief Fährt den ES8388 bei noch laufenden I2S-Takten vollständig herunter, bevor die Quelle ihre Takte ändert oder stoppt. Verstärker bleibt aus. M5.Speaker darf nicht laufen. Bis zu drei Versuche, zusammen schlimmstenfalls einige Millisekunden. @return true bei bestätigtem Power-down. */
    bool powerDownRadioCodec();

    /** @brief Schaltet ausschließlich Codec und Verstärker stumm, ohne I2S zu beenden. Nur aus dem UI-Task aufrufen. @return true bei bestätigten I2C-Zugriffen. */
    bool muteRadio();

    /** @brief Gibt den vorbereiteten Codec frei und den Verstärker nur bei freier Buchse. Der Aufrufer muss frisches, leises PCM sicherstellen. @param speaker false lässt den Lautsprecherverstärker aus, etwa bei gestecktem Kopfhörer. @return true bei bestätigter Freigabe. */
    bool unmuteRadio(bool speaker);

    /** @brief Schaltet nur den Lautsprecherverstärker E1.P1 per Lesen, Ändern, Schreiben. Codec und übrige Expanderbits bleiben unverändert. Nur aus dem UI-Task aufrufen. @param on true schaltet ihn ein. @return true bei bestätigtem I2C-Zugriff. */
    bool setSpeaker(bool on);

    /** @brief Liest den Lautsprecherverstärker E1.P1 aus dem Ausgangslatch. @param on Erhält den Zustand, nur bei Erfolg gültig. @return true bei bestätigtem I2C-Zugriff. */
    bool readSpeaker(bool &on);

    /** @brief Liest die Kopfhörer-Erkennung E1.P7 einmal, ohne Expanderregister zu schreiben. @param inserted Erhält true bei gestecktem Stecker, nur bei Erfolg gültig. @return true bei bestätigtem I2C-Zugriff. */
    bool readHeadphone(bool &inserted);

    /** @brief Register eines PI4IOE5V6408 zur Diagnose. */
    struct ExpanderRegisters
    {
        uint8_t direction = 0;     ///< Register 0x03
        uint8_t output = 0;        ///< Register 0x05
        uint8_t highImpedance = 0; ///< Register 0x07
        uint8_t pullEnable = 0;    ///< Register 0x0b
        uint8_t pullSelect = 0;    ///< Register 0x0d
        uint8_t input = 0;         ///< Register 0x0f, Pegel an den Pins
    };

    /** @brief Liest die sechs Register eines Expanders, ohne zu schreiben. @param address ExpanderAddress (E1) oder Expander2Address (E2). @param out Erhält die Werte, nur bei Erfolg vollständig. @return true bei bestätigten I2C-Zugriffen. */
    bool readExpander(uint8_t address, ExpanderRegisters &out);

    /** @brief Liest ein ES8388-Register zur Diagnose, ohne zu schreiben. @param value Erhält den Wert, nur bei Erfolg gültig. @return true bei bestätigtem I2C-Zugriff. */
    bool readCodecRegister(uint8_t address, uint8_t &value);

    /** @brief Schreibt zur Diagnose ein ES8388-Register und liest es zurück. Nur für USB-Versuche, ändert keinen gespeicherten Zustand. @return true, wenn der Rückleser den Wert bestätigt. */
    bool writeCodecRegister(uint8_t address, uint8_t value);

    /**
     * @brief Schaltet die 5 V an M5-Bus 28 über EXT5V_EN (E1.P2), wie M5Unified setExtOutput(on, ext_PA): an = Pull-up und Latch 1, aus = Pull-down und Latch 0.
     *
     * Ändert per Lesen, Ändern, Schreiben nur Bit 2 von Pull-Auswahl, Pull-Freigabe und Ausgangslatch. Die Richtung bleibt Eingang, wie M5Unified Power.begin sie setzt. Alle übrigen Bits von E1 und ganz E2 (USB-A an E2.P3) bleiben unverändert. Nur aus dem UI-Task aufrufen.
     * @param on true schaltet ein.
     * @return true bei bestätigten I2C-Zugriffen.
     */
    bool setBusPower(bool on);

    /** @brief Ob EXT5V_EN laut gelesener E1-Register freigeben soll: als treibender Ausgang das Latch, sonst ein aktiver Pull-up. Den tatsächlichen Pegel zeigt ExpanderRegisters::input. */
    inline bool busPowerSelected(const ExpanderRegisters &e1)
    {
        if ((e1.direction & BusPowerMask) && !(e1.highImpedance & BusPowerMask))
            return e1.output & BusPowerMask;
        return (e1.pullEnable & BusPowerMask) && (e1.pullSelect & BusPowerMask);
    }

    /** @brief Entprellt die Kopfhörer-Erkennung aus zyklischen Abfragen im UI-Task, ohne zu warten. Bis zur ersten Bestätigung gilt die Buchse als frei, der Lautsprecher arbeitet dann wie ohne Erkennung. */
    class HeadphoneDebounce
    {
    public:
        /** @brief Prüft, ob die nächste Abfrage fällig ist. Überlaufsicher. @param now millis(). */
        bool due(uint32_t now) const
        {
            return static_cast<int32_t>(now - m_nextPoll) >= 0;
        }

        /** @brief Übernimmt eine Abfrage und plant die nächste. Ein abweichender Zustand gilt erst nach HeadphoneStableReads gleichen Abfragen in Folge. Ein Lesefehler ändert nichts und beginnt die Zählung neu. @param now millis(). @param read I2C-Lesen bestätigt. @param inserted Gelesener Steckerzustand. @return true, wenn sich der entprellte Zustand geändert hat. */
        bool update(uint32_t now, bool read, bool inserted)
        {
            m_nextPoll = now + HeadphonePollMs;
            if (!read || inserted == m_inserted)
            {
                m_count = 0;
                return false;
            }
            if (++m_count < HeadphoneStableReads)
                return false;
            m_count = 0;
            m_inserted = inserted;
            return true;
        }

        /** @brief Entprellter Steckerzustand. */
        bool inserted() const
        {
            return m_inserted;
        }

    private:
        uint32_t m_nextPoll = 0; ///< millis() der nächsten fälligen Abfrage
        uint8_t m_count = 0;     ///< Gleiche abweichende Abfragen in Folge
        bool m_inserted = false; ///< Entprellter Zustand, true bei gestecktem Stecker
    };
}

#endif
