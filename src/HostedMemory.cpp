#include "HostedMemory.h"

#include <cstddef>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>

// Die Header von esp_hosted 2.11.6 haben keine eigene extern-"C"-Klammer.
extern "C"
{
#include <esp_hosted_host_fw_ver.h>
#include <esp_hosted_os_abstraction.h>
}

// Die Umleitung beruht auf dem disassemblierten Code genau dieser Fassung: alle Aufrufer von _h_malloc_align, ihre Größen und Übertragungslängen sowie die Funktionstabelle g_hosted_osi_funcs. Eine andere Fassung muss erst neu geprüft werden, bevor diese Sperre angepasst wird.
static_assert(ESP_HOSTED_VERSION_MAJOR_1 == 2 && ESP_HOSTED_VERSION_MINOR_1 == 11 && ESP_HOSTED_VERSION_PATCH_1 == 6, "esp_hosted-Fassung geändert: Aufrufer von _h_malloc_align und SDIO-Übertragungslängen neu prüfen");
// Die Bibliothek ruft den Eintrag als siebten Zeiger der Tabelle auf (Offset 24 auf dem P4). Er steht vor allen bedingt übersetzten Einträgen, seine Lage hängt nicht von deren Makros ab.
static_assert(offsetof(hosted_osi_funcs_t, _h_malloc_align) == 6 * sizeof(void *), "Aufbau von hosted_osi_funcs_t weicht von esp_hosted 2.11.6 ab");

namespace
{
    constexpr uint32_t PsramCaps = MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA | MALLOC_CAP_8BIT; ///< Fähigkeiten für den PSRAM-Weg. Das DMA-Bit lässt den Heap Adresse und Länge auf die Cachezeile legen (esp_heap_adjust_alignment_to_hw), danach entfernt er es für SPIRAM. So teilt sich kein Puffer eine Cachezeile mit Nachbarn, deren Inhalt die Cachepflege des SDMMC-Treibers (Zurückschreiben vor, Verwerfen nach der Übertragung) sonst träfe

    void *(*s_original)(size_t, size_t) = nullptr;           ///< Bisheriger Eintrag (hosted_malloc_align: intern, DMA, 8 Bit) für kleine Anforderungen und als Rückfall
    HostedMemory::Stats s_stats = {};                        ///< Zähler, nur unter s_lock
    portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;      ///< Schützt s_stats gegen die Tasks von esp_hosted auf beiden Kernen

    /** @brief Ersatz für _h_malloc_align, aufgerufen aus den Tasks von esp_hosted. Ab HostedMemory::PsramThreshold zuerst PSRAM mit der verlangten Ausrichtung, sonst und bei vollem PSRAM der bisherige interne Weg. @param size Größe in Bytes. @param align Verlangte Ausrichtung in Bytes (esp_hosted: 64). @return Puffer oder nullptr. */
    void *allocate(size_t size, size_t align)
    {
        void *buffer = size >= HostedMemory::PsramThreshold ? heap_caps_aligned_alloc(align, size, PsramCaps) : nullptr;
        const bool psram = buffer != nullptr;
        if (!buffer)
            buffer = s_original(size, align);
        taskENTER_CRITICAL(&s_lock);
        if (psram)
        {
            ++s_stats.psramCount;
            s_stats.psramBytes += static_cast<uint32_t>(size);
        }
        else if (buffer)
        {
            ++s_stats.internalCount;
            s_stats.internalBytes += static_cast<uint32_t>(size);
        }
        if (size > s_stats.largest)
            s_stats.largest = static_cast<uint32_t>(size);
        taskEXIT_CRITICAL(&s_lock);
        return buffer;
    }
}

void HostedMemory::install()
{
    if (g_hosted_osi_funcs._h_malloc_align == allocate)
        return;
    s_original = g_hosted_osi_funcs._h_malloc_align;
    g_hosted_osi_funcs._h_malloc_align = allocate;
}

HostedMemory::Stats HostedMemory::stats()
{
    taskENTER_CRITICAL(&s_lock);
    const Stats copy = s_stats;
    taskEXIT_CRITICAL(&s_lock);
    return copy;
}
