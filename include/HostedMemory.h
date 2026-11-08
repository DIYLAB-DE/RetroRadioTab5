#ifndef HOSTED_MEMORY_H
#define HOSTED_MEMORY_H

#include <cstddef>
#include <cstdint>

/** @brief Legt die DMA-Transportpuffer von esp_hosted (zwei SDIO-Empfangspuffer bis 30.720 Byte, Paketpoolblöcke zu 1.664 Byte) ins PSRAM statt in den knappen internen DMA-Speicher. Dazu ersetzt install() den Eintrag _h_malloc_align der öffentlichen Funktionstabelle g_hosted_osi_funcs, über den esp_hosted alle diese Puffer anfordert. Freigegeben wird weiter über _h_free_align (free()), das beide Speicher kennt. */
namespace HostedMemory
{
    constexpr size_t PsramThreshold = 512; ///< Anforderungen ab dieser Größe in Bytes gehen zuerst ins PSRAM. Ihre SDIO-Übertragungen (ganze 512er-Blöcke) passen in den Puffer. Kleinere bleiben intern: Der Registerpuffer (20 Byte) liest über den Ausweichpuffer des SDMMC-Treibers, die Slave-Konfiguration (320 Byte) wird auf 512 Byte aufgefüllt gesendet, Übertragung und Cachepflege reichten im PSRAM über den Puffer hinaus.

    /** @brief Zählerstand seit dem Start. Bytes sind die angeforderten Größen ohne Aufrundung, Freigaben werden nicht abgezogen. */
    struct Stats
    {
        uint32_t psramCount;    ///< Anforderungen, die im PSRAM landeten
        uint32_t psramBytes;    ///< Summe dieser Anforderungen in Bytes
        uint32_t internalCount; ///< Anforderungen, die im internen DMA-Speicher landeten (unter der Schwelle oder PSRAM voll)
        uint32_t internalBytes; ///< Summe dieser Anforderungen in Bytes
        uint32_t largest;       ///< Größte Anforderung in Bytes, auch eine gescheiterte
    };

    /** @brief Leitet die Allokationen von esp_hosted um. Einmal in setup() vor jedem WLAN-Aufruf aufrufen, solange esp_hosted noch nichts angefordert hat. Ein weiterer Aufruf ändert nichts. */
    void install();

    /** @brief Liefert eine in sich stimmige Kopie der Zähler. Aus jedem Task, nicht aus Interrupts. */
    Stats stats();
}

#endif
