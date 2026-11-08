#include "Tab5Board.h"

#include <M5Unified.h>
#include <esp_heap_caps.h>

bool Tab5Board::silenceBeforeDisplay()
{
    // M5GFX setzt beim Erkennen des Tab5 kurz SPK_EN. Der Codec muss deshalb bereits vorher stumm sein.
    if (!M5.In_I2C.begin(I2C_NUM_1, I2cSda, I2cScl))
        return false;
    const bool amplifierOff = M5.In_I2C.bitOff(ExpanderAddress, ExpanderOutputRegister, SpeakerMask, I2cFrequency);
    const bool codecMuted = M5.In_I2C.bitOn(CodecAddress, CodecMuteRegister, CodecMuteMask, I2cFrequency);
    M5.In_I2C.release();
    return amplifierOff && codecMuted;
}

bool Tab5Board::testPsram(size_t bytes)
{
    if (bytes == 0 || bytes % sizeof(uint32_t) != 0)
        return false;
    auto buffer = static_cast<volatile uint32_t *>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!buffer)
        return false;
    bool passed = true;
    constexpr uint32_t Patterns[] = {0x55aa55aaU, 0xaa55aa55U};
    for (const uint32_t pattern : Patterns)
    {
        for (size_t index = 0; index < bytes / sizeof(uint32_t); ++index)
            buffer[index] = pattern ^ static_cast<uint32_t>(index);
        for (size_t index = 0; index < bytes / sizeof(uint32_t); ++index)
        {
            if (buffer[index] != (pattern ^ static_cast<uint32_t>(index)))
            {
                passed = false;
                break;
            }
        }
        if (!passed)
            break;
    }
    heap_caps_free(const_cast<uint32_t *>(buffer));
    return passed;
}
