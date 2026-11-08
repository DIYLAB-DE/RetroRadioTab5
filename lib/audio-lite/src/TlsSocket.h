#ifndef AUDIO_LITE_TLSSOCKET_H
#define AUDIO_LITE_TLSSOCKET_H

/**
 * @file TlsSocket.h
 * @brief Verschlüsselter Byte-Socket über esp-tls
 */

#include "NetSocket.h"

struct esp_tls;

// ===================================================================
// TlsSocket
// ===================================================================

/**
 * @brief Verschlüsselte TLS-Verbindung über esp-tls
 *
 * Das Serverzertifikat wird gegen das im Framework eingebaute Zertifikatsbündel geprüft. Eigene CA-Dateien gibt es nicht, und die Prüfung ist nicht abschaltbar. Der Handshake läuft blockierend innerhalb des Zeitfensters von `connect()`, danach steht der Socket auf non-blocking: `read()` und `available()` kehren immer sofort zurück.
 *
 * `available()` meldet ausschließlich bereits entschlüsselte Bytes. Ein `read()` über genau diese Anzahl trifft deshalb immer auf vorhandene Daten und wartet nie.
 *
 * Eine Instanz gehört genau einem Task und synchronisiert nichts.
 */
class TlsSocket : public NetSocket
{
public:
    TlsSocket() = default;

    /**
     * @brief Destruktor, schließt eine offene Verbindung
     */
    ~TlsSocket() override;

    TlsSocket(const TlsSocket &) = delete;
    TlsSocket &operator=(const TlsSocket &) = delete;
    TlsSocket(TlsSocket &&) = delete;
    TlsSocket &operator=(TlsSocket &&) = delete;

    bool connect(const char *host, uint16_t port, uint32_t timeoutMs = 0) override;
    void stop() override;
    bool connected() override;
    int available() override;
    int read() override;
    int read(uint8_t *buf, size_t len) override;
    int print(const char *str) override;
    void setTimeout(uint32_t ms) override;

private:
    esp_tls *m_tls = nullptr;   ///< esp-tls-Handle, nullptr wenn keine Verbindung offen ist
    bool m_fatalError = false;  ///< true nach einem harten TLS-Fehler, connected() meldet dann false
    uint32_t m_timeoutMs = 250; ///< Zeitgrenze für Schreibvorgänge in Millisekunden
};

#endif // AUDIO_LITE_TLSSOCKET_H
