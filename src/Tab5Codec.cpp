#include "Tab5Board.h"

#include <M5Unified.h>

// Registerfolge nach M5Unified 0.2.25, M5Unified::_speaker_enabled_cb_tab5.
// Copyright (c) 2021 M5Stack, MIT-Lizenz: referenz/M5Unified-LICENSE.
// Anpassungen: 32 Bit, 256fs, Start stumm und analoge Ausgangsverstärkung 0 dB.
namespace
{
    constexpr uint8_t DacPowerRegister = 0x04;  ///< ES8388-Register der DAC-Versorgung
    constexpr uint8_t ChipPowerRegister = 0x02; ///< ES8388-Register der Chip-Versorgung
    constexpr uint8_t DacPowerDown = 0xc0;      ///< Wert für abgeschaltete DACs
    constexpr uint8_t ChipPowerDown = 0xff;     ///< Wert für abgeschalteten Chip
    constexpr uint32_t MuteSettleMs = 1;        ///< Wartezeit nach dem Stummschalten vor dem Herunterfahren
    constexpr int PowerDownAttempts = 3;        ///< Versuche für Stummschalten und Herunterfahren, ein Versuch dauert etwa 1 ms plus sechs I2C-Zugriffe
    constexpr uint32_t StmStartSettleMs = 50;   ///< Einschwingzeit des Ausgangs nach dem Start der Zustandsmaschine, bevor der Verstärker freigegeben wird. Ohne sie knackte der Start hörbar

    /** @brief Ein ES8388-Register mit seinem Startwert. */
    struct CodecRegister
    {
        uint8_t address;
        uint8_t value;
    };

    /** @brief Registerfolge für den Radiobetrieb in Schreibreihenfolge: Soft-Reset, Versorgung, Format- und Ausgangsregister, zuletzt ein Reset der DAC-Zustandsmaschine. prepareRadioCodec liest die Register 0x02, 0x04, 0x08 und 0x17..0x19 zurück. */
    constexpr CodecRegister RadioRegisters[] = {
        {0x00, 0x80}, {0x00, 0x00}, {0x00, 0x00}, {0x00, 0x0e}, {0x01, 0x00}, {0x02, 0x0a}, {0x03, 0xff}, {0x04, 0x3c}, {0x05, 0x00}, {0x06, 0x00}, {0x07, 0x7c}, {0x08, 0x00}, {0x17, Tab5Board::DacWordLength}, {0x18, 0x02}, // DACFsRatio=00010: 256fs. Takte kommen vom P4.
        {0x19, 0x24}, // Soft-Ramp und DAC-Mute bleiben aktiv.
        {0x1a, 0x00},
        {0x1b, 0x00},
        {0x1c, 0x08},
        {0x1d, 0x00},
        {0x26, 0x00},
        {0x27, 0xb8},
        {0x2a, 0xb8},
        {0x2b, 0x08},
        {0x2d, 0x00},
        {0x2e, 0x1e},
        {0x2f, 0x1e},
        {0x30, 0x1e},
        {0x31, 0x1e},
        {0x02, 0x1a}, // DAC-Zustandsmaschine zurücksetzen (Bit 4) und wieder freigeben, nachdem Wortlänge und Taktverhältnis stehen: Mit dem Start aus der Reset-Vorgabe richtete sich der Empfänger etwa jeden zweiten Start neben dem Wort aus. Referenzspannung und Ausgänge bleiben dabei an, sonst knackt es.
        {0x02, 0x0a}};

    /** @brief Prüft ein Codec-Register durch Rücklesen. */
    bool codecRegisterMatches(uint8_t address, uint8_t expected)
    {
        uint8_t value = 0;
        return M5.In_I2C.readRegister(Tab5Board::CodecAddress, address, &value, 1, Tab5Board::I2cFrequency) && value == expected;
    }

    /** @brief Ein Versuch: Verstärker aus, DAC stumm, danach DAC und Chip herunterfahren und durch Rücklesen bestätigen. @return Power-down bestätigt. */
    bool powerDownOnce()
    {
        if (!Tab5Board::muteRadio())
            return false;
        m5gfx::delay(MuteSettleMs);
        if (!M5.In_I2C.writeRegister8(Tab5Board::CodecAddress, DacPowerRegister, DacPowerDown, Tab5Board::I2cFrequency) || !M5.In_I2C.writeRegister8(Tab5Board::CodecAddress, ChipPowerRegister, ChipPowerDown, Tab5Board::I2cFrequency))
        {
            Tab5Board::muteRadio();
            return false;
        }
        return codecRegisterMatches(DacPowerRegister, DacPowerDown) && codecRegisterMatches(ChipPowerRegister, ChipPowerDown);
    }
}

bool Tab5Board::muteRadio()
{
    const bool amplifierOff = M5.In_I2C.bitOff(ExpanderAddress, ExpanderOutputRegister, SpeakerMask, I2cFrequency);
    const bool codecMuted = M5.In_I2C.bitOn(CodecAddress, CodecMuteRegister, CodecMuteMask, I2cFrequency);
    return amplifierOff && codecMuted;
}

bool Tab5Board::powerDownRadioCodec()
{
    if (M5.Speaker.isRunning())
        return false;
    // Ein einzelner I2C-Fehler (NACK) darf weder den Verstärker eingeschaltet lassen noch den Codec bis zum Neustart sperren.
    for (int attempt = 0; attempt < PowerDownAttempts; ++attempt)
        if (powerDownOnce())
            return true;
    return false;
}

bool Tab5Board::prepareRadioCodec()
{
    // Ein P4-Neustart setzt den weiter versorgten Codec nicht zurück. Bei laufenden I2S-Takten zuerst die DACs vollständig herunterfahren.
    if (!powerDownRadioCodec())
        return false;
    for (const auto &reg : RadioRegisters)
    {
        if (!M5.In_I2C.writeRegister8(CodecAddress, reg.address, reg.value, I2cFrequency))
        {
            muteRadio();
            return false;
        }
    }
    // Der Ausgang schwingt nach dem Start der Zustandsmaschine ein, der Aufrufer gibt den Verstärker direkt danach frei.
    m5gfx::delay(StmStartSettleMs);
    const bool verified = codecRegisterMatches(0x02, 0x0a) && codecRegisterMatches(0x08, 0x00) && codecRegisterMatches(0x17, DacWordLength) && codecRegisterMatches(0x18, 0x02) && codecRegisterMatches(0x19, 0x24) && codecRegisterMatches(0x04, 0x3c);
    if (!verified)
        muteRadio();
    return verified;
}

bool Tab5Board::unmuteRadio(bool speaker)
{
    if (!codecRegisterMatches(0x17, DacWordLength) || !codecRegisterMatches(0x18, 0x02) || !codecRegisterMatches(0x04, 0x3c))
        return false;
    // Ohne Lautsprecher bleibt der Verstärker aus dem vorangegangenen Power-down aus.
    if (!M5.In_I2C.bitOff(CodecAddress, CodecMuteRegister, CodecMuteMask, I2cFrequency) || (speaker && !M5.In_I2C.bitOn(ExpanderAddress, ExpanderOutputRegister, SpeakerMask, I2cFrequency)))
    {
        muteRadio();
        return false;
    }
    return true;
}

bool Tab5Board::setSpeaker(bool on)
{
    return on ? M5.In_I2C.bitOn(ExpanderAddress, ExpanderOutputRegister, SpeakerMask, I2cFrequency) : M5.In_I2C.bitOff(ExpanderAddress, ExpanderOutputRegister, SpeakerMask, I2cFrequency);
}

bool Tab5Board::readSpeaker(bool &on)
{
    uint8_t value = 0;
    if (!M5.In_I2C.readRegister(ExpanderAddress, ExpanderOutputRegister, &value, 1, I2cFrequency))
        return false;
    on = value & SpeakerMask;
    return true;
}

bool Tab5Board::readHeadphone(bool &inserted)
{
    uint8_t value = 0;
    if (!M5.In_I2C.readRegister(ExpanderAddress, ExpanderInputRegister, &value, 1, I2cFrequency))
        return false;
    inserted = value & HeadphoneMask;
    return true;
}

bool Tab5Board::readExpander(uint8_t address, ExpanderRegisters &out)
{
    const struct
    {
        uint8_t reg;
        uint8_t *value;
    } reads[] = {{ExpanderDirectionRegister, &out.direction}, {ExpanderOutputRegister, &out.output}, {ExpanderHighImpedanceRegister, &out.highImpedance}, {ExpanderPullEnableRegister, &out.pullEnable}, {ExpanderPullSelectRegister, &out.pullSelect}, {ExpanderInputRegister, &out.input}};

    for (const auto &read : reads)
        if (!M5.In_I2C.readRegister(address, read.reg, read.value, 1, I2cFrequency))
            return false;
    return true;
}

bool Tab5Board::setBusPower(bool on)
{
    // Erst den Pull, dann das Latch, in der Reihenfolge von M5Unified. Das Latch wirkt nur, falls P2 doch Ausgang ist.
    const bool pull = on ? M5.In_I2C.bitOn(ExpanderAddress, ExpanderPullSelectRegister, BusPowerMask, I2cFrequency) : M5.In_I2C.bitOff(ExpanderAddress, ExpanderPullSelectRegister, BusPowerMask, I2cFrequency);
    if (!pull || !M5.In_I2C.bitOn(ExpanderAddress, ExpanderPullEnableRegister, BusPowerMask, I2cFrequency))
        return false;
    const bool latch = on ? M5.In_I2C.bitOn(ExpanderAddress, ExpanderOutputRegister, BusPowerMask, I2cFrequency) : M5.In_I2C.bitOff(ExpanderAddress, ExpanderOutputRegister, BusPowerMask, I2cFrequency);
    // Der Pegel am Pin bestätigt, dass der interne Pull gegen die Beschaltung von EXT5V_EN ankommt.
    uint8_t level = 0;
    return latch && M5.In_I2C.readRegister(ExpanderAddress, ExpanderInputRegister, &level, 1, I2cFrequency) && static_cast<bool>(level & BusPowerMask) == on;
}

bool Tab5Board::readCodecRegister(uint8_t address, uint8_t &value)
{
    return M5.In_I2C.readRegister(CodecAddress, address, &value, 1, I2cFrequency);
}

bool Tab5Board::writeCodecRegister(uint8_t address, uint8_t value)
{
    if (!M5.In_I2C.writeRegister8(CodecAddress, address, value, I2cFrequency))
        return false;
    return codecRegisterMatches(address, value);
}
