#include "TcpSocket.h"

#include <lwip/netdb.h>
#include <lwip/sockets.h>

#include <esp_log.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>

namespace {
    const char *TAG = "audio-lite";

    constexpr int SOCKET_INVALID = -1;        ///< Kennwert für "kein Deskriptor offen"
    constexpr size_t PORT_STR_LEN = 8;        ///< Puffer für den Port als Text bei der Namensauflösung
    constexpr uint32_t MS_PER_SECOND = 1000;  ///< Millisekunden je Sekunde
    constexpr uint32_t US_PER_MS = 1000;      ///< Mikrosekunden je Millisekunde
    constexpr uint32_t CONNECT_TIMEOUT_MS = 3000; ///< Fenster für den Verbindungsaufbau, wie Arduinos Vorgabe
    constexpr size_t RECV_CHUNK_MAX = 1436;   ///< Obergrenze je recv-Aufruf, eine TCP-Segmentgröße wie bei Arduino
}

// ===================================================================
// Auf- und Abbau
// ===================================================================

/**
 * @brief Destruktor, schließt eine offene Verbindung
 */
TcpSocket::~TcpSocket()
{
    stop();
}

/**
 * @brief Legt Zeitgrenzen und TCP_NODELAY auf dem offenen Deskriptor fest
 */
bool TcpSocket::applySocketOptions()
{
    if (m_fd == SOCKET_INVALID)
        return false;

    struct timeval tv;
    tv.tv_sec = m_timeoutMs / MS_PER_SECOND;
    tv.tv_usec = (m_timeoutMs % MS_PER_SECOND) * US_PER_MS;

    if (lwip_setsockopt(m_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0)
    {
        ESP_LOGE(TAG, "SO_RCVTIMEO fehlgeschlagen, errno %d", errno);
        return false;
    }

    if (lwip_setsockopt(m_fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) < 0)
    {
        ESP_LOGE(TAG, "SO_SNDTIMEO fehlgeschlagen, errno %d", errno);
        return false;
    }

    // Ohne TCP_NODELAY sammelt der Stack kurze Anfrage-Header und verzögert sie um bis zu 200 ms
    int flag = 1;
    if (lwip_setsockopt(m_fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag)) < 0)
        ESP_LOGW(TAG, "TCP_NODELAY fehlgeschlagen, errno %d", errno);

    return true;
}

/**
 * @brief Löst den Namen auf und baut die Verbindung mit Zeitgrenze auf
 */
bool TcpSocket::connect(const char *host, uint16_t port, uint32_t timeoutMs)
{
    if (!host || host[0] == '\0')
    {
        ESP_LOGE(TAG, "connect ohne Hostnamen");
        return false;
    }

    stop();

    // Eigene Vorgabe für den Verbindungsaufbau, NICHT m_timeoutMs: setTimeout() setzt die
    // Lese-/Schreibgrenze (330 ms), aber Arduinos connect() nutzt sein eigenes 3000-ms-Fenster.
    // Ein TCP-Handshake zu einem Redirect-Ziel schafft 330 ms oft nicht.
    const uint32_t effectiveTimeout = (timeoutMs > 0) ? timeoutMs : CONNECT_TIMEOUT_MS;

    char portStr[PORT_STR_LEN];
    const int written = snprintf(portStr, sizeof(portStr), "%u", static_cast<unsigned>(port));
    if (written < 0 || static_cast<size_t>(written) >= sizeof(portStr))
    {
        ESP_LOGE(TAG, "Port %u nicht darstellbar", static_cast<unsigned>(port));
        return false;
    }

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *res = nullptr;
    if (lwip_getaddrinfo(host, portStr, &hints, &res) != 0 || !res)
    {
        ESP_LOGE(TAG, "Namensauflösung fehlgeschlagen: %s", host);
        return false;
    }

    m_fd = lwip_socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (m_fd == SOCKET_INVALID)
    {
        ESP_LOGE(TAG, "socket() fehlgeschlagen, errno %d", errno);
        lwip_freeaddrinfo(res);
        return false;
    }

    // Verbindungsaufbau nicht blockierend, damit die Zeitgrenze über select() greift
    const int savedFlags = lwip_fcntl(m_fd, F_GETFL, 0);
    lwip_fcntl(m_fd, F_SETFL, savedFlags | O_NONBLOCK);

    int rc = lwip_connect(m_fd, res->ai_addr, res->ai_addrlen);
    lwip_freeaddrinfo(res);

    if (rc < 0 && errno != EINPROGRESS)
    {
        ESP_LOGE(TAG, "connect(%s:%u) fehlgeschlagen, errno %d", host, static_cast<unsigned>(port), errno);
        stop();
        return false;
    }

    if (rc < 0)
    {
        fd_set writeSet;
        FD_ZERO(&writeSet);
        FD_SET(m_fd, &writeSet);

        struct timeval tv;
        tv.tv_sec = effectiveTimeout / MS_PER_SECOND;
        tv.tv_usec = (effectiveTimeout % MS_PER_SECOND) * US_PER_MS;

        rc = lwip_select(m_fd + 1, nullptr, &writeSet, nullptr, &tv);
        if (rc <= 0)
        {
            ESP_LOGE(TAG, "Zeitgrenze beim Verbinden mit %s:%u nach %u ms", host, static_cast<unsigned>(port), static_cast<unsigned>(effectiveTimeout));
            stop();
            return false;
        }

        int soError = 0;
        socklen_t soErrorLen = sizeof(soError);
        if (lwip_getsockopt(m_fd, SOL_SOCKET, SO_ERROR, &soError, &soErrorLen) < 0 || soError != 0)
        {
            ESP_LOGE(TAG, "Verbindung zu %s:%u abgewiesen, SO_ERROR %d", host, static_cast<unsigned>(port), soError);
            stop();
            return false;
        }
    }

    // Zurück auf blockierend, ab hier begrenzen SO_RCVTIMEO und SO_SNDTIMEO
    lwip_fcntl(m_fd, F_SETFL, savedFlags);

    if (!applySocketOptions())
    {
        stop();
        return false;
    }

    ESP_LOGI(TAG, "Verbunden mit %s:%u", host, static_cast<unsigned>(port));
    return true;
}

/**
 * @brief Schließt eine offene Verbindung
 */
void TcpSocket::stop()
{
    if (m_fd == SOCKET_INVALID)
        return;

    // Erst beide Richtungen beenden und ungelesene Restdaten leeren, dann schließen. Eine
    // halb offene Verbindung mit gepufferten Restdaten (Gegenstelle hat längst beendet)
    // brachte beim direkten close den TCP-Stack aus dem Tritt, der Assert schlug später
    // im TIME-WAIT-Timer zu.
    lwip_shutdown(m_fd, SHUT_RDWR);
    uint8_t drainBuf[64];
    int guard = 0;
    while (lwip_recv(m_fd, drainBuf, sizeof(drainBuf), MSG_DONTWAIT) > 0 && ++guard < 512)
    {
    }

    lwip_close(m_fd);
    m_fd = SOCKET_INVALID;
}

// ===================================================================
// Zustand
// ===================================================================

/**
 * @brief Prüft, ob die Gegenstelle die Verbindung noch hält
 */
bool TcpSocket::connected()
{
    if (m_fd == SOCKET_INVALID)
        return false;

    // Exakt die Prüfung aus Arduinos NetworkClient::connected(): ein Byte mit MSG_PEEK
    // anschauen und den Fehlercode auswerten. Auch ein ordentlich geschlossenes Ende gilt
    // solange als verbunden, wie keine Fehlerbedingung vorliegt. Restdaten bleiben lesbar.
    uint8_t dummy = 0;
    const int rc = lwip_recv(m_fd, &dummy, 1, MSG_DONTWAIT | MSG_PEEK);
    if (rc > 0)
        return true;

    switch (errno) {
        case ENOTCONN:
        case EPIPE:
        case ECONNRESET:
        case ECONNREFUSED:
        case ECONNABORTED:
            return false;
        default:
            return true;
    }
}

/**
 * @brief Anzahl der sofort lesbaren Bytes
 */
int TcpSocket::available()
{
    if (m_fd == SOCKET_INVALID)
        return 0;

    int count = 0;
    if (lwip_ioctl(m_fd, FIONREAD, &count) < 0)
        return 0;

    return count;
}

// ===================================================================
// Lesen und Schreiben
// ===================================================================

/**
 * @brief Liest ein einzelnes Byte
 */
int TcpSocket::read()
{
    uint8_t b = 0;
    const int rc = read(&b, 1);
    if (rc != 1)
        return -1;

    return static_cast<int>(b);
}

/**
 * @brief Liest höchstens len Bytes
 */
int TcpSocket::read(uint8_t *buf, size_t len)
{
    if (m_fd == SOCKET_INVALID || !buf || len == 0)
        return 0;

    // Höchstens eine TCP-Segmentgröße pro recv, wie Arduinos NetworkClient (Puffer 1436).
    // Große recv-Aufrufe sammeln in esp-lwIP über viele gekettete pbufs - ein Pfad, den
    // Arduino nie durchläuft und der hier deterministisch verfälschte Daten lieferte.
    // Die Aufrufer verbuchen Teillesungen ohnehin korrekt.
    if (len > RECV_CHUNK_MAX)
        len = RECV_CHUNK_MAX;

    // MSG_DONTWAIT wie Arduinos NetworkClient: Ist nichts da, kommt sofort "nichts" zurück,
    // nie eine Wartezeit im Kernel. Die Library hat für leere Sockets eigene Wiederholungs-
    // logik, ein blockierendes recv hielt beim byteweisen Lesen der Chunk-Header dagegen
    // den aufrufenden Task bis zu 330 ms an - sichtbar als einfrierende Visualisierung bei
    // Chunked-Sendern, sobald der Anfangs-Burst des Servers aufgebraucht war.
    const int rc = lwip_recv(m_fd, buf, len, MSG_DONTWAIT);
    if (rc >= 0)
        return rc;

    if (errno == EAGAIN || errno == EWOULDBLOCK)
        return 0;

    ESP_LOGE(TAG, "recv fehlgeschlagen, errno %d", errno);
    return -1;
}

/**
 * @brief Sendet eine Zeichenkette vollständig
 */
int TcpSocket::print(const char *str)
{
    if (m_fd == SOCKET_INVALID || !str)
        return -1;

    const size_t total = strlen(str);
    size_t sent = 0;

    while (sent < total)
    {
        const int rc = lwip_send(m_fd, str + sent, total - sent, 0);
        if (rc > 0)
        {
            sent += static_cast<size_t>(rc);
            continue;
        }

        if (rc < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
        {
            ESP_LOGE(TAG, "Zeitgrenze beim Senden nach %u von %u Bytes", static_cast<unsigned>(sent), static_cast<unsigned>(total));
            return -1;
        }

        ESP_LOGE(TAG, "send fehlgeschlagen, errno %d", errno);
        return -1;
    }

    return static_cast<int>(sent);
}

// ===================================================================
// Konfiguration
// ===================================================================

/**
 * @brief Setzt die Zeitgrenze für Lesen und Schreiben
 */
void TcpSocket::setTimeout(uint32_t ms)
{
    m_timeoutMs = ms;

    if (m_fd != SOCKET_INVALID)
        applySocketOptions();
}
