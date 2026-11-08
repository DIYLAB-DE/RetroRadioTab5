#include "TlsSocket.h"

#include <esp_crt_bundle.h>
#include <esp_tls.h>
#include <mbedtls/ssl.h>

#include <lwip/sockets.h>

#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstring>
#include <fcntl.h>

namespace {
    const char *TAG = "audio-lite";

    constexpr uint32_t CONNECT_TIMEOUT_MS = 8000; ///< Fenster für Namensauflösung, TCP-Aufbau und TLS-Handshake zusammen
    constexpr uint32_t US_PER_MS = 1000;          ///< Mikrosekunden je Millisekunde
    constexpr TickType_t SEND_RETRY_DELAY = 1;    ///< Wartezeit in Ticks zwischen zwei Sendeversuchen bei vollem Puffer
}

// ===================================================================
// Auf- und Abbau
// ===================================================================

/**
 * @brief Destruktor, schließt eine offene Verbindung
 */
TlsSocket::~TlsSocket()
{
    stop();
}

/**
 * @brief Baut die TLS-Verbindung auf und prüft das Serverzertifikat gegen das eingebaute Bündel
 */
bool TlsSocket::connect(const char *host, uint16_t port, uint32_t timeoutMs)
{
    if (!host || host[0] == '\0')
    {
        ESP_LOGE(TAG, "connect ohne Hostnamen");
        return false;
    }

    stop();

    // Eigene Vorgabe für den Verbindungsaufbau, NICHT m_timeoutMs: setTimeout() setzt die
    // Lese-/Schreibgrenze, ein vollständiger Handshake braucht auf diesem Chip aber je nach
    // Server über eine Sekunde und bekommt deshalb ein eigenes Fenster.
    const uint32_t effectiveTimeout = (timeoutMs > 0) ? timeoutMs : CONNECT_TIMEOUT_MS;

    m_tls = esp_tls_init();
    if (!m_tls)
    {
        ESP_LOGE(TAG, "esp_tls_init fehlgeschlagen, kein Speicher");
        return false;
    }

    esp_tls_cfg_t cfg = {};
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.timeout_ms = static_cast<int>(effectiveTimeout);

    const int rc = esp_tls_conn_new_sync(host, static_cast<int>(strlen(host)), port, &cfg, m_tls);
    if (rc != 1)
    {
        int tlsCode = 0;
        int certFlags = 0;
        esp_tls_error_handle_t errHandle = nullptr;
        if (esp_tls_get_error_handle(m_tls, &errHandle) == ESP_OK && errHandle)
            esp_tls_get_and_clear_last_error(errHandle, &tlsCode, &certFlags);
        ESP_LOGE(TAG, "TLS-Aufbau zu %s:%u fehlgeschlagen, mbedtls -0x%04x, Zertifikatsflags 0x%x", host, static_cast<unsigned>(port), static_cast<unsigned>(-tlsCode), static_cast<unsigned>(certFlags));
        stop();
        return false;
    }

    // Ab hier darf kein Aufruf mehr warten: der Deskriptor wird auf non-blocking gestellt,
    // read() und available() melden bei leerem Socket sofort "nichts da". Die Aufrufer haben
    // für diesen Fall eigene Wiederholungslogik.
    int fd = -1;
    if (esp_tls_get_conn_sockfd(m_tls, &fd) != ESP_OK || fd < 0)
    {
        ESP_LOGE(TAG, "TLS-Socket-Deskriptor nicht abfragbar");
        stop();
        return false;
    }
    const int flags = lwip_fcntl(fd, F_GETFL, 0);
    lwip_fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    ESP_LOGI(TAG, "TLS-Verbindung mit %s:%u steht", host, static_cast<unsigned>(port));
    return true;
}

/**
 * @brief Schließt die Verbindung und gibt den TLS-Kontext frei
 */
void TlsSocket::stop()
{
    if (!m_tls)
        return;

    esp_tls_conn_destroy(m_tls);
    m_tls = nullptr;
    m_fatalError = false;
}

// ===================================================================
// Zustand
// ===================================================================

/**
 * @brief Prüft, ob die Verbindung noch nutzbar ist
 */
bool TlsSocket::connected()
{
    // Die Verbindung gilt als offen, solange kein harter Fehler aufgetreten ist. Auch nach
    // einem close_notify der Gegenstelle bleibt sie "verbunden", damit Restdaten über
    // available() und read() abholbar bleiben.
    return m_tls != nullptr && !m_fatalError;
}

/**
 * @brief Anzahl der bereits entschlüsselten, sofort lesbaren Bytes
 */
int TlsSocket::available()
{
    if (!m_tls)
        return 0;

    auto *ssl = static_cast<mbedtls_ssl_context *>(esp_tls_get_ssl_context(m_tls));
    if (!ssl)
        return 0;

    // mbedtls_ssl_get_bytes_avail() zählt nur, was schon entschlüsselt im TLS-Puffer liegt.
    // Der Leseaufruf mit Länge 0 davor holt einen im Kernel wartenden Record herein und
    // entschlüsselt ihn, ohne Anwendungsdaten zu konsumieren. Ohne diesen Schritt bliebe der
    // Rückgabewert dauerhaft 0, obwohl Daten anliegen, und die Aufrufer würden nie lesen.
    unsigned char dummy = 0;
    const int rc = mbedtls_ssl_read(ssl, &dummy, 0);
    if (rc < 0 && rc != MBEDTLS_ERR_SSL_WANT_READ && rc != MBEDTLS_ERR_SSL_WANT_WRITE && rc != MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY)
    {
        if (!m_fatalError)
            ESP_LOGE(TAG, "TLS-Lesefehler, mbedtls -0x%04x", static_cast<unsigned>(-rc));
        m_fatalError = true;
        return 0;
    }

    return static_cast<int>(mbedtls_ssl_get_bytes_avail(ssl));
}

// ===================================================================
// Lesen und Schreiben
// ===================================================================

/**
 * @brief Liest ein einzelnes Byte
 */
int TlsSocket::read()
{
    uint8_t b = 0;
    const int rc = read(&b, 1);
    if (rc != 1)
        return -1;

    return static_cast<int>(b);
}

/**
 * @brief Liest höchstens len Bytes aus dem entschlüsselten Datenstrom
 */
int TlsSocket::read(uint8_t *buf, size_t len)
{
    if (!m_tls || !buf || len == 0)
        return 0;

    const ssize_t rc = esp_tls_conn_read(m_tls, buf, len);
    if (rc > 0)
        return static_cast<int>(rc);

    // 0 heißt: die Gegenstelle hat ordentlich geschlossen, close_notify ist dieselbe Aussage
    // auf TLS-Ebene, alle Anwendungsdaten waren zu diesem Zeitpunkt bereits ausgeliefert.
    // WANT_READ/WANT_WRITE heißt: gerade kein vollständiger Record entschlüsselbar. Alles
    // drei wird als "nichts da" gemeldet, nie gewartet.
    if (rc == 0 || rc == ESP_TLS_ERR_SSL_WANT_READ || rc == ESP_TLS_ERR_SSL_WANT_WRITE || rc == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY)
        return 0;

    if (!m_fatalError)
        ESP_LOGE(TAG, "TLS-Lesefehler, mbedtls -0x%04x", static_cast<unsigned>(-rc));
    m_fatalError = true;
    return -1;
}

/**
 * @brief Sendet eine Zeichenkette vollständig über die TLS-Verbindung
 */
int TlsSocket::print(const char *str)
{
    if (!m_tls || !str)
        return -1;

    const size_t total = strlen(str);
    size_t sent = 0;
    const int64_t deadline = esp_timer_get_time() + static_cast<int64_t>(m_timeoutMs) * US_PER_MS;

    while (sent < total)
    {
        const ssize_t rc = esp_tls_conn_write(m_tls, str + sent, total - sent);
        if (rc > 0)
        {
            sent += static_cast<size_t>(rc);
            continue;
        }

        // Ein voller Sendepuffer liefert auf dem non-blocking Socket WANT_WRITE, innerhalb
        // der Zeitgrenze wird der Rest wiederholt angeboten.
        if (rc == 0 || rc == ESP_TLS_ERR_SSL_WANT_READ || rc == ESP_TLS_ERR_SSL_WANT_WRITE)
        {
            if (esp_timer_get_time() >= deadline)
            {
                ESP_LOGE(TAG, "Zeitgrenze beim Senden nach %u von %u Bytes", static_cast<unsigned>(sent), static_cast<unsigned>(total));
                return -1;
            }
            vTaskDelay(SEND_RETRY_DELAY);
            continue;
        }

        if (!m_fatalError)
            ESP_LOGE(TAG, "TLS-Sendefehler, mbedtls -0x%04x", static_cast<unsigned>(-rc));
        m_fatalError = true;
        return -1;
    }

    return static_cast<int>(sent);
}

// ===================================================================
// Konfiguration
// ===================================================================

/**
 * @brief Setzt die Zeitgrenze für Schreibvorgänge
 */
void TlsSocket::setTimeout(uint32_t ms)
{
    m_timeoutMs = ms;
}
