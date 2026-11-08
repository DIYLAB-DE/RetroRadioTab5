#ifndef WEB_REPLY_H
#define WEB_REPLY_H

#include <Arduino.h>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

/** @brief Antwortweg vom UI-Koordinator zum wartenden HTTP-Task: Queue der Tiefe 1, die nur den Abschluss des gerade erwarteten Auftrags annimmt. Ein später Abschluss eines bereits abgelaufenen Auftrags kann so die Antwort eines neueren nicht überschreiben. */
class WebReply
{
public:
    /** @brief Abschluss eines Auftrags. */
    struct Completion
    {
        uint32_t id = 0;  ///< Kennung des Auftrags
        int status = 500; ///< HTTP-Statuscode
    };

    /** @brief Legt die Queue an. @return false bei fehlendem Speicher. */
    bool begin()
    {
        m_queue = xQueueCreate(1, sizeof(Completion));
        return m_queue != nullptr;
    }

    /** @brief Gibt die Queue frei. */
    void end()
    {
        if (m_queue)
            vQueueDelete(m_queue);
        m_queue = nullptr;
        m_expected = 0;
    }

    /** @brief Verwirft liegengebliebene Abschlüsse und nimmt ab jetzt nur noch id an. Läuft im HTTP-Task vor dem Senden des Auftrags, damit ein sofortiger Abschluss nicht verloren geht. @param id Kennung des Auftrags, nicht 0. */
    void expect(uint32_t id)
    {
        Completion old;
        while (xQueueReceive(m_queue, &old, 0) == pdPASS)
        {
        }
        m_expected = id;
    }

    /** @brief Wartet im HTTP-Task auf den Abschluss des erwarteten Auftrags und beendet danach die Erwartung. @param timeoutMs Höchste Wartezeit in ms. @return HTTP-Statuscode, 504 ohne Abschluss. */
    int wait(uint32_t timeoutMs)
    {
        const uint32_t id = m_expected.load(), start = millis();
        int status = 504;
        while (millis() - start < timeoutMs)
        {
            Completion done;
            // Die Kennung erneut prüfen: Ein veralteter Abschluss kann zwischen Prüfung in complete() und Schreiben in die Queue gelangen.
            if (xQueueReceive(m_queue, &done, pdMS_TO_TICKS(50)) == pdPASS && done.id == id)
            {
                status = done.status;
                break;
            }
        }
        m_expected = 0;
        return status;
    }

    /** @brief Beendet die Erwartung ohne Warten, wenn der Auftrag nicht übergeben werden konnte. */
    void cancel()
    {
        m_expected = 0;
    }

    /** @brief Meldet im UI-Task den Abschluss eines Auftrags, jede andere als die gerade erwartete Kennung wird verworfen. @param id Kennung des Auftrags. @param status HTTP-Statuscode. */
    void complete(uint32_t id, int status)
    {
        const Completion value{id, status};
        if (m_queue && id && id == m_expected.load())
            xQueueOverwrite(m_queue, &value);
    }

private:
    QueueHandle_t m_queue = nullptr;     ///< Abschlüsse, Tiefe 1
    std::atomic<uint32_t> m_expected{0}; ///< Kennung, auf die der HTTP-Task wartet, 0 = keine
};
#endif
