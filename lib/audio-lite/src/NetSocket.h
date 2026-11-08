#ifndef AUDIO_LITE_NETSOCKET_H
#define AUDIO_LITE_NETSOCKET_H

/**
 * @file NetSocket.h
 * @brief Byte-Socket, über den die Library HTTP selbst spricht
 *
 * audio-lite benutzt keinen HTTP-Client. Die Protokoll-Logik (Anfrage-Header, Antwort-Header, ICY-Metadaten, Chunked-Encoding, HLS und TS) liegt vollständig in `Audio.cpp` und braucht darunter nur einen Strom aus Bytes. Diese Schnittstelle ist genau dieser Strom und damit die einzige Stelle, an der eine verschlüsselte Verbindung später andockt.
 *
 * Die acht Operationen sind nicht frei gewählt, sondern exakt die, die der bestehende HTTP-Code an 32 Stellen benutzt. Wer hier etwas hinzufügt, ohne dass eine Aufrufstelle es verlangt, vergrößert die Fläche, die eine spätere TLS-Implementierung nachbauen muss.
 */

#include <cstddef>
#include <cstdint>

// ===================================================================
// NetSocket
// ===================================================================

/**
 * @brief Abstrakter Byte-Socket
 *
 * Version 1 liefert mit `TcpSocket` eine unverschlüsselte Implementierung über lwIP. Eine spätere TLS-Fassung tritt als zweite Implementierung daneben, ohne dass der HTTP-Code davon erfährt.
 *
 * Eine Instanz gehört genau einem Task und synchronisiert nichts.
 */
class NetSocket
{
public:
    virtual ~NetSocket() = default;

    /**
     * @brief Baut eine Verbindung auf
     * @param host Hostname oder IP-Adresse als Text
     * @param port Zielport
     * @param timeoutMs Zeitgrenze für den Verbindungsaufbau, 0 übernimmt den mit `setTimeout()` gesetzten Wert
     * @return true wenn die Verbindung steht
     *
     * Eine bereits offene Verbindung wird zuvor geschlossen.
     */
    virtual bool connect(const char *host, uint16_t port, uint32_t timeoutMs = 0) = 0;

    /**
     * @brief Schließt die Verbindung
     *
     * Darf auch aufgerufen werden, wenn keine Verbindung offen ist.
     */
    virtual void stop() = 0;

    /**
     * @brief Meldet, ob die Gegenstelle die Verbindung noch hält
     * @return true solange die Verbindung steht
     *
     * Liefert false, sobald die Gegenstelle ordentlich geschlossen hat. Noch ungelesene Bytes können danach weiterhin über `available()` und `read()` abgeholt werden.
     */
    virtual bool connected() = 0;

    /**
     * @brief Anzahl der Bytes, die sofort gelesen werden können
     * @return Byte-Anzahl, 0 wenn nichts anliegt
     *
     * Der Wert ist eine Untergrenze und wird vom Aufrufer benutzt, um die Größe des nächsten Lesevorgangs zu bestimmen. Ein `read()` über genau diese Anzahl darf deshalb nicht blockieren.
     */
    virtual int available() = 0;

    /**
     * @brief Liest ein einzelnes Byte
     * @return Das Byte als Wert von 0 bis 255, oder -1 wenn nichts anliegt
     */
    virtual int read() = 0;

    /**
     * @brief Liest einen Block
     * @param buf Zielpuffer
     * @param len Höchstzahl der zu lesenden Bytes
     * @return Anzahl gelesener Bytes, 0 wenn nichts anlag, -1 im Fehlerfall
     *
     * Liest höchstens `len` Bytes und wartet nicht darauf, dass `len` voll wird.
     */
    virtual int read(uint8_t *buf, size_t len) = 0;

    /**
     * @brief Schreibt eine nullterminierte Zeichenkette vollständig
     * @param str Zu sendender Text
     * @return Anzahl gesendeter Bytes, oder -1 im Fehlerfall
     *
     * Kehrt erst zurück, wenn alles gesendet wurde oder ein Fehler aufgetreten ist. Teilsendungen werden intern wiederholt.
     */
    virtual int print(const char *str) = 0;

    /**
     * @brief Setzt die Zeitgrenze für Lese- und Schreibvorgänge
     * @param ms Zeitgrenze in Millisekunden
     *
     * Der Wert gilt in Millisekunden, nicht in Sekunden.
     */
    virtual void setTimeout(uint32_t ms) = 0;
};

#endif // AUDIO_LITE_NETSOCKET_H
