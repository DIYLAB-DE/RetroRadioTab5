#include "SdCardWatch.h"

void SdCardWatch::mounted(bool ready, uint32_t now)
{
    m_ready = m_mounted = ready;
    m_probeNow = false;
    m_probeAt = now;
}

bool SdCardWatch::probed(bool answered)
{
    if (!m_ready || answered)
        return false;
    m_ready = false;
    if (++m_generation == 0)
        ++m_generation;
    return true;
}

void SdCardWatch::unmounted()
{
    m_mounted = m_ready = false;
}

SdCardWatch::Step SdCardWatch::poll(uint32_t now, bool audioBusy)
{
    if (m_mounted && !m_ready)
        return audioBusy ? Step::None : Step::Unmount;
    if (!m_mounted)
    {
        if (!m_retry)
            return Step::None;
        m_retry = false;
        return Step::Mount;
    }
    if (!m_probeNow && now - m_probeAt < ProbeMs)
        return Step::None;
    m_probeNow = false;
    m_probeAt = now;
    return Step::Probe;
}
