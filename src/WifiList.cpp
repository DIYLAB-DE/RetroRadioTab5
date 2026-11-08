#include "WifiList.h"
#include <algorithm>
#include <cstring>

// Netzfelder und Sortierung nach RSSI selektiv nach RetroRadio/WifiList.cpp, ea4229a34fb39faaff23619092cf09cafb8c51cf. Ohne WiFi-Aufruf, Schleife oder Vector. GPL-3.0, siehe referenz/RetroRadio-LICENSE.
void WifiList::open(LangIndex language)
{
    m_language = language;
    m_count = m_page = 0;
    m_more = false;
    m_state = State::Scanning;
    m_contact.cancel();
}

bool WifiList::setNetworks(const Network *networks, size_t count, bool more)
{
    if (count > MaxNetworks || (count && !networks))
        return false;
    for (size_t i = 0; i < count; ++i)
        if (!networks[i].ssid[0] || !SettingsUi::validText(networks[i].ssid, sizeof(networks[i].ssid)))
            return false;
    m_count = count;
    m_more = more;
    m_page = 0;
    m_state = State::Ready;
    m_contact.cancel();
    for (size_t i = 0; i < count; ++i)
        m_networks[i] = networks[i];
    std::sort(m_networks, m_networks + count, [](const Network &a, const Network &b)
    {
        return a.rssi > b.rssi;
    });
    return true;
}

WifiList::Result WifiList::touch(SettingsUi::TouchPhase phase, int x, int y)
{
    int target = -1;
    if (m_state == State::Ready)
        for (size_t row = 0; row < PageSize; ++row)
            if (m_page * PageSize + row < m_count && SettingsUi::Rect{24, 94 + static_cast<int>(row) * 80, 1232, 72}.contains(x, y))
                target = static_cast<int>(row);
    for (int i = 0; i < 5; ++i)
    {
        const bool enabled = i == 0 || i == 4 || (i == 1 && m_page > 0) || (i == 2 && (m_page + 1) * PageSize < m_count) || (i == 3 && m_state != State::Scanning);
        if (enabled && SettingsUi::Rect{24 + i * 248, 542, 240, 64}.contains(x, y))
            target = 100 + i;
    }
    target = m_contact.process(phase, target);
    if (target >= 0 && target < static_cast<int>(PageSize))
        return {Action::Selected, static_cast<int>(m_page * PageSize) + target};
    if (target == 100)
        return {Action::Cancel, -1};
    if (target == 101)
        --m_page;
    if (target == 102)
        ++m_page;
    if (target == 103)
        return {Action::Rescan, -1};
    if (target == 104)
        return {Action::Manual, -1};
    return {};
}
