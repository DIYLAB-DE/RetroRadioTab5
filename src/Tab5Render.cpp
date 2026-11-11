#include "Tab5Render.h"
#include "Tab5PixelTransfer.h"
#include "Logger.h"
#include <lgfx/v1/platforms/esp32p4/Panel_DSI.hpp>
#include <lgfx/v1/platforms/esp32p4/Bus_DSI.hpp>
#include <Arduino.h>
#include <algorithm>
#include <atomic>
#include <driver/ppa.h>
#include <esp_cache.h>
#include <esp_lcd_mipi_dsi.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <sdkconfig.h>

namespace
{
    constexpr uint32_t PpaTimeoutUs = 500000; ///< Zeitgrenze für die Fertigmeldung einer PPA-Übertragung, ein Vollbild mit Drehung braucht rund 45 ms
    constexpr uintptr_t CacheLine = CONFIG_CACHE_L2_CACHE_LINE_SIZE; ///< Cachezeile des PSRAM in Byte (64 in den Kernbibliotheken). Die PPA verlangt den Bildspeicher auf dieser Grenze, esp_lcd legt ihn so an

    lgfx::LGFX_Device *s_display = nullptr; ///< Das in begin() gebundene Display
    uint16_t *s_framebuffer = nullptr;      ///< DSI-Framebuffer in Panelausrichtung (720x1280)
    unsigned s_mode = 2;                    ///< 2 = PPA, 1 = gekachelter CPU-Weg (gilt ohne PPA und ab dem ersten PPA-Fehler)
    ppa_client_handle_t s_ppa = nullptr;    ///< PPA-Client, nullptr ohne PPA
    StaticSemaphore_t s_doneStorage;        ///< Speicher des Fertig-Semaphors im internen RAM, statisch, damit eine verspätete Meldung nie freigegebenen Speicher trifft
    SemaphoreHandle_t s_done = nullptr;     ///< Gibt onDone im Interrupt, nimmt pushRegion mit Zeitgrenze
    std::atomic<uint32_t> s_doneJob{0};     ///< Auftragsnummer der letzten Fertigmeldung, schreibt onDone
    std::atomic<uint32_t> s_doneAt{0};      ///< Zeitpunkt der letzten Fertigmeldung (untere 32 Bit von esp_timer in µs)
    uint32_t s_jobs = 0;                    ///< Nummer des letzten PPA-Auftrags, nur UI-Task
    uint32_t s_submittedAt = 0;             ///< Zeitpunkt des letzten PPA-Auftrags wie s_doneAt, nur UI-Task
    bool s_pending = false;                 ///< Der Auftrag s_jobs wartet nach der Zeitgrenze noch auf seine Meldung, bis dahin überträgt die CPU
    uint32_t s_timeouts = 0;                ///< Zeitgrenzen seit dem Start, nur UI-Task
    uint32_t s_late = 0;                    ///< Verspätete Meldungen seit dem Start, nur UI-Task
    StaticSemaphore_t s_frameStorage;       ///< Speicher des Bild-Semaphors im internen RAM
    SemaphoreHandle_t s_frame = nullptr;    ///< Gibt onRefreshDone im Interrupt, nimmt waitFrame
    std::atomic<uint32_t> s_frames{0};      ///< Bildfertigmeldungen seit dem Start, schreibt onRefreshDone
    std::atomic<uint32_t> s_frameAtMs{0};   ///< Zeitpunkt der letzten Bildfertigmeldung auf der Basis von millis(), schreibt onRefreshDone
    std::atomic<uint32_t> s_frameAtUs{0};   ///< Zeitpunkt der letzten Bildfertigmeldung wie s_doneAt, schreibt onRefreshDone
    std::atomic<uint32_t> s_framePeriodUs{0}; ///< Abstand der letzten beiden Bildfertigmeldungen in µs, schreibt onRefreshDone
    bool s_frameSync = false;               ///< Bildsignal registriert. Fällt auf false, wenn danach keine Meldung kommt, nur UI-Task

    /** @brief Untere 32 Bit der Systemzeit in µs, Differenzen bleiben rund 71 min richtig. Auch im Interrupt nutzbar. */
    uint32_t nowUs()
    {
        return static_cast<uint32_t>(esp_timer_get_time());
    }

    /** @brief on_trans_done der PPA, im DMA2D-Interrupt. @param user Auftragsnummer. @return true, wenn ein Taskwechsel nötig ist. */
    bool onDone(ppa_client_handle_t, ppa_event_data_t *, void *user)
    {
        s_doneAt.store(nowUs());
        s_doneJob.store(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(user)));
        BaseType_t woken = pdFALSE;
        xSemaphoreGiveFromISR(s_done, &woken);
        return woken == pdTRUE;
    }

    /** @brief Zugang zum DPI-Panel-Handle, den M5GFX in Panel_DSI geschützt hält. Die Zeigerbildung auf das geschützte Mitglied aus einer abgeleiteten Klasse ist reguläres C++ und braucht keinen Bibliothekspatch. Die Klasse wird nie instanziiert. */
    struct PanelZugang : lgfx::Panel_DSI
    {
        static esp_lcd_panel_handle_t handle(lgfx::Panel_DSI *panel)
        {
            return panel->*(&PanelZugang::_disp_panel_handle);
        }
    };

    /** @brief on_refresh_done des DPI-Treibers, im Interrupt am Ende jedes ans Panel ausgelesenen Bildes. @return true, wenn ein Taskwechsel nötig ist. */
    bool onRefreshDone(esp_lcd_panel_handle_t, esp_lcd_dpi_panel_event_data_t *, void *)
    {
        const int64_t now = esp_timer_get_time();
        const uint32_t us = static_cast<uint32_t>(now);
        s_frameAtMs.store(static_cast<uint32_t>(now / 1000));
        if (s_frames.load())
            s_framePeriodUs.store(us - s_frameAtUs.load());
        s_frameAtUs.store(us);
        s_frames.fetch_add(1);
        BaseType_t woken = pdFALSE;
        xSemaphoreGiveFromISR(s_frame, &woken);
        return woken == pdTRUE;
    }

    /** @brief Cacheabgleich eines Bereichs, auf ganze Cachezeilen (CacheLine) ausgeweitet. @param direction ESP_CACHE_MSYNC_FLAG_DIR_C2M (CPU-Änderungen in den Speicher schreiben) oder ESP_CACHE_MSYNC_FLAG_DIR_M2C (CPU-Kopie verwerfen). */
    void syncCache(const void *address, size_t bytes, int direction)
    {
        const uintptr_t begin = reinterpret_cast<uintptr_t>(address) & ~(CacheLine - 1);
        const uintptr_t end = (reinterpret_cast<uintptr_t>(address) + bytes + CacheLine - 1) & ~(CacheLine - 1);
        esp_cache_msync(reinterpret_cast<void *>(begin), end - begin, direction | ESP_CACHE_MSYNC_FLAG_TYPE_DATA);
    }

    /** @brief Nimmt die verspätete Meldung des offenen Auftrags an, sobald sie da ist. Danach überträgt wieder die PPA. Nur UI-Task. */
    void checkPending()
    {
        if (!s_pending || s_doneJob.load() != s_jobs)
            return;
        s_pending = false;
        ++s_late;
        LOG_PLAIN_F("PPA_SPAET Auftrag=%lu NachMs=%lu Verspaetet=%lu", static_cast<unsigned long>(s_jobs), static_cast<unsigned long>((s_doneAt.load() - s_submittedAt) / 1000), static_cast<unsigned long>(s_late));
    }

    /** @brief Wartet höchstens bis zur Zeitgrenze auf die Meldung des Auftrags job, eine übrig gebliebene Meldung eines früheren Auftrags beendet das Warten nicht. @param start Zeitpunkt der Übergabe (nowUs). @return true bei rechtzeitiger Meldung. */
    bool waitDone(uint32_t job, uint32_t start)
    {
        for (;;)
        {
            const uint32_t waited = nowUs() - start;
            if (waited >= PpaTimeoutUs)
                return false;
            if (xSemaphoreTake(s_done, pdMS_TO_TICKS((PpaTimeoutUs - waited) / 1000) + 1) == pdTRUE && s_doneJob.load() == job)
                return true;
        }
    }
}

bool Tab5Render::begin(lgfx::LGFX_Device &display)
{
    auto *panel = display.panel();
    if (!panel || !panel->getBus() || panel->getBus()->busType() != lgfx::bus_type_t::bus_dsi)
        return false;
    const auto config = panel->config();
    const auto rotation = display.getRotation();
    if (config.panel_width != 720 || config.panel_height != 1280 || config.offset_rotation || (rotation != 3 && rotation != 1) || display.width() != 1280 || display.height() != 720)
        return false;
    auto *dsi = static_cast<lgfx::Panel_DSI *>(panel);
    s_framebuffer = static_cast<uint16_t *>(dsi->config_detail().buffer);
    if (!s_framebuffer)
        return false;
    s_display = &display;
    if (!s_done)
        s_done = xSemaphoreCreateBinaryStatic(&s_doneStorage);
    ppa_client_config_t client{};
    client.oper_type = PPA_OPERATION_SRM;
    client.max_pending_trans_num = 1;
    ppa_event_callbacks_t callbacks{};
    callbacks.on_trans_done = onDone;
    const uintptr_t address = reinterpret_cast<uintptr_t>(s_framebuffer);
    const esp_err_t registered = address & (CacheLine - 1) ? ESP_ERR_INVALID_ARG : ppa_register_client(&client, &s_ppa);
    if (registered != ESP_OK)
    {
        s_ppa = nullptr;
        s_mode = 1;
        LOG_ERROR_F("PPA nicht verfügbar, CPU-Weg: Bildspeicher=0x%08lx Fehler=%d", static_cast<unsigned long>(address), registered);
    }
    else if (ppa_client_register_event_callbacks(s_ppa, &callbacks) != ESP_OK)
    {
        // Ohne Fertigmeldung endete jede nicht blockierende Übertragung erst an der Zeitgrenze.
        ppa_unregister_client(s_ppa);
        s_ppa = nullptr;
        s_mode = 1;
        LOG_ERROR("PPA ohne Fertigmeldung, CPU-Weg");
    }
    if (!s_frame)
        s_frame = xSemaphoreCreateBinaryStatic(&s_frameStorage);
    esp_lcd_dpi_panel_event_callbacks_t frameCallbacks{};
    frameCallbacks.on_refresh_done = onRefreshDone;
    auto *dpi = PanelZugang::handle(dsi);
    s_frameSync = dpi && esp_lcd_dpi_panel_register_event_callbacks(dpi, &frameCallbacks, nullptr) == ESP_OK;
    if (!s_frameSync)
        LOG_ERROR("Bildsignal des DPI-Treibers nicht verfügbar, die Nadel läuft frei");
    return true;
}

bool Tab5Render::waitFrame(uint32_t timeoutMs)
{
    if (!s_frameSync)
        return false;
    // Eine schon vorliegende Meldung gehört zu einem früheren Bild, erst die nächste richtet den Aufrufer am Panel aus.
    xSemaphoreTake(s_frame, 0);
    if (xSemaphoreTake(s_frame, pdMS_TO_TICKS(timeoutMs)) == pdTRUE)
        return true;
    if (s_frames.load() == 0)
    {
        // Der Treiber meldet keine Bilder, weiteres Warten würde jede Fahrt nur verzögern.
        s_frameSync = false;
        LOG_ERROR("Bildsignal des DPI-Treibers bleibt aus, die Nadel läuft frei");
    }
    return false;
}

uint32_t Tab5Render::lastFrameMs()
{
    return s_frameAtMs.load();
}

uint32_t Tab5Render::frames()
{
    return s_frames.load();
}

uint32_t Tab5Render::lastFrameUs()
{
    return s_frameAtUs.load();
}

uint32_t Tab5Render::framePeriodUs()
{
    return s_framePeriodUs.load();
}

uint16_t *Tab5Render::framebuffer(lgfx::LGFX_Device &display)
{
    const auto rotation = display.getRotation();
    return s_display == &display && (rotation == 3 || rotation == 1) ? s_framebuffer : nullptr;
}

void Tab5Render::flushFramebuffer(lgfx::LGFX_Device &display, int x, int y, int width, int height)
{
    if (width > 0 && height > 0)
        display.panel()->display(x, y, width, height);
}

void Tab5Render::pushRegion(lgfx::LGFX_Device &display, const uint16_t *source, int stride, int x, int y, int width, int height)
{
    if (!source || width <= 0 || height <= 0)
        return;
    const auto rotation = display.getRotation();
    // Querformat in beiden Lagen: Rotation 3 (Standard) und Rotation 1 (um 180 Grad gedreht).
    if (s_display == &display && s_framebuffer && (rotation == 3 || rotation == 1))
    {
        int32_t cx, cy, cw, ch;
        display.getClipRect(&cx, &cy, &cw, &ch);
        const int left = std::max({0, x, static_cast<int>(cx)}), top = std::max({0, y, static_cast<int>(cy)});
        const int right = std::min({1280, x + width, static_cast<int>(cx + cw)}), bottom = std::min({720, y + height, static_cast<int>(cy + ch)});
        if (right <= left || bottom <= top)
            return;
        bool transferred = false;
        const int w = right - left, h = bottom - top;
        checkPending();
        if (s_mode == 2 && s_ppa && !s_pending && w * h >= 1024)
        {
            ppa_srm_oper_config_t op{};
            op.in.buffer = source;
            op.in.pic_w = stride;
            op.in.pic_h = height;
            op.in.block_w = w;
            op.in.block_h = h;
            op.in.block_offset_x = left - x;
            op.in.block_offset_y = top - y;
            op.in.srm_cm = PPA_SRM_COLOR_MODE_RGB565;
            op.out.buffer = s_framebuffer;
            op.out.buffer_size = 1280 * 720 * 2;
            op.out.pic_w = 720;
            op.out.pic_h = 1280;
            if (rotation == 3)
            {
                op.out.block_offset_x = top;
                op.out.block_offset_y = 1280 - right;
            }
            else
            {
                op.out.block_offset_x = 720 - bottom;
                op.out.block_offset_y = left;
            }
            op.out.srm_cm = PPA_SRM_COLOR_MODE_RGB565;
            op.rotation_angle = rotation == 3 ? PPA_SRM_ROTATION_ANGLE_90 : PPA_SRM_ROTATION_ANGLE_270;
            op.scale_x = op.scale_y = 1;
            op.byte_swap = true;
            // Nicht blockierend, damit das Warten eine Zeitgrenze hat. Die Meldung kommt über onDone mit der Auftragsnummer.
            op.mode = PPA_TRANS_MODE_NON_BLOCKING;
            const uint32_t job = s_jobs + 1;
            op.user_data = reinterpret_cast<void *>(static_cast<uintptr_t>(job));
            // Nur der UI-Task schreibt ins Display. Vor DMA die CPU-Änderungen der gelesenen Quellzeilen und der beschriebenen Panelzeilen sichern, danach die CPU-Kopie der Panelzeilen verwerfen. Der Block belegt im Panel die Zeilen [firstRow, firstRow + w) zu je 1440 Byte zusammenhängend. Der Abgleich des ganzen Framebuffers kostete je Übertragung rund 2 ms.
            const int firstRow = rotation == 3 ? 1280 - right : left;
            auto *target = reinterpret_cast<uint8_t *>(s_framebuffer) + static_cast<size_t>(firstRow) * 720 * 2;
            const size_t targetBytes = static_cast<size_t>(w) * 720 * 2;
            syncCache(source + static_cast<ptrdiff_t>(top - y) * stride, static_cast<size_t>(h) * stride * 2, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
            syncCache(target, targetBytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
            // Eine übrig gebliebene Meldung eines früheren Auftrags darf dieses Warten nicht beenden.
            xSemaphoreTake(s_done, 0);
            s_submittedAt = nowUs();
            s_jobs = job;
            const auto error = ppa_do_scale_rotate_mirror(s_ppa, &op);
            if (error == ESP_OK && waitDone(job, s_submittedAt))
            {
                syncCache(target, targetBytes, ESP_CACHE_MSYNC_FLAG_DIR_M2C);
                transferred = true;
            }
            else if (error == ESP_OK)
            {
                // Die Transaktion kann noch laufen und später in diesen Bereich des Framebuffers schreiben. Bis ihre Meldung kommt, überträgt die CPU und die PPA bekommt keinen weiteren Auftrag.
                s_pending = true;
                ++s_timeouts;
                LOG_PLAIN_F("PPA_ZEITGRENZE Auftrag=%lu Region=%d,%d,%dx%d Rotation=%u Zeitgrenze=%lu", static_cast<unsigned long>(job), left, top, w, h, static_cast<unsigned>(rotation), static_cast<unsigned long>(s_timeouts));
            }
            else
            {
                LOG_PLAIN_F("TRANSFER PPA Fehler=%d, gekachelter Fallback", error);
                s_mode = 1;
            }
        }
        if (!transferred)
        {
            if (rotation == 3)
                Tab5PixelTransfer::rotate3(s_framebuffer, 720, 1280, source + (top - y) * stride + left - x, stride, left, top, w, h);
            else
                Tab5PixelTransfer::rotate1(s_framebuffer, 720, 1280, source + (top - y) * stride + left - x, stride, left, top, w, h);
        }
        // Die unveränderte Panelimplementierung schreibt die betroffenen Cachezeilen für DSI zurück.
        display.panel()->display(left, top, right - left, bottom - top);
    }
    else
    {
        display.startWrite();
        for (int row = 0; row < height; ++row)
            display.pushImage(x, y + row, width, 1, reinterpret_cast<const lgfx::swap565_t *>(source + row * stride));
        display.endWrite();
    }
}

void Tab5Render::pushSprite(lgfx::LGFX_Device *display, lgfx::LGFX_Sprite &sprite, int x, int y)
{
    if (!display)
        return;
    if (sprite.getRotation() != 0 || sprite.getColorDepth() != 16)
    {
        sprite.pushSprite(display, x, y);
        return;
    }
    pushRegion(*display, static_cast<const uint16_t *>(sprite.getBuffer()), sprite.width(), x, y, sprite.width(), sprite.height());
}

Tab5Render::PpaCounters Tab5Render::ppaCounters()
{
    checkPending();
    return {s_timeouts, s_late, s_mode == 2 && s_ppa, s_jobs};
}

const char *Tab5Render::panelName(lgfx::LGFX_Device &display)
{
    auto *panel = display.panel();
    auto *touch = display.touch();
    if (!panel || !touch || !panel->getBus() || panel->getBus()->busType() != lgfx::bus_type_t::bus_dsi)
        return "unbekannt";
    const int address = touch->config().i2c_addr;
    if (address == 0x14)
        return "ILI9881C+GT911";
    if (address != 0x55)
        return "unbekannt";
    return static_cast<lgfx::Bus_DSI *>(panel->getBus())->config().lane_mbps == 900 ? "ST7121" : "ST7123";
}
