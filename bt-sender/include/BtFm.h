#ifndef BT_FM_H
#define BT_FM_H

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

/** @brief UKW-Modulator QN8007 (Quintic) am I2C des Pico: Registerwerte nach Datenblatt, RDS-Gruppenbildung und eine nicht blockierende Zustandsmaschine über eine Busschnittstelle, ohne Hardwarezugriff und per Hosttest geprüft. Der Ton kommt direkt vom I2S des Tab5 an J2 der Baugruppe (32-Bit-Slots, Philips). Der Pico stellt nur Frequenz, Leistung, Rate, Stumm und RDS ein. Anschlüsse und Protokoll in bt-sender/README.md, Verdrahtung in docs/Bluetooth-Sender.md. */
namespace BtFm
{
    constexpr uint8_t Addresses[] = {0x2B, 0x2C, 0x2A};   ///< I2C-Adressen, die der Start der Reihe nach antestet: Vorgabe des Datenblatts bei SEB Low (0101011), die Adresse eines vorhandenen Treibers der Baugruppe, die Registervorgabe bei SEB High
    constexpr size_t AddressCount = sizeof(Addresses);    ///< Anzahl der Kandidaten
    constexpr uint32_t MinKhz = 76000;                     ///< Unteres Bandende laut Datenblatt
    constexpr uint32_t MaxKhz = 108000;                    ///< Oberes Bandende laut Datenblatt
    constexpr uint32_t StepKhz = 50;                       ///< Kanalraster: Kanal = (f - 76000 kHz) / 50 kHz, zehn Bit in CH und CH_STEP
    constexpr uint32_t DefaultKhz = 87500;                 ///< Frequenz bis zur ersten UKW FREQ-Zeile, unteres Ende des europäischen Bands
    constexpr uint8_t MaxPower = 15;                       ///< Leistung 0..15 im Protokoll: 0 ist die kleinste Stufe (101,5 dBµV, PAGAIN 15), 15 die größte (124 dBµV, PAGAIN 0), Schritt 1,5 dB
    constexpr uint8_t DefaultPower = 0;                    ///< Leistung bis zur ersten UKW LEISTUNG-Zeile: die kleinste Stufe
    constexpr uint32_t Rates[] = {32000, 40000, 44100, 48000}; ///< Quellraten, die der I2S-Eingang des Chips annimmt (I2SDRATE 0..3). Bei jeder anderen bleibt der Modulator stumm
    constexpr uint32_t ProbeIntervalMs = 10000;            ///< Abstand der Nachprüfung ohne Modulator, eine I2C-Abfrage je Durchlauf
    constexpr uint32_t ResetSettleMs = 50;                 ///< Wartezeit nach dem Softwarereset (SWRST), bevor die Register geschrieben werden
    constexpr uint32_t StartSettleMs = 700;                ///< Wartezeit nach TXREQ, bis PLL und Sender bereit sind. Das Datenblatt nennt höchstens 0,6 s ab Einschalten
    constexpr uint32_t RateMuteMs = 50;                    ///< Stumm nach einem Ratenwechsel, bis der I2S-Empfänger des Chips wieder eingerastet ist
    constexpr uint32_t RdsPollMs = 20;                     ///< Abstand der STATUS2-Abfragen, ob der Chip die Gruppe übernommen hat. Eine Gruppe dauert 87,6 ms
    constexpr uint32_t RdsTimeoutMs = 500;                 ///< Ohne Übernahme in dieser Zeit gilt die Gruppe als verloren, die nächste folgt
    constexpr uint8_t MaxBusErrors = 3;                    ///< So viele Busfehler hintereinander gelten als verschwundener Modulator

    /** @brief Nutzerregister laut Datenblatt (Tabelle 13). */
    enum Reg : uint8_t
    {
        System1 = 0x00,   ///< TXREQ, CHSC, STNBY, TXI2S, RDSEN, CCA_CH_DIS
        System2 = 0x01,   ///< SWRST, RECAL, ST_MO_TX, TC, RDSTXRDY, TMOUT
        AnaCtl1 = 0x03,   ///< MUTE_EN, I2S_SCKINV, RSTB_BB, ANT_SEL, XSEL
        RegVga = 0x04,    ///< RIN (analoge Eingangsimpedanz), XCSEL (Lastkapazität des Quarzes)
        Cid1 = 0x05,      ///< Produktfamilie und kleine Revision
        Cid2 = 0x06,      ///< CID3 Produkt (0001 QN8007, 0101 QN8007L), CID4 große Revision
        I2s = 0x07,       ///< Wortbreite, Rate, Master/Slave, Format
        Ch = 0x08,        ///< Kanal Bits 7:0
        ChStep = 0x0B,    ///< Suchlaufraster, Grenzen und Kanal Bits 9:8
        TxFdev = 0x0E,    ///< Gesamthub, 0,69 kHz je Stufe
        GainTxPlt = 0x0F, ///< I2S-Puffer leeren, Pilothub, Interruptfreigaben
        Rds0 = 0x10,      ///< Erstes der acht RDS-Datenbytes 10h bis 17h
        RdsFdev = 0x18,   ///< RDS-Hub, 0,35 kHz je Stufe
        Status1 = 0x1A,   ///< I2S-Über- und Unterlauf, Eingangsübersteuerung
        Status2 = 0x1B,   ///< RDS_RXTXUPD in Bit 7
        PagCal = 0x5A     ///< PAG_DIS, IPOW, PAGAIN
    };

    constexpr uint8_t System1TxReq = 0x40;     ///< Sendebetrieb
    constexpr uint8_t System1TxI2s = 0x04;     ///< Ton vom I2S statt von den analogen Eingängen
    constexpr uint8_t System1RdsEn = 0x02;     ///< RDS mitsenden
    constexpr uint8_t System1ChManual = 0x01;  ///< CCA_CH_DIS: Kanal aus CH statt aus dem Suchlauf
    constexpr uint8_t System2SwRst = 0x80;     ///< Alle Register auf Vorgabe
    constexpr uint8_t System2Mono = 0x10;      ///< ST_MO_TX, bleibt 0 für Stereo
    constexpr uint8_t System2Tc75 = 0x08;      ///< TC: 75 µs Pre-Emphasis, 0 sind die 50 µs für Europa
    constexpr uint8_t System2RdsTxRdy = 0x04;  ///< Kippen übergibt die acht RDS-Bytes
    constexpr uint8_t System2NoStandby = 0x03; ///< TMOUT 11: nie von selbst in den Standby
    constexpr uint8_t AnaMute = 0x80;          ///< MUTE_EN
    constexpr uint8_t AnaBasebandRun = 0x20;   ///< RSTB_BB = 1: Basisband läuft, 0 hält es im Reset
    constexpr uint8_t AnaTxAntenna = 0x10;     ///< ANT_SEL: Sendeantenne an RFO
    constexpr uint8_t CrystalSelect = 0x0B;    ///< XSEL 1011: 26 MHz, der Quarz der Baugruppe
    constexpr uint8_t InputImpedance = 0x02;   ///< RIN 10: 40 kOhm an den analogen Eingängen, die mit I2S ungenutzt bleiben
    constexpr uint8_t CrystalCap = 0x20;       ///< XCSEL: Lastkapazität 10 pF + 0,32 pF je Stufe, 0x20 sind 20 pF (Vorgabe des Datenblatts). Am Empfänger nachzuprüfen
    constexpr uint8_t I2sWidth32 = 0xC0;       ///< I2SBW 11: 32-Bit-Wörter, wie die Slots des Tab5 mit dem 16-Bit-Ton oben
    constexpr uint8_t I2sSlaveI2s = 0x01;      ///< I2SMODE 0 (Slave, Takte vom Tab5) und I2SFMT 001 (I2S)
    constexpr uint8_t ChStepBase = 0x60;       ///< FSTEP 01, CH_STP 10, CH_STA 00 (Vorgaben). Dazu kommen die Kanalbits 1:0
    constexpr uint8_t Deviation = 108;         ///< TX_FDEV: 108 x 0,69 kHz = 74,5 kHz Gesamthub
    constexpr uint8_t PilotGain = 9;           ///< GAIN_TXPLT 1001: Pilothub 9 % von 75 kHz
    constexpr uint8_t I2sClearFlags = 0xC0;    ///< I2SUNDFL_CLR und I2SOVFL_CLR, nach dem Setzen wieder löschen
    constexpr uint8_t RdsDeviation = 8;        ///< RDSFDEV: 8 x 0,35 kHz = 2,8 kHz. Die Vorgabe 6 (2,1 kHz) reicht manchen Empfängern nicht
    constexpr uint8_t RdsFdevReserved = 0x80;  ///< Bit 7 von RDSFDEV ist reserviert und steht in der Vorgabe auf 1
    constexpr uint8_t PagDisable = 0x40;       ///< PAG_DIS: Leistung aus IPOW und PAGAIN statt aus der Kalibrierung, IPOW 00 sind 4 mA
    constexpr uint8_t Status2RdsUpdated = 0x80; ///< RDS_RXTXUPD: kippt, sobald der Chip die acht Bytes übernommen hat

    /** @brief Erkannter Chip. */
    enum class Chip : uint8_t
    {
        None,    ///< Kein Modulator oder unbekannte Kennung
        Qn8007,  ///< CID3 0001: mit RDS
        Qn8007L  ///< CID3 0101: ohne RDS
    };

    /** @brief Zustand des Modulators, wie die UKW-Zeile ihn meldet. */
    enum class State : uint8_t
    {
        Absent,    ///< FEHLT: kein Modulator am Bus
        Resetting, ///< Nach dem Softwarereset, Register folgen. Im Protokoll AUS
        Idle,      ///< AUS: Modulator da, Sender aus
        Starting,  ///< START: TXREQ gesetzt, PLL rastet ein
        On         ///< AN oder STUMM: sendet
    };

    /** @brief Chip aus CID2 (Register 06h), Bits 7:4. */
    inline Chip chipFromCid2(uint8_t cid2)
    {
        switch (cid2 >> 4)
        {
        case 1:
            return Chip::Qn8007;
        case 5:
            return Chip::Qn8007L;
        default:
            return Chip::None;
        }
    }

    /** @brief Name des Chips für Protokoll und Log, „-“ ohne Modulator. */
    inline const char *name(Chip chip)
    {
        return chip == Chip::Qn8007 ? "QN8007" : chip == Chip::Qn8007L ? "QN8007L"
                                                                       : "-";
    }

    /** @brief Frequenz im Band und im 50-kHz-Raster. */
    inline bool validKhz(uint32_t khz)
    {
        return khz >= MinKhz && khz <= MaxKhz && khz % StepKhz == 0;
    }

    /** @brief Kanalindex 0..640 zu einer gültigen Frequenz. */
    inline uint16_t channelFor(uint32_t khz)
    {
        return static_cast<uint16_t>((khz - MinKhz) / StepKhz);
    }

    /** @brief I2SDRATE-Code 0..3 zur Quellrate, -1 für eine Rate, die der Chip nicht annimmt. */
    inline int rateCode(uint32_t rate)
    {
        for (size_t i = 0; i < sizeof(Rates) / sizeof(Rates[0]); ++i)
            if (Rates[i] == rate)
                return static_cast<int>(i);
        return -1;
    }

    /** @brief Spannung am Antennenausgang in Zehntel dBµV zu einer Leistungsstufe: 124 dBµV minus 1,5 dB je PAGAIN-Stufe. */
    inline unsigned powerDbuV10(uint8_t power)
    {
        return 1240u - 15u * (MaxPower - (power > MaxPower ? MaxPower : power));
    }

    constexpr size_t PsChars = 8;        ///< Programmname (PS): acht Zeichen in vier Gruppen 0A
    constexpr size_t RtChars = 64;       ///< Radiotext: bis 64 Zeichen in sechzehn Gruppen 2A
    constexpr size_t TextBytes = 192;    ///< Längster Eingabetext in Byte UTF-8, mehr als eine Protokollzeile fasst
    constexpr uint16_t Pi = 0xD5A2;      ///< Programmkennung: Landeskennung D, der Rest frei gewählt. Keine zugeteilte Kennung
    constexpr uint8_t Pty = 0;           ///< Programmtyp „nicht festgelegt“
    constexpr uint16_t NoAf = 0xE0CD;    ///< Block 3 der Gruppe 0A: Code 224 „keine Alternativfrequenz“, dahinter der Füllcode 205
    constexpr uint8_t RdsCr = 0x0D;      ///< Ende eines kürzeren Radiotexts
    constexpr uint8_t RdsUnknown = '?';  ///< Ersatz für Zeichen ohne Platz im RDS-Zeichensatz

    /** @brief Liest ein UTF-8-Zeichen ab p. @param codePoint Ergebnis, 0xFFFD bei ungültiger Folge. @return Zeiger hinter das Zeichen, mindestens p + 1. */
    inline const char *decodeUtf8(const char *p, uint32_t &codePoint)
    {
        const uint8_t first = static_cast<uint8_t>(*p);
        size_t more = 0;
        if (first < 0x80)
            codePoint = first;
        else if ((first & 0xE0) == 0xC0)
        {
            codePoint = first & 0x1F;
            more = 1;
        }
        else if ((first & 0xF0) == 0xE0)
        {
            codePoint = first & 0x0F;
            more = 2;
        }
        else if ((first & 0xF8) == 0xF0)
        {
            codePoint = first & 0x07;
            more = 3;
        }
        else
        {
            codePoint = 0xFFFD;
            return p + 1;
        }
        for (size_t i = 1; i <= more; ++i)
        {
            const uint8_t next = static_cast<uint8_t>(p[i]);
            if ((next & 0xC0) != 0x80)
            {
                codePoint = 0xFFFD;
                return p + i;
            }
            codePoint = codePoint << 6 | (next & 0x3F);
        }
        return p + more + 1;
    }

    /** @brief Zeichen des RDS-Zeichensatzes (IEC 62106, Tabelle E.1) zu einem Unicode-Zeichen: ASCII unverändert, lateinische Buchstaben mit Akzenten, Umlaute, ß, Euro und Gradzeichen aus der Tabelle, Steuerzeichen als Leerzeichen, alles andere als Fragezeichen. */
    inline uint8_t rdsChar(uint32_t codePoint)
    {
        if (codePoint < 0x20)
            return ' ';
        if (codePoint < 0x7F)
            return static_cast<uint8_t>(codePoint);
        switch (codePoint)
        {
        case 0x00E1:
            return 0x80; // á
        case 0x00E0:
            return 0x81; // à
        case 0x00E9:
            return 0x82; // é
        case 0x00E8:
            return 0x83; // è
        case 0x00ED:
            return 0x84; // í
        case 0x00EC:
            return 0x85; // ì
        case 0x00F3:
            return 0x86; // ó
        case 0x00F2:
            return 0x87; // ò
        case 0x00FA:
            return 0x88; // ú
        case 0x00F9:
            return 0x89; // ù
        case 0x00D1:
            return 0x8A; // Ñ
        case 0x00C7:
            return 0x8B; // Ç
        case 0x00DF:
            return 0x8D; // ß
        case 0x00E2:
            return 0x90; // â
        case 0x00E4:
            return 0x91; // ä
        case 0x00EA:
            return 0x92; // ê
        case 0x00EB:
            return 0x93; // ë
        case 0x00EE:
            return 0x94; // î
        case 0x00EF:
            return 0x95; // ï
        case 0x00F4:
            return 0x96; // ô
        case 0x00F6:
            return 0x97; // ö
        case 0x00FB:
            return 0x98; // û
        case 0x00FC:
            return 0x99; // ü
        case 0x00F1:
            return 0x9A; // ñ
        case 0x00E7:
            return 0x9B; // ç
        case 0x20AC:
            return 0xA9; // €
        case 0x00A3:
            return 0xAA; // £
        case 0x00B0:
            return 0xBB; // °
        case 0x00A7:
            return 0xBF; // §
        case 0x00C1:
            return 0xC0; // Á
        case 0x00C0:
            return 0xC1; // À
        case 0x00C9:
            return 0xC2; // É
        case 0x00C8:
            return 0xC3; // È
        case 0x00CD:
            return 0xC4; // Í
        case 0x00CC:
            return 0xC5; // Ì
        case 0x00D3:
            return 0xC6; // Ó
        case 0x00D2:
            return 0xC7; // Ò
        case 0x00DA:
            return 0xC8; // Ú
        case 0x00D9:
            return 0xC9; // Ù
        case 0x00C2:
            return 0xD0; // Â
        case 0x00C4:
            return 0xD1; // Ä
        case 0x00CA:
            return 0xD2; // Ê
        case 0x00CB:
            return 0xD3; // Ë
        case 0x00CE:
            return 0xD4; // Î
        case 0x00CF:
            return 0xD5; // Ï
        case 0x00D4:
            return 0xD6; // Ô
        case 0x00D6:
            return 0xD7; // Ö
        case 0x00DB:
            return 0xD8; // Û
        case 0x00DC:
            return 0xD9; // Ü
        case 0x00C5:
            return 0xE1; // Å
        case 0x00C6:
            return 0xE2; // Æ
        case 0x0152:
            return 0xE3; // Œ
        case 0x00DD:
            return 0xE5; // Ý
        case 0x00D5:
            return 0xE6; // Õ
        case 0x00D8:
            return 0xE7; // Ø
        case 0x00E5:
            return 0xF1; // å
        case 0x00E6:
            return 0xF2; // æ
        case 0x0153:
            return 0xF3; // œ
        case 0x00FD:
            return 0xF5; // ý
        case 0x00F5:
            return 0xF6; // õ
        case 0x00F8:
            return 0xF7; // ø
        default:
            return RdsUnknown;
        }
    }

    /** @brief Wandelt UTF-8 in höchstens capacity RDS-Zeichen, ohne Nullbyte. @return Anzahl geschriebener Zeichen. */
    inline size_t toRds(const char *utf8, uint8_t *out, size_t capacity)
    {
        size_t used = 0;
        for (const char *p = utf8; *p && used < capacity;)
        {
            uint32_t codePoint = 0;
            p = decodeUtf8(p, codePoint);
            out[used++] = rdsChar(codePoint);
        }
        return used;
    }

    /** @brief Bildet die Gruppen 0A (Programmname) und 2A (Radiotext) im Wechsel, der Chip hängt die Prüfwörter selbst an. Nur aus loop(). */
    class RdsEncoder
    {
    public:
        /** @brief Übernimmt den Programmnamen: die ersten acht RDS-Zeichen, kürzere mit Leerzeichen aufgefüllt. @return true, wenn er sich geändert hat. */
        bool setPs(const char *utf8)
        {
            uint8_t ps[PsChars];
            const size_t used = toRds(utf8, ps, PsChars);
            memset(ps + used, ' ', PsChars - used);
            if (memcmp(ps, m_ps, PsChars) == 0)
                return false;
            memcpy(m_ps, ps, PsChars);
            m_psSegment = 0;
            return true;
        }

        /** @brief Übernimmt den Radiotext: bis 64 RDS-Zeichen, ein kürzerer endet mit CR und füllt seine letzte Gruppe mit Leerzeichen. Ein leerer Text schaltet die Gruppen 2A ab. Jede Änderung kippt das A/B-Bit, damit Empfänger den alten Text verwerfen. @return true, wenn er sich geändert hat. */
        bool setRt(const char *utf8)
        {
            uint8_t rt[RtChars];
            size_t used = toRds(utf8, rt, RtChars);
            if (used < RtChars)
                rt[used++] = RdsCr;
            const uint8_t segments = static_cast<uint8_t>(utf8[0] ? (used + 3) / 4 : 0);
            const size_t padded = segments * 4;
            memset(rt + used, ' ', RtChars - used);
            if (segments == m_rtSegments && memcmp(rt, m_rt, padded) == 0)
                return false;
            memcpy(m_rt, rt, RtChars);
            m_rtSegments = segments;
            m_rtSegment = 0;
            m_ab = !m_ab;
            return true;
        }

        /** @brief Liefert die nächste Gruppe als vier Informationswörter: abwechselnd 0A und 2A, ohne Radiotext nur 0A. */
        void next(uint16_t (&blocks)[4])
        {
            blocks[0] = Pi;
            if (m_rtSegments == 0 || !m_rtTurn)
            {
                const uint8_t segment = m_psSegment;
                m_psSegment = static_cast<uint8_t>((m_psSegment + 1) % (PsChars / 2));
                // Gruppe 0A: Typ 0, TP 0, PTY, TA 0, M/S 1 (Musik), DI d0 = 1 (Stereo) in der vierten Gruppe, Adresse des Zeichenpaars.
                blocks[1] = static_cast<uint16_t>((static_cast<uint16_t>(Pty) << 5) | 0x0008 | (segment == 3 ? 0x0004 : 0) | segment);
                blocks[2] = NoAf;
                blocks[3] = static_cast<uint16_t>((m_ps[2 * segment] << 8) | m_ps[2 * segment + 1]);
            }
            else
            {
                const uint8_t segment = m_rtSegment;
                m_rtSegment = static_cast<uint8_t>((m_rtSegment + 1) % m_rtSegments);
                // Gruppe 2A: Typ 2, TP 0, PTY, A/B-Bit, Adresse des Viererblocks.
                blocks[1] = static_cast<uint16_t>(0x2000 | (static_cast<uint16_t>(Pty) << 5) | (m_ab ? 0x0010 : 0) | segment);
                blocks[2] = static_cast<uint16_t>((m_rt[4 * segment] << 8) | m_rt[4 * segment + 1]);
                blocks[3] = static_cast<uint16_t>((m_rt[4 * segment + 2] << 8) | m_rt[4 * segment + 3]);
            }
            m_rtTurn = !m_rtTurn;
        }

        /** @brief Programmname als RDS-Zeichen, acht Byte ohne Nullbyte. */
        const uint8_t *ps() const
        {
            return m_ps;
        }

        /** @brief Gruppen 2A je Durchlauf des Radiotexts, 0 ohne Text. */
        uint8_t rtSegments() const
        {
            return m_rtSegments;
        }

        /** @brief Aktuelles A/B-Bit des Radiotexts. */
        bool abFlag() const
        {
            return m_ab;
        }

    private:
        uint8_t m_ps[PsChars] = {' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '}; ///< Programmname
        uint8_t m_rt[RtChars] = {};                                       ///< Radiotext mit CR und Füllung
        uint8_t m_rtSegments = 0;                                         ///< Gruppen 2A je Durchlauf
        uint8_t m_psSegment = 0;                                          ///< Nächstes Zeichenpaar
        uint8_t m_rtSegment = 0;                                          ///< Nächster Viererblock
        bool m_ab = false;                                                ///< A/B-Bit
        bool m_rtTurn = false;                                            ///< Die nächste Gruppe ist 2A
    };

    /** @brief Zugriff auf den I2C-Bus. Auf dem Pico über den I2C-Master-Treiber von ESP-IDF (FmBus in main.cpp, select() prüft die Adresse dort still), im Hosttest eine Attrappe. Jeder Aufruf überträgt eine kurze Nachricht und kehrt sofort zurück. */
    class Bus
    {
    public:
        virtual ~Bus() = default;
        /** @brief Wählt die Geräteadresse (7 Bit) für die folgenden Zugriffe. */
        virtual void select(uint8_t address) = 0;
        /** @brief Schreibt count Byte ab Register reg (fortlaufende Adressen). @return false ohne Bestätigung des Chips. */
        virtual bool write(uint8_t reg, const uint8_t *data, size_t count) = 0;
        /** @brief Liest ein Register. @return false ohne Bestätigung des Chips. */
        virtual bool read(uint8_t reg, uint8_t &value) = 0;
    };

    /** @brief Logausgabe der Zustandsmaschine, eine Zeile je Aufruf ohne Zeilenende. */
    using LogLine = void (*)(const char *line);

    /**
     * @brief Zustandsmaschine des Modulators: findet den Chip, schreibt die Register nach Datenblatt, folgt den Wünschen des Tab5 (an/aus, Frequenz, Leistung, Rate, Stumm, RDS-Texte) und hält den RDS-Puffer gefüllt.
     *
     * service() wird aus loop() im Takt von 5 ms aufgerufen und wartet nie. Wartezeiten laufen über Zeitstempel. Ohne Modulator prüft er alle ProbeIntervalMs mit einer I2C-Abfrage nach. Antwortet der Chip dreimal hintereinander nicht, gilt er als verschwunden und UKW als aus. Nur aus loop().
     */
    class Modulator
    {
    public:
        /** @param bus I2C-Zugriff. @param log Logzeilen, nullptr ohne Log. */
        Modulator(Bus &bus, LogLine log) : m_bus(bus), m_log(log)
        {
        }

        /** @brief Sender ein- oder ausschalten. Wirkt, sobald ein Modulator da ist. */
        void setEnabled(bool enabled)
        {
            m_enabled = enabled;
        }

        /** @brief Frequenz in kHz, vom Aufrufer mit validKhz() geprüft. Ein Wechsel geht sofort an den Chip. */
        void setFrequency(uint32_t khz)
        {
            if (khz == m_khz)
                return;
            m_khz = khz;
            m_frequencyDirty = true;
        }

        /** @brief Leistungsstufe 0..MaxPower, ein Wechsel geht sofort an den Chip. */
        void setPower(uint8_t power)
        {
            power = power > MaxPower ? MaxPower : power;
            if (power == m_power)
                return;
            m_power = power;
            m_powerDirty = true;
        }

        /** @brief Quellrate des I2S vom Tab5 in Hz, bei einer Rate außerhalb Rates bleibt der Modulator stumm. */
        void setRate(uint32_t rate)
        {
            if (rate == m_rate)
                return;
            m_rate = rate;
            m_rateDirty = true;
        }

        /** @brief Stummwunsch des Tab5 (STUMM AN/AUS). */
        void setMuted(bool muted)
        {
            m_tab5Muted = muted;
        }

        /** @brief Programmname für RDS. @return true bei Änderung. */
        bool setPs(const char *utf8)
        {
            return m_rds.setPs(utf8);
        }

        /** @brief Radiotext für RDS. @return true bei Änderung. */
        bool setRt(const char *utf8)
        {
            return m_rds.setRt(utf8);
        }

        /** @brief Ein Schritt der Zustandsmaschine. @param nowMs millis(). */
        void service(uint32_t nowMs)
        {
            switch (m_state)
            {
            case State::Absent:
                if (!m_probed || nowMs - m_probeAtMs >= ProbeIntervalMs)
                {
                    m_probeAtMs = nowMs;
                    const bool first = !m_probed;
                    m_probed = true;
                    if (!probe())
                    {
                        if (first)
                            say("UKW kein Modulator an G32/G33 (Adressen 0x2B, 0x2C, 0x2A), Nachprüfung alle %lu s", static_cast<unsigned long>(ProbeIntervalMs / 1000));
                        return;
                    }
                    say("UKW Modulator %s an 0x%02X, Revision %u.%u%s", name(m_chip), m_address, m_revisionMajor, m_revisionMinor, m_chip == Chip::Qn8007L ? ", ohne RDS" : ", mit RDS");
                    if (!writeReg(System2, System2SwRst))
                        return;
                    m_state = State::Resetting;
                    m_stateAtMs = nowMs;
                }
                return;
            case State::Resetting:
                if (nowMs - m_stateAtMs < ResetSettleMs)
                    return;
                if (!program())
                    return;
                m_state = State::Idle;
                m_presenceChanged = true;
                say("UKW bereit: %lu kHz, Leistung %u (%u,%u dBµV), Hub %u,%u kHz, Pilot %u %%, 50 µs, Rate %s", static_cast<unsigned long>(m_khz), m_power, powerDbuV10(m_power) / 10, powerDbuV10(m_power) % 10, static_cast<unsigned>(Deviation * 69 / 100), static_cast<unsigned>(Deviation * 69 / 10 % 10), PilotGain, rateSupported() ? "passt" : "nicht unterstützt");
                break;
            case State::Idle:
            case State::Starting:
            case State::On:
                break;
            }
            if (!applyPending(nowMs))
                return;
            if (m_state == State::Idle && m_enabled)
            {
                if (!writeReg(System1, runWord(true)))
                    return;
                m_state = State::Starting;
                m_stateAtMs = nowMs;
                say("UKW Sender an, %lu kHz", static_cast<unsigned long>(m_khz));
            }
            else if (m_state != State::Idle && !m_enabled)
            {
                if (!writeReg(System1, runWord(false)))
                    return;
                m_state = State::Idle;
                m_rdsState = RdsState::Ready;
                applyMute(nowMs);
                say("UKW Sender aus");
            }
            else if (m_state == State::Starting && nowMs - m_stateAtMs >= StartSettleMs)
            {
                m_state = State::On;
                m_rdsState = RdsState::Ready;
                say("UKW sendet auf %lu kHz%s", static_cast<unsigned long>(m_khz), audible() ? "" : ", stumm");
            }
            if (!applyMute(nowMs))
                return;
            if (m_state == State::On)
                serviceRds(nowMs);
        }

        /** @brief Modulator gefunden und eingerichtet. */
        bool present() const
        {
            return m_state != State::Absent && m_state != State::Resetting;
        }

        /** @brief Erkannter Chip, None ohne Modulator. */
        Chip chip() const
        {
            return m_chip;
        }

        /** @brief Zustand der Maschine. */
        State state() const
        {
            return m_state;
        }

        /** @brief I2C-Adresse des gefundenen Chips. */
        uint8_t address() const
        {
            return m_address;
        }

        /** @brief Gewünschte Frequenz in kHz. */
        uint32_t frequency() const
        {
            return m_khz;
        }

        /** @brief Gewünschte Leistungsstufe. */
        uint8_t power() const
        {
            return m_power;
        }

        /** @brief Quellrate laut setRate(). */
        uint32_t rate() const
        {
            return m_rate;
        }

        /** @brief Der Chip nimmt die Quellrate an. */
        bool rateSupported() const
        {
            return rateCode(m_rate) >= 0;
        }

        /** @brief Der Sender läuft und gibt den Ton wieder, also nicht stumm. */
        bool audible() const
        {
            return m_state == State::On && !m_muteWanted;
        }

        /** @brief Zustand im Protokoll: FEHLT, AUS, START, AN oder STUMM. */
        const char *stateName() const
        {
            switch (m_state)
            {
            case State::Absent:
                return "FEHLT";
            case State::Resetting:
            case State::Idle:
                return "AUS";
            case State::Starting:
                return "START";
            default:
                return audible() ? "AN" : "STUMM";
            }
        }

        /** @brief Liefert einmal true, nachdem ein Modulator erkannt wurde oder verschwunden ist. */
        bool takePresenceChange()
        {
            const bool changed = m_presenceChanged;
            m_presenceChanged = false;
            return changed;
        }

        /** @brief Übergebene RDS-Gruppen seit dem Start. */
        uint32_t rdsGroups() const
        {
            return m_rdsGroups;
        }

        /** @brief RDS-Gruppen, die der Chip nicht binnen RdsTimeoutMs übernahm. */
        uint32_t rdsTimeouts() const
        {
            return m_rdsTimeouts;
        }

        /** @brief Busfehler seit dem Start. */
        uint32_t busErrors() const
        {
            return m_busErrors;
        }

        /** @brief Zuletzt geschriebener Wert von SYSTEM1, für Hosttest und Diagnose. */
        uint8_t system1() const
        {
            return m_system1;
        }

        /** @brief Zuletzt geschriebener Wert von ANACTL1. */
        uint8_t anaCtl1() const
        {
            return m_anaCtl1;
        }

    private:
        /** @brief Schritte der RDS-Übergabe. */
        enum class RdsState : uint8_t
        {
            Ready,  ///< Nächste Gruppe schreiben
            Waiting ///< Auf das Kippen von RDS_RXTXUPD warten
        };

        /** @brief Loggt eine formatierte Zeile, falls ein Log angemeldet ist. */
        __attribute__((format(printf, 2, 3))) void say(const char *format, ...)
        {
            if (!m_log)
                return;
            char line[128];
            va_list args;
            va_start(args, format);
            vsnprintf(line, sizeof(line), format, args);
            va_end(args);
            m_log(line);
        }

        /** @brief Testet die Adressen der Reihe nach über CID2. Merkt Adresse, Chip und Revision. @return true mit erkanntem Chip. */
        bool probe()
        {
            for (size_t i = 0; i < AddressCount; ++i)
            {
                m_bus.select(Addresses[i]);
                uint8_t cid2 = 0;
                if (!m_bus.read(Cid2, cid2))
                    continue;
                const Chip chip = chipFromCid2(cid2);
                if (chip == Chip::None)
                    continue;
                uint8_t cid1 = 0;
                m_bus.read(Cid1, cid1);
                m_address = Addresses[i];
                m_chip = chip;
                m_revisionMajor = static_cast<uint8_t>((cid2 & 0x0F) + 1);
                m_revisionMinor = static_cast<uint8_t>((cid1 & 0x03) + 1);
                m_busErrors = 0;
                m_errorRun = 0;
                return true;
            }
            return false;
        }

        /** @brief Wertet einen Busfehler: nach MaxBusErrors hintereinander gilt der Modulator als verschwunden. @return immer false. */
        bool fail(const char *what)
        {
            ++m_busErrors;
            if (++m_errorRun < MaxBusErrors)
                return false;
            say("UKW Modulator antwortet nicht mehr (%s), Nachprüfung alle %lu s", what, static_cast<unsigned long>(ProbeIntervalMs / 1000));
            m_state = State::Absent;
            m_chip = Chip::None;
            m_errorRun = 0;
            m_rdsState = RdsState::Ready;
            m_presenceChanged = true;
            return false;
        }

        /** @brief Schreibt ein Register und führt die Schatten von SYSTEM1, SYSTEM2 und ANACTL1 nach. */
        bool writeReg(uint8_t reg, uint8_t value)
        {
            if (!m_bus.write(reg, &value, 1))
                return fail("Schreiben");
            m_errorRun = 0;
            if (reg == System1)
                m_system1 = value;
            else if (reg == System2)
                m_system2 = value;
            else if (reg == AnaCtl1)
                m_anaCtl1 = value;
            return true;
        }

        /** @brief SYSTEM1 für Sender an oder aus: I2S-Ton, Kanal aus CH, RDS nur beim Chip mit RDS. */
        uint8_t runWord(bool transmit) const
        {
            return static_cast<uint8_t>(System1TxI2s | System1ChManual | (m_chip == Chip::Qn8007 ? System1RdsEn : 0) | (transmit ? System1TxReq : 0));
        }

        /** @brief ANACTL1 mit oder ohne Stummbit. */
        static uint8_t anaWord(bool muted)
        {
            return static_cast<uint8_t>((muted ? AnaMute : 0) | AnaBasebandRun | AnaTxAntenna | CrystalSelect);
        }

        /** @brief Schreibt nach dem Reset alle Register: Pre-Emphasis 50 µs, Stereo, kein Standby, Basisbandreset, Quarz, I2S, Kanal, Hub, Pilot, RDS-Hub, Leistung. Der Sender bleibt dabei aus und stumm. */
        bool program()
        {
            const uint8_t i2s = i2sWord();
            const uint16_t channel = channelFor(m_khz);
            const uint8_t ch = static_cast<uint8_t>(channel & 0xFF), chStep = static_cast<uint8_t>(ChStepBase | ((channel >> 8) & 0x03));
            const uint8_t regVga = static_cast<uint8_t>((InputImpedance << 6) | CrystalCap), pilot = static_cast<uint8_t>(PilotGain << 2), rdsFdev = RdsFdevReserved | RdsDeviation, pag = static_cast<uint8_t>(PagDisable | (MaxPower - m_power));
            const bool ok = writeReg(System2, System2NoStandby) && writeReg(AnaCtl1, static_cast<uint8_t>(anaWord(true) & ~AnaBasebandRun)) && writeReg(AnaCtl1, anaWord(true)) && writeReg(RegVga, regVga) && writeReg(I2s, i2s) && writeReg(Ch, ch) && writeReg(ChStep, chStep) && writeReg(TxFdev, Deviation) && writeReg(GainTxPlt, pilot) && writeReg(RdsFdev, rdsFdev) && writeReg(PagCal, pag) && writeReg(System1, runWord(false));
            if (!ok)
                return false;
            m_frequencyDirty = m_powerDirty = m_rateDirty = false;
            m_muteWanted = true;
            return true;
        }

        /** @brief I2S-Register: 32-Bit-Wörter, Rate laut Code (bei einer nicht unterstützten Rate 48 kHz, der Ton bleibt dann stumm), Slave, Format I2S. */
        uint8_t i2sWord() const
        {
            const int code = rateCode(m_rate);
            return static_cast<uint8_t>(I2sWidth32 | ((code < 0 ? 3 : code) << 4) | I2sSlaveI2s);
        }

        /** @brief Schreibt geänderte Frequenz, Leistung und Rate. Nach einer neuen Rate werden die I2S-Pufferflags gelöscht und der Ton bleibt RateMuteMs stumm. */
        bool applyPending(uint32_t nowMs)
        {
            if (m_frequencyDirty)
            {
                const uint16_t channel = channelFor(m_khz);
                if (!writeReg(Ch, static_cast<uint8_t>(channel & 0xFF)) || !writeReg(ChStep, static_cast<uint8_t>(ChStepBase | ((channel >> 8) & 0x03))))
                    return false;
                m_frequencyDirty = false;
                say("UKW Frequenz %lu kHz", static_cast<unsigned long>(m_khz));
            }
            if (m_powerDirty)
            {
                if (!writeReg(PagCal, static_cast<uint8_t>(PagDisable | (MaxPower - m_power))))
                    return false;
                m_powerDirty = false;
                say("UKW Leistung %u (%u,%u dBµV)", m_power, powerDbuV10(m_power) / 10, powerDbuV10(m_power) % 10);
            }
            if (m_rateDirty)
            {
                const uint8_t pilot = static_cast<uint8_t>(PilotGain << 2);
                if (!writeReg(AnaCtl1, anaWord(true)) || !writeReg(I2s, i2sWord()) || !writeReg(GainTxPlt, pilot | I2sClearFlags) || !writeReg(GainTxPlt, pilot))
                    return false;
                m_muteWanted = true;
                m_rateDirty = false;
                m_unmuteNotBeforeMs = nowMs + RateMuteMs;
                if (rateSupported())
                    say("UKW Rate %lu Hz", static_cast<unsigned long>(m_rate));
                else
                    say("UKW stumm: Quellrate %lu Hz nimmt der Chip nicht an (32000, 40000, 44100, 48000)", static_cast<unsigned long>(m_rate));
            }
            return true;
        }

        /** @brief Führt das Stummbit nach: stumm, solange das Tab5 es will, der Sender nicht läuft, die Rate nicht passt oder ein Ratenwechsel jünger als RateMuteMs ist. */
        bool applyMute(uint32_t nowMs)
        {
            const bool muted = m_tab5Muted || m_state != State::On || !rateSupported() || static_cast<int32_t>(nowMs - m_unmuteNotBeforeMs) < 0;
            if (muted == m_muteWanted)
                return true;
            if (!writeReg(AnaCtl1, anaWord(muted)))
                return false;
            m_muteWanted = muted;
            if (m_state == State::On)
                say("UKW %s", muted ? "stumm" : "Ton frei");
            return true;
        }

        /** @brief Hält den RDS-Puffer gefüllt: Gruppe schreiben, RDSTXRDY kippen, dann alle RdsPollMs prüfen, ob RDS_RXTXUPD gekippt ist. Nur beim Chip mit RDS und laufendem Sender. */
        void serviceRds(uint32_t nowMs)
        {
            if (m_chip != Chip::Qn8007)
                return;
            if (m_rdsState == RdsState::Ready)
            {
                uint8_t status = 0;
                if (!m_bus.read(Status2, status))
                {
                    fail("STATUS2");
                    return;
                }
                m_rdsUpdated = status & Status2RdsUpdated;
                uint16_t blocks[4];
                m_rds.next(blocks);
                uint8_t bytes[8];
                for (size_t i = 0; i < 4; ++i)
                {
                    bytes[2 * i] = static_cast<uint8_t>(blocks[i] >> 8);
                    bytes[2 * i + 1] = static_cast<uint8_t>(blocks[i]);
                }
                if (!m_bus.write(Rds0, bytes, sizeof(bytes)))
                {
                    fail("RDS-Puffer");
                    return;
                }
                if (!writeReg(System2, static_cast<uint8_t>(m_system2 ^ System2RdsTxRdy)))
                    return;
                m_rdsState = RdsState::Waiting;
                m_rdsAtMs = m_rdsPollAtMs = nowMs;
                return;
            }
            if (nowMs - m_rdsPollAtMs < RdsPollMs)
                return;
            m_rdsPollAtMs = nowMs;
            uint8_t status = 0;
            if (!m_bus.read(Status2, status))
            {
                fail("STATUS2");
                return;
            }
            if ((status & Status2RdsUpdated) != m_rdsUpdated)
            {
                ++m_rdsGroups;
                m_rdsState = RdsState::Ready;
            }
            else if (nowMs - m_rdsAtMs >= RdsTimeoutMs)
            {
                if (m_rdsTimeouts++ == 0)
                    say("RDS Gruppe nicht übernommen (Zeitlimit %lu ms)", static_cast<unsigned long>(RdsTimeoutMs));
                m_rdsState = RdsState::Ready;
            }
        }

        Bus &m_bus;                           ///< I2C-Zugriff
        LogLine m_log;                        ///< Logausgabe, nullptr ohne
        RdsEncoder m_rds;                     ///< Gruppenbildung
        State m_state = State::Absent;        ///< Zustand
        RdsState m_rdsState = RdsState::Ready; ///< Schritt der RDS-Übergabe
        Chip m_chip = Chip::None;             ///< Erkannter Chip
        uint8_t m_address = 0;                ///< Seine Adresse
        uint8_t m_revisionMajor = 0;          ///< CID4 + 1
        uint8_t m_revisionMinor = 0;          ///< CID2 + 1
        bool m_probed = false;                ///< Erste Suche erledigt
        bool m_enabled = false;               ///< Wunsch: Sender an
        bool m_tab5Muted = true;              ///< Wunsch des Tab5: stumm
        bool m_muteWanted = true;             ///< Zuletzt geschriebenes Stummbit
        bool m_frequencyDirty = false;        ///< Frequenz noch nicht geschrieben
        bool m_powerDirty = false;            ///< Leistung noch nicht geschrieben
        bool m_rateDirty = false;             ///< Rate noch nicht geschrieben
        bool m_presenceChanged = false;       ///< Für takePresenceChange()
        uint32_t m_khz = DefaultKhz;          ///< Frequenz in kHz
        uint8_t m_power = DefaultPower;       ///< Leistungsstufe
        uint32_t m_rate = 44100;              ///< Quellrate in Hz
        uint8_t m_system1 = 0;                ///< Schatten von SYSTEM1
        uint8_t m_system2 = 0;                ///< Schatten von SYSTEM2
        uint8_t m_anaCtl1 = 0;                ///< Schatten von ANACTL1
        uint8_t m_rdsUpdated = 0;             ///< RDS_RXTXUPD vor der letzten Übergabe
        uint8_t m_errorRun = 0;               ///< Busfehler hintereinander
        uint32_t m_busErrors = 0;             ///< Busfehler gesamt
        uint32_t m_rdsGroups = 0;             ///< Übernommene Gruppen
        uint32_t m_rdsTimeouts = 0;           ///< Nicht übernommene Gruppen
        uint32_t m_probeAtMs = 0;             ///< Zeit der letzten Suche
        uint32_t m_stateAtMs = 0;             ///< Beginn des aktuellen Zustands
        uint32_t m_unmuteNotBeforeMs = 0;     ///< Frühestes Einblenden nach einem Ratenwechsel
        uint32_t m_rdsAtMs = 0;               ///< Übergabe der laufenden Gruppe
        uint32_t m_rdsPollAtMs = 0;           ///< Letzte STATUS2-Abfrage
    };
}

#endif
