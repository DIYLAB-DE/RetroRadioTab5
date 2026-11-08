#include "WifiSequence.h"
#include <cstring>

void WifiSequence::start()
{
    m_active = true;
    m_attempt = false;
    m_next = 0;
}

void WifiSequence::interrupt()
{
    m_active = m_attempt = false;
}

void WifiSequence::tested(bool connected, const char *ssid)
{
    m_temporary[0] = 0;
    if (!connected)
        start();
    else if (ssid)
    {
        strncpy(m_temporary, ssid, sizeof(m_temporary) - 1);
        m_temporary[sizeof(m_temporary) - 1] = 0;
    }
}

void WifiSequence::restore(const char *const (&ssids)[MaxProfiles])
{
    if (!m_temporary[0])
        return;
    bool saved = false;
    for (const char *ssid : ssids)
        saved |= ssid && !strcmp(ssid, m_temporary);
    m_temporary[0] = 0;
    if (!saved)
        start();
}

WifiSequence::Step WifiSequence::poll(uint32_t now, uint32_t connections, const char *const (&ssids)[MaxProfiles])
{
    // Ein frisch gestarteter Ablauf trennt jede bestehende Verbindung, ältere Verbindungsmeldungen beenden ihn deshalb nicht.
    if (m_active && !m_attempt)
    {
        m_connections = connections;
        return next(now, ssids);
    }
    if (connections != m_connections)
    {
        m_connections = connections;
        m_active = m_attempt = false;
        return Step::Connected;
    }
    if (m_active && now - m_since >= AttemptMs)
        return next(now, ssids);
    return Step::None;
}

WifiSequence::Step WifiSequence::next(uint32_t now, const char *const (&ssids)[MaxProfiles])
{
    for (size_t i = 0; i < MaxProfiles; ++i)
    {
        const size_t index = (m_next + i) % MaxProfiles;
        if (ssids[index] && ssids[index][0])
        {
            m_profile = index;
            m_next = (index + 1) % MaxProfiles;
            m_attempt = true;
            m_since = now;
            return Step::Connect;
        }
    }
    m_active = m_attempt = false;
    return Step::None;
}
