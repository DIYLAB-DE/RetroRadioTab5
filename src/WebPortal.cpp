#include "WebPortal.h"
#include "FtpService.h"
#include "WebValidation.h"
#include "StringUtils.h"
#include "StationsDefault.h"
#include "Logger.h"
#include "Thema.h"
#include <algorithm>
#include "index_html_gz.h"
#include <cJSON.h>
#include <esp_random.h>
#include <esp_heap_caps.h>
#include <esp_ota_ops.h>
#include <esp_timer.h>
#include <mbedtls/platform_util.h>
#include <memory>
#include <cmath>
#include <ctime>
#include <lwip/sockets.h>
#include <cerrno>
#include <new>
// Die Firmware-Kennung kommt wie in RetroRadio über das Build-Flag FW_VERSION (scripts/fw_version.py).
#ifndef FW_VERSION
#define FW_VERSION "1.0"
#endif

namespace
{
    using Json = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;
    constexpr size_t HttpSendBlock = 1024;
    constexpr size_t TransportReserve = 32768;
    // Die Aufnahme umfasst das ganze Display.
    constexpr int ShotWidth = 1280, ShotHeight = 720;
    constexpr uint32_t ShotBytes = ShotWidth * ShotHeight * 2;
    constexpr const char *ModernGruppen[] = {"Fest", "Akzente", "Text", "Erweitert"}; ///< Namen der ModernGruppe in der Antwort von GET /api/theme
    constexpr uint32_t FtpWaitStepMs = 50;  ///< Abfrageabstand, während der Dienstworker ein FTP-Passwort erzeugt
    constexpr int FtpPasswordSteps = 60;    ///< Höchstens 3 s auf ein erstmals erzeugtes Passwort warten
    constexpr int FtpRenewSteps = 100;      ///< Höchstens 5 s auf ein erneuertes und gespeichertes Passwort warten

    /** @brief Begrenzt ausstehende HTTP-Sendedaten, damit dem C6-SDIO-Transport interner RAM bleibt. */
    int sendBounded(httpd_handle_t, int socket, const char *bytes, size_t length, int flags)
    {
        const uint32_t started = millis();
        while (heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) < TransportReserve || heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) < 4096)
        {
            if (millis() - started >= 3000)
                return HTTPD_SOCK_ERR_TIMEOUT;
            vTaskDelay(pdMS_TO_TICKS(2));
        }
        const int sent = send(socket, bytes, std::min(length, HttpSendBlock), flags);
        if (sent < 0)
            return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR ? HTTPD_SOCK_ERR_TIMEOUT : HTTPD_SOCK_ERR_FAIL;
        vTaskDelay(pdMS_TO_TICKS(2));
        return sent;
    }

    /** @brief Installiert die Sendebegrenzung vor dem ersten HTTP-Header jeder Verbindung. */
    esp_err_t openHttpSession(httpd_handle_t server, int socket)
    {
        return httpd_sess_set_send_override(server, socket, sendBounded);
    }

    /** @brief Kodiert genau eine JSON-Antwort mit Cacheverbot. */
    esp_err_t sendJson(httpd_req_t *r, cJSON *value, int code = 200)
    {
        Json owned(value, cJSON_Delete);
        char *body = value ? cJSON_PrintUnformatted(value) : nullptr;
        if (!body)
        {
            httpd_resp_set_status(r, "507 Insufficient Storage");
            return httpd_resp_send(r, "{}", 2);
        }
        const char *status = code == 200 ? "200 OK" : code == 400 ? "400 Bad Request"
                                                  : code == 401   ? "401 Unauthorized"
                                                  : code == 403   ? "403 Forbidden"
                                                  : code == 404   ? "404 Not Found"
                                                  : code == 409   ? "409 Conflict"
                                                  : code == 413   ? "413 Content Too Large"
                                                  : code == 429   ? "429 Too Many Requests"
                                                  : code == 503   ? "503 Service Unavailable"
                                                  : code == 504   ? "504 Gateway Timeout"
                                                  : code == 507   ? "507 Insufficient Storage"
                                                                  : "500 Internal Server Error";
        httpd_resp_set_status(r, status);
        httpd_resp_set_type(r, "application/json; charset=utf-8");
        httpd_resp_set_hdr(r, "Cache-Control", "no-store");
        const auto result = httpd_resp_send(r, body, strlen(body));
        cJSON_free(body);
        return result;
    }

    /** @brief Liefert den Namen eines Ablehnungsgrunds für das Feld reason, nullptr ohne eigenen Grund. */
    const char *reasonName(WebFileStore::Reason reason)
    {
        using R = WebFileStore::Reason;
        switch (reason)
        {
        case R::Exists:
            return "exists";
        case R::Directory:
            return "directory";
        case R::NotEmpty:
            return "notEmpty";
        case R::Inside:
            return "inside";
        case R::Playing:
            return "playing";
        case R::Ftp:
            return "ftp";
        default:
            return nullptr;
        }
    }

    /** @brief Liefert bestätigten Erfolg oder einen tatsächlich ermittelten Fehler. Bei einer Ablehnung der Dateiverwaltung zusätzlich den Grund im Feld reason, aus dem der Browser seine Meldung wählt. */
    esp_err_t result(httpd_req_t *r, int code, WebFileStore::Reason reason = WebFileStore::Reason::None)
    {
        auto *o = cJSON_CreateObject();
        cJSON_AddBoolToObject(o, "success", code == 200);
        if (code != 200)
            cJSON_AddStringToObject(o, "error", code == 401 ? "PIN erforderlich." : code == 403 ? "Zugriff verweigert."
                                                                                : code == 409   ? "Zustand geändert oder Dienst belegt. Bitte aktualisieren."
                                                                                : code == 413   ? "Grenze für Größe oder Einträge überschritten."
                                                                                : code == 503   ? "Dienst nicht verfügbar."
                                                                                : code == 504   ? "Abschluss noch nicht bestätigt. Zustand vor Wiederholung prüfen."
                                                                                : code == 429   ? "Bitte vor dem nächsten Versuch warten."
                                                                                : code == 400   ? "Ungültige Eingabe."
                                                                                : code == 404   ? "Nicht gefunden."
                                                                                                : "Vorgang fehlgeschlagen.");
        if (code != 200 && reasonName(reason))
            cJSON_AddStringToObject(o, "reason", reasonName(reason));
        return sendJson(r, o, code);
    }

    /** @brief Beantwortet einen abgewiesenen Anmeldeversuch mit der Wartezeit bis zum nächsten zulässigen Versuch in ganzen Sekunden (Feld retryAfter und Kopf Retry-After). @param code 401 oder 429. @param waitMs Wartezeit in ms. */
    esp_err_t retry(httpd_req_t *r, int code, uint64_t waitMs)
    {
        const unsigned long seconds = (waitMs + 999) / 1000;
        char header[16];
        snprintf(header, sizeof(header), "%lu", seconds);
        httpd_resp_set_hdr(r, "Retry-After", header);
        auto *o = cJSON_CreateObject();
        cJSON_AddBoolToObject(o, "success", false);
        cJSON_AddStringToObject(o, "error", code == 401 ? "PIN erforderlich." : "Bitte vor dem nächsten Versuch warten.");
        cJSON_AddNumberToObject(o, "retryAfter", seconds);
        return sendJson(r, o, code);
    }

    /** @brief Liefert den Zustand der PIN-Sperre für den Browser wie lockStatusJson im Original. @param enabled Gespeicherter Schalter. @param locked Der Browser ist nicht angemeldet. Ohne Sperre gilt er nie als gesperrt. */
    const char *lockStatus(bool enabled, bool locked)
    {
        if (!enabled)
            return "{\"enabled\":false,\"locked\":false}";
        return locked ? "{\"enabled\":true,\"locked\":true}" : "{\"enabled\":true,\"locked\":false}";
    }

    /** @brief Liest nur vollständig empfangene, begrenzte Textkörper ohne eingebettete Nullbytes. */
    char *body(httpd_req_t *r)
    {
        if (!r->content_len || r->content_len > WebValidation::MaxBody)
            return nullptr;
        auto *bytes = static_cast<char *>(heap_caps_malloc(r->content_len + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!bytes)
            return nullptr;
        size_t received = 0;
        while (received < r->content_len)
        {
            const int n = httpd_req_recv(r, bytes + received, r->content_len - received);
            if (n <= 0)
            {
                free(bytes);
                return nullptr;
            }
            received += n;
        }
        if (memchr(bytes, 0, received))
        {
            free(bytes);
            return nullptr;
        }
        bytes[received] = 0;
        return bytes;
    }

    /** @brief Prüft JSON einschließlich Verschachtelungstiefe (vor dem Parser), doppelter Schlüssel und vollständigem Ende. */
    cJSON *parse(const char *text)
    {
        if (!text || strstr(text, "\\u0000") || !WebValidation::jsonDepth(text))
            return nullptr;
        const char *end = nullptr;
        cJSON *value = cJSON_ParseWithOpts(text, &end, true);
        if (!value)
            return nullptr;
        // Die Rekursion ist durch jsonDepth() auf MaxJsonDepth Ebenen begrenzt.
        auto valid = [](auto &&self, const cJSON *node) -> bool
        {
            for (auto *p = node->child; p; p = p->next)
            {
                if (cJSON_IsObject(node))
                    for (auto *q = node->child; q != p; q = q->next)
                        if (q->string && p->string && !strcmp(q->string, p->string))
                            return false;
                if (!self(self, p))
                    return false;
            }
            return true;
        };
        if (!valid(valid, value))
        {
            cJSON_Delete(value);
            return nullptr;
        }
        return value;
    }

    /** @brief Übernimmt einen vollständigen JSON-String ohne Trunkierung. */
    bool field(cJSON *o, const char *key, char *out, size_t capacity)
    {
        const auto *v = cJSON_GetObjectItemCaseSensitive(o, key);
        if (!cJSON_IsString(v) || !WebValidation::text(v->valuestring, capacity))
            return false;
        memcpy(out, v->valuestring, strlen(v->valuestring) + 1);
        return true;
    }

    /** @brief Prüft eine ganzzahlige JSON-Zahl innerhalb ihrer wirklichen Grenzen. */
    bool number(cJSON *o, const char *key, double min, double max, double &out)
    {
        const auto *v = cJSON_GetObjectItemCaseSensitive(o, key);
        if (!cJSON_IsNumber(v) || !std::isfinite(v->valuedouble) || v->valuedouble < min || v->valuedouble > max || std::floor(v->valuedouble) != v->valuedouble)
            return false;
        out = v->valuedouble;
        return true;
    }

    /** @brief Baut die Antwort von GET /api/theme: theme (gespeicherter Themenindex), stored, colors mit den 18 bearbeitbaren Grundfarben von Modern (name, value und default als „#RRGGBB“, group Akzente, Text oder Erweitert) in der Reihenfolge von ModernFarbe sowie fixed mit Grund und Fläche (name, value) für die Vorschau. @param theme Gespeicherter Themenindex. @param colors Gespeicherte Grundfarben. @param stored rr_tab5/modern_v1 vorhanden. */
    cJSON *themeJson(uint8_t theme, const ModernFarben &colors, bool stored)
    {
        auto *o = cJSON_CreateObject();
        cJSON_AddNumberToObject(o, "theme", theme);
        cJSON_AddBoolToObject(o, "stored", stored);
        auto *list = cJSON_AddArrayToObject(o, "colors");
        auto *fixed = cJSON_AddArrayToObject(o, "fixed");
        for (size_t i = 0; i < ModernFarbenAnzahl; ++i)
        {
            const auto &base = ModernGrundfarben[i];
            const bool editable = i >= ModernErsteBearbeitbare;
            char value[8], preset[8];
            snprintf(value, sizeof(value), "#%06lX", static_cast<unsigned long>((editable ? colors.rgb[i] : base.vorgabe) & 0xFFFFFF));
            snprintf(preset, sizeof(preset), "#%06lX", static_cast<unsigned long>(base.vorgabe));
            auto *entry = cJSON_CreateObject();
            cJSON_AddStringToObject(entry, "name", base.name);
            cJSON_AddStringToObject(entry, "value", value);
            if (editable)
            {
                cJSON_AddStringToObject(entry, "default", preset);
                cJSON_AddStringToObject(entry, "group", ModernGruppen[static_cast<size_t>(base.gruppe)]);
            }
            cJSON_AddItemToArray(editable ? list : fixed, entry);
        }
        return o;
    }

    /** @brief Übernimmt das Objekt colors einer Anfrage („Name“: „#RRGGBB“, Groß- und Kleinbuchstaben) in colors. Nicht genannte Farben bleiben. @param o Anfrageobjekt. @param colors Ausgangsstand, bei false teilweise geändert. @return false ohne Objekt colors, bei leerem Objekt, unbekanntem oder festem Namen (Grund, Flaeche) oder einem Wert, der kein String „#“ mit genau sechs Hexziffern ist. */
    bool modernFields(cJSON *o, ModernFarben &colors)
    {
        const auto *list = cJSON_GetObjectItemCaseSensitive(o, "colors");
        if (!cJSON_IsObject(list) || !list->child)
            return false;
        for (const auto *item = list->child; item; item = item->next)
        {
            const size_t index = modernFarbeNachName(item->string);
            uint32_t rgb = 0;
            if (index < ModernErsteBearbeitbare || index >= ModernFarbenAnzahl || !cJSON_IsString(item) || !WebValidation::text(item->valuestring, 8) || item->valuestring[0] != '#' || !rgbAusHex(item->valuestring + 1, rgb))
                return false;
            colors.rgb[index] = rgb;
        }
        return true;
    }
}

bool WebPortal::begin(AppServices &services, StationManager &stations, uint16_t *pixels, FtpService *ftp)
{
    auto release = [&]()
    {
        if (m_server)
            httpd_stop(m_server);
        m_server = nullptr;
        if (m_commands)
            vQueueDelete(m_commands);
        m_commands = nullptr;
        m_reply.end();
        free(m_stationScratch);
        m_pixels = nullptr;
        m_stationScratch = nullptr;
    };
    m_services = &services;
    m_stations = &stations;
    m_ftp = ftp;
    m_commands = xQueueCreate(1, sizeof(Command));
    const bool replyReady = m_reply.begin();
    // Der Aufnahmeplatz gehört dem Aufrufer und wird mit dem USB-Screenshot geteilt, ohne ihn antwortet nur /screenshot mit 503.
    m_pixels = pixels;
    m_stationScratch = static_cast<Station *>(heap_caps_malloc(MAX_STATIONS * sizeof(Station), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!m_commands || !replyReady || !m_stationScratch)
    {
        release();
        return false;
    }
    for (int i = 0; i < MAX_STATIONS; ++i)
        new (&m_stationScratch[i]) Station{};
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.core_id = 0;
    config.task_priority = 1;
    config.stack_size = 24576;
    config.max_uri_handlers = 4;
    config.max_open_sockets = 5;
    config.lru_purge_enable = true;
    config.recv_wait_timeout = 3;
    config.send_wait_timeout = 3;
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.open_fn = openHttpSession;
    if (httpd_start(&m_server, &config) != ESP_OK)
    {
        release();
        return false;
    }
    for (auto method : {HTTP_GET, HTTP_POST})
    {
        httpd_uri_t uri{};
        uri.uri = "/*";
        uri.method = method;
        uri.handler = handle;
        uri.user_ctx = this;
        if (httpd_register_uri_handler(m_server, &uri) != ESP_OK)
        {
            release();
            return false;
        }
    }
    return true;
}

void WebPortal::publish(const State &value)
{
    portENTER_CRITICAL(&m_lock);
    const bool locked = value.locked && !m_state.locked;
    m_state = value;
    portEXIT_CRITICAL(&m_lock);
    if (locked)
        invalidateSessions();
}

WebPortal::State WebPortal::state()
{
    portENTER_CRITICAL(&m_lock);
    auto copy = m_state;
    portEXIT_CRITICAL(&m_lock);
    return copy;
}

WebFileStore::Reason WebPortal::leaseReason()
{
    if (m_services->fileLease() == FileLease::Owner::Ftp)
        return WebFileStore::Reason::Ftp;
    return state().sdBusy ? WebFileStore::Reason::Playing : WebFileStore::Reason::None;
}

bool WebPortal::take(Command &command)
{
    if (!m_commands || xQueueReceive(m_commands, &command, 0) != pdPASS)
        return false;
    // Die Senderliste steht bei StationsChanged schon im Flash, Revision und Senderwahl müssen ihr in jedem Fall folgen.
    if (command.action == Action::StationsChanged)
        return true;
    if (command.action != Action::ReleaseFiles && command.generation != m_authGeneration.load())
    {
        complete(command.id, 401);
        return false;
    }
    if (static_cast<int32_t>(millis() - command.expires) >= 0)
    {
        complete(command.id, 504);
        return false;
    }
    return true;
}

void WebPortal::complete(uint32_t id, int status)
{
    m_reply.complete(id, status);
}

bool WebPortal::capture(lgfx::LGFX_Device &display)
{
    if (!m_pixels || display.width() != ShotWidth || display.height() != ShotHeight)
        return false;
    display.waitDMA();
    display.readRect(0, 0, ShotWidth, ShotHeight, reinterpret_cast<lgfx::rgb565_t *>(m_pixels));
    return true;
}

int WebPortal::execute(Command &command)
{
    command.id = 0x80000000u | (++m_sequence & 0x3fffffffu);
    command.expires = millis() + 15000;
    command.generation = m_authGeneration.load();
    if (command.action == Action::Settings)
        command.settings.id = command.id;
    if (command.action == Action::Theme)
        command.settings.id = command.modern.id = command.id;
    m_reply.expect(command.id);
    if (xQueueSend(m_commands, &command, 0) != pdPASS)
    {
        m_reply.cancel();
        return 503;
    }
    return m_reply.wait(16000);
}

bool WebPortal::authenticated(httpd_req_t *r)
{
    // Ohne PIN-Sperre öffnet sich das Portal wie im Original ohne Anmeldung.
    if (!m_services->pinLockEnabled())
        return true;
    char cookie[256]{};
    if (httpd_req_get_hdr_value_str(r, "Cookie", cookie, sizeof(cookie)) != ESP_OK)
        return false;
    const uint32_t generation = m_authGeneration.load();
    for (const auto &s : m_sessions)
        if (s.token[0] && s.generation == generation && millis() - s.created < 30 * 60 * 1000u)
        {
            char expected[48];
            snprintf(expected, sizeof(expected), "rr_session=%s", s.token);
            for (const char *p = cookie; *p;)
            {
                while (*p == ' ')
                    ++p;
                const char *end = strchr(p, ';');
                if (!end)
                    end = p + strlen(p);
                if (static_cast<size_t>(end - p) == strlen(expected) && !memcmp(p, expected, end - p))
                    return true;
                p = *end ? end + 1 : end;
            }
        }
    return false;
}

bool WebPortal::trusted(httpd_req_t *r, bool post)
{
    char host[96]{}, origin[112]{};
    if (httpd_req_get_hdr_value_str(r, "Host", host, sizeof(host)) != ESP_OK)
    {
        LOG_WARNING("Webanfrage ohne gültigen Host-Kopf abgewiesen.");
        return false;
    }
    Settings::Configuration config;
    m_services->configuration(config);
    const bool own = WebValidation::ownHost(host, m_services->networkStatus().ip, config.hostname);
    mbedtls_platform_zeroize(&config, sizeof(config));
    if (!own)
    {
        // Der Kopf stammt vom Client, Steuerzeichen nicht ins Terminal durchreichen.
        for (char *p = host; *p; ++p)
            if (static_cast<unsigned char>(*p) < 0x20 || *p == 0x7f)
                *p = '?';
        LOG_WARNING_F("Webanfrage an fremden Host abgewiesen: %s", host);
        return false;
    }
    if (!post)
        return true;
    if (httpd_req_get_hdr_value_len(r, "Origin"))
        return httpd_req_get_hdr_value_str(r, "Origin", origin, sizeof(origin)) == ESP_OK && WebValidation::sameOrigin(origin, host);
    char site[32]{};
    if (httpd_req_get_hdr_value_str(r, "Sec-Fetch-Site", site, sizeof(site)) == ESP_OK && strcmp(site, "same-origin") && strcmp(site, "none"))
        return false;
    return true;
}

esp_err_t WebPortal::handle(httpd_req_t *r)
{
    return static_cast<WebPortal *>(r->user_ctx)->route(r);
}

char *WebPortal::statusJson(bool radio)
{
    const auto s = state();
    Json o(cJSON_CreateObject(), cJSON_Delete);
    if (radio)
    {
        Station station;
        m_stations->getStation(s.station, station);
        cJSON_AddStringToObject(o.get(), "module", s.radio ? "RETRO_RADIO" : "NONE");
        cJSON_AddNumberToObject(o.get(), "stationId", s.station);
        cJSON_AddStringToObject(o.get(), "stationName", station.name);
        cJSON_AddStringToObject(o.get(), "streamTitle", s.title);
        cJSON_AddStringToObject(o.get(), "bitrate", s.bitrate);
        cJSON_AddNumberToObject(o.get(), "volume", s.volume);
        cJSON_AddBoolToObject(o.get(), "muted", s.muted);
        cJSON_AddNumberToObject(o.get(), "balance", s.balance);
        cJSON_AddNumberToObject(o.get(), "gainLow", s.tone[0]);
        cJSON_AddNumberToObject(o.get(), "gainMid", s.tone[1]);
        cJSON_AddNumberToObject(o.get(), "gainHigh", s.tone[2]);
        cJSON_AddNumberToObject(o.get(), "magicEyeStyle", s.eyeStyle);
        cJSON_AddBoolToObject(o.get(), "playing", s.playing);
        cJSON_AddNumberToObject(o.get(), "stationsRevision", s.stationsRevision);
    }
    else
    {
        Settings::Configuration config;
        m_services->configuration(config);
        const auto net = m_services->networkStatus();
        char text[128];
        auto *wifi = cJSON_AddObjectToObject(o.get(), "wifi");
        cJSON_AddStringToObject(wifi, "ssid", net.ssid);
        cJSON_AddStringToObject(wifi, "hostname", config.hostname);
        snprintf(text, sizeof(text), "%u.%u.%u.%u", net.ip[0], net.ip[1], net.ip[2], net.ip[3]);
        cJSON_AddStringToObject(wifi, "ip", text);
        cJSON_AddStringToObject(wifi, "mac", net.mac);
        cJSON_AddNumberToObject(wifi, "channel", net.channel);
        cJSON_AddNumberToObject(wifi, "quality", std::max(0, std::min(100, 2 * (net.rssi + 100))));
        snprintf(text, sizeof(text), "%d dBm", net.rssi);
        cJSON_AddStringToObject(wifi, "signal", text);
        auto *ntp = cJSON_AddObjectToObject(o.get(), "ntp");
        cJSON_AddStringToObject(ntp, "server", config.ntpServer);
        cJSON_AddStringToObject(ntp, "region", config.timezone);
        cJSON_AddStringToObject(ntp, "zone", config.timezonePosix);
        time_t now = time(nullptr);
        tm local{};
        localtime_r(&now, &local);
        cJSON_AddStringToObject(ntp, "dst", local.tm_isdst > 0 ? "+1 h" : "0 h");
        // Tag im Jahr und ISO-Kalenderwoche im Textformat des Originals, die Seite übersetzt ihn.
        strftime(text, sizeof(text), "%j. Tag im Jahr, KW %V", &local);
        cJSON_AddStringToObject(ntp, "dayKW", text);
        cJSON_AddStringToObject(ntp, "lastSync", m_services->ntpSynced() ? "Synchronisiert" : "—");
        snprintf(text, sizeof(text), "%lu min", static_cast<unsigned long>(config.syncIntervalMs / 60000));
        cJSON_AddStringToObject(ntp, "interval", text);
        auto *system = cJSON_AddObjectToObject(o.get(), "system");
        cJSON_AddStringToObject(system, "mcu", "ESP32-P4 Rev. 1.0 / 360 MHz");
        cJSON_AddStringToObject(system, "ram", "32 MiB PSRAM");
        snprintf(text, sizeof(text), "%lu / %lu Bytes", static_cast<unsigned long>(s.firmwareBytes), static_cast<unsigned long>(s.appCapacity));
        cJSON_AddStringToObject(system, "software", text);
        cJSON_AddStringToObject(system, "firmware", FW_VERSION);
        cJSON_AddStringToObject(system, "uart", "USB JTAG/Serial");
        cJSON_AddNumberToObject(system, "heapFree", ESP.getFreeHeap());
        // Fragmentierung des internen Heaps in Prozent: Anteil des freien Speichers außerhalb des größten freien Blocks.
        const size_t freeInternal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL), largestBlock = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
        cJSON_AddNumberToObject(system, "heapFragmentation", freeInternal > 0 && largestBlock <= freeInternal ? 100 - largestBlock * 100 / freeInternal : 0);
        cJSON_AddNumberToObject(system, "psramFree", ESP.getFreePsram());
        cJSON_AddNumberToObject(system, "psramTotal", ESP.getPsramSize());
        cJSON_AddNumberToObject(system, "uptimeSeconds", millis() / 1000);
        cJSON_AddNumberToObject(system, "appAddress", s.appAddress);
        cJSON_AddNumberToObject(system, "appCapacity", s.appCapacity);
        cJSON_AddNumberToObject(system, "firmwareBytes", s.firmwareBytes);
        if (m_ftp)
            ftpJson(cJSON_AddObjectToObject(o.get(), "ftp"));
        mbedtls_platform_zeroize(&config, sizeof(config));
    }
    return cJSON_PrintUnformatted(o.get());
}

esp_err_t WebPortal::route(httpd_req_t *r)
{
    char route[128];
    const size_t length = strcspn(r->uri, "?");
    if (length >= sizeof(route))
        return result(r, 404);
    memcpy(route, r->uri, length);
    route[length] = 0;
    const bool post = r->method == HTTP_POST;
    httpd_resp_set_hdr(r, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_hdr(r, "X-Frame-Options", "DENY");
    if (!trusted(r, post))
        return result(r, 403);
    if (!post && (!strcmp(route, "/") || !strcmp(route, "/index.html")))
    {
        httpd_resp_set_type(r, "text/html; charset=utf-8");
        httpd_resp_set_hdr(r, "Content-Encoding", "gzip");
        httpd_resp_set_hdr(r, "Cache-Control", "no-cache");
        httpd_resp_set_hdr(r, "Content-Security-Policy", "default-src 'self'; script-src 'self' 'unsafe-inline'; style-src 'self' 'unsafe-inline'; font-src 'self' data:; img-src 'self' data: blob:; connect-src 'self'; frame-ancestors 'none'; object-src 'none'; base-uri 'none'");
        return httpd_resp_send(r, reinterpret_cast<const char *>(INDEX_HTML_GZ), INDEX_HTML_GZ_SIZE);
    }
    if (!post && !strcmp(route, "/favicon.ico"))
    {
        httpd_resp_set_status(r, "204 No Content");
        return httpd_resp_send(r, nullptr, 0);
    }
    if (post && !strcmp(route, "/api/lock/verify"))
    {
        // Während Mindestabstand oder Sperre wird die PIN nicht geprüft, auch die richtige nicht.
        const uint64_t now = esp_timer_get_time() / 1000;
        if (const uint64_t wait = m_login.remaining(now))
            return retry(r, 429, wait);
        char *input = body(r);
        char pin[5]{};
        bool ok = input && WebValidation::form(input, "pin", pin, sizeof(pin)) && m_services->validatePin(pin);
        if (input)
        {
            mbedtls_platform_zeroize(input, r->content_len);
            free(input);
        }
        mbedtls_platform_zeroize(pin, sizeof(pin));
        if (!ok)
        {
            const uint32_t lock = m_login.failed(now);
            if (!lock)
                return result(r, 401);
            LOG_WARNING_F("Web-Anmeldung nach %u Fehlversuchen für %lu s gesperrt.", m_login.failures(), static_cast<unsigned long>(lock / 1000));
            return retry(r, 401, lock);
        }
        m_login.succeeded(now);
        Session *session = &m_sessions[0];
        for (auto &s : m_sessions)
            if (!s.token[0] || static_cast<int32_t>(s.created - session->created) < 0)
                session = &s;
        constexpr char hex[] = "0123456789abcdef";
        uint8_t random[16];
        esp_fill_random(random, sizeof(random));
        for (size_t i = 0; i < 16; ++i)
        {
            session->token[i * 2] = hex[random[i] >> 4];
            session->token[i * 2 + 1] = hex[random[i] & 15];
        }
        session->token[32] = 0;
        session->generation = m_authGeneration.load();
        session->created = millis();
        char cookie[128];
        snprintf(cookie, sizeof(cookie), "rr_session=%s; Path=/; HttpOnly; SameSite=Strict; Max-Age=1800", session->token);
        httpd_resp_set_hdr(r, "Set-Cookie", cookie);
        auto *o = cJSON_CreateObject();
        cJSON_AddBoolToObject(o, "ok", true);
        return sendJson(r, o);
    }
    const bool auth = authenticated(r);
    if (!post && !strcmp(route, "/events"))
    {
        httpd_resp_set_type(r, "text/event-stream");
        httpd_resp_set_hdr(r, "Cache-Control", "no-store");
        httpd_resp_send_chunk(r, "retry: 1000\n\n", 13);
        // Jeder Browser erfährt zuerst den Zustand der PIN-Sperre, angemeldet folgen Status und Radio.
        char lock[64];
        snprintf(lock, sizeof(lock), "event: lock\ndata: %s\n\n", lockStatus(m_services->pinLockEnabled(), !auth));
        if (httpd_resp_send_chunk(r, lock, HTTPD_RESP_USE_STRLEN) != ESP_OK)
            return ESP_FAIL;
        if (auth)
            for (bool radio : {false, true})
            {
                char *json = statusJson(radio);
                if (!json)
                    return ESP_FAIL;
                const char *event = radio ? "event: radio\ndata: " : "event: stats\ndata: ";
                const auto a = httpd_resp_send_chunk(r, event, strlen(event));
                const auto b = a == ESP_OK ? httpd_resp_send_chunk(r, json, strlen(json)) : a;
                cJSON_free(json);
                if (b != ESP_OK)
                    return b;
                if (httpd_resp_send_chunk(r, "\n\n", 2) != ESP_OK)
                    return ESP_FAIL;
            }
        return httpd_resp_send_chunk(r, nullptr, 0);
    }
    if (!auth)
        return result(r, 401);
    if (post && !strcmp(route, "/api/lock"))
    {
        invalidateSessions();
        return result(r, 200);
    }
    if (!post && (!strcmp(route, "/api/status") || !strcmp(route, "/api/radio/status")))
    {
        char *json = statusJson(!strcmp(route, "/api/radio/status"));
        if (!json)
            return result(r, 507);
        httpd_resp_set_type(r, "application/json");
        httpd_resp_set_hdr(r, "Cache-Control", "no-store");
        auto sent = httpd_resp_send(r, json, strlen(json));
        cJSON_free(json);
        return sent;
    }
    if (post && !strcmp(route, "/api/files/upload"))
        return upload(r);
    auto wipe = [&](char *p)
    {
        if (p)
        {
            mbedtls_platform_zeroize(p, r->content_len);
            free(p);
        }
    };
    std::unique_ptr<char, decltype(wipe)> input(post && r->content_len ? body(r) : nullptr, wipe);
    if (post && r->content_len && !input)
        return result(r, r->content_len > WebValidation::MaxBody ? 413 : 400);
    if (post && !strcmp(route, "/api/lock/mode"))
        return lockMode(r, input.get());
    if (!strcmp(route, "/api/settings") || !strcmp(route, "/api/settime") || !strcmp(route, "/factoryreset"))
        return settings(r, input.get());
    if (!strcmp(route, "/api/theme") || !strcmp(route, "/api/theme/modern/reset"))
        return theme(r, input.get());
    if (!strcmp(route, "/api/stations") || !strcmp(route, "/api/radio/presets") || !strcmp(route, "/api/radio/reset-stations"))
        return stations(r, input.get());
    if (!strcmp(route, "/api/files/tree"))
        return tree(r, input.get());
    if (!strcmp(route, "/api/sdcard/info") || !strncmp(route, "/api/files/", 11))
        return files(r, input.get());
    if (!strcmp(route, "/api/ftp") || !strncmp(route, "/api/ftp/", 9))
        return ftp(r, input.get());
    if (!post && !strcmp(route, "/screenshot"))
        return screenshot(r);
    Command cmd;
    char value[32]{};
    int code = 400;
    if (post && !strcmp(route, "/api/reboot"))
    {
        cmd.action = Action::Reboot;
        code = execute(cmd);
    }
    else if (post && !strcmp(route, "/api/radio/volume") && WebValidation::form(input.get(), "volume", value, sizeof(value)) && WebValidation::integer(value, 0, 100, cmd.values[0]))
    {
        cmd.action = Action::Volume;
        code = execute(cmd);
    }
    else if (post && !strcmp(route, "/api/radio/mute") && WebValidation::form(input.get(), "mute", value, sizeof(value)) && (!strcmp(value, "true") || !strcmp(value, "false")))
    {
        cmd.action = Action::Mute;
        cmd.values[0] = !strcmp(value, "true");
        code = execute(cmd);
    }
    else if (post && !strcmp(route, "/api/radio/balance") && WebValidation::form(input.get(), "balance", value, sizeof(value)) && WebValidation::integer(value, -16, 16, cmd.values[0]))
    {
        cmd.action = Action::Balance;
        code = execute(cmd);
    }
    else if (post && !strcmp(route, "/api/radio/tone"))
    {
        const char *keys[] = {"low", "mid", "high"};
        bool valid = true;
        for (int i = 0; i < 3; ++i)
            valid = valid && WebValidation::form(input.get(), keys[i], value, sizeof(value)) && WebValidation::integer(value, -12, 12, cmd.values[i]);
        if (valid)
        {
            cmd.action = Action::Tone;
            code = execute(cmd);
        }
    }
    else if (post && !strcmp(route, "/api/radio/station") && WebValidation::form(input.get(), "id", value, sizeof(value)) && WebValidation::integer(value, 0, MAX_STATIONS - 1, cmd.values[0]))
    {
        cmd.action = Action::Station;
        code = execute(cmd);
    }
    else if (strncmp(route, "/api/radio/", 11))
        code = 404;
    return result(r, code);
}

esp_err_t WebPortal::settings(httpd_req_t *r, const char *input)
{
    Settings::Configuration config;
    const auto revision = m_services->configuration(config);
    if (r->method == HTTP_GET)
    {
        if (strcmp(r->uri, "/api/settings"))
        {
            mbedtls_platform_zeroize(&config, sizeof(config));
            return result(r, 404);
        }
        auto *o = cJSON_CreateObject();
        cJSON_AddBoolToObject(o, "pinLockEnabled", config.pinLock);
        cJSON_AddStringToObject(o, "hostname", config.hostname);
        cJSON_AddStringToObject(o, "ntpServer", config.ntpServer);
        cJSON_AddStringToObject(o, "timezoneRegion", config.timezone);
        cJSON_AddStringToObject(o, "timezoneStr", config.timezonePosix);
        cJSON_AddNumberToObject(o, "syncIntervalMs", config.syncIntervalMs);
        cJSON_AddStringToObject(o, "language", config.language == LangIndex::DE ? "de" : "en");
        cJSON_AddNumberToObject(o, "revision", revision);
        mbedtls_platform_zeroize(&config, sizeof(config));
        return sendJson(r, o);
    }
    Json data(parse(input), cJSON_Delete);
    Command cmd;
    cmd.action = Action::Settings;
    auto &job = cmd.settings;
    job.baselineRevision = revision;
    job.configuration = config;
    mbedtls_platform_zeroize(&config, sizeof(config));
    int code = 400;
    if (!strcmp(r->uri, "/factoryreset"))
    {
        // Der Reset setzt nur den Namensraum rr_tab5 zurück, keine NVS-Gesamtlöschung.
        double value;
        if (!data)
            code = 400;
        else if (cJSON_IsObject(data.get()) && number(data.get(), "revision", 1, UINT32_MAX, value) && value == revision)
        {
            job.type = Settings::RequestType::ResetDefaults;
            cmd.restart = true;
            code = execute(cmd);
        }
        else
            code = 409;
    }
    else if (!strcmp(r->uri, "/api/settime"))
    {
        char date[32]{};
        int year, month, day, hour, minute, second = 0, n = 0;
        if (field(data.get(), "datetime", date, sizeof(date)) && sscanf(date, "%d-%d-%dT%d:%d:%d%n", &year, &month, &day, &hour, &minute, &second, &n) == 6 && date[n] == 0 && year >= 2000 && year <= 2099 && month >= 1 && month <= 12 && day >= 1 && day <= 31 && hour >= 0 && hour <= 23 && minute >= 0 && minute <= 59 && second >= 0 && second <= 59)
        {
            job.type = Settings::RequestType::SetTime;
            job.localTime = {static_cast<uint16_t>(year), static_cast<uint8_t>(month), static_cast<uint8_t>(day), static_cast<uint8_t>(hour), static_cast<uint8_t>(minute)};
            job.second = second;
            code = Settings::validDate(job.localTime) ? execute(cmd) : 400;
        }
    }
    else if (cJSON_IsObject(data.get()) && !cJSON_HasObjectItem(data.get(), "hostname") && cJSON_HasObjectItem(data.get(), "lockPinNew"))
    {
        // Der Dialog „PIN ändern“ schickt wie im Original nur die beiden PINs. Gespeichert wird allein die PIN auf dem aktuellen Stand, ohne Neustart.
        job.type = Settings::RequestType::Save;
        job.changedFields = Settings::Pin;
        bool valid = field(data.get(), "lockPinNew", job.pin, sizeof(job.pin)) && field(data.get(), "lockPinCurrent", job.currentPin, sizeof(job.currentPin)) && m_services->validatePin(job.currentPin);
        for (int i = 0; i < 4; ++i)
            valid = valid && job.pin[i] >= '0' && job.pin[i] <= '9';
        code = valid ? execute(cmd) : 403;
    }
    else if (cJSON_IsObject(data.get()))
    {
        double version, interval;
        char language[3]{};
        job.type = Settings::RequestType::Save;
        job.changedFields = Settings::Hostname | Settings::Time | Settings::Language;
        bool valid = number(data.get(), "revision", 1, UINT32_MAX, version) && version == revision;
        if (!valid)
            code = 409;
        else
        {
            valid = field(data.get(), "hostname", job.configuration.hostname, sizeof(job.configuration.hostname)) && field(data.get(), "ntpServer", job.configuration.ntpServer, sizeof(job.configuration.ntpServer)) && field(data.get(), "timezoneRegion", job.configuration.timezone, sizeof(job.configuration.timezone)) && field(data.get(), "timezoneStr", job.configuration.timezonePosix, sizeof(job.configuration.timezonePosix)) && field(data.get(), "language", language, sizeof(language)) && number(data.get(), "syncIntervalMs", 60000, 86400000, interval) && (!strcmp(language, "de") || !strcmp(language, "en"));
            if (valid)
            {
                job.configuration.language = !strcmp(language, "de") ? LangIndex::DE : LangIndex::EN;
                job.configuration.syncIntervalMs = interval;
                if (cJSON_HasObjectItem(data.get(), "lockPinNew") || cJSON_HasObjectItem(data.get(), "lockPinCurrent"))
                {
                    valid = field(data.get(), "lockPinNew", job.pin, sizeof(job.pin)) && field(data.get(), "lockPinCurrent", job.currentPin, sizeof(job.currentPin)) && m_services->validatePin(job.currentPin);
                    for (int i = 0; i < 4; ++i)
                        valid = valid && job.pin[i] >= '0' && job.pin[i] <= '9';
                    if (valid)
                        job.changedFields |= Settings::Pin;
                    else
                        code = 403;
                }
                // Das Thema speichert nur /api/theme, die Kopie der Konfiguration trägt das gespeicherte unverändert weiter.
                valid = valid && AppServices::validConfiguration(job.configuration);
                cmd.restart = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(data.get(), "restart"));
                if (valid)
                    code = execute(cmd);
            }
        }
    }
    mbedtls_platform_zeroize(&cmd, sizeof(cmd));
    // Auch die Parserkopie enthält kurzzeitig die neue PIN, vor Freigabe löschen.
    if (data)
        for (const char *key : {"lockPinCurrent", "lockPinNew"})
        {
            auto *v = cJSON_GetObjectItemCaseSensitive(data.get(), key);
            if (cJSON_IsString(v))
                mbedtls_platform_zeroize(v->valuestring, strlen(v->valuestring));
        }
    return result(r, code);
}

esp_err_t WebPortal::lockMode(httpd_req_t *r, const char *input)
{
    char value[8]{}, pin[5]{};
    if (!WebValidation::form(input, "enabled", value, sizeof(value)) || (strcmp(value, "true") && strcmp(value, "false")))
        return result(r, 400);
    const bool enabled = !strcmp(value, "true");
    int code = 200;
    // Wie im Original ist der schon geltende Zustand ohne Speichern und ohne PIN erfolgreich.
    if (enabled != m_services->pinLockEnabled())
    {
        if (!enabled && !(WebValidation::form(input, "pin", pin, sizeof(pin)) && m_services->validatePin(pin)))
            code = 403;
        else
        {
            Command cmd;
            cmd.action = Action::Settings;
            auto &job = cmd.settings;
            job.type = Settings::RequestType::Save;
            job.changedFields = Settings::PinLock;
            job.baselineRevision = m_services->configuration(job.configuration);
            job.configuration.pinLock = enabled;
            memcpy(job.currentPin, pin, sizeof(pin));
            code = execute(cmd);
            // Die Kopie der Konfiguration enthält auch die WLAN-Passwörter.
            mbedtls_platform_zeroize(&cmd, sizeof(cmd));
        }
    }
    mbedtls_platform_zeroize(pin, sizeof(pin));
    if (code != 200)
        return result(r, code);
    // Nach dem Einschalten sind alle Sitzungen abgemeldet, der Browser sperrt dann wie im Original.
    const char *status = lockStatus(m_services->pinLockEnabled(), !authenticated(r));
    httpd_resp_set_type(r, "application/json");
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    return httpd_resp_send(r, status, HTTPD_RESP_USE_STRLEN);
}

esp_err_t WebPortal::theme(httpd_req_t *r, const char *input)
{
    Command cmd;
    cmd.action = Action::Theme;
    auto &job = cmd.modern;
    auto &save = cmd.settings;
    const bool stored = m_services->modernColors(job.colors);
    const auto revision = m_services->configuration(save.configuration);
    const uint8_t current = save.configuration.theme;
    int code = 400;
    if (r->method == HTTP_GET)
        code = strcmp(r->uri, "/api/theme") ? 404 : 200;
    else
    {
        job.reset = !strcmp(r->uri, "/api/theme/modern/reset");
        Json data(parse(input), cJSON_Delete);
        if (!job.reset && strcmp(r->uri, "/api/theme"))
            code = 404;
        else if (cJSON_IsObject(data.get()))
        {
            // Fehlt theme, bleibt das gespeicherte Thema. Fehlt colors, bleiben die Grundfarben. Ein ungültiger Teil weist die ganze Anfrage ab.
            const bool theme = !job.reset && cJSON_HasObjectItem(data.get(), "theme"), colors = job.reset || cJSON_HasObjectItem(data.get(), "colors");
            double index = 0;
            bool valid = (theme || colors) && (!theme || number(data.get(), "theme", 0, ThemenAnzahl - 1, index)) && (job.reset || !colors || modernFields(data.get(), job.colors));
            if (valid && theme)
            {
                save.type = Settings::RequestType::Save;
                save.baselineRevision = revision;
                save.configuration.theme = static_cast<uint8_t>(index);
                save.changedFields = Settings::Theme;
                valid = AppServices::validConfiguration(save.configuration);
            }
            cmd.values[0] = colors;
            cmd.restart = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(data.get(), "restart"));
            if (valid)
                code = execute(cmd);
        }
    }
    // Die Kopie der Konfiguration enthält auch die WLAN-Passwörter.
    mbedtls_platform_zeroize(&save, sizeof(save));
    if (r->method == HTTP_GET && code == 200)
        return sendJson(r, themeJson(current, job.colors, stored));
    return result(r, code);
}

esp_err_t WebPortal::stations(httpd_req_t *r, const char *input)
{
    const bool presets = !strcmp(r->uri, "/api/radio/presets");
    if (r->method == HTTP_GET)
    {
        if (!presets && strcmp(r->uri, "/api/stations"))
            return result(r, 404);
        m_stations->withStations([&](const Station *s, int count)
        {
            for (int i = 0; i < MAX_STATIONS; ++i)
                m_stationScratch[i] = i < count ? s[i] : Station{};
        });
        auto *o = cJSON_CreateObject();
        auto *array = cJSON_AddArrayToObject(o, presets ? "presets" : "stations");
        cJSON_AddNumberToObject(o, "revision", state().stationsRevision);
        for (int i = 0; i < MAX_STATIONS; ++i)
        {
            const auto &s = m_stationScratch[i];
            if (presets && !s.preset)
                continue;
            auto *entry = cJSON_CreateObject();
            cJSON_AddNumberToObject(entry, "id", i);
            cJSON_AddStringToObject(entry, "shortName", s.shortName);
            cJSON_AddStringToObject(entry, "name", s.name);
            if (!presets)
            {
                cJSON_AddStringToObject(entry, "url", s.url);
                cJSON_AddBoolToObject(entry, "preset", s.preset);
            }
            cJSON_AddItemToArray(array, entry);
        }
        return sendJson(r, o);
    }
    if (presets)
        return result(r, 404);
    const bool reset = !strcmp(r->uri, "/api/radio/reset-stations");
    Json data(parse(input), cJSON_Delete);
    double revision;
    if (!data)
        return result(r, 400);
    if (!number(data.get(), "revision", 1, UINT32_MAX, revision) || revision != state().stationsRevision)
        return result(r, 409);
    Command cmd;
    cmd.action = Action::StationsChanged;
    auto *order = reset ? nullptr : cJSON_GetObjectItemCaseSensitive(data.get(), "order");
    cmd.reorder = order != nullptr;
    if (reset)
    {
        // Defaults werden aus der bereits übernommenen Quelle gelesen, erst danach gemeinsam gespeichert.
        for (int i = 0; i < MAX_STATIONS; ++i)
        {
            auto &s = m_stationScratch[i];
            s = Station{};
            if (i < DEFAULT_STATION_COUNT)
            {
                stringutils::safeCopy(s.shortName, DEFAULT_STATIONS[i].shortName);
                stringutils::safeCopy(s.name, DEFAULT_STATIONS[i].name);
                stringutils::safeCopy(s.url, DEFAULT_STATIONS[i].url);
                s.preset = DEFAULT_STATIONS[i].preset;
            }
        }
    }
    else
    {
        auto *array = cJSON_GetObjectItemCaseSensitive(data.get(), "stations");
        if (!cJSON_IsArray(array) || cJSON_GetArraySize(array) != MAX_STATIONS)
            return result(r, 400);
        if (order)
        {
            if (!cJSON_IsArray(order) || cJSON_GetArraySize(order) != MAX_STATIONS)
                return result(r, 400);
            bool seen[MAX_STATIONS]{};
            for (int i = 0; i < MAX_STATIONS; ++i)
            {
                const auto *v = cJSON_GetArrayItem(order, i);
                if (!cJSON_IsNumber(v) || !std::isfinite(v->valuedouble) || v->valuedouble < 0 || v->valuedouble >= MAX_STATIONS || std::floor(v->valuedouble) != v->valuedouble || seen[static_cast<int>(v->valuedouble)])
                    return result(r, 400);
                seen[static_cast<int>(v->valuedouble)] = true;
                cmd.order[i] = static_cast<uint8_t>(v->valuedouble);
            }
        }
        int favorites = 0;
        for (int i = 0; i < MAX_STATIONS; ++i)
        {
            auto *entry = cJSON_GetArrayItem(array, i);
            auto &s = m_stationScratch[i];
            s = Station{};
            auto *preset = cJSON_GetObjectItemCaseSensitive(entry, "preset");
            if (!field(entry, "shortName", s.shortName, sizeof(s.shortName)) || !field(entry, "name", s.name, sizeof(s.name)) || !field(entry, "url", s.url, sizeof(s.url)) || !cJSON_IsBool(preset))
                return result(r, 400);
            s.preset = cJSON_IsTrue(preset);
            if (s.preset)
                ++favorites;
            if (s.url[0] && ((strncmp(s.url, "http://", 7) && strncmp(s.url, "https://", 8)) || !s.name[0] || !s.shortName[0]))
                return result(r, 400);
            if (!s.url[0] && (s.shortName[0] || s.name[0] || s.preset))
                return result(r, 400);
        }
        if (favorites > 10)
            return result(r, 400);
    }
    // Ohne order alle geänderten URLs markieren, da die lokale Senderwahl während des Speicherns wechseln kann. Mit order darf
    // sich nichts außer der Reihenfolge ändern, der Koordinator ordnet die Senderwahl erst beim Anwenden neu zu.
    bool sameStations = true;
    m_stations->withStations([&](const Station *old, int count)
    {
        for (int i = 0; i < MAX_STATIONS; ++i)
            if (cmd.reorder)
            {
                const Station *before = cmd.order[i] < count ? &old[cmd.order[i]] : nullptr;
                const auto &s = m_stationScratch[i];
                if (strcmp(before ? before->shortName : "", s.shortName) || strcmp(before ? before->name : "", s.name) || strcmp(before ? before->url : "", s.url) || (before && before->preset) != s.preset)
                    sameStations = false;
            }
            else if (strcmp(i < count ? old[i].url : "", m_stationScratch[i].url))
                cmd.values[i / 24] |= 1u << (i % 24);
    });
    if (!sameStations)
        return result(r, 409);
    // Nur dieser Task stellt Aufträge ein. Ein jetzt freier Platz bleibt bis execute() frei, nach dem Speichern scheitert der Auftrag nicht mehr an der Warteschlange.
    if (!uxQueueSpacesAvailable(m_commands))
        return result(r, 503);
    if (!m_stations->saveStations(m_stationScratch))
        return result(r, 500);
    const bool reorder = cmd.reorder;
    const int code = execute(cmd);
    const auto applied = state();
    auto *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "success", code == 200);
    cJSON_AddNumberToObject(o, "revision", applied.stationsRevision);
    if (reorder)
        cJSON_AddNumberToObject(o, "stationId", applied.station);
    if (code == 200)
        return sendJson(r, o);
    cJSON_Delete(o);
    return result(r, code);
}

esp_err_t WebPortal::files(httpd_req_t *r, const char *input)
{
    using K = AppServices::WebFileKind;
    const auto media = m_services->mediaStatus();
    const bool ready = media.media == SdServiceStatus::Media::Ready;
    if (r->method == HTTP_GET && !strncmp(r->uri, "/api/sdcard/info", 16))
    {
        auto *o = cJSON_CreateObject();
        cJSON_AddBoolToObject(o, "available", ready);
        cJSON_AddStringToObject(o, "cardType", "SD");
        cJSON_AddNumberToObject(o, "totalBytes", media.totalBytes);
        cJSON_AddNumberToObject(o, "usedBytes", media.usedBytes);
        cJSON_AddNumberToObject(o, "freeBytes", media.totalBytes - media.usedBytes);
        if (!ready)
            cJSON_AddStringToObject(o, "message", "Keine SD-Karte erkannt.");
        return sendJson(r, o);
    }
    AppServices::WebFileRequest job;
    char query[512]{};
    if (r->method == HTTP_GET && !strncmp(r->uri, "/api/files/list?", 16))
    {
        if (!ready)
        {
            auto *o = cJSON_CreateObject();
            cJSON_AddBoolToObject(o, "available", false);
            cJSON_AddStringToObject(o, "message", "Keine SD-Karte erkannt.");
            cJSON_AddArrayToObject(o, "files");
            return sendJson(r, o);
        }
        if (httpd_req_get_url_query_str(r, query, sizeof(query)) != ESP_OK || !WebValidation::form(query, "path", job.path, sizeof(job.path)))
            return result(r, 400);
        job.kind = K::List;
    }
    else if (r->method == HTTP_POST)
    {
        if (!strcmp(r->uri, "/api/files/mkdir"))
            job.kind = K::Mkdir;
        else if (!strcmp(r->uri, "/api/files/delete"))
            job.kind = K::Delete;
        else if (!strcmp(r->uri, "/api/files/rename"))
            job.kind = K::Rename;
        else
            return result(r, 404);
        if (!WebValidation::form(input, job.kind == K::Rename ? "oldPath" : "path", job.path, sizeof(job.path)) || (job.kind == K::Rename && !WebValidation::form(input, "newPath", job.target, sizeof(job.target))))
            return result(r, 400);
    }
    else
        return result(r, 404);
    if (!WebValidation::path(job.path, job.kind == K::List) || (job.kind == K::Rename && !WebValidation::path(job.target, false)))
        return result(r, 400);
    Command command;
    command.action = Action::LeaseFiles;
    int code = execute(command);
    if (code != 200)
        return result(r, code, code == 409 ? leaseReason() : WebFileStore::Reason::None);
    const auto answer = m_services->webFile(job);
    command.action = Action::ReleaseFiles;
    execute(command);
    if (answer.status != 200)
        return result(r, answer.status, answer.reason);
    if (job.kind != K::List)
        return result(r, 200);
    auto *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "available", true);
    cJSON_AddNumberToObject(o, "skipped", answer.skipped);
    auto *array = cJSON_AddArrayToObject(o, "files");
    for (size_t i = 0; i < answer.count; ++i)
    {
        const auto &file = m_services->webEntries()[i];
        auto *entry = cJSON_CreateObject();
        // Der vollständige Name steht nur im Pfad. Der Browser baut daraus die Pfade für Öffnen, Umbenennen und Löschen.
        cJSON_AddStringToObject(entry, "name", strrchr(file.path, '/') + 1);
        cJSON_AddNumberToObject(entry, "size", file.size);
        cJSON_AddBoolToObject(entry, "isDirectory", file.directory);
        cJSON_AddItemToArray(array, entry);
    }
    return sendJson(r, o);
}

esp_err_t WebPortal::tree(httpd_req_t *r, const char *input)
{
    using K = AppServices::WebFileKind;
    if (r->method == HTTP_POST)
    {
        AppServices::WebFileRequest job;
        char action[8]{};
        if (!WebValidation::form(input, "path", job.path, sizeof(job.path)) || !WebValidation::path(job.path, false) || !WebValidation::form(input, "action", action, sizeof(action)))
            return result(r, 400);
        if (!strcmp(action, "count"))
            job.kind = K::TreeCount;
        else if (!strcmp(action, "delete"))
            job.kind = K::TreeDelete;
        else
            return result(r, 400);
        Command command;
        command.action = Action::LeaseFiles;
        const int code = execute(command);
        if (code != 200)
            return result(r, code, code == 409 ? leaseReason() : WebFileStore::Reason::None);
        const auto answer = m_services->webFile(job);
        if (answer.status != 200)
        {
            command.action = Action::ReleaseFiles;
            execute(command);
            return result(r, answer.status, answer.reason);
        }
        // Ab hier hält der Dienstworker die Kartensperre und gibt sie am Ende der Baumarbeit selbst frei.
    }
    else if (r->method != HTTP_GET)
        return result(r, 404);
    const auto tree = m_services->webTree();
    auto *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "success", true);
    cJSON_AddNumberToObject(o, "id", tree.id);
    cJSON_AddStringToObject(o, "action", tree.remove ? "delete" : "count");
    cJSON_AddBoolToObject(o, "running", tree.running);
    cJSON_AddStringToObject(o, "path", tree.path);
    cJSON_AddNumberToObject(o, "files", tree.files);
    cJSON_AddNumberToObject(o, "folders", tree.folders);
    if (tree.id && !tree.running)
    {
        cJSON_AddNumberToObject(o, "status", tree.status);
        if (reasonName(tree.reason))
            cJSON_AddStringToObject(o, "reason", reasonName(tree.reason));
        if (tree.failed[0])
            cJSON_AddStringToObject(o, "failed", tree.failed);
    }
    return sendJson(r, o);
}

void WebPortal::ftpJson(cJSON *o)
{
    if (!o)
        return;
    const auto status = m_ftp->status();
    char text[32] = "";
    cJSON_AddBoolToObject(o, "enabled", status.enabled);
    cJSON_AddBoolToObject(o, "listening", status.listening);
    cJSON_AddStringToObject(o, "state", !status.enabled ? "off" : state().sdBusy ? "sd"
                                                              : status.client    ? "client"
                                                                                 : "on");
    if (status.ip[0])
        snprintf(text, sizeof(text), "ftp://%u.%u.%u.%u", status.ip[0], status.ip[1], status.ip[2], status.ip[3]);
    cJSON_AddStringToObject(o, "address", text);
    cJSON_AddNumberToObject(o, "port", status.port);
    cJSON_AddStringToObject(o, "user", FtpProtocol::User);
    text[0] = 0;
    if (status.client)
        snprintf(text, sizeof(text), "%u.%u.%u.%u", status.clientIp[0], status.clientIp[1], status.clientIp[2], status.clientIp[3]);
    cJSON_AddStringToObject(o, "client", text);
    cJSON_AddBoolToObject(o, "loggedIn", status.loggedIn);
    cJSON_AddStringToObject(o, "transfer", status.transfer == FtpSession::Transfer::None ? "" : FtpService::transferName(status.transfer));
    cJSON_AddBoolToObject(o, "password", status.password);
}

esp_err_t WebPortal::ftp(httpd_req_t *r, const char *input)
{
    if (!m_ftp)
        return result(r, 503);
    const bool post = r->method == HTTP_POST;
    if (!strcmp(r->uri, "/api/ftp"))
    {
        if (post)
        {
            Json request(parse(input), cJSON_Delete);
            const auto *enabled = request ? cJSON_GetObjectItemCaseSensitive(request.get(), "enabled") : nullptr;
            if (!cJSON_IsObject(request.get()) || !cJSON_IsBool(enabled))
                return result(r, 400);
            m_ftp->setEnabled(cJSON_IsTrue(enabled));
        }
        auto *o = cJSON_CreateObject();
        ftpJson(o);
        return sendJson(r, o);
    }
    if (!post)
        return result(r, 404);
    if (!strcmp(r->uri, "/api/ftp/password"))
    {
        char password[FtpProtocol::PasswordCapacity]{};
        bool present = m_ftp->password(password);
        // Vor dem ersten Einschalten gibt es noch kein Passwort, der Dienstworker erzeugt und speichert eines.
        if (!present && m_services->requestFtpPassword(false))
            for (int step = 0; step < FtpPasswordSteps && !present; ++step)
            {
                vTaskDelay(pdMS_TO_TICKS(FtpWaitStepMs));
                present = m_ftp->password(password);
            }
        if (!present)
            return result(r, 503);
        Json o(cJSON_CreateObject(), cJSON_Delete);
        cJSON_AddBoolToObject(o.get(), "success", true);
        cJSON *secret = cJSON_AddStringToObject(o.get(), "password", password);
        mbedtls_platform_zeroize(password, sizeof(password));
        char *text = secret ? cJSON_PrintUnformatted(o.get()) : nullptr;
        if (secret)
            mbedtls_platform_zeroize(secret->valuestring, strlen(secret->valuestring));
        if (!text)
            return result(r, 507);
        LOG_INFO("FTP Passwort im Webportal abgerufen");
        httpd_resp_set_type(r, "application/json; charset=utf-8");
        httpd_resp_set_hdr(r, "Cache-Control", "no-store");
        const size_t length = strlen(text);
        const auto sent = httpd_resp_send(r, text, length);
        mbedtls_platform_zeroize(text, length);
        cJSON_free(text);
        return sent;
    }
    if (!strcmp(r->uri, "/api/ftp/renew"))
    {
        const uint32_t before = m_ftp->passwordRevision();
        if (!m_ftp->renewPassword())
            return result(r, 503);
        // Erst das gespeicherte neue Passwort zählt. Scheitert das Speichern, gilt das bisherige weiter.
        for (int step = 0; step < FtpRenewSteps; ++step)
        {
            vTaskDelay(pdMS_TO_TICKS(FtpWaitStepMs));
            if (m_ftp->passwordRevision() != before)
                return result(r, 200);
        }
        return result(r, 500);
    }
    return result(r, 404);
}

esp_err_t WebPortal::upload(httpd_req_t *r)
{
    using K = AppServices::WebFileKind;
    constexpr size_t ChunkBytes = 4096;
    char query[768]{}, phase[16]{}, text[32]{};
    int value = 0;
    if (httpd_req_get_url_query_str(r, query, sizeof(query)) != ESP_OK || !WebValidation::form(query, "phase", phase, sizeof(phase)))
        return result(r, 400);
    if (m_services->fileLease() != FileLease::Owner::Web)
        m_uploadId = 0;
    AppServices::WebFileRequest job;
    auto finish = [&](bool commit)
    {
        job.kind = commit ? K::Commit : K::Abort;
        const auto answer = m_services->webFile(job);
        if (answer.status != 200)
        {
            job.kind = K::Abort;
            m_services->webFile(job);
        }
        Command cmd;
        cmd.action = Action::ReleaseFiles;
        execute(cmd);
        m_uploadId = 0;
        return answer;
    };
    if (!strcmp(phase, "begin"))
    {
        if (m_uploadId)
            return result(r, 409);
        if (r->content_len || !WebValidation::form(query, "path", job.path, sizeof(job.path)) || !WebValidation::path(job.path, false) || !WebValidation::form(query, "size", text, sizeof(text)) || !WebValidation::integer(text, 0, WebValidation::MaxUpload, value))
            return result(r, 400);
        // Ersetzen nur mit overwrite=1, das der Browser erst nach der Rückfrage sendet. Jeder andere Wert ersetzt nichts.
        job.overwrite = WebValidation::form(query, "overwrite", text, sizeof(text)) && !strcmp(text, "1");
        Command cmd;
        cmd.action = Action::LeaseFiles;
        const int status = execute(cmd);
        if (status != 200)
            return result(r, status, status == 409 ? leaseReason() : WebFileStore::Reason::None);
        job.kind = K::Begin;
        job.size = value;
        const auto begun = m_services->webFile(job);
        if (begun.status != 200)
        {
            finish(false);
            return result(r, begun.status, begun.reason);
        }
        m_uploadId = (esp_random() & 0x7fffffffu) | 1u;
        m_uploadSize = value;
        m_uploaded = 0;
        m_uploadGeneration = m_authGeneration.load();
        auto *o = cJSON_CreateObject();
        cJSON_AddBoolToObject(o, "success", true);
        cJSON_AddNumberToObject(o, "id", m_uploadId);
        return sendJson(r, o);
    }
    if (!WebValidation::form(query, "id", text, sizeof(text)) || !WebValidation::integer(text, 1, INT_MAX, value) || !m_uploadId || static_cast<uint32_t>(value) != m_uploadId)
        return result(r, 409);
    if (m_uploadGeneration != m_authGeneration.load())
    {
        finish(false);
        return result(r, 401);
    }
    if (!strcmp(phase, "abort"))
        return result(r, finish(false).status);
    if (!strcmp(phase, "commit"))
    {
        if (r->content_len || m_uploaded != m_uploadSize)
            return result(r, 400);
        const auto committed = finish(true);
        return result(r, committed.status, committed.reason);
    }
    if (strcmp(phase, "chunk") || !WebValidation::form(query, "offset", text, sizeof(text)) || !WebValidation::integer(text, 0, WebValidation::MaxUpload, value) || static_cast<uint32_t>(value) != m_uploaded || !r->content_len || r->content_len > ChunkBytes || m_uploaded + r->content_len > m_uploadSize)
        return result(r, 400);
    size_t received = 0;
    while (received < r->content_len)
    {
        const int count = httpd_req_recv(r, reinterpret_cast<char *>(job.data) + received, r->content_len - received);
        if (count <= 0)
        {
            finish(false);
            return result(r, 400);
        }
        received += count;
    }
    job.kind = K::Chunk;
    job.count = received;
    const auto written = m_services->webFile(job);
    if (written.status != 200)
    {
        finish(false);
        return result(r, written.status, written.reason);
    }
    m_uploaded += received;
    return result(r, 200);
}

esp_err_t WebPortal::screenshot(httpd_req_t *r)
{
    // Belegt den gemeinsamen Aufnahmeplatz vor dem Auftrag bis zum Ende der Übertragung, der USB-Screenshot wird solange abgewiesen.
    if (m_shotBusy.exchange(true))
        return result(r, 409);

    struct Release
    {
        std::atomic<bool> &busy;

        ~Release()
        {
            busy = false;
        }
    } release{m_shotBusy};

    Command cmd;
    cmd.action = Action::Screenshot;
    const int code = execute(cmd);
    if (code != 200)
        return result(r, code);
    const uint32_t generation = m_authGeneration.load();
    uint8_t header[66]{};
    header[0] = 'B';
    header[1] = 'M';
    auto put = [&](size_t at, uint32_t value, size_t count)
    {
        for (size_t i = 0; i < count; ++i)
            header[at + i] = value >> (i * 8);
    };
    put(2, 66 + ShotBytes, 4);
    put(10, 66, 4);
    put(14, 40, 4);
    put(18, ShotWidth, 4);
    put(22, ShotHeight, 4);
    put(26, 1, 2);
    put(28, 16, 2);
    put(30, 3, 4);
    put(34, ShotBytes, 4);
    put(54, 0xf800, 4);
    put(58, 0x07e0, 4);
    put(62, 0x001f, 4);
    httpd_resp_set_type(r, "image/bmp");
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    if (httpd_resp_send_chunk(r, reinterpret_cast<char *>(header), sizeof(header)) != ESP_OK)
        return ESP_FAIL;
    // Der HTTP-Server verwirft Requestheader nach dem ersten Sendeblock. Die zuvor geprüfte Sitzung über ihre Generation schützen.
    for (int row = ShotHeight - 1; row >= 0; --row)
    {
        if (generation != m_authGeneration.load())
            return ESP_FAIL;
        if (httpd_resp_send_chunk(r, reinterpret_cast<char *>(m_pixels + row * ShotWidth), ShotWidth * 2) != ESP_OK)
            return ESP_FAIL;
    }
    return httpd_resp_send_chunk(r, nullptr, 0);
}
