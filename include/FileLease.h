#ifndef FILE_LEASE_H
#define FILE_LEASE_H

#include <atomic>
#include <cstdint>

/** @brief Ausschließlicher Kartenzugriff für die Dateiverwaltung von Webportal und FTP. Ihn hält höchstens ein Besitzer. Vergeben wird nur im UI-Koordinator, weil nur er weiß, ob die SD-Wiedergabe läuft oder ansteht. Freigeben darf jeder Besitzer selbst. Solange die Sperre gehalten wird, startet keine SD-Wiedergabe. */
class FileLease
{
public:
    /** @brief Besitzer der Sperre. */
    enum class Owner : uint8_t
    {
        None, ///< Frei
        Web,  ///< Datei-Manager des Webportals
        Ftp   ///< FTP-Dienst
    };

    /** @brief Antwort auf eine Anforderung. */
    enum class Answer : uint8_t
    {
        Granted, ///< Sperre erteilt
        Playing, ///< SD-Wiedergabe läuft oder ein SD-Auftrag wartet
        Busy     ///< Ein anderer Besitzer hält die Sperre
    };

    /** @brief Entscheidet über eine Anforderung. Nur im UI-Koordinator aufrufen. @param owner Anfordernder Besitzer, nicht None. @param sdBusy SD-Wiedergabe läuft oder ein SD-Auftrag wartet. @return Granted nur, wenn die Sperre frei war und die Karte nicht spielt. */
    Answer grant(Owner owner, bool sdBusy)
    {
        if (owner == Owner::None)
            return Answer::Busy;
        if (sdBusy)
            return Answer::Playing;
        Owner expected = Owner::None;
        return m_owner.compare_exchange_strong(expected, owner) ? Answer::Granted : Answer::Busy;
    }

    /** @brief Gibt die Sperre frei, wenn owner sie hält. Die Sperre eines anderen Besitzers bleibt unberührt. */
    void release(Owner owner)
    {
        Owner expected = owner;
        m_owner.compare_exchange_strong(expected, Owner::None);
    }

    /** @brief Liefert den aktuellen Besitzer. */
    Owner owner() const
    {
        return m_owner.load();
    }

    /** @brief Meldet, ob irgendein Besitzer die Sperre hält. */
    bool held() const
    {
        return m_owner.load() != Owner::None;
    }

private:
    std::atomic<Owner> m_owner{Owner::None}; ///< Aktueller Besitzer
};

/** @brief Briefkasten für genau eine Anforderung der Sperre aus einem anderen Task an den UI-Koordinator, ohne Warteschlange und ohne Speicheranforderung. Der anfragende Task benutzt ask(), answered() und cancel(), der UI-Koordinator pending() und answer(). Zieht der Anfragende nach einer Zeitüberschreitung zurück, gibt genau die Seite eine verspätet erteilte Sperre frei, deren Rückgabewert das verlangt. Beide Freigaben betreffen nur den Besitzer der Anfrage und liegen vor jeder neuen Vergabe. */
class FileLeaseRequest
{
public:
    /** @brief Stellt eine neue Anforderung, nur im anfragenden Task. @return Kennung 1 bis 0xFFFFFF. */
    uint32_t ask()
    {
        m_sequence = m_sequence % 0xFFFFFFu + 1;
        m_ask.store(m_sequence);
        return m_sequence;
    }

    /** @brief Liefert die Antwort auf die Anforderung id, sobald sie vorliegt. @param answer Ziel, nur bei true gesetzt. */
    bool answered(uint32_t id, FileLease::Answer &answer) const
    {
        const uint32_t reply = m_reply.load();
        if (reply >> 8 != id)
            return false;
        answer = static_cast<FileLease::Answer>(reply & 0xffu);
        return true;
    }

    /** @brief Zieht eine unbeantwortete Anforderung zurück. @return true, wenn die Sperre inzwischen doch erteilt wurde. Der Anfragende muss sie dann selbst freigeben. */
    bool cancel(uint32_t id)
    {
        m_ask.store(0);
        FileLease::Answer answer;
        return answered(id, answer) && answer == FileLease::Answer::Granted;
    }

    /** @brief Liefert im UI-Koordinator die Kennung einer noch unbeantworteten Anforderung, sonst 0. */
    uint32_t pending() const
    {
        const uint32_t id = m_ask.load();
        return id != m_answered ? id : 0;
    }

    /** @brief Speichert im UI-Koordinator die Antwort auf id. @return true, wenn die Sperre erteilt wurde, der Anfragende aber schon zurückgezogen hat. Der UI-Koordinator muss sie dann freigeben. */
    bool answer(uint32_t id, FileLease::Answer answer)
    {
        m_answered = id;
        m_reply.store(id << 8 | static_cast<uint32_t>(answer));
        return answer == FileLease::Answer::Granted && m_ask.load() != id;
    }

private:
    std::atomic<uint32_t> m_ask{0};   ///< Offene Anforderung, 0 ohne
    std::atomic<uint32_t> m_reply{0}; ///< Letzte Antwort: Kennung in den oberen 24 Bit, Answer im untersten Byte
    uint32_t m_sequence = 0;          ///< Zähler der Kennungen, nur im anfragenden Task
    uint32_t m_answered = 0;          ///< Zuletzt beantwortete Kennung, nur im UI-Koordinator
};
#endif
