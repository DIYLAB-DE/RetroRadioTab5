#ifndef TAB5_PPA_GUARD_H
#define TAB5_PPA_GUARD_H
#include <cstdint>

/** @brief Rechenregel zum Chipfehler DIG-734 der PPA für Übertragungen mit 90 oder 270 Grad Drehung, RGB565 und Maßstab 1. Die 2D-DMA zählt bei eingeschalteter Makroblock-Reihenfolge einen Restblock falsch, der ihre FIFO-Tiefe nicht übersteigt. Der Treiber schaltet die Reihenfolge dann ab, rechnet in ESP-IDF 5.5.2 (Kernbibliotheken, esp-idf 87912cd291) aber mit den ungedrehten Maßen des Blocks. Die Regel nennt die Flächen, für die das nicht reicht, und den Teil davon, der trotzdem an die PPA gehen darf. Enthält der Treiber die Korrektur aus esp-idf 469aa16, bleibt nur der Grenzfall von genau FifoBits übrig. */
namespace Tab5PpaGuard
{
    constexpr uint32_t TransferWidth = 64; ///< Pixel, in die die PPA eine Zeile bei RGB565 am Ausgang teilt
    constexpr uint32_t MacroBlock = 16;    ///< Zeilen eines Makroblocks, fest 16 auf Chiprevision unter 3 (esp32p4_es)
    constexpr uint32_t PixelBits = 16;     ///< Bit je Pixel am Ausgang
    constexpr uint32_t FifoBits = 12 * 128; ///< FIFO-Tiefe der 2D-DMA in Bit

    /** @brief Größe des letzten Teilblocks in Bit. Ein Rest 0 zählt als ganzer Block. @param width Breite in Teilungsrichtung TransferWidth. @param height Höhe in Teilungsrichtung MacroBlock. */
    constexpr uint32_t tailBits(uint32_t width, uint32_t height)
    {
        const uint32_t tailWidth = width % TransferWidth ? width % TransferWidth : TransferWidth;
        const uint32_t tailHeight = height % MacroBlock ? height % MacroBlock : MacroBlock;
        return tailWidth * tailHeight * PixelBits;
    }

    /** @brief true, wenn die PPA die Fläche in mehr als einen Teilblock zerlegt. Parameter wie tailBits. */
    constexpr bool split(uint32_t width, uint32_t height)
    {
        return width > TransferWidth || height > MacroBlock;
    }

    /** @brief Entscheidung des Treibers der Kernbibliotheken: Er schaltet die Makroblock-Reihenfolge ab, wenn der Restblock nach den ungedrehten Maßen unter FifoBits liegt. @param blockWidth in.block_w des Auftrags. @param blockHeight in.block_h des Auftrags. */
    constexpr bool driverBypasses(uint32_t blockWidth, uint32_t blockHeight)
    {
        return split(blockWidth, blockHeight) && tailBits(blockWidth, blockHeight) < FifoBits;
    }

    /** @brief true, wenn die Makroblock-Reihenfolge bei 90 oder 270 Grad abgeschaltet sein muss. Die Drehung vertauscht Breite und Höhe des Blocks. Ein Restblock von genau FifoBits zählt mit, weil auch er die Fertigmeldung verlieren kann (espressif/esp-idf#19462). Parameter wie driverBypasses. */
    constexpr bool needsBypass(uint32_t blockWidth, uint32_t blockHeight)
    {
        return split(blockHeight, blockWidth) && tailBits(blockHeight, blockWidth) <= FifoBits;
    }

    /** @brief true für eine Fläche, die bei 90 oder 270 Grad nicht an die PPA gehen darf: Die Abschaltung wäre nötig, der Treiber nimmt sie aber nicht vor. Parameter wie driverBypasses. */
    constexpr bool unprotected(uint32_t blockWidth, uint32_t blockHeight)
    {
        return needsBypass(blockWidth, blockHeight) && !driverBypasses(blockWidth, blockHeight);
    }

    /** @brief Teil einer Fläche ab ihrer linken oberen Ecke, in den Maßen des Auftrags. */
    struct Part
    {
        uint32_t width;  ///< in.block_w des Teils, 0 ohne Teil
        uint32_t height; ///< in.block_h des Teils, 0 ohne Teil
    };

    /** @brief Größter Teil ab der linken oberen Ecke, den die PPA bei 90 oder 270 Grad übertragen darf. Eine Fläche, für die unprotected nicht gilt, bleibt ganz. Sonst endet die Breite auf einem Vielfachen von MacroBlock und, wenn das nicht reicht, die Höhe auf einem Vielfachen von TransferWidth, dann ist der gedrehte Restblock ein voller Teilblock. Übrig bleiben für einen anderen Weg rechts höchstens MacroBlock - 1 Spalten und unten höchstens 6 Zeilen (FifoBits / (MacroBlock * PixelBits)). @return {0, 0}, wenn kein solcher Teil bleibt, also bei weniger als MacroBlock Spalten oder bei höchstens 6 Zeilen. Parameter wie driverBypasses. */
    constexpr Part safePart(uint32_t blockWidth, uint32_t blockHeight)
    {
        if (!unprotected(blockWidth, blockHeight))
            return {blockWidth, blockHeight};
        const uint32_t width = blockWidth - blockWidth % MacroBlock;
        uint32_t height = blockHeight;
        if (width && unprotected(width, height))
            height -= height % TransferWidth;
        if (!width || !height || unprotected(width, height))
            return {0, 0};
        return {width, height};
    }
}
#endif
