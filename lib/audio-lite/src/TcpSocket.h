#ifndef AUDIO_LITE_TCPSOCKET_H
#define AUDIO_LITE_TCPSOCKET_H

/**
 * @file TcpSocket.h
 * @brief Unverschlüsselter Byte-Socket über lwIP
 */

#include "NetSocket.h"

// ===================================================================
// TcpSocket
// ===================================================================

/**
 * @brief Unverschlüsselte TCP-Verbindung über lwIP-Sockets
 *
 * Die Klasse puffert **nicht** selbst. Jedes `read()` geht unmittelbar an `recv()`, und `available()` meldet ausschließlich den Füllstand des Kernel-Puffers über `FIONREAD`. Das ist der Grund, warum `available()` hier dieselbe Bedeutung hat wie in der Arduino-Vorlage: deren Wert ist die Summe aus eigenem Puffer und `FIONREAD`, und ohne eigenen Puffer bleibt genau der zweite Summand übrig.
 *
 * Die Folge ist ein Systemaufruf je Byte, wenn der HTTP-Code Antwort-Header zeichenweise einliest. Das betrifft einige hundert Bytes je Verbindungsaufbau und liegt nicht im Audio-Pfad, der ausschließlich in Blöcken der Größe `available()` liest.
 *
 * Eine Instanz gehört genau einem Task und synchronisiert nichts.
 */
class TcpSocket : public NetSocket
{
public:
    TcpSocket() = default;

    /**
     * @brief Destruktor, schließt eine offene Verbindung
     */
    ~TcpSocket() override;

    TcpSocket(const TcpSocket &) = delete;
    TcpSocket &operator=(const TcpSocket &) = delete;
    TcpSocket(TcpSocket &&) = delete;
    TcpSocket &operator=(TcpSocket &&) = delete;

    bool connect(const char *host, uint16_t port, uint32_t timeoutMs = 0) override;
    void stop() override;
    bool connected() override;
    int available() override;
    int read() override;
    int read(uint8_t *buf, size_t len) override;
    int print(const char *str) override;
    void setTimeout(uint32_t ms) override;

private:
    /**
     * @brief Legt Sende- und Empfangs-Zeitgrenze sowie TCP_NODELAY auf dem offenen Deskriptor fest
     * @return true wenn alle Optionen gesetzt werden konnten
     *
     * Muss nach jedem erfolgreichen Verbindungsaufbau laufen, weil die Optionen am Deskriptor hängen und nicht an der Instanz.
     */
    bool applySocketOptions();

    int m_fd = -1;              ///< Socket-Deskriptor, -1 wenn keine Verbindung offen ist
    uint32_t m_timeoutMs = 250; ///< Zeitgrenze für Lesen und Schreiben in Millisekunden
};

#endif // AUDIO_LITE_TCPSOCKET_H
