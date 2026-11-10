#include "Tab5Render.h"
// Bedienrollen und Wickelmodell aus DIYLAB-DE/RetroRadio, ea4229a34fb39faaff23619092cf09cafb8c51cf, GPL-3.0 gemäß Repository-LICENSE.
// Native 1280x720-Zeichenfläche (SdPlayerTab5Layout) mit asynchronen Auftragsbestätigungen und zentraler Dienstanbindung für Tab5.
#include "SDPlayer.h"
#include "VisualizerView.h"
#include "StringUtils.h"
#include "fonts/ComfortaaBold15.h"
#include "fonts/ComfortaaBold21.h"
#include "fonts/ComfortaaBold24.h"
#include "fonts/ComfortaaBold27.h"
#include "Thema.h"
#include "IconShapes.h"
#include "fonts/LeagueGothicCond51.h"
#include <esp_heap_caps.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <strings.h>

namespace
{
    namespace SL = SdPlayerTab5Layout;
    namespace RL = RadioTab5Layout;
    constexpr int ListX = SL::Playlist::X, ListY = SL::Playlist::Y, ListWidth = SL::Playlist::W, RowHeight = SL::Playlist::ROW_H, ListHeight = SL::Playlist::H;
    constexpr uint32_t HoldDelay = 600, SeekRepeat = 400, SeekStep = 10;
    constexpr uint32_t RepeatHold = 800; ///< Haltezeit von Play in ms, ab der die ganze Playlist wiederholt wird, Original 800
    constexpr int KnobX = RL::Knob::X + RL::Knob::MID, KnobY = RL::Knob::Y + RL::Knob::MID; ///< Drehknopfmitte, Original (53,426)

    /** @brief Länge des Ordneranteils von path einschließlich des letzten Schrägstrichs, 0 ohne Schrägstrich. */
    size_t folderLength(const char *path)
    {
        const char *slash = strrchr(path, '/');
        return slash ? static_cast<size_t>(slash - path) + 1 : 0;
    }
}

bool SdPlayer::begin(lgfx::LGFX_Device &display, VisualizerView &visualizer, SdPlayerCallbacks callbacks)
{
    if (m_ready)
        return m_display == &display && m_visualizer == &visualizer;
    m_display = &display;
    m_visualizer = &visualizer;
    m_callbacks = callbacks;
    if (!m_tracks)
        m_tracks = static_cast<Track *>(heap_caps_calloc(MaxTracks, sizeof(Track), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!m_tracks || !prepareGraphics())
        return false;
    if (!m_explorer.begin(display, {this, fileRequest, filesSelected}))
        return false;
    m_ready = true;
    return true;
}

SdPlayer::~SdPlayer()
{
    heap_caps_free(m_tracks);
    for (auto *pixels : m_bitmaps)
        heap_caps_free(pixels);
    free(m_emptyFolderIcon);
    free(m_muteIcon);
}

bool SdPlayer::show(LangIndex language)
{
    if (!m_ready || !m_visualizer->setBounds(RL::Visual::X, RL::Visual::Y, RL::Visual::W, RL::Visual::H))
        return false;
    m_visible = true;
    m_language = language;
    m_explorer.setLanguage(language);
    invalidate();
    return true;
}

void SdPlayer::hide()
{
    m_visible = false;
    m_explorer.cancel();
    m_messageBox.close();
    m_sleep.cancel();
    m_scrollVelocity = 0;
    m_confirmClear = false;
    m_messageBox.close();
    m_sleep.cancel();
    m_visualizer->cancelTouch();
    m_hit = Hit::None;
    m_playbackId = m_confirmedId = m_pending = 0;
    m_stopping = false;
    m_repeat = RepeatMode::Off;
    resetTrack();
}

void SdPlayer::invalidate()
{
    m_fullDirty = true;
    m_dirty = All;
    if (m_visualizer)
        m_visualizer->invalidate();
    m_explorer.invalidate();
}

void SdPlayer::setLanguage(LangIndex language)
{
    if (language != m_language)
    {
        m_language = language;
        m_explorer.setLanguage(language);
        invalidate();
    }
}

bool SdPlayer::fileRequest(void *context, const SdFileRequest &request)
{
    auto &self = *static_cast<SdPlayer *>(context);
    if (request.kind != SdFileRequest::Kind::Cancel && (self.m_services.media != SdServiceStatus::Media::Ready || request.mediaGeneration != self.m_services.mediaGeneration))
        return false;
    if (request.kind == SdFileRequest::Kind::List && !self.m_services.list)
        return false;
    if (request.kind == SdFileRequest::Kind::Collect && !self.m_services.collect)
        return false;
    return self.m_callbacks.files && self.m_callbacks.files(self.m_callbacks.context, request);
}

bool SdPlayer::filesSelected(void *context, const SdFileEntry *entries, size_t count)
{
    return static_cast<SdPlayer *>(context)->addFiles(entries, count);
}

void SdPlayer::setServices(const SdServiceStatus &status)
{
    if (m_haveServices && static_cast<int32_t>(status.revision - m_serviceRevision) <= 0)
        return;
    const bool changedMedium = m_haveServices && status.mediaGeneration != m_services.mediaGeneration;
    m_haveServices = true;
    m_serviceRevision = status.revision;
    if (changedMedium || status.media != SdServiceStatus::Media::Ready)
    {
        m_explorer.cancel();
        m_messageBox.close();
        m_sleep.cancel();
        m_scrollVelocity = 0;
        m_confirmClear = false;
        m_hit = Hit::None;
        const bool stopping = m_playbackId && request(SdPlayerAction::Type::Stop);
        if (!stopping)
            m_pending = 0;
        m_stopping = stopping;
        m_playbackId = m_confirmedId = 0;
        m_repeat = RepeatMode::Off;
        resetTrack();
        if (changedMedium)
        {
            m_count = 0;
            m_selected = 0;
            m_scroll = 0;
        }
        invalidate();
    }
    m_services = status;
    m_dirty |= Header | Metadata | List | Controls;
}

bool SdPlayer::request(SdPlayerAction::Type type, uint32_t position, const Track *track, int value, int control)
{
    SdPlayerAction action;
    action.type = type;
    action.requestId = ++m_sequence;
    if (!action.requestId)
        action.requestId = ++m_sequence;
    action.mediaGeneration = m_services.mediaGeneration;
    action.playbackId = m_playbackId;
    action.trackId = m_count && m_selected < m_count ? m_tracks[m_selected].id : 0;
    action.positionSeconds = position;
    action.value = value;
    action.control = control;
    if (track)
    {
        action.trackId = track->id;
        action.playbackId = action.requestId;
        stringutils::safeCopy(action.path, track->file.path);
    }
    if (!m_callbacks.action || !m_callbacks.action(m_callbacks.context, action))
    {
        m_localError = true;
        m_message = m_callbacks.action ? SdPlayerKey::QueueError : SdPlayerKey::NoService;
        m_dirty |= Header | Controls;
        return false;
    }
    const bool clearedError = m_localError;
    m_localError = false;
    if (type <= SdPlayerAction::Type::Mute)
    {
        m_pending = action.requestId;
        m_pendingType = type;
    }
    // Lautstärke, Balance und Klang zeichnen Knopf bzw. Schieberegler erst mit dem bestätigten Wert aus setPlayback(). Beim Drehen kommt bis dahin je Durchlauf ein neuer Auftrag, und Kopfzeile samt Tastenleiste kosteten jedes Mal rund 55 ms.
    if (clearedError || (type != SdPlayerAction::Type::Volume && type != SdPlayerAction::Type::Balance && type != SdPlayerAction::Type::Tone))
        m_dirty |= Header | Controls;
    return true;
}

void SdPlayer::resetTrack()
{
    m_playback = {};
    m_coverReady = false;
    m_coverRevision = 0;
    m_coverMessage = SdPlayerKey::NoCover;
    m_wrapAngleL = m_wrapAngleR = 0;
    m_dirty |= Tape | Cover | Metadata | Progress | List;
}

bool SdPlayer::play(size_t index)
{
    if (index >= m_count || m_services.media != SdServiceStatus::Media::Ready || !m_services.play)
        return false;
    if (!request(SdPlayerAction::Type::PlayFile, 0, &m_tracks[index]))
        return false;
    // Ein Ordnerbild gilt für alle Titel seines Ordners und bleibt beim Wechsel darin stehen, bis der Dienst das Cover des neuen Titels liefert.
    const size_t folder = folderLength(m_tracks[index].file.path);
    const bool keepCover = m_coverReady && m_coverFolder[0] && m_coverFolderMedia == m_services.mediaGeneration && folder == strlen(m_coverFolder) && !strncmp(m_coverFolder, m_tracks[index].file.path, folder);
    m_selected = index;
    m_playbackId = m_pending;
    m_confirmedId = m_terminalId = 0;
    m_stopping = false;
    resetTrack();
    m_coverReady = keepCover;
    m_playback.state = SdPlaybackStatus::State::Starting;
    m_coverMessage = m_services.cover ? SdPlayerKey::LoadingCover : SdPlayerKey::NoCover;
    revealSelected();
    return true;
}

bool SdPlayer::restartPlaylist()
{
    for (size_t i = 0; i < m_count; ++i)
        m_tracks[i].played = false;
    return play(0);
}

bool SdPlayer::stop()
{
    m_repeat = RepeatMode::Off;
    if (!request(SdPlayerAction::Type::Stop))
        return false;
    m_playbackId = m_confirmedId = 0;
    m_stopping = true;
    resetTrack();
    return true;
}

void SdPlayer::clear()
{
    if (!stop())
        return;
    m_count = m_selected = 0;
    m_scroll = 0;
    m_scrollVelocity = 0;
    m_confirmClear = false;
    m_cassetteDesignIdx = (m_cassetteDesignIdx + 1 + (millis() % 7)) % 8;
    invalidate();
}

bool SdPlayer::addFiles(const SdFileEntry *entries, size_t count)
{
    if (!m_ready || m_services.media != SdServiceStatus::Media::Ready || (count && !entries) || count > MaxTracks)
        return false;
    size_t added = 0;
    for (size_t i = 0; i < count; ++i)
    {
        if (entries[i].directory || !FileExplorer::supportedFile(entries[i].path) || !FileExplorer::validText(entries[i].name, sizeof(entries[i].name)))
        {
            m_localError = true;
            m_message = SdPlayerKey::InvalidPath;
            m_dirty |= Header;
            return false;
        }
        bool duplicate = false;
        for (size_t j = 0; j < m_count; ++j)
            duplicate |= strcmp(m_tracks[j].file.path, entries[i].path) == 0;
        for (size_t j = 0; j < i; ++j)
            duplicate |= strcmp(entries[j].path, entries[i].path) == 0;
        if (!duplicate)
            ++added;
    }
    if (added > MaxTracks - m_count)
    {
        m_localError = true;
        m_message = SdPlayerKey::Limit;
        m_dirty |= Header;
        return false;
    }
    for (size_t i = 0; i < count; ++i)
    {
        bool duplicate = false;
        for (size_t j = 0; j < m_count; ++j)
            duplicate |= strcmp(m_tracks[j].file.path, entries[i].path) == 0;
        if (duplicate)
            continue;
        uint32_t id;
        bool used;
        do
        {
            id = ++m_nextTrackId;
            used = id == 0;
            for (size_t j = 0; j < m_count; ++j)
                used |= m_tracks[j].id == id;
        } while (used);
        m_tracks[m_count++] = {id, entries[i], false, false};
    }
    m_localError = false;
    m_message = added ? SdPlayerKey::Added : SdPlayerKey::Duplicate;
    m_dirty |= List | Header | Metadata | Controls;
    return true;
}

bool SdPlayer::acceptFiles(const SdFileResult &result)
{
    const bool wasOpen = m_explorer.active();
    const bool accepted = m_explorer.acceptResult(result);
    if (wasOpen && !m_explorer.active())
    {
        m_hit = Hit::None;
        invalidate();
    }
    return accepted;
}

void SdPlayer::setPlayback(const SdPlaybackStatus &status)
{
    if (m_haveAudio && static_cast<int32_t>(status.revision - m_audioRevision) <= 0)
        return;
    m_haveAudio = true;
    m_audioRevision = status.revision;
    // Knopf (rund 9 ms) und Schieberegler (rund 14 ms) nur einzeln neu zeichnen, die ganze Tastenleiste dazu verzögerte jede Lautstärkestufe
    if (status.volume != m_state.volume || status.muted != m_state.muted)
        m_dirty |= Knob;
    if (status.balance != m_state.balance || memcmp(status.tone, m_state.tone, 3))
        m_dirty |= Tracks;
    m_state = status;
    if (m_muted != status.muted)
    {
        m_muted = status.muted;
        m_dirty |= Knob;
    }
    if (m_pending && status.acknowledgedRequest == m_pending)
    {
        const auto pendingType = m_pendingType;
        m_pending = 0;
        m_localError = status.commandFailed;
        if (status.commandFailed)
            m_message = SdPlayerKey::CommandError;
        if (pendingType == SdPlayerAction::Type::Stop || pendingType == SdPlayerAction::Type::Home)
        {
            m_stopping = false;
            m_playback.state = status.commandFailed ? SdPlaybackStatus::State::Error : SdPlaybackStatus::State::Stopped;
        }
        m_dirty |= Header | Controls;
    }
    if (m_playbackId && m_confirmedId == m_playbackId && status.source != SdPlaybackStatus::Source::Sd)
    {
        m_playbackId = m_confirmedId = m_pending = 0;
        m_stopping = false;
        m_repeat = RepeatMode::Off;
        resetTrack();
        m_playback.state = SdPlaybackStatus::State::Stopped;
        m_dirty |= Header | Controls;
    }
    if (!m_playbackId || !m_count || status.source != SdPlaybackStatus::Source::Sd || status.mediaGeneration != m_services.mediaGeneration || status.playbackId != m_playbackId || status.trackId != m_tracks[m_selected].id)
        return;
    if (m_terminalId == m_playbackId && m_playback.state == SdPlaybackStatus::State::Error && status.state != SdPlaybackStatus::State::Stopped)
        return;
    if (!status.fileConfirmed && m_confirmedId != m_playbackId && (status.state == SdPlaybackStatus::State::Playing || status.state == SdPlaybackStatus::State::Paused || status.state == SdPlaybackStatus::State::Ended))
        return;
    if (status.positionSeconds != m_playback.positionSeconds || status.durationSeconds != m_playback.durationSeconds || status.durationKnown != m_playback.durationKnown)
        m_dirty |= Progress;
    // Nur sichtbare Änderungen zeichnen: audio-lite mittelt die Bitrate ohne Nominalwert alle 50 ms neu, die Kassette zeichnet Interpret und Wickel selbst nach.
    if (status.state != m_playback.state)
        m_dirty |= Header | Controls | List;
    const char *path = m_tracks[m_selected].file.path;
    if (displayKbps(path, status.bitRate) != displayKbps(path, m_playback.bitRate) || strncmp(status.album, m_playback.album, sizeof(status.album)) || strncmp(status.title, m_playback.title, sizeof(status.title)) || strncmp(status.artist, m_playback.artist, sizeof(status.artist)))
        m_dirty |= Metadata;
    m_playback = status;
    if (!FileExplorer::validText(m_playback.title, sizeof(m_playback.title)))
        m_playback.title[0] = 0;
    if (!FileExplorer::validText(m_playback.artist, sizeof(m_playback.artist)))
        m_playback.artist[0] = 0;
    if (!FileExplorer::validText(m_playback.album, sizeof(m_playback.album)))
        m_playback.album[0] = 0;
    if (m_playback.durationKnown && m_playback.positionSeconds > m_playback.durationSeconds)
        m_playback.positionSeconds = m_playback.durationSeconds;
    if (status.fileConfirmed)
        m_confirmedId = m_playbackId;
    if (status.state == SdPlaybackStatus::State::Error)
    {
        m_terminalId = m_playbackId;
        m_tracks[m_selected].failed = true;
        m_coverReady = false;
        m_dirty |= Cover;
        m_dirty |= List;
    }
    if (status.state == SdPlaybackStatus::State::Ended && m_confirmedId == m_playbackId && m_terminalId != m_playbackId)
    {
        m_terminalId = m_playbackId;
        m_tracks[m_selected].played = true;
        m_tracks[m_selected].failed = false;
        m_dirty |= List;
        if (!m_visible)
            return;
        if (m_repeat == RepeatMode::Track)
            play(m_selected);
        else if (m_selected + 1 < m_count)
            play(m_selected + 1);
        else if (m_repeat == RepeatMode::Playlist)
            restartPlaylist();
    }
}

bool SdPlayer::setCover(const SdCoverFrame &cover)
{
    if (!m_ready || !m_playbackId || !m_count || cover.mediaGeneration != m_services.mediaGeneration || cover.playbackId != m_playbackId || cover.trackId != m_tracks[m_selected].id || static_cast<int32_t>(cover.revision - m_coverRevision) <= 0)
        return false;
    if (cover.state == SdCoverFrame::State::Ready)
    {
        if (!cover.pixels || cover.width != CoverWidth || cover.height != CoverHeight || cover.pixelCount != static_cast<size_t>(CoverWidth) * CoverHeight)
            return false;
        m_cover.pushImage(0, 0, CoverWidth, CoverHeight, cover.pixels);
        m_coverReady = true;
        const size_t folder = cover.folder ? folderLength(m_tracks[m_selected].file.path) : 0;
        if (folder && folder < sizeof(m_coverFolder))
        {
            memcpy(m_coverFolder, m_tracks[m_selected].file.path, folder);
            m_coverFolder[folder] = 0;
            m_coverFolderMedia = cover.mediaGeneration;
        }
        else
            m_coverFolder[0] = 0;
    }
    else
    {
        m_coverReady = false;
        m_coverFolder[0] = 0;
        m_coverMessage = cover.state == SdCoverFrame::State::Error ? SdPlayerKey::CoverError : SdPlayerKey::NoCover;
    }
    m_coverRevision = cover.revision;
    m_dirty |= Cover;
    return true;
}

void SdPlayer::revealSelected()
{
    const int top = static_cast<int>(m_selected) * RowHeight;
    if (top < m_scroll)
        m_scroll = top;
    else if (top + RowHeight > m_scroll + ListHeight)
        m_scroll = top + RowHeight - ListHeight;
    m_scrollVelocity = 0;
    m_inertiaRemainder = 0;
    m_dirty |= List;
}

bool SdPlayer::canSeek() const
{
    return m_services.seek && m_services.media == SdServiceStatus::Media::Ready && m_playbackId && !m_pending && m_playback.seekable && m_playback.durationKnown && m_playback.durationSeconds > 1 && m_playback.state == SdPlaybackStatus::State::Playing;
}

SdPlayer::Rect SdPlayer::bounds(Hit hit) const
{
    switch (hit)
    {
    case Hit::Previous:
    case Hit::Play:
    case Hit::Pause:
    case Hit::Stop:
    case Hit::Next:
        return {SL::Transport::X0 + SL::Transport::STEP * (static_cast<int>(hit) - static_cast<int>(Hit::Previous)), SL::Transport::Y, SL::Transport::SIZE, SL::Transport::SIZE};
    case Hit::Browse:
        return {SL::Buttons::BROWSE_X, SL::Buttons::Y, SL::Buttons::W, SL::Buttons::H};
    case Hit::Clear:
        return {SL::Buttons::CLEAR_X, SL::Buttons::Y, SL::Buttons::W, SL::Buttons::H};
    case Hit::Reset:
        return {SL::Buttons::RESET_X, SL::Buttons::Y, SL::Buttons::W, SL::Buttons::H};
    case Hit::Power:
        return {SL::Buttons::POWER_X, SL::Buttons::Y, SL::Buttons::W, SL::Buttons::H};
    case Hit::Track:
        return {ListX, ListY, ListWidth, ListHeight};
    case Hit::Tape:
        return {SL::Cassette::X, SL::Cassette::Y, SL::Cassette::W, SL::Cassette::H};
    case Hit::Progress:
        return {SL::Progress::X, SL::Progress::TOUCH_Y, SL::Progress::W, SL::Progress::TOUCH_H};
    case Hit::Visualizer:
        return {RL::Visual::X, RL::Visual::Y, RL::Visual::W, SL::Visualizer::TOUCH_H};
    default:
        return {0, 0, 0, 0};
    }
}

SdPlayer::Hit SdPlayer::hitAt(int x, int y, int &row) const
{
    row = -1;
    if (x < 0 || x >= SL::WIDTH || y < 0 || y >= SL::HEIGHT)
        return Hit::None;
    const Hit hits[] = {Hit::Previous, Hit::Play, Hit::Pause, Hit::Stop, Hit::Next, Hit::Browse, Hit::Clear, Hit::Reset, Hit::Power, Hit::Tape, Hit::Progress, Hit::Track, Hit::Visualizer};
    for (auto hit : hits)
        if (bounds(hit).contains(x, y))
        {
            if (hit == Hit::Track)
            {
                row = (y - ListY + m_scroll) / RowHeight;
                if (y - ListY + m_scroll < 0 || row >= static_cast<int>(m_count))
                    row = -1;
            }
            if (hit == Hit::Progress && !canSeek())
                return Hit::None;
            return hit;
        }
    if (x < RL::Knob::X + RL::Knob::SIZE + RL::Knob::TOUCH_MARGIN && y > RL::Knob::Y - RL::Knob::TOUCH_MARGIN)
    {
        const int dx = x - KnobX, dy = y - KnobY;
        if (dx * dx + dy * dy <= RL::Knob::TOUCH_MUTE_R * RL::Knob::TOUCH_MUTE_R)
            return Hit::Mute;
        if (dx * dx + dy * dy >= RL::Knob::TOUCH_RING_R * RL::Knob::TOUCH_RING_R)
            return Hit::Knob;
    }
    const int pixel = y - RL::Tracks::Y;
    if (pixel > RL::Tracks::PIXEL_MAX - 1 && pixel < RL::Tracks::PIXEL_MIN + 1)
        for (int i = 0; i < 4; ++i)
        {
            const int left = RL::Tracks::X + RL::Tracks::SLIDER_X0 + RL::Tracks::SLIDER_STEP * i;
            if (x >= left && x <= left + RL::Tracks::TOUCH_W)
                return static_cast<Hit>(static_cast<int>(Hit::Balance) + i);
        }
    return Hit::None;
}

void SdPlayer::activate(Hit hit, int row)
{
    switch (hit)
    {
    case Hit::Home:
        request(SdPlayerAction::Type::Home);
        break;
    case Hit::Stop:
        stop();
        break;
    case Hit::Mute:
        request(SdPlayerAction::Type::Mute);
        break;
    case Hit::Browse:
        if (m_services.media == SdServiceStatus::Media::Ready && m_services.list)
            m_explorer.open(m_services.mediaGeneration, m_language, m_services.collect);
        else
        {
            request(SdPlayerAction::Type::RetryMedia);
            m_localError = true;
            m_message = stateText();
            m_dirty |= Metadata;
        }
        break;
    case Hit::Clear:
        if (m_count)
            openDialog(hit);
        break;
    case Hit::Reset:
    case Hit::Power:
        openDialog(hit);
        break;
    case Hit::Tape:
        m_cassetteDesignIdx = (m_cassetteDesignIdx + 1) % 8;
        m_dirty |= Tape;
        break;
    case Hit::Track:
        if (row >= 0)
            play(row);
        break;
    case Hit::Play:
    {
        if (!m_count)
            break;
        // Gehalten wiederholt Play die ganze Playlist. Kurz schaltet es bei laufendem oder startendem Titel die Titelwiederholung um, ohne ihn neu zu starten
        const bool hold = static_cast<int32_t>(m_now - m_pressTime) >= static_cast<int32_t>(RepeatHold);
        if (m_playback.state == SdPlaybackStatus::State::Playing || m_playback.state == SdPlaybackStatus::State::Starting)
        {
            if (hold)
                m_repeat = RepeatMode::Playlist;
            else
                m_repeat = m_repeat == RepeatMode::Off ? RepeatMode::Track : RepeatMode::Off;
            m_dirty |= Controls;
            break;
        }
        if (m_pending)
            break;
        if (m_playback.state == SdPlaybackStatus::State::Paused)
        {
            // Fortsetzen behält den Modus, nur gehalten wechselt er auf die Playlist
            if (request(SdPlayerAction::Type::Resume) && hold)
                m_repeat = RepeatMode::Playlist;
        }
        else if (m_playback.state == SdPlaybackStatus::State::Ended && m_selected + 1 == m_count ? restartPlaylist() : play(m_selected))
            m_repeat = hold ? RepeatMode::Playlist : RepeatMode::Off;
        break;
    }
    case Hit::Pause:
        if (!m_pending && m_services.pause && (m_playback.state == SdPlaybackStatus::State::Playing || m_playback.state == SdPlaybackStatus::State::Paused))
            request(m_playback.state == SdPlaybackStatus::State::Paused ? SdPlayerAction::Type::Resume : SdPlayerAction::Type::Pause);
        break;
    case Hit::Previous:
    case Hit::Next:
    {
        if (!m_count)
            break;
        const bool active = m_playback.state == SdPlaybackStatus::State::Playing || m_playback.state == SdPlaybackStatus::State::Paused || m_playback.state == SdPlaybackStatus::State::Starting;
        size_t target = m_selected;
        if (hit == Hit::Next)
            if (target + 1 < m_count)
                ++target;
            else
                break;
        else if (!(active && m_playback.positionSeconds > 3) && target)
            --target;
        if (active)
            play(target);
        else
        {
            m_selected = target;
            resetTrack();
            revealSelected();
            m_dirty |= Controls;
        }
        break;
    }
    default:
        break;
    }
}

bool SdPlayer::touch(const SdTouch &physical)
{
    if (!m_visible || !m_ready)
        return false;
    const SdTouch &e = physical;
    m_now = e.now;
    if (e.phase == SdTouchPhase::Cancel)
    {
        m_hit = Hit::None;
        m_cancelled = true;
        m_pressed = -1;
        m_scrollVelocity = 0;
        m_dragged = false;
        m_visualizer->cancelTouch();
        m_messageBox.cancelTouch();
        m_scene.setTouch(-1, -1, true);
        m_sleep.update();
        m_scene.setTouch(-1, -1, false);
        if (m_explorer.active())
            m_explorer.touch(e);
        m_dirty |= Controls | Progress;
        return true;
    }
    if (m_explorer.active())
    {
        m_explorer.touch(e);
        if (!m_explorer.active())
            invalidate();
        return true;
    }
    const bool down = e.phase == SdTouchPhase::Press || e.phase == SdTouchPhase::Move;
    if (m_messageBox.active())
    {
        m_dialogTime = e.now;
        int result = m_messageBox.touch(static_cast<SettingsUi::TouchPhase>(e.phase), e.x, e.y);
        if (result >= 0)
        {
            m_messageBox.close();
            if (result == 0)
            {
                if (m_resetDialog)
                    request(SdPlayerAction::Type::Reset);
                else
                    clear();
            }
            invalidate();
        }
        else
        {
            m_messageBox.draw(m_scene);
            presentRect(RL::Dialog::MESSAGE_X, RL::Dialog::MESSAGE_Y, RL::Dialog::MESSAGE_W, RL::Dialog::MESSAGE_H);
        }
        return true;
    }
    if (m_sleep.isActive())
    {
        m_scene.setTouch(e.x, e.y, down);
        m_sleep.update();
        if (!m_sleep.isActive())
            invalidate();
        else
            presentRect(RL::Dialog::SLEEP_X, RL::Dialog::SLEEP_Y, RL::Dialog::SLEEP_W, RL::Dialog::SLEEP_H);
        return true;
    }
    // Der Visualizer erhält wie beim Radio Koordinaten relativ zu seiner Fläche.
    const auto visualizerTouch = [&](bool contact)
    {
        RadioPreferences before, after;
        m_visualizer->preferences(before);
        m_visualizer->handleTouch(e.x - RL::Visual::X, e.y - RL::Visual::Y, contact);
        m_visualizer->preferences(after);
        if (before.mode != after.mode || memcmp(before.styles, after.styles, 8))
            request(SdPlayerAction::Type::VisualizerChanged);
    };
    int row = -1;
    const auto hit = hitAt(e.x, e.y, row);
    // Nur den berührten Teil neu zeichnen: Knopf, Schieberegler oder die Tastenleiste samt beiden (rund 37 ms). Playlist, Kassette, Fortschritt und Visualizer zeichnen sich selbst.
    const auto controlDirty = [](Hit h) -> uint16_t
    {
        if (h == Hit::Knob)
            return Knob;
        if (h >= Hit::Balance && h <= Hit::High)
            return Tracks;
        return h != Hit::None && h != Hit::Track && h != Hit::Tape && h != Hit::Progress && h != Hit::Visualizer ? Controls : 0;
    };
    if (e.phase == SdTouchPhase::Press)
    {
        m_hit = hit;
        m_pressRow = row;
        m_pressY = e.y;
        m_pressScroll = m_scroll;
        m_pressTime = e.now;
        m_lastSeek = e.now;
        m_cancelled = m_dragged = m_held = false;
        if (hit == Hit::Track)
        {
            m_cancelled = fabsf(m_scrollVelocity) > 0.5f || m_scroll < 0 || m_scroll > std::max(0, static_cast<int>(m_count) * RowHeight - ListHeight);
            m_scrollVelocity = m_inertiaRemainder = 0;
            m_pressX = e.x;
            m_lastTouchY = e.y;
            m_lastTouchTime = e.now;
        }
        if (hit == Hit::Mute)
            activate(hit, row);
        if (hit == Hit::Knob)
        {
            m_pressed = 7;
            if (static_cast<int>(m_totalDegrees / 36) != m_state.volume)
                m_totalDegrees = m_state.volume * 36.0f;
            m_lastAngle = atan2f(e.y - KnobY, e.x - KnobX) * 57.29578f;
        }
        m_dirty |= controlDirty(hit);
    }
    if (down)
    {
        if (m_hit == Hit::Visualizer)
            visualizerTouch(true);
        if (m_hit == Hit::Knob && !m_state.muted)
        {
            float angle = atan2f(e.y - KnobY, e.x - KnobX) * 57.29578f, delta = fmodf(angle - m_lastAngle + 540, 360) - 180;
            if (fabsf(delta) >= 2)
            {
                m_lastAngle = angle;
                m_totalDegrees = std::clamp(m_totalDegrees + delta, 0.0f, 3600.0f);
                // Die Markierung folgt dem Finger stufenlos, die Lautstärke ändert sich weiter alle 36 Grad
                m_dirty |= Knob;
                const int v = m_totalDegrees / 36;
                if (v != m_state.volume)
                    request(SdPlayerAction::Type::Volume, 0, nullptr, v);
            }
        }
        const int p = e.y - RL::Tracks::Y;
        if (m_hit >= Hit::Balance && m_hit <= Hit::High && p > RL::Tracks::PIXEL_MAX - 1 && p < RL::Tracks::PIXEL_MIN + 1)
        {
            const int i = static_cast<int>(m_hit) - static_cast<int>(Hit::Balance);
            request(i ? SdPlayerAction::Type::Tone : SdPlayerAction::Type::Balance, 0, nullptr, i ? (p > RL::Tracks::PIXEL_ZERO ? map(p, RL::Tracks::PIXEL_MIN, RL::Tracks::PIXEL_ZERO, -12, 0) : map(p, RL::Tracks::PIXEL_ZERO, RL::Tracks::PIXEL_MAX, 0, 12)) : map(p, RL::Tracks::PIXEL_MIN, RL::Tracks::PIXEL_MAX, -16, 16), i - 1);
        }
        if (m_hit == Hit::Progress)
        {
            m_seekTarget = static_cast<uint64_t>(std::clamp<int>(e.x - SL::Progress::X, 0, SL::Progress::W)) * m_playback.durationSeconds / SL::Progress::W;
            m_dirty |= Progress;
        }
        else if (e.phase == SdTouchPhase::Move && m_hit == Hit::Track)
        {
            constexpr int over = SL::Playlist::OVERSCROLL, drag = SL::Playlist::DRAG_THRESHOLD;
            // Der GT911 liefert zwischen zwei Abtastungen dieselbe Position, die Schleife läuft schneller: Geschwindigkeit nur bei echter Änderung und über die Zeit seit der letzten Änderung, sonst teilt ein Sprung durch einen zu kurzen Schleifenabstand.
            const bool movedY = e.y != m_lastTouchY;
            m_dragged |= abs(e.y - m_pressY) > drag;
            if (m_dragged)
            {
                m_scroll = m_pressScroll - static_cast<int>((e.y - m_pressY) * 1.8f);
                const uint32_t dt = e.now - m_lastTouchTime;
                if (movedY && dt && dt < 100)
                    m_scrollVelocity = ((m_lastTouchY - e.y) * 1000.0f / dt * 0.7f + m_scrollVelocity * 0.3f) * 1.5f;
                const int maximum = std::max(0, static_cast<int>(m_count) * RowHeight - ListHeight);
                if (m_scroll < -over)
                    m_scroll = -over + (m_scroll + over) * 0.3f;
                else if (m_scroll > maximum + over)
                    m_scroll = maximum + over + (m_scroll - maximum - over) * 0.3f;
                m_dirty |= Scroll;
            }
            if (e.x < ListX || e.x >= ListX + ListWidth || abs(e.x - m_pressX) > drag)
                m_cancelled = true;
            if (movedY)
            {
                m_lastTouchY = e.y;
                m_lastTouchTime = e.now;
            }
        }
        else if (e.phase == SdTouchPhase::Move && m_hit != Hit::Knob && m_hit != Hit::Visualizer && hit != m_hit)
            m_cancelled = true;
    }
    if (e.phase == SdTouchPhase::Release)
    {
        const auto pressed = m_hit;
        m_hit = Hit::None;
        m_pressed = -1;
        m_dirty |= controlDirty(pressed);
        m_scrollVelocity = std::clamp(m_scrollVelocity, -3000.0f, 3000.0f);
        if (pressed == Hit::Track)
            m_dirty |= Scroll;
        if (pressed == Hit::Track && e.now - m_lastTouchTime >= 100)
            m_scrollVelocity = 0; // Finger ruhte vor dem Loslassen: kein Wurf
        if (pressed == Hit::Visualizer)
            visualizerTouch(false);
        else if (pressed == Hit::Progress)
        {
            if (canSeek())
                request(SdPlayerAction::Type::Seek, std::min(m_seekTarget, m_playback.durationSeconds - 1));
            m_dirty |= Progress;
        }
        else if (!m_cancelled && !m_dragged && !m_held && pressed == hit && pressed != Hit::Mute && (pressed != Hit::Track || e.now - m_pressTime <= 300))
            activate(pressed, row);
    }
    return true;
}

void SdPlayer::openDialog(Hit hit)
{
    m_scene.setTouch(0, 0, false);
    m_dialogTime = m_now;
    syncPlaylistToScene();
    if (hit == Hit::Power)
    {
        // Der Dialog behält nur die Zeiger, das Haus-Etikett für sofortiges Verlassen trägt die Farbe Gem.HausSymbol als Hexzahl und lebt deshalb statisch.
        static char home[24];
        snprintf(home, sizeof(home), "^HOME:48:%04X^", farbe(Rolle::GemHausSymbol));
        static const char *labels[] = {"5", "15", "25", "45", "60", home};
        static const int values[] = {5, 15, 25, 45, 60, 0};
        m_sleep.setBounds(RL::Dialog::SLEEP_X, RL::Dialog::SLEEP_Y, RL::Dialog::SLEEP_W, RL::Dialog::SLEEP_H);
        m_sleep.setTitle(I18n::sdPlayer(SdPlayerKey::SleepTitle, m_language));
        m_sleep.setMessage(I18n::sdPlayer(SdPlayerKey::SleepMessage, m_language));
        m_sleep.setCancelLabel(m_language == LangIndex::DE ? "abbruch" : "cancel");
        m_sleep.setStopLabel(m_language == LangIndex::DE ? "stopp" : "stop");
        m_sleep.setOptions(labels, values, 6);
        m_sleep.setTimeout(10000);
        m_sleep.setCallback([this](int minutes)
        {
            if (minutes == 0)
                request(SdPlayerAction::Type::Home);
            else if (minutes > 0)
                m_sleep.startSleepCountdown(minutes, [this]()
                {
                    request(SdPlayerAction::Type::Sleep);
                });
            invalidate();
        });
        m_sleep.start();
        presentRect(RL::Dialog::SLEEP_X, RL::Dialog::SLEEP_Y, RL::Dialog::SLEEP_W, RL::Dialog::SLEEP_H);
    }
    else
    {
        m_resetDialog = hit == Hit::Reset;
        const char *buttons[] = {m_language == LangIndex::DE ? "weiter" : "continue", m_language == LangIndex::DE ? "abbruch" : "cancel"};
        m_messageBox.setBounds(RL::Dialog::MESSAGE_X, RL::Dialog::MESSAGE_Y, RL::Dialog::MESSAGE_W, RL::Dialog::MESSAGE_H);
        m_messageBox.open(m_resetDialog ? I18n::sdPlayer(SdPlayerKey::ResetTitle, m_language) : I18n::sdPlayer(SdPlayerKey::ClearTitle, m_language), m_resetDialog ? I18n::sdPlayer(SdPlayerKey::ResetMessage, m_language) : I18n::sdPlayer(SdPlayerKey::ClearMessage, m_language), "", buttons, 2);
        m_messageBox.draw(m_scene);
        presentRect(RL::Dialog::MESSAGE_X, RL::Dialog::MESSAGE_Y, RL::Dialog::MESSAGE_W, RL::Dialog::MESSAGE_H);
    }
}

SdPlayerKey SdPlayer::stateText() const
{
    if (m_services.media == SdServiceStatus::Media::Unavailable)
        return SdPlayerKey::Unavailable;
    if (m_services.media == SdServiceStatus::Media::Mounting)
        return SdPlayerKey::Mounting;
    if (m_services.media == SdServiceStatus::Media::Error)
        return SdPlayerKey::MountError;
    if (m_localError)
        return m_message;
    if (m_playback.state == SdPlaybackStatus::State::Error)
        return SdPlayerKey::ReadError;
    if (m_playback.state == SdPlaybackStatus::State::Starting)
        return SdPlayerKey::Starting;
    return SdPlayerKey::Ready;
}

void SdPlayer::drawControls()
{
    const Hit hits[] = {Hit::Previous, Hit::Play, Hit::Pause, Hit::Stop, Hit::Next, Hit::Browse, Hit::Clear, Hit::Reset, Hit::Power};
    // Der Wiederholungsmodus bleibt auch während der Pause und zwischen zwei Titeln sichtbar, nach einem Lesefehler wie im Stopp nicht
    const auto state = m_playback.state;
    const bool active = state == SdPlaybackStatus::State::Starting || state == SdPlaybackStatus::State::Playing || state == SdPlaybackStatus::State::Paused || state == SdPlaybackStatus::State::Ended;
    Rolle playRole = state == SdPlaybackStatus::State::Playing ? Rolle::SDPlayLaeuft : Rolle::GemAkzent;
    if (active && m_repeat == RepeatMode::Track)
        playRole = Rolle::SDPlayWiederholTitel;
    else if (active && m_repeat == RepeatMode::Playlist)
        playRole = Rolle::SDPlayWiederholListe;
    char play[24];
    snprintf(play, sizeof(play), "^PLAY:48:%04X^", farbe(playRole));
    m_buttons[1].setLabelText(play);
    drawPause();
    const uint16_t panel = farbe(Rolle::GemFlaeche), ground = farbe(Rolle::GemGrund);
    for (int i = 0; i < 9; ++i)
    {
        if (i == 2)
            continue;
        const auto b = bounds(hits[i]);
        m_scene.fillRect(b.x, b.y, b.w, b.h, i < 5 ? panel : ground);
        m_buttons[i].drawButton(m_hit == hits[i] && !m_cancelled);
        if (i == 1)
            m_buttons[i].drawButton(m_hit == hits[i] && !m_cancelled);
        presentRect(b.x, b.y, b.w, b.h);
    }
    drawKnob(m_state.muted ? 0 : m_state.volume);
    drawTracks(m_state.balance, m_state.tone[0], m_state.tone[1], m_state.tone[2]);
}

void SdPlayer::drawFrame()
{
    // Die Szene wird vollständig aufgebaut und erst am Ende von draw() in einem Stück übertragen, kein Zwischenbild auf dem Display.
    m_bulk = true;
    const uint16_t ground = farbe(Rolle::GemGrund), divider = farbe(Rolle::GemTeiler);
    m_scene.clear(ground);
    m_scene.fillRect(0, SL::Frame::PANEL_Y, SL::Frame::PANEL_W, SL::Frame::PANEL_H, farbe(Rolle::GemFlaeche));
    m_scene.fillRect(0, SL::Frame::TOP_DIVIDER_Y, SL::WIDTH, SL::Frame::DIVIDER_H, divider);
    m_scene.fillRect(0, SL::Frame::BOTTOM_DIVIDER_Y, SL::WIDTH, SL::Frame::DIVIDER_H, divider);
    m_scene.fillRect(SL::Frame::PANEL_W, SL::Frame::TOP_DIVIDER_Y, SL::Frame::GAP_W, SL::Frame::DIVIDER_H, ground);
    m_scene.fillRect(SL::Frame::PANEL_W, SL::Frame::BOTTOM_DIVIDER_Y, SL::Frame::GAP_W, SL::Frame::DIVIDER_H, ground);
    m_fullDirty = false;
    m_dirty = All;
    m_lastDrawnSecond = -1;
}

void SdPlayer::drawTape(uint32_t now, bool force)
{
    m_now = now;
    stringutils::safeCopy(m_currentArtist, m_playback.artist);
    if (force)
        applyCassetteDesign();
    else
    {
        drawCassetteLabel();
        updateCassette();
    }
}

void SdPlayer::drawPlaylistRow(size_t i, int y)
{
    const bool selected = i == m_selected && m_playback.state != SdPlaybackStatus::State::Ended;
    const uint16_t bg = farbe(selected ? Rolle::GemAuswahl : Rolle::GemFlaeche), fg = farbe(m_tracks[i].played ? Rolle::SDTitelGespielt : Rolle::GemText);
    if (selected)
        m_playlist.fillRect(0, y, ListWidth, RowHeight, bg);
    char num[8], name[128], shown[128];
    snprintf(num, sizeof(num), "%2u.", static_cast<unsigned>(i + 1));
    m_playlist.setTextColor(farbe(selected ? Rolle::SDTitelAktiv : m_tracks[i].played ? Rolle::SDTitelGespielt
                                                                                      : Rolle::GemTextSchwach),
                            bg);
    m_playlist.drawString(num, SL::Playlist::NUMBER_X, y + RowHeight / 2);
    cleanupFilename(strrchr(m_tracks[i].file.path, '/') + 1, name, sizeof(name));
    int left = m_playlist.textWidth("999.") + SL::Playlist::NAME_GAP;
    truncateWithEllipsis(&m_playlist, name, shown, sizeof(shown), ListWidth - left - SL::Playlist::NAME_MARGIN);
    m_playlist.setTextColor(fg, bg);
    m_playlist.drawString(shown, left, y + RowHeight / 2);
}

void SdPlayer::renderPlaylistStrip(int from, int to)
{
    // Inhaltszeile c liegt in Spritezeile (c-m_ringBase) mod ListHeight, ein Streifen über das Pufferende wird in zwei Abschnitten gezeichnet.
    m_playlist.loadFont(ComfortaaBold24);
    m_playlist.setTextDatum(ML_DATUM);
    const uint16_t panel = farbe(Rolle::GemFlaeche);
    for (int c = from; c < to;)
    {
        const int row = ((c - m_ringBase) % ListHeight + ListHeight) % ListHeight, length = std::min(to - c, ListHeight - row), offset = row - c;
        m_playlist.setClipRect(0, row, ListWidth, length);
        m_playlist.fillRect(0, row, ListWidth, length, panel);
        const int first = std::max(0, c / RowHeight - 1), last = std::min(static_cast<int>(m_count) - 1, (c + length) / RowHeight);
        for (int i = first; i <= last; ++i)
            if (i * RowHeight < c + length && (i + 1) * RowHeight > c)
                drawPlaylistRow(i, i * RowHeight + offset);
        m_playlist.clearClipRect();
        c += length;
    }
}

void SdPlayer::presentPlaylist()
{
    const int top = ((m_scroll - m_ringBase) % ListHeight + ListHeight) % ListHeight;
    const auto *pixels = static_cast<const uint16_t *>(m_playlist.getBuffer());
    if (m_bulk)
    {
        syncPlaylistToScene();
        return;
    }
    // Direkt aus dem Sprite: die Kopie in die Szene kostet bei der PSRAM-Bandbreite rund 13 ms je Bild.
    Tab5Render::pushRegion(*m_display, pixels + top * ListWidth, ListWidth, ListX, ListY, ListWidth, ListHeight - top);
    if (top)
        Tab5Render::pushRegion(*m_display, pixels, ListWidth, ListX, ListY + ListHeight - top, ListWidth, top);
}

void SdPlayer::syncPlaylistToScene()
{
    const int top = ((m_scroll - m_ringBase) % ListHeight + ListHeight) % ListHeight;
    const auto *src = static_cast<const uint16_t *>(m_playlist.getBuffer());
    auto *dst = static_cast<uint16_t *>(m_scene.getBuffer());
    for (int row = 0; row < ListHeight; ++row)
        memcpy(dst + (ListY + row) * SL::WIDTH + ListX, src + ((top + row) % ListHeight) * ListWidth, ListWidth * sizeof(uint16_t));
}

void SdPlayer::draw()
{
    const bool full = m_fullDirty;
    if (full)
        drawFrame();
    if (m_dirty & Tape)
        drawTape(m_now, true);
    if (m_dirty & Cover)
        drawCover();
    if (m_dirty & (List | Scroll))
    {
        // Inhalt geändert: sichtbares Fenster vollständig zeichnen. Nur verschoben: ausschließlich die neu sichtbaren Zeilen in den Ringpuffer.
        if ((m_dirty & List) || !m_count || abs(m_scroll - m_ringScroll) >= ListHeight)
        {
            m_playlist.clear(farbe(Rolle::GemFlaeche));
            m_playlist.loadFont(ComfortaaBold24);
            m_playlist.setTextDatum(ML_DATUM);
            if (!m_count)
                drawEmptyPlaylist();
            for (size_t i = 0; i < m_count; ++i)
            {
                const int y = static_cast<int>(i) * RowHeight - m_scroll;
                if (y > -RowHeight && y < ListHeight)
                    drawPlaylistRow(i, y);
            }
            m_ringBase = m_ringScroll = m_scroll;
        }
        else if (m_scroll > m_ringScroll)
        {
            renderPlaylistStrip(m_ringScroll + ListHeight, m_scroll + ListHeight);
            m_ringScroll = m_scroll;
        }
        else if (m_scroll < m_ringScroll)
        {
            renderPlaylistStrip(m_scroll, m_ringScroll);
            m_ringScroll = m_scroll;
        }
        presentPlaylist();
        constexpr int barX = SL::Playlist::SCROLLBAR_X, barW = SL::Playlist::SCROLLBAR_W;
        int total = m_count * RowHeight, max = std::max(0, total - ListHeight);
        m_scene.fillRect(barX, ListY, barW, ListHeight, farbe(max ? Rolle::GemScrollSpur : Rolle::GemFlaeche));
        if (max)
        {
            int h = std::max(SL::Playlist::SCROLLBAR_MIN_H, static_cast<int>(lroundf(static_cast<float>(ListHeight) * ListHeight / total))), y = lroundf(static_cast<float>(std::clamp(m_scroll, 0, max)) * (ListHeight - h) / max);
            m_scene.fillRect(barX, ListY + y, barW, h, farbe(m_dragged ? Rolle::GemScrollDaumenGezogen : Rolle::GemScrollDaumen));
        }
        presentRect(barX, ListY, barW, ListHeight);
    }
    if (m_dirty & Progress)
    {
        const uint32_t time = m_hit == Hit::Progress ? m_seekTarget : m_playback.positionSeconds, duration = m_playback.durationSeconds;
        namespace P = SL::Progress;
        m_progress.clear(farbe(Rolle::SDFortschrittGrund));
        if (duration)
        {
            int width = P::W * (static_cast<uint64_t>(time) * 100 / duration) / 100;
            m_progress.fillRect(0, 0, width, P::H, farbe(Rolle::SDFortschrittBalken));
            m_progress.fillRect(0, P::BAR_LINE_Y, width, P::BAR_LINE_H, farbe(Rolle::SDFortschrittLinie));
        }
        char current[16], end[16];
        snprintf(current, sizeof(current), "%02lu:%02lu", static_cast<unsigned long>(time / 60), static_cast<unsigned long>(time % 60));
        snprintf(end, sizeof(end), "%02lu:%02lu", static_cast<unsigned long>(duration / 60), static_cast<unsigned long>(duration % 60));
        m_progress.loadFont(ComfortaaBold24);
        m_progress.setTextColor(farbe(Rolle::GemTextHell));
        m_progress.setTextDatum(MC_DATUM);
        m_progress.drawString("/", P::SLASH_X, P::TEXT_Y);
        m_progress.setTextDatum(ML_DATUM);
        m_progress.drawString(current, P::CURRENT_X, P::TEXT_Y);
        m_progress.drawString(end, P::END_X, P::TEXT_Y);
        present(m_progress, P::X, P::Y);
    }
    if (m_dirty & (Metadata | Header))
    {
        // Rahmenlinien nur beim Neuaufbau, drawMetaText(false) löscht die Textfläche selbst und überträgt die Zeile einmal.
        if (full)
            drawMetaText(true);
        const bool failure = stateText() != SdPlayerKey::Ready;
        const uint16_t text = farbe(Rolle::GemText);
        if (failure)
            drawMetaText(false, " ", I18n::sdPlayer(stateText(), m_language), text, text, text, " - ");
        else if (m_playback.state == SdPlaybackStatus::State::Ended)
            drawMetaText(false, " ", I18n::sdPlayer(SdPlayerKey::PlaylistEnd, m_language), text, text, text, " - ");
        else if (m_playbackId && m_count)
        {
            char title[128];
            cleanupFilename(strrchr(m_tracks[m_selected].file.path, '/') + 1, title, sizeof(title));
            drawMetaText(false, m_playback.artist, m_playback.title[0] ? m_playback.title : title);
        }
        else
            drawMetaText(false, " ", I18n::sdPlayer(SdPlayerKey::NoPlayback, m_language), text, text, text, " - ");
        namespace D = SL::Details;
        constexpr int detailsW = SL::Cover::W, detailsH = SL::Cover::DETAILS_H, line = SL::Cover::LINE;
        const uint16_t border = farbe(Rolle::GemRahmen);
        m_details.clear(farbe(Rolle::SDDetailGrund));
        m_details.fillRect(0, 0, line, detailsH, border);
        m_details.fillRect(detailsW - line, 0, line, detailsH, border);
        char first[160]{}, second[160]{}, shown[160]{};
        int maxTrackWidth = D::TEXT_W;
        auto folderName = [&](char *out, size_t size)
        {
            stringutils::safeCopy(out, size, m_tracks[m_selected].file.path);
            char *slash = strrchr(out, '/');
            if (slash)
                *slash = 0;
            const char *folder = strrchr(out, '/');
            if (folder && folder[1])
                memmove(out, folder + 1, strlen(folder + 1) + 1);
            else
                stringutils::safeCopy(out, size, I18n::sdPlayer(SdPlayerKey::AlbumUnknown, m_language));
        };
        if (m_playbackId && m_count)
        {
            if (m_playback.album[0])
                stringutils::safeCopy(first, m_playback.album);
            else
            {
                stringutils::safeCopy(first, m_tracks[m_selected].file.path);
                char *slash = strrchr(first, '/');
                if (slash)
                    *slash = 0;
                const char *folder = strrchr(first, '/');
                if (folder && folder[1])
                    memmove(first, folder + 1, strlen(folder + 1) + 1);
                else
                    stringutils::safeCopy(first, I18n::sdPlayer(SdPlayerKey::AlbumUnknown, m_language));
            }
            snprintf(second, sizeof(second), I18n::sdPlayer(SdPlayerKey::TrackInfoFormat, m_language), static_cast<int>(m_selected + 1), static_cast<int>(m_count));
            const char *ext = strrchr(m_tracks[m_selected].file.path, '.');
            char type[8]{}, rate[24]{};
            if (ext)
                for (int i = 0; i < 7 && ext[i + 1]; ++i)
                    type[i] = toupper(ext[i + 1]);
            if (const uint32_t rateK = displayKbps(m_tracks[m_selected].file.path, m_playback.bitRate))
                snprintf(rate, sizeof(rate), "%lu kbps", static_cast<unsigned long>(rateK));
            m_details.loadFont(ComfortaaBold15);
            m_details.setTextDatum(MC_DATUM);
            m_details.setTextColor(farbe(Rolle::SDBadgeText));
            int tw = m_details.textWidth(type) + D::BADGE_PAD, rw = rate[0] ? m_details.textWidth(rate) + D::BADGE_PAD : 0, x = detailsW - tw - D::BADGE_RIGHT;
            const uint16_t color = farbe(!strcmp(type, "MP3") ? Rolle::SDBadgeMp3 : !strcmp(type, "FLAC") ? Rolle::SDBadgeFlac
                                                                                : !strcmp(type, "OGG")    ? Rolle::SDBadgeOgg
                                                                                                          : Rolle::SDBadgeAac);
            maxTrackWidth = detailsW - D::TEXT_X - tw - rw - (rw ? D::BADGE_GAP : 0) - D::BADGE_RIGHT - D::TRACK_GAP;
            constexpr int badgeMid = D::BADGE_Y + D::BADGE_H / 2;
            m_details.fillRect(x, D::BADGE_Y, tw, D::BADGE_H, color);
            m_details.drawString(type, x + tw / 2, badgeMid);
            if (rw)
            {
                m_details.fillRect(x - rw - D::BADGE_GAP, D::BADGE_Y, rw, D::BADGE_H, farbe(Rolle::SDBadgeBitrate));
                m_details.drawString(rate, x - rw / 2 - D::BADGE_GAP, badgeMid);
            }
        }
        else
        {
            if (m_services.totalBytes)
            {
                constexpr uint64_t gib = 1024ULL * 1024 * 1024;
                uint64_t free = m_services.totalBytes > m_services.usedBytes ? m_services.totalBytes - m_services.usedBytes : 0;
                unsigned f = (free * 10 + gib / 2) / gib, t = (m_services.totalBytes * 10 + gib / 2) / gib;
                snprintf(first, sizeof(first), I18n::sdPlayer(SdPlayerKey::CapacityFormat, m_language), f / 10, f % 10, t / 10, t % 10);
            }
            else
                stringutils::safeCopy(first, I18n::sdPlayer(SdPlayerKey::NoCard, m_language));
            if (!m_count)
                stringutils::safeCopy(second, I18n::sdPlayer(SdPlayerKey::AddTracksHint, m_language));
            else
            {
                char folder[128];
                folderName(folder, sizeof(folder));
                if (m_count == 1)
                    snprintf(second, sizeof(second), I18n::sdPlayer(SdPlayerKey::SummaryOneFormat, m_language), folder);
                else
                    snprintf(second, sizeof(second), I18n::sdPlayer(SdPlayerKey::SummaryFormat, m_language), static_cast<int>(m_count), folder);
            }
        }
        m_details.loadFont(ComfortaaBold21);
        m_details.setTextDatum(TL_DATUM);
        m_details.setTextColor(text);
        truncateWithEllipsis(&m_details, first, shown, sizeof(shown), m_playbackId ? D::TEXT_W_PLAYING : D::TEXT_W);
        m_details.drawString(shown, D::TEXT_X, D::LINE1_Y);
        m_details.setTextColor(!m_count ? farbe(Rolle::GemTextGedaempft) : text);
        if (!m_playbackId)
        {
            truncateWithEllipsis(&m_details, second, shown, sizeof(shown), D::TEXT_W);
            m_details.drawString(shown, D::TEXT_X, D::LINE2_Y);
        }
        else if (m_details.textWidth(second) <= maxTrackWidth)
            m_details.drawString(second, D::TEXT_X, D::LINE2_Y);
        present(m_details, SL::Cover::X, SL::Cover::DETAILS_Y);
    }
    if (m_dirty & Controls)
        drawControls();
    else
    {
        if (m_dirty & Knob)
            drawKnob(m_state.muted ? 0 : m_state.volume);
        if (m_dirty & Tracks)
            drawTracks(m_state.balance, m_state.tone[0], m_state.tone[1], m_state.tone[2]);
    }
    m_dirty = 0;
    if (full)
    {
        m_bulk = false;
        presentRect(0, 0, SL::WIDTH, SL::HEIGHT);
        m_visualizer->invalidate();
    }
}

void SdPlayer::update(uint32_t now, AudioManager &audio)
{
    if (!m_visible || !m_ready)
        return;
    m_now = now;
    const bool wasModal = modalActive();
    m_sleep.update();
    if (m_messageBox.active() && static_cast<int32_t>(now - m_dialogTime) >= 10000)
        m_messageBox.close();
    if (wasModal && !modalActive())
        invalidate();
    if (m_explorer.active())
    {
        m_explorer.update();
        audio.consumeAudioData();
        return;
    }
    if (m_messageBox.active() || m_sleep.isActive())
    {
        audio.consumeAudioData();
        return;
    }
    if ((m_hit == Hit::Previous || m_hit == Hit::Next) && !m_cancelled && static_cast<int32_t>(now - m_pressTime) >= static_cast<int32_t>(HoldDelay))
    {
        m_held = true;
        if (canSeek() && static_cast<int32_t>(now - m_lastSeek) >= static_cast<int32_t>(SeekRepeat))
        {
            m_lastSeek = now;
            uint32_t target = m_hit == Hit::Previous ? (m_playback.positionSeconds > 10 ? m_playback.positionSeconds - 10 : 0) : std::min(m_playback.durationSeconds > 3 ? m_playback.durationSeconds - 3 : 0, m_playback.positionSeconds + 10);
            if (target != m_playback.positionSeconds)
                request(SdPlayerAction::Type::Seek, target);
        }
    }
    // Kommt oder geht der Akku, erscheint oder verschwindet sein Symbol. Der Weg des Moduswechsels räumt die Fläche und baut Uhr, Symbol und Buttons neu auf.
    if (m_state.battery.present != m_batteryPresent)
    {
        m_batteryPresent = m_state.battery.present;
        m_drawnMode = static_cast<VisMode>(VIS_MODE_COUNT);
    }
    if (m_drawnMode != m_visualizer->mode())
    {
        m_scene.fillRect(RL::Visual::X, RL::Visual::Y, RL::Visual::W, RL::Visual::H, farbe(Rolle::GemGrund));
        presentRect(RL::Visual::X, RL::Visual::Y, RL::Visual::W, RL::Visual::H);
        m_drawnMode = m_visualizer->mode();
        m_lastDrawnSecond = -1;
        m_dirty |= Controls;
        m_visualizer->invalidate();
    }
    updateScrollPhysics(now);
    if (m_playback.state == SdPlaybackStatus::State::Paused && now - m_lastPulseTime >= 50)
    {
        m_lastPulseTime = now;
        drawPause();
    }
    draw();
    drawTape(now, false);
    m_visualizer->update(now, audio);
    drawClock();
}

void SdPlayer::drawPause()
{
    const uint16_t panel = farbe(Rolle::GemFlaeche);
    uint16_t color = farbe(Rolle::GemAkzent);
    // SD.PausePuls: in der Pause pendelt das Symbol im 3-s-Takt zwischen Gem.TextHell und dem Tastengrund Gem.Flaeche
    if (m_playback.state == SdPlaybackStatus::State::Paused)
    {
        const float phase = static_cast<float>(m_now % 3000) / 3000;
        color = blendColor(farbe(Rolle::GemTextHell), panel, 0.5f + 0.5f * sinf(2 * 3.14159265f * phase));
    }
    char label[32];
    snprintf(label, sizeof(label), "^PAUSE:48:%04X^", color);
    m_buttons[2].setLabelText(label);
    const auto b = bounds(Hit::Pause);
    m_scene.fillRect(b.x, b.y, b.w, b.h, panel);
    m_buttons[2].drawButton(m_hit == Hit::Pause && !m_cancelled);
    m_buttons[2].drawButton(m_hit == Hit::Pause && !m_cancelled);
    presentRect(b.x, b.y, b.w, b.h);
}

void SdPlayer::updateScrollPhysics(uint32_t now)
{
    // Ein gehaltener Finger führt die Liste selbst, die Zeitbasis beginnt erst beim Loslassen.
    if (m_hit == Hit::Track)
    {
        m_lastScrollTime = now;
        return;
    }
    const uint32_t elapsed = now - m_lastScrollTime;
    if (elapsed < 16)
        return;
    // Originalschritte für 60 Hz. Bei längeren Bildern werden sie nachgeholt, damit Trägheit und Rückfederung unabhängig von der Bildzeit gleich schnell laufen.
    const int steps = std::min<uint32_t>(elapsed / 16, 6);
    m_lastScrollTime = steps < 6 ? m_lastScrollTime + steps * 16 : now;
    m_dragged = false;
    const int maximum = std::max(0, static_cast<int>(m_count) * RowHeight - ListHeight), before = m_scroll;
    for (int step = 0; step < steps; ++step)
    {
        if (m_scroll < 0 || m_scroll > maximum)
        {
            const int target = m_scroll < 0 ? 0 : maximum;
            m_scroll += (target - m_scroll) * 0.35f;
            if (abs(m_scroll - target) <= 2)
            {
                m_scroll = target;
                m_scrollVelocity = 0;
            }
        }
        else if (fabsf(m_scrollVelocity) > 0.5f)
        {
            const float delta = m_scrollVelocity / 60 + m_inertiaRemainder;
            const int pixels = lroundf(delta);
            m_inertiaRemainder = delta - pixels;
            m_scroll += pixels;
            m_scrollVelocity *= 0.92f;
            if (fabsf(m_scrollVelocity) < 1)
                m_scrollVelocity = 0;
            if (m_scroll < 0 || m_scroll > maximum)
            {
                m_scroll = std::clamp(m_scroll, 0, maximum);
                m_scrollVelocity = 0;
            }
        }
        else
        {
            m_scrollVelocity = 0;
            break;
        }
    }
    if (before != m_scroll)
        m_dirty |= Scroll;
}

uint32_t SdPlayer::displayKbps(const char *path, uint32_t bitRate)
{
    if (!bitRate)
        return 0;
    const char *ext = path ? strrchr(path, '.') : nullptr;
    if (ext && !strcasecmp(ext, ".flac"))
        return bitRate / 1000;
    // Wie snapToStandardBitrate im Original: nächstgelegene Standardrate, damit die mittelnde VBR-Schätzung nicht laufend neu zeichnen lässt.
    constexpr uint16_t rates[] = {32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320};
    uint32_t best = UINT32_MAX, result = bitRate / 1000;
    for (auto candidate : rates)
    {
        const uint32_t delta = static_cast<uint32_t>(abs(static_cast<int>(bitRate / 1000) - candidate));
        if (delta < best)
        {
            best = delta;
            result = candidate;
        }
    }
    return result;
}
