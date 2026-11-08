#ifndef BT_PROTOCOL_H
#define BT_PROTOCOL_H

#include "Tab5Board.h"
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

/** @brief Zeilenprotokoll zum Bluetooth-Sender (Pico): Steuerung, Tonweg, Leitungstest, UKW-Modulator und Nachführen des Pico-Zustands, ohne Hardwarezugriff und per Hosttest geprüft. Protokoll in bt-sender/README.md, Verdrahtung in docs/Bluetooth-Sender.md, die Gegenseite in bt-sender/src/main.cpp. */
namespace BtProtocol
{
    constexpr uint32_t Baud = 115200;                     ///< 8N1 ohne Flusssteuerung, wie im Pico
    constexpr size_t MaxLine = 127;                       ///< Längste Zeile ohne Zeilenende in Byte, in beide Richtungen. Längere oder mit Steuerzeichen werden verworfen
    constexpr uint32_t ReplyTimeoutMs = 300;              ///< Wartezeit auf eine Antwort, der Pico bedient die UART alle 5 ms
    constexpr const char *Hello = "HALLO";                ///< Tab5 -> Pico, Antwort HelloReply mit dem Firmwarestand
    constexpr const char *HelloReply = "HALLO STAND=";    ///< Anfang der Antwort auf Hello, danach der Firmwarestand ohne Leerzeichen
    constexpr const char *Count = "ZAEHLEN";              ///< Tab5 -> Pico: Flankenzähler auf null setzen und starten
    constexpr const char *CountReply = "ZAEHLEN LAEUFT";  ///< Antwort auf Count
    constexpr const char *Result = "ERGEBNIS";            ///< Tab5 -> Pico: Flankenzähler anhalten, Antwort „ERGEBNIS G18=<n> G19=<n> G21=<n>“
    constexpr const char *ResultReply = "ERGEBNIS ";      ///< Anfang der Antwort auf Result
    constexpr const char *Search = "SUCHE";               ///< Tab5 -> Pico: Suche mit fester Höchstdauer (rund 10 s), nur im Zustand STILL
    constexpr const char *Connect = "VERBINDE";           ///< Tab5 -> Pico, nach einem Leerzeichen die Adresse. Ein Versuch
    constexpr const char *Disconnect = "TRENNE";          ///< Tab5 -> Pico: Suche, Verbindungsversuche und Verbindung beenden
    constexpr const char *Forget = "VERGISS";             ///< Tab5 -> Pico: wie Disconnect, dazu Kopplungen und gespeicherten Kopfhörer löschen
    constexpr const char *Query = "STATUS";               ///< Tab5 -> Pico: Zustand, Adresse und Name
    constexpr const char *Tone = "TESTTON";               ///< Tab5 -> Pico: 3 s Testton mit 5 % der Vollaussteuerung, nur verbunden
    constexpr const char *MuteOn = "STUMM AN";            ///< Tab5 -> Pico: Ton ausblenden und Stille senden, Antwort gleichlautend. Der Pico beginnt jede Verbindung stumm
    constexpr const char *MuteOff = "STUMM AUS";          ///< Tab5 -> Pico: Ton aus dem I2S-Eingang einblenden, Antwort gleichlautend
    constexpr const char *SetRate = "RATE";               ///< Tab5 -> Pico, nach einem Leerzeichen die Abtastrate der Quelle in Hz. Antwort gleichlautend oder FEHLER RATE
    constexpr uint32_t MinRate = 8000;                    ///< Kleinste Quellrate, wie audio-lite und der Pico
    constexpr uint32_t MaxRate = 96000;                   ///< Größte Quellrate, die der Pico umrechnet
    constexpr const char *Volume = "LAUTSTAERKE";         ///< Tab5 -> Pico, nach einem Leerzeichen die Kopfhörerlautstärke 0..HeadphoneMax für AVRCP. Antwort gleichlautend
    constexpr const char *HeadphoneReport = "KOPFHOERER"; ///< Pico -> Tab5, unaufgefordert: Lautstärke des Kopfhörers nach jeder Verbindung, nach STATUS bei bestehender Verbindung und nach seinen Tasten
    constexpr uint8_t HeadphoneMax = 127;                 ///< Größte absolute AVRCP-Lautstärke
    constexpr uint8_t KnobMax = 100;                      ///< Größte Stellung des Drehknopfs
    constexpr const char *Ukw = "UKW";                    ///< Tab5 -> Pico: UKW-Zeile anfordern (UkwReply), der Pico sendet sie auch unaufgefordert bei jedem Wechsel seines UKW-Zustands
    constexpr const char *UkwOn = "UKW AN";               ///< Tab5 -> Pico: Modulator senden lassen. Antwort gleichlautend, ohne Modulator FEHLER KEIN_UKW
    constexpr const char *UkwOff = "UKW AUS";             ///< Tab5 -> Pico: Sender aus. Antwort gleichlautend, ohne Modulator FEHLER KEIN_UKW
    constexpr const char *UkwFrequency = "UKW FREQ";      ///< Tab5 -> Pico, nach einem Leerzeichen die Frequenz in kHz im Band UkwMinKhz..UkwMaxKhz und Raster UkwStepKhz. Antwort gleichlautend, FEHLER FREQ oder FEHLER KEIN_UKW
    constexpr const char *UkwPower = "UKW LEISTUNG";      ///< Tab5 -> Pico, nach einem Leerzeichen die Leistungsstufe 0..UkwMaxPower. Antwort gleichlautend, FEHLER LEISTUNG oder FEHLER KEIN_UKW
    constexpr const char *RdsPs = "RDS PS";               ///< Tab5 -> Pico, nach einem Leerzeichen der Programmname, kodiert wie Namen, leer löscht ihn. Antwort „RDS PS OK“, FEHLER TEXT oder FEHLER KEIN_UKW
    constexpr const char *RdsRt = "RDS RT";               ///< Tab5 -> Pico, ebenso der Radiotext. Antwort „RDS RT OK“
    constexpr const char *UkwReply = "UKW ";              ///< Anfang der UKW-Zeile „UKW <Zustand> <kHz> <Leistung> <Rate|-> <Chip|->“
    constexpr uint32_t UkwMinKhz = 76000;                 ///< Unteres Bandende des Chips laut Datenblatt
    constexpr uint32_t UkwMaxKhz = 108000;                ///< Oberes Bandende
    constexpr uint32_t UkwStepKhz = 50;                   ///< Kanalraster des Chips
    constexpr uint8_t UkwMaxPower = 15;                   ///< Leistungsstufen 0 (101,5 dBµV) bis 15 (124 dBµV), Schritt 1,5 dB
    constexpr size_t RdsTextBytes = 96;                   ///< Längster RDS-Text, den das Tab5 vorhält, in Byte UTF-8 samt Nullbyte. Der Pico kürzt auf 8 bzw. 64 Zeichen, die Zeile kürzt encodeText()

    /** @brief Frequenz im Band des Chips und in seinem Raster. */
    inline bool validUkwKhz(uint32_t khz)
    {
        return khz >= UkwMinKhz && khz <= UkwMaxKhz && khz % UkwStepKhz == 0;
    }

    /** @brief Rechnet die Stellung des Drehknopfs 0..KnobMax in die Kopfhörerlautstärke 0..HeadphoneMax um, gerundet. fromHeadphone() kehrt das für jede Stellung genau um. */
    inline uint8_t toHeadphone(uint8_t knob)
    {
        return static_cast<uint8_t>((static_cast<unsigned>(knob > KnobMax ? KnobMax : knob) * HeadphoneMax + KnobMax / 2) / KnobMax);
    }

    /** @brief Rechnet eine Kopfhörerlautstärke 0..HeadphoneMax in die Stellung des Drehknopfs um, gerundet. */
    inline uint8_t fromHeadphone(uint8_t level)
    {
        return static_cast<uint8_t>((static_cast<unsigned>(level > HeadphoneMax ? HeadphoneMax : level) * KnobMax + HeadphoneMax / 2) / HeadphoneMax);
    }

    constexpr size_t StandCapacity = 16;                  ///< Firmwarestand bzw. Fehlerkennung bis 15 Zeichen samt Nullbyte
    constexpr size_t MaxCountDigits = 5;                  ///< Der Zähler des Pico reicht bis 32767
    constexpr size_t AddressSize = 18;                    ///< „AA:BB:CC:DD:EE:FF“ samt Nullbyte
    constexpr size_t NameBytes = 30;                      ///< Längster Gerätename in Byte UTF-8, wie im Pico. Der Pico kürzt an einer Zeichengrenze
    constexpr size_t MaxHits = 16;                        ///< Verschiedene Geräte je Suche, wie im Pico
    constexpr size_t LineCount = 3;                       ///< Geprüfte I2S-Leitungen
    constexpr uint16_t TestEdges = 100;                   ///< Flanken je I2S-Leitung. Gerade, damit die Leitung Low beginnt und endet
    constexpr const char *LineNames[LineCount] = {"BCLK", "WS", "DATEN"};                                    ///< Namen in den Logzeilen
    constexpr uint8_t LinePins[LineCount] = {Tab5Board::BtBclk, Tab5Board::BtWs, Tab5Board::BtData};        ///< Tab5-Seite je Leitung
    constexpr const char *PicoInputs[LineCount] = {"G18", "G19", "G21"};                                     ///< Pico-Eingang je Leitung, in dieser Reihenfolge in der ERGEBNIS-Zeile
    constexpr size_t IdleCount = 2 + LineCount;                                                              ///< Leitungen mit Ruhepegelprüfung
    constexpr uint8_t IdlePins[IdleCount] = {Tab5Board::BtUartTx, Tab5Board::BtUartRx, Tab5Board::BtBclk, Tab5Board::BtWs, Tab5Board::BtData}; ///< G6, G7, dann die I2S-Leitungen

    /** @brief Befund einer Leitung. */
    enum class Verdict : uint8_t
    {
        Unchecked, ///< Nicht geprüft, etwa weil eine frühere Prüfung scheiterte
        Ok,        ///< Richtig verbunden
        Missing,   ///< Kein Signal am Pico, bei der UART auch: kein Pico erkannt oder keine Antwort
        Swapped,   ///< Signal kommt an einem anderen Pico-Eingang an bzw. UART-Leitungen getauscht
        Short,     ///< Signal kommt zusätzlich an einem anderen Eingang an oder die Leitung liegt in Ruhe auf High
        Disturbed  ///< Unvollständige oder unerwartete Zählung, wechselnder Ruhepegel oder ungültige Antwort
    };

    /** @brief Name eines Befunds in den Logzeilen, ASCII und klein. */
    inline const char *name(Verdict verdict)
    {
        switch (verdict)
        {
        case Verdict::Ok:
            return "ok";
        case Verdict::Missing:
            return "fehlt";
        case Verdict::Swapped:
            return "vertauscht";
        case Verdict::Short:
            return "kurzschluss";
        case Verdict::Disturbed:
            return "gestoert";
        default:
            return "ungeprueft";
        }
    }

    /** @brief Setzt Zeilen aus einzelnen Bytes zusammen. CR wird übergangen. Leere, zu lange und Zeilen mit Steuerzeichen (auch das Nullbyte einer Leitungsunterbrechung) werden verworfen. */
    class LineReader
    {
    public:
        /** @brief Nimmt ein Byte auf. @return true, wenn damit eine gültige Zeile vollständig ist. Sie steht bis zum nächsten Byte in line(). */
        bool push(char byte)
        {
            if (byte == '\r')
                return false;
            if (byte == '\n')
            {
                m_line[m_used] = 0;
                const bool complete = !m_invalid && m_used > 0;
                m_used = 0;
                m_invalid = false;
                return complete;
            }
            if (byte < 0x20 || byte > 0x7e || m_used >= MaxLine)
                m_invalid = true;
            else
                m_line[m_used++] = byte;
            return false;
        }

        /** @brief Zuletzt vollständige Zeile ohne Zeilenende. */
        const char *line() const
        {
            return m_line;
        }

    private:
        char m_line[MaxLine + 1]{}; ///< Laufende oder zuletzt vollständige Zeile
        size_t m_used = 0;          ///< Belegte Zeichen der laufenden Zeile
        bool m_invalid = false;     ///< Laufende Zeile wird verworfen
    };

    /** @brief Liest den Firmwarestand aus „HALLO STAND=<Stand>“. Der Stand ist 1 bis 15 sichtbare Zeichen ohne Leerzeichen. @return false bei jeder anderen Zeile, stand bleibt dann unverändert. */
    inline bool parseHello(const char *line, char (&stand)[StandCapacity])
    {
        const size_t prefix = strlen(HelloReply);
        if (strncmp(line, HelloReply, prefix) != 0)
            return false;
        const char *text = line + prefix;
        const size_t length = strlen(text);
        if (length == 0 || length >= StandCapacity)
            return false;
        for (size_t i = 0; i < length; ++i)
            if (text[i] <= 0x20 || text[i] > 0x7e)
                return false;
        memcpy(stand, text, length + 1);
        return true;
    }

    /** @brief Liest die Flanken aus „ERGEBNIS G18=<n> G19=<n> G21=<n>“ mit genau einem Leerzeichen zwischen den Feldern und 1 bis MaxCountDigits Ziffern je Wert. @return false bei jeder anderen Zeile, counts ist dann unbestimmt. */
    inline bool parseResult(const char *line, uint32_t (&counts)[LineCount])
    {
        const size_t prefix = strlen(Result);
        if (strncmp(line, Result, prefix) != 0)
            return false;
        const char *p = line + prefix;
        for (size_t i = 0; i < LineCount; ++i)
        {
            const size_t name = strlen(PicoInputs[i]);
            if (p[0] != ' ' || strncmp(p + 1, PicoInputs[i], name) != 0 || p[1 + name] != '=')
                return false;
            p += name + 2;
            uint32_t value = 0;
            size_t digits = 0;
            for (; *p >= '0' && *p <= '9'; ++p)
            {
                if (++digits > MaxCountDigits)
                    return false;
                value = value * 10 + static_cast<uint32_t>(*p - '0');
            }
            if (digits == 0)
                return false;
            counts[i] = value;
        }
        return *p == 0;
    }

    /**
     * @brief Bewertet die Zählung, nachdem das Tab5 nur die Leitung expected mit TestEdges Flanken geschaltet hat.
     * @param counts Flanken an G18, G19 und G21 des Pico
     * @param expected Index des richtigen Pico-Eingangs
     * @param partner Erhält bei Swapped und Short den Index des anderen Eingangs, sonst -1
     */
    inline Verdict judge(const uint32_t (&counts)[LineCount], size_t expected, int &partner)
    {
        partner = -1;
        size_t full = 0, partial = 0;
        int other = -1;
        for (size_t i = 0; i < LineCount; ++i)
        {
            if (i == expected || counts[i] == 0)
                continue;
            if (counts[i] == TestEdges)
            {
                ++full;
                other = static_cast<int>(i);
            }
            else
                ++partial;
        }
        const uint32_t own = counts[expected];
        if (partial || full > 1 || (own != 0 && own != TestEdges))
            return Verdict::Disturbed;
        if (own == 0 && !full)
            return Verdict::Missing;
        if (!full)
            return Verdict::Ok;
        partner = other;
        return own ? Verdict::Short : Verdict::Swapped;
    }

    /**
     * @brief Bewertet die Ruhepegel der UART-Leitungen, bevor das Tab5 eine Leitung treibt.
     *
     * Der Pico hält seine TX-Leitung in Ruhe High, alle anderen Leitungen ziehen beide Seiten auf Low. Ein High an G7 kann deshalb nur vom laufenden Pico kommen. Nur dann treibt das Tab5, damit es einen ausgeschalteten Pico nicht über dessen Schutzdioden speist und nicht gegen einen Ausgang treibt.
     * @param tx Ruhepegel an BtUartTx (G6): 0, 1 oder -1 bei wechselndem Pegel
     * @param rx Ruhepegel an BtUartRx (G7), ebenso
     * @return Ok bei versorgtem Pico (G7 High, G6 Low), Missing ohne Pico, Swapped bei getauschten UART-Leitungen, Short bei beiden High, Disturbed bei wechselndem Pegel
     */
    inline Verdict judgeIdle(int tx, int rx)
    {
        if (tx < 0 || rx < 0)
            return Verdict::Disturbed;
        if (rx == 0)
            return tx ? Verdict::Swapped : Verdict::Missing;
        return tx ? Verdict::Short : Verdict::Ok;
    }

    /** @brief Zustand des Pico laut STATUS. */
    enum class LinkState : uint8_t
    {
        Unknown,    ///< Noch keine Antwort, etwa ohne Pico
        Idle,       ///< STILL: weder Suche noch Verbindung, der Pico funkt nicht
        Searching,  ///< SUCHT
        Connecting, ///< VERBINDET, auch in der Pause zwischen zwei Versuchen nach dem Start
        Connected   ///< VERBUNDEN
    };

    constexpr const char *StateNames[] = {"UNBEKANNT", "STILL", "SUCHT", "VERBINDET", "VERBUNDEN"}; ///< Namen in der Reihenfolge von LinkState, ab Idle wie im Protokoll

    /** @brief Name eines Zustands für Logzeilen, ab Idle wie im Protokoll. */
    inline const char *name(LinkState state)
    {
        return StateNames[static_cast<size_t>(state) < sizeof(StateNames) / sizeof(StateNames[0]) ? static_cast<size_t>(state) : 0];
    }

    /** @brief Zustand des UKW-Modulators laut UKW-Zeile. */
    enum class UkwState : uint8_t
    {
        Unknown,  ///< Noch keine UKW-Zeile, etwa bei einem Pico vor Stand 5.3
        Missing,  ///< FEHLT: kein Modulator am I2C des Pico
        Off,      ///< AUS: Modulator da, Sender aus
        Starting, ///< START: eingeschaltet, PLL rastet ein
        On,       ///< AN: sendet mit Ton
        Muted     ///< STUMM: sendet, aber stumm (Tab5 stumm oder Quellrate nicht unterstützt)
    };

    constexpr const char *UkwStateNames[] = {"UNBEKANNT", "FEHLT", "AUS", "START", "AN", "STUMM"}; ///< Namen in der Reihenfolge von UkwState, ab Missing wie im Protokoll
    constexpr size_t UkwStateCount = sizeof(UkwStateNames) / sizeof(UkwStateNames[0]);               ///< Anzahl der Zustände
    constexpr size_t UkwChipSize = 8;                                                               ///< Chipname „QN8007“, „QN8007L“ oder „-“ samt Nullbyte

    /** @brief Name eines UKW-Zustands für Logzeilen, ab Missing wie im Protokoll. */
    inline const char *name(UkwState state)
    {
        return UkwStateNames[static_cast<size_t>(state) < UkwStateCount ? static_cast<size_t>(state) : 0];
    }

    /** @brief Stand des UKW-Modulators aus der letzten UKW-Zeile. */
    struct UkwView
    {
        UkwState state = UkwState::Unknown; ///< Zustand
        uint32_t khz = 0;                   ///< Frequenz des Pico in kHz, 0 ohne Zeile
        uint8_t power = 0;                  ///< Leistungsstufe des Pico
        uint32_t rate = 0;                  ///< Quellrate am Modulator in Hz, 0 wenn der Chip sie nicht annimmt oder keiner da ist
        char chip[UkwChipSize] = {};        ///< Chipname, leer ohne Zeile, „-“ ohne Modulator

        /** @brief Ein Modulator ist erkannt und eingerichtet. */
        bool present() const
        {
            return state != UkwState::Unknown && state != UkwState::Missing;
        }
    };

    /** @brief Ein Bluetooth-Gerät aus Suche, Verbindung oder Status. */
    struct Device
    {
        char address[AddressSize]{}; ///< „AA:BB:CC:DD:EE:FF“ in Großbuchstaben, leer ohne Gerät
        char name[NameBytes + 1]{};  ///< Name in UTF-8, leer, solange unbekannt
        int8_t rssi = 0;             ///< Empfangsstärke in dBm, nur bei rssiKnown
        bool rssiKnown = false;      ///< Die Suche lieferte eine Empfangsstärke
    };

    /** @brief Art einer Zeile des Pico. */
    enum class Reply : uint8_t
    {
        Hello,          ///< HALLO STAND=<Stand>
        SearchStarted,  ///< SUCHE LAEUFT
        Found,          ///< TREFFER <Adresse> <RSSI|?>[ <Name>]
        SearchEnd,      ///< SUCHE ENDE <Anzahl>
        ConnectStarted, ///< VERBINDE LAEUFT
        Connected,      ///< VERBUNDEN <Adresse>[ <Name>], unaufgefordert. Mit nachgefragtem Namen auch ein zweites Mal
        Disconnected,   ///< GETRENNT <Adresse>, unaufgefordert: Verbindung beendet oder letzter Versuch gescheitert
        DisconnectDone, ///< TRENNE OK
        ForgetDone,     ///< VERGISS OK
        ToneStarted,    ///< TESTTON LAEUFT
        MuteDone,       ///< STUMM AN oder STUMM AUS
        RateDone,       ///< RATE <Hz>
        VolumeDone,     ///< LAUTSTAERKE <0..127>, Antwort auf Volume
        HeadphoneLevel, ///< KOPFHOERER <0..127>, unaufgefordert
        Status,         ///< STATUS <Zustand> <Adresse|->[ <Name>]
        UkwReport,      ///< UKW <Zustand> <kHz> <Leistung> <Rate|-> <Chip|->, auf Ukw und unaufgefordert bei jedem Zustandswechsel
        UkwDone,        ///< UKW AN, UKW AUS, UKW FREQ <kHz> oder UKW LEISTUNG <n>, Antwort auf den gleichlautenden Befehl
        RdsDone,        ///< RDS PS OK oder RDS RT OK
        Error,          ///< FEHLER <Kennung>
        Test            ///< Antwort des Leitungstests (ZAEHLEN LAEUFT, ERGEBNIS ...), im Protokollbetrieb ohne Wirkung
    };

    /** @brief Ausgewertete Zeile des Pico. */
    struct Message
    {
        Reply kind = Reply::Test;            ///< Art der Zeile
        LinkState state = LinkState::Unknown; ///< Bei Status
        Device device;                       ///< Bei Found, Connected, Disconnected und Status. Bei Status leer für „-“
        uint16_t count = 0;                  ///< Bei SearchEnd: Anzahl der Treffer
        char text[StandCapacity]{};          ///< Bei Hello der Firmwarestand, bei Error die Kennung
        bool muted = true;                   ///< Bei MuteDone: STUMM AN
        uint32_t rate = 0;                   ///< Bei RateDone: Quellrate in Hz
        uint8_t level = 0;                   ///< Bei VolumeDone und HeadphoneLevel: Kopfhörerlautstärke 0..HeadphoneMax
        UkwView ukw;                         ///< Bei UkwReport
    };

    /** @brief Liest eine Dezimalzahl ohne Vorzeichen mit 1 bis digits Ziffern bis zum Zeilenende. @return false bei jedem anderen Text oder einem Wert über maximum. */
    inline bool readNumber(const char *text, size_t digits, uint32_t maximum, uint32_t &value)
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

    /** @brief Wert einer Hexziffer in Groß- oder Kleinschreibung, -1 für jedes andere Zeichen. */
    inline int hexValue(char digit)
    {
        if (digit >= '0' && digit <= '9')
            return digit - '0';
        if (digit >= 'A' && digit <= 'F')
            return digit - 'A' + 10;
        if (digit >= 'a' && digit <= 'f')
            return digit - 'a' + 10;
        return -1;
    }

    /** @brief Liest eine Adresse „AA:BB:CC:DD:EE:FF“ mit großen oder kleinen Hexziffern am Anfang von text. @return Zeiger hinter die Adresse und address in Großbuchstaben, nullptr bei jedem anderen Text (address dann unbestimmt). */
    inline const char *readAddress(const char *text, char (&address)[AddressSize])
    {
        for (size_t i = 0; i + 1 < AddressSize; ++i)
        {
            const char c = text[i];
            if (i % 3 == 2 ? c != ':' : hexValue(c) < 0)
                return nullptr;
            address[i] = c >= 'a' && c <= 'f' ? static_cast<char>(c - 'a' + 'A') : c;
        }
        address[AddressSize - 1] = 0;
        return text + AddressSize - 1;
    }

    /** @brief Prüft eine vollständige Adresse aus USB oder Oberfläche und schreibt sie in Großbuchstaben. @return false bei jedem anderen Text. */
    inline bool parseAddress(const char *text, char (&address)[AddressSize])
    {
        const char *end = readAddress(text, address);
        return end && *end == 0;
    }

    /** @brief Liest einen Namen im Protokoll: %XX steht für ein Byte, sonst sichtbares ASCII oder Leerzeichen. Dekodiert höchstens NameBytes Byte, keine Steuerzeichen. @return false bei leerem, zu langem oder fehlerhaftem Text, name ist dann unbestimmt. */
    inline bool decodeName(const char *text, char (&name)[NameBytes + 1])
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
            if (byte < 0x20 || byte == 0x7f || used == NameBytes)
                return false;
            name[used++] = static_cast<char>(byte);
        }
        name[used] = 0;
        return used > 0;
    }

    /** @brief Liest das Ende einer Gerätezeile: nichts oder genau ein Leerzeichen und ein Name. @return false bei jedem anderen Rest. */
    inline bool readOptionalName(const char *text, Device &device)
    {
        device.name[0] = 0;
        if (*text == 0)
            return true;
        return *text == ' ' && decodeName(text + 1, device.name);
    }

    /** @brief Liest die Empfangsstärke einer TREFFER-Zeile: „?“ oder eine Zahl von -128 bis 127 mit höchstens drei Ziffern. @return Zeiger dahinter, nullptr bei jedem anderen Text. */
    inline const char *readRssi(const char *text, Device &device)
    {
        device.rssiKnown = false;
        if (*text == '?')
            return text + 1;
        const bool negative = *text == '-';
        const char *p = text + (negative ? 1 : 0);
        int value = 0;
        size_t digits = 0;
        for (; *p >= '0' && *p <= '9'; ++p)
        {
            if (++digits > 3)
                return nullptr;
            value = value * 10 + (*p - '0');
        }
        if (digits == 0)
            return nullptr;
        value = negative ? -value : value;
        if (value < -128 || value > 127)
            return nullptr;
        device.rssi = static_cast<int8_t>(value);
        device.rssiKnown = true;
        return p;
    }

    /** @brief Prüft, ob line mit prefix beginnt, und liefert dann den Rest. */
    inline const char *after(const char *line, const char *prefix)
    {
        const size_t length = strlen(prefix);
        return strncmp(line, prefix, length) == 0 ? line + length : nullptr;
    }

    /** @brief Kopiert das Feld bis zum nächsten Leerzeichen oder Zeilenende nach out. @return Zeiger auf das Trennzeichen, nullptr bei leerem Feld oder einem, das capacity samt Nullbyte sprengt. */
    inline const char *readToken(const char *text, char *out, size_t capacity)
    {
        size_t used = 0;
        for (; *text && *text != ' '; ++text)
        {
            if (used + 1 >= capacity)
                return nullptr;
            out[used++] = *text;
        }
        out[used] = 0;
        return used ? text : nullptr;
    }

    /** @brief Liest den Rest einer UKW-Zeile: Zustand, kHz, Leistung, Rate oder „-“, Chip oder „-“, durch je ein Leerzeichen getrennt. @return false bei jedem anderen Text, view ist dann unbestimmt. */
    inline bool readUkw(const char *text, UkwView &view)
    {
        const char *p = nullptr;
        size_t state = 1;
        for (; state < UkwStateCount && !p; ++state)
            if ((p = after(text, UkwStateNames[state])) && *p != ' ')
                p = nullptr;
        if (!p)
            return false;
        view.state = static_cast<UkwState>(state - 1);
        char token[12];
        uint32_t value = 0;
        if (!(p = readToken(p + 1, token, sizeof(token))) || *p != ' ' || !readNumber(token, 6, UkwMaxKhz, value))
            return false;
        view.khz = value;
        if (!(p = readToken(p + 1, token, sizeof(token))) || *p != ' ' || !readNumber(token, 2, UkwMaxPower, value))
            return false;
        view.power = static_cast<uint8_t>(value);
        if (!(p = readToken(p + 1, token, sizeof(token))) || *p != ' ')
            return false;
        if (strcmp(token, "-") == 0)
            view.rate = 0;
        else if (readNumber(token, 6, MaxRate, value) && value >= MinRate)
            view.rate = value;
        else
            return false;
        if (!(p = readToken(p + 1, view.chip, sizeof(view.chip))) || *p != 0)
            return false;
        for (const char *c = view.chip; *c; ++c)
            if (!((*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9') || *c == '-'))
                return false;
        return true;
    }

    /** @brief Schreibt text für das Protokoll wie die Namen des Pico: sichtbares ASCII und Leerzeichen außer % bleiben, jedes andere Byte wird %XX. Endet an einer UTF-8-Zeichengrenze, sobald das nächste Zeichen capacity samt Nullbyte sprengen würde. */
    inline void encodeText(const char *text, char *out, size_t capacity)
    {
        size_t used = 0;
        for (const char *p = text; *p;)
        {
            // Länge der UTF-8-Folge ab p, kaputte Folgen zählen byteweise.
            const uint8_t first = static_cast<uint8_t>(*p);
            size_t bytes = first < 0x80 ? 1 : (first & 0xE0) == 0xC0 ? 2
                                          : (first & 0xF0) == 0xE0   ? 3
                                          : (first & 0xF8) == 0xF0   ? 4
                                                                     : 1;
            for (size_t i = 1; i < bytes; ++i)
                if ((static_cast<uint8_t>(p[i]) & 0xC0) != 0x80)
                    bytes = i;
            size_t encoded = 0;
            for (size_t i = 0; i < bytes; ++i)
            {
                const uint8_t byte = static_cast<uint8_t>(p[i]);
                encoded += byte >= 0x20 && byte <= 0x7e && byte != '%' ? 1 : 3;
            }
            if (used + encoded + 1 > capacity)
                break;
            for (size_t i = 0; i < bytes; ++i)
            {
                const uint8_t byte = static_cast<uint8_t>(p[i]);
                if (byte >= 0x20 && byte <= 0x7e && byte != '%')
                    out[used++] = static_cast<char>(byte);
                else
                    used += static_cast<size_t>(snprintf(out + used, capacity - used, "%%%02X", byte));
            }
            p += bytes;
        }
        out[used] = 0;
    }

    /**
     * @brief Wertet eine Zeile des Pico streng nach Protokoll aus (bt-sender/README.md).
     * @param line Zeile ohne Zeilenende, wie LineReader sie liefert
     * @param message Ergebnis, nur bei true gültig
     * @return false bei jeder unbekannten oder fehlerhaften Zeile
     */
    inline bool parseMessage(const char *line, Message &message)
    {
        message = Message();
        const char *rest = nullptr;
        if (parseHello(line, message.text))
            message.kind = Reply::Hello;
        else if (strcmp(line, "SUCHE LAEUFT") == 0)
            message.kind = Reply::SearchStarted;
        else if (strcmp(line, "VERBINDE LAEUFT") == 0)
            message.kind = Reply::ConnectStarted;
        else if (strcmp(line, "TRENNE OK") == 0)
            message.kind = Reply::DisconnectDone;
        else if (strcmp(line, "VERGISS OK") == 0)
            message.kind = Reply::ForgetDone;
        else if (strcmp(line, "TESTTON LAEUFT") == 0)
            message.kind = Reply::ToneStarted;
        else if (strcmp(line, MuteOn) == 0 || strcmp(line, MuteOff) == 0)
        {
            message.kind = Reply::MuteDone;
            message.muted = strcmp(line, MuteOn) == 0;
        }
        else if ((rest = after(line, "RATE ")))
        {
            if (!readNumber(rest, 6, MaxRate, message.rate) || message.rate < MinRate)
                return false;
            message.kind = Reply::RateDone;
        }
        else if ((rest = after(line, "LAUTSTAERKE ")) || (rest = after(line, "KOPFHOERER ")))
        {
            uint32_t value = 0;
            if (!readNumber(rest, 3, HeadphoneMax, value))
                return false;
            message.kind = line[0] == 'K' ? Reply::HeadphoneLevel : Reply::VolumeDone;
            message.level = static_cast<uint8_t>(value);
        }
        else if (strcmp(line, CountReply) == 0 || after(line, ResultReply))
            message.kind = Reply::Test;
        else if ((rest = after(line, "SUCHE ENDE ")))
        {
            uint32_t value = 0;
            size_t digits = 0;
            for (; *rest >= '0' && *rest <= '9'; ++rest)
            {
                if (++digits > 3)
                    return false;
                value = value * 10 + static_cast<uint32_t>(*rest - '0');
            }
            if (digits == 0 || *rest || value > MaxHits)
                return false;
            message.kind = Reply::SearchEnd;
            message.count = static_cast<uint16_t>(value);
        }
        else if ((rest = after(line, "TREFFER ")))
        {
            rest = readAddress(rest, message.device.address);
            if (!rest || *rest != ' ' || !(rest = readRssi(rest + 1, message.device)) || !readOptionalName(rest, message.device))
                return false;
            message.kind = Reply::Found;
        }
        else if ((rest = after(line, "VERBUNDEN ")))
        {
            rest = readAddress(rest, message.device.address);
            if (!rest || !readOptionalName(rest, message.device))
                return false;
            message.kind = Reply::Connected;
        }
        else if ((rest = after(line, "GETRENNT ")))
        {
            if (!parseAddress(rest, message.device.address))
                return false;
            message.kind = Reply::Disconnected;
        }
        else if ((rest = after(line, "STATUS ")))
        {
            size_t state = 1;
            const char *field = nullptr;
            for (; state < sizeof(StateNames) / sizeof(StateNames[0]) && !field; ++state)
                if ((field = after(rest, StateNames[state])) && *field != ' ')
                    field = nullptr;
            if (!field)
                return false;
            message.state = static_cast<LinkState>(state - 1);
            ++field;
            if (*field == '-')
                rest = field + 1;
            else if (!(rest = readAddress(field, message.device.address)))
                return false;
            if (!readOptionalName(rest, message.device) || (*field == '-' && message.device.name[0]))
                return false;
            message.kind = Reply::Status;
        }
        else if (strcmp(line, UkwOn) == 0 || strcmp(line, UkwOff) == 0 || strcmp(line, "RDS PS OK") == 0 || strcmp(line, "RDS RT OK") == 0)
            message.kind = line[0] == 'R' ? Reply::RdsDone : Reply::UkwDone;
        else if ((rest = after(line, "UKW FREQ ")))
        {
            uint32_t value = 0;
            if (!readNumber(rest, 6, UkwMaxKhz, value) || !validUkwKhz(value))
                return false;
            message.kind = Reply::UkwDone;
        }
        else if ((rest = after(line, "UKW LEISTUNG ")))
        {
            uint32_t value = 0;
            if (!readNumber(rest, 2, UkwMaxPower, value))
                return false;
            message.kind = Reply::UkwDone;
        }
        else if ((rest = after(line, UkwReply)))
        {
            if (!readUkw(rest, message.ukw))
                return false;
            message.kind = Reply::UkwReport;
        }
        else if ((rest = after(line, "FEHLER ")))
        {
            const size_t length = strlen(rest);
            if (length == 0 || length >= StandCapacity)
                return false;
            for (size_t i = 0; i < length; ++i)
                if (!((rest[i] >= 'A' && rest[i] <= 'Z') || rest[i] == '_'))
                    return false;
            memcpy(message.text, rest, length + 1);
            message.kind = Reply::Error;
        }
        else
            return false;
        return true;
    }

    /** @brief Stand des Pico aus Sicht des Tab5, nachgeführt aus seinen Zeilen. */
    struct LinkView
    {
        LinkState state = LinkState::Unknown; ///< Zuletzt gemeldeter Zustand
        Device device;                        ///< Gegenstelle bei VERBINDET und VERBUNDEN, sonst der gespeicherte Kopfhörer. Leere Adresse ohne
        Device hits[MaxHits];                 ///< Treffer der laufenden oder letzten Suche, je Adresse einmal
        uint8_t hitCount = 0;                 ///< Belegte Plätze in hits
        bool searchDone = false;              ///< Die letzte Suche ist beendet
        char error[StandCapacity]{};          ///< Kennung der letzten FEHLER-Antwort, leer ohne. BtSender leert sie mit jedem gesendeten Befehl
        bool muted = true;                    ///< Laut Pico stumm, jede neue Verbindung und jede Trennung macht ihn wieder stumm
        uint32_t rate = 0;                    ///< Vom Pico bestätigte Quellrate in Hz, 0 ohne
        int16_t headphoneLevel = -1;          ///< Kopfhörerlautstärke 0..HeadphoneMax laut Pico, -1 ohne Verbindung oder bis zur ersten Meldung
        uint32_t levelReports = 0;            ///< Zählt die KOPFHOERER-Meldungen, der Drehknopf übernimmt jede neue
        UkwView ukw;                          ///< UKW-Modulator laut letzter UKW-Zeile
    };

    /**
     * @brief Führt view mit einer Zeile des Pico nach. Treffer derselben Adresse werden zusammengefasst, ein später gemeldeter Name ergänzt.
     * @return true, wenn danach STATUS den Zustand bestätigen soll: nach Suchende, Trennung, TRENNE und VERGISS, weil der Pico danach eine Verbindung beginnen oder den gespeicherten Kopfhörer ändern kann
     */
    inline bool apply(LinkView &view, const Message &message)
    {
        switch (message.kind)
        {
        case Reply::SearchStarted:
            view.state = LinkState::Searching;
            view.hitCount = 0;
            view.searchDone = false;
            return false;
        case Reply::Found:
            for (size_t i = 0; i < view.hitCount; ++i)
            {
                Device &hit = view.hits[i];
                if (strcmp(hit.address, message.device.address) != 0)
                    continue;
                if (message.device.name[0])
                    memcpy(hit.name, message.device.name, sizeof(hit.name));
                if (message.device.rssiKnown)
                {
                    hit.rssi = message.device.rssi;
                    hit.rssiKnown = true;
                }
                return false;
            }
            if (view.hitCount < MaxHits)
                view.hits[view.hitCount++] = message.device;
            return false;
        case Reply::SearchEnd:
            view.state = LinkState::Idle;
            view.searchDone = true;
            return true;
        case Reply::ConnectStarted:
            view.state = LinkState::Connecting;
            return false;
        case Reply::Connected:
            if (view.state != LinkState::Connected)
            {
                view.muted = true;
                view.headphoneLevel = -1;
            }
            view.state = LinkState::Connected;
            view.device = message.device;
            return false;
        case Reply::Disconnected:
            view.state = LinkState::Idle;
            view.muted = true;
            view.headphoneLevel = -1;
            return true;
        case Reply::MuteDone:
            view.muted = message.muted;
            return false;
        case Reply::RateDone:
            view.rate = message.rate;
            return false;
        case Reply::VolumeDone:
            view.headphoneLevel = message.level;
            return false;
        case Reply::HeadphoneLevel:
            view.headphoneLevel = message.level;
            ++view.levelReports;
            return false;
        case Reply::DisconnectDone:
        case Reply::ForgetDone:
            return true;
        case Reply::Status:
            view.state = message.state;
            view.device = message.device;
            return false;
        case Reply::UkwReport:
            view.ukw = message.ukw;
            return false;
        case Reply::Error:
            memcpy(view.error, message.text, sizeof(view.error));
            return false;
        default:
            return false;
        }
    }

    /** @brief Entscheidet, ob die I2S-Signale des Codecs zusätzlich an G16, G17 und G18 liegen: bei offener UART, also nachweislich versorgtem Pico (judgeIdle und HALLO). Der Pico gibt den Ton immer an seinen optischen Ausgang und bei verbundenem Kopfhörer zusätzlich per Bluetooth. Der UKW-Modulator hört an denselben Leitungen mit. @param open UART offen. */
    inline bool routeAudio(bool open)
    {
        return open;
    }

    /** @brief Entscheidet, ob der Ton dem Bluetooth-Kopfhörer gehört: Tonweg angelegt und Kopfhörer verbunden. Dann schweigen Lautsprecher und Buchse und der Drehknopf regelt den Kopfhörer. @param routed I2S0 liegt an G16, G17 und G18. @param state Zustand laut Pico. */
    inline bool headphoneOutput(bool routed, LinkState state)
    {
        return routed && state == LinkState::Connected;
    }

    /** @brief Gleicht Stummschaltung, Quellrate und Kopfhörerlautstärke des Pico mit dem Wunsch des Tab5 ab, eine Zeile je Aufruf. STUMM AN geht vor RATE und LAUTSTAERKE, STUMM AUS nach ihnen, damit der Pico nie mit einer alten Rate oder Lautstärke laut wird. */
    class OutputSync
    {
    public:
        /** @brief Vergisst den gesendeten Stand: nach dem Öffnen der UART und nach jeder neuen Verbindung, mit der der Pico wieder stumm beginnt und seine gespeicherte Lautstärke meldet. */
        void forget()
        {
            m_known = false;
            m_rate = 0;
            m_volume = -1;
            m_volumeAcks = 0;
        }

        /**
         * @brief Liefert die nächste fällige Zeile und merkt sie als gesendet.
         * @param muted Gewünschter Stummzustand
         * @param rate Gewünschte Quellrate in Hz. Außerhalb MinRate..MaxRate, etwa 0 ohne Quelle, wird sie nicht gesendet
         * @param volume Gewünschte Stellung des Drehknopfs 0..KnobMax für den Kopfhörer, -1 ohne Wunsch
         * @param line Ziel für die Zeile ohne Zeilenende
         * @return false, wenn nichts zu senden ist
         */
        bool next(bool muted, uint32_t rate, int volume, char (&line)[MaxLine + 1])
        {
            if (muted && (!m_known || !m_muted))
            {
                m_known = m_muted = true;
                snprintf(line, sizeof(line), "%s", MuteOn);
                return true;
            }
            if (rate >= MinRate && rate <= MaxRate && rate != m_rate)
            {
                m_rate = rate;
                snprintf(line, sizeof(line), "%s %lu", SetRate, static_cast<unsigned long>(rate));
                return true;
            }
            if (volume >= 0 && volume <= KnobMax && volume != m_volume)
            {
                m_volume = volume;
                ++m_volumeAcks;
                snprintf(line, sizeof(line), "%s %u", Volume, static_cast<unsigned>(toHeadphone(static_cast<uint8_t>(volume))));
                return true;
            }
            if (!muted && (!m_known || m_muted))
            {
                m_known = true;
                m_muted = false;
                snprintf(line, sizeof(line), "%s", MuteOff);
                return true;
            }
            return false;
        }

        /** @brief Eine KOPFHOERER-Meldung gilt als gesendeter Stand, damit der Drehknopf, der ihr folgt, sie nicht zurückschickt. @param level Gemeldete Kopfhörerlautstärke. */
        void noteReport(uint8_t level)
        {
            m_volume = fromHeadphone(level);
        }

        /** @brief Der Pico hat eine LAUTSTAERKE-Zeile bestätigt. */
        void volumeAcked()
        {
            if (m_volumeAcks)
                --m_volumeAcks;
        }

        /** @brief Eine gesendete LAUTSTAERKE-Zeile ist noch unbestätigt, eine KOPFHOERER-Meldung bis dahin stammt von vorher und würde den Drehknopf zurückstellen. */
        bool awaitingVolume() const
        {
            return m_volumeAcks > 0;
        }

    private:
        bool m_known = false;     ///< m_muted entspricht dem Pico
        bool m_muted = true;      ///< Zuletzt gesendeter Stummzustand
        uint32_t m_rate = 0;      ///< Zuletzt gesendete Quellrate, 0 ohne
        int m_volume = -1;        ///< Zuletzt gesendete oder gemeldete Stellung des Drehknopfs, -1 unbekannt
        uint8_t m_volumeAcks = 0; ///< Gesendete, noch unbestätigte LAUTSTAERKE-Zeilen
    };

    /** @brief Wunsch des Tab5 an den UKW-Modulator. Die Texte sind UTF-8 und versioniert, damit UkwSync nur Änderungen sendet. */
    struct UkwWish
    {
        bool enabled = false;   ///< Sender an
        uint32_t khz = 0;       ///< Frequenz in kHz, außerhalb validUkwKhz() wird sie nicht gesendet
        uint8_t power = 0;      ///< Leistungsstufe 0..UkwMaxPower
        const char *ps = "";    ///< Programmname
        const char *rt = "";    ///< Radiotext
        uint32_t psVersion = 0; ///< Zählt jede Änderung von ps, 0 heißt noch nie gesetzt
        uint32_t rtVersion = 0; ///< Ebenso für rt
    };

    /** @brief Gleicht den UKW-Modulator mit dem Wunsch des Tab5 ab, eine Zeile je Aufruf, nur mit erkanntem Modulator: erst AUS, falls er ausgehen soll, dann Frequenz, Leistung und RDS-Texte, zuletzt AN. So startet er nie mit alten Werten. */
    class UkwSync
    {
    public:
        /** @brief Vergisst den gesendeten Stand: nach dem Öffnen der UART und sobald der Pico einen Modulator meldet, weil der dann mit Vorgaben beginnt. */
        void forget()
        {
            m_known = false;
            m_on = false;
            m_khz = 0;
            m_power = -1;
            m_psVersion = 0;
            m_rtVersion = 0;
        }

        /**
         * @brief Liefert die nächste fällige Zeile und merkt sie als gesendet.
         * @param wish Wunsch des Tab5
         * @param present Der Pico meldet einen Modulator, sonst wird nichts gesendet
         * @param line Ziel für die Zeile ohne Zeilenende
         * @return false, wenn nichts zu senden ist
         */
        bool next(const UkwWish &wish, bool present, char (&line)[MaxLine + 1])
        {
            if (!present)
                return false;
            if (m_known && m_on && !wish.enabled)
            {
                m_on = false;
                snprintf(line, sizeof(line), "%s", UkwOff);
                return true;
            }
            if (validUkwKhz(wish.khz) && wish.khz != m_khz)
            {
                m_khz = wish.khz;
                snprintf(line, sizeof(line), "%s %lu", UkwFrequency, static_cast<unsigned long>(wish.khz));
                return true;
            }
            if (wish.power <= UkwMaxPower && wish.power != m_power)
            {
                m_power = wish.power;
                snprintf(line, sizeof(line), "%s %u", UkwPower, static_cast<unsigned>(wish.power));
                return true;
            }
            if (wish.psVersion != m_psVersion)
            {
                m_psVersion = wish.psVersion;
                textLine(RdsPs, wish.ps, line);
                return true;
            }
            if (wish.rtVersion != m_rtVersion)
            {
                m_rtVersion = wish.rtVersion;
                textLine(RdsRt, wish.rt, line);
                return true;
            }
            if (!m_known || m_on != wish.enabled)
            {
                m_known = true;
                m_on = wish.enabled;
                snprintf(line, sizeof(line), "%s", wish.enabled ? UkwOn : UkwOff);
                return true;
            }
            return false;
        }

    private:
        /** @brief Baut „<Befehl> <kodierter Text>“, der Text gekürzt auf die Zeile. */
        static void textLine(const char *command, const char *text, char (&line)[MaxLine + 1])
        {
            const int used = snprintf(line, sizeof(line), "%s ", command);
            encodeText(text, line + used, sizeof(line) - static_cast<size_t>(used));
        }

        bool m_known = false;     ///< AN oder AUS wurde seit forget() gesendet
        bool m_on = false;        ///< Zuletzt gesendet: an
        uint32_t m_khz = 0;       ///< Zuletzt gesendete Frequenz, 0 ohne
        int m_power = -1;         ///< Zuletzt gesendete Leistung, -1 ohne
        uint32_t m_psVersion = 0; ///< Version des gesendeten Programmnamens
        uint32_t m_rtVersion = 0; ///< Version des gesendeten Radiotexts
    };

    /** @brief Zugriff des Leitungstests auf Leitungen und UART. Auf dem Tab5 BtSender, im Hosttest eine Attrappe. Zu Beginn sind alle Leitungen Eingänge mit Pull-down. */
    class TestPort
    {
    public:
        virtual ~TestPort() = default;
        /** @brief Liest den Pegel einer Leitung mehrfach, ohne sie zu treiben. @return 0 oder 1, -1 bei wechselndem Pegel. */
        virtual int idleLevel(uint8_t pin) = 0;
        /** @brief Öffnet die UART an BtUartTx und BtUartRx. Ab hier treibt das Tab5 BtUartTx. @return false, wenn der Treiber nicht startet. */
        virtual bool openUart() = 0;
        /** @brief Sendet command mit Zeilenende und wartet höchstens ReplyTimeoutMs auf die erste Zeile, die mit expected beginnt. Andere Zeilen werden übergangen. @return false ohne passende Antwort. */
        virtual bool exchange(const char *command, const char *expected, char (&reply)[MaxLine + 1]) = 0;
        /** @brief Treibt pin für edges Flanken, beginnend und endend Low, danach wieder Eingang mit Pull-down. */
        virtual void toggle(uint8_t pin, uint16_t edges) = 0;
    };

    /** @brief Ergebnis eines Leitungstests. */
    struct TestResult
    {
        int levels[IdleCount] = {-1, -1, -1, -1, -1}; ///< Ruhepegel an IdlePins vor dem Test: 0, 1 oder -1 bei wechselndem Pegel
        bool powered = false;                         ///< G7 in Ruhe High und G6 Low: Pico versorgt, erst danach treibt das Tab5 eine Leitung
        Verdict uart = Verdict::Unchecked;            ///< Befund der beiden UART-Leitungen
        char stand[StandCapacity]{};                  ///< Firmwarestand aus der HALLO-Antwort, leer ohne Antwort
        Verdict lines[LineCount]{};                   ///< Befund je I2S-Leitung in der Reihenfolge von LinePins
        int partner[LineCount] = {-1, -1, -1};        ///< Bei Swapped und Short der beteiligte Pico-Eingang, sonst -1
        uint32_t counts[LineCount][LineCount]{};      ///< Gezählte Flanken je geschalteter Leitung an G18, G19, G21
    };

    /**
     * @brief Führt den Leitungstest aus: Ruhepegel, HALLO, dann jede I2S-Leitung einzeln mit Zählung im Pico.
     *
     * Erst wenn judgeIdle() den Pico als versorgt erkennt, treibt das Tab5 eine Leitung. Eine I2S-Leitung, die in Ruhe nicht Low ist, wird nie getrieben. Schließen muss der Aufrufer.
     */
    inline TestResult runLineTest(TestPort &port)
    {
        TestResult result;
        for (size_t i = 0; i < IdleCount; ++i)
            result.levels[i] = port.idleLevel(IdlePins[i]);
        const Verdict idle = judgeIdle(result.levels[0], result.levels[1]);
        if (idle != Verdict::Ok)
        {
            result.uart = idle;
            return result;
        }
        result.powered = true;
        if (!port.openUart())
            return result;
        char reply[MaxLine + 1]{};
        if (!port.exchange(Hello, HelloReply, reply) || !parseHello(reply, result.stand))
        {
            result.uart = Verdict::Missing;
            return result;
        }
        result.uart = Verdict::Ok;
        for (size_t i = 0; i < LineCount; ++i)
        {
            const int level = result.levels[2 + i];
            if (level != 0)
            {
                result.lines[i] = level > 0 ? Verdict::Short : Verdict::Disturbed;
                continue;
            }
            if (!port.exchange(Count, CountReply, reply))
            {
                result.uart = Verdict::Disturbed;
                break;
            }
            port.toggle(LinePins[i], TestEdges);
            if (!port.exchange(Result, ResultReply, reply) || !parseResult(reply, result.counts[i]))
            {
                result.uart = Verdict::Disturbed;
                break;
            }
            result.lines[i] = judge(result.counts[i], i, result.partner[i]);
        }
        return result;
    }
}

#endif
