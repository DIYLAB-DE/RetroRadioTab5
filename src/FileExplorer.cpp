#include "Tab5Render.h"
// Selektive Neufassung der Bedienrollen aus DIYLAB-DE/RetroRadio, ea4229a34fb39faaff23619092cf09cafb8c51cf, GPL-3.0 gemäß Repository-LICENSE.
#include "FileExplorer.h"
#include "StringUtils.h"
#include "fonts/ComfortaaBold21.h"
#include "fonts/ComfortaaBold24.h"
#include "fonts/ComfortaaBold27.h"
#include "fonts/LeagueGothicCond51.h"
#include "RadioTab5Layout.h"
#include "SdPlayerTab5Layout.h"
#include "IconShapes.h"
#include "Thema.h"
#include <esp_heap_caps.h>
#include <algorithm>
#include <iterator>
#include <cstring>
#include <strings.h>
#include <cstdlib>
#include <cmath>

namespace
{
    // Nativ 1,5-fach (SdPlayerTab5Layout::Explorer). Szene, Liste und Tasten in Koordinaten relativ zur linken oberen Dialogecke.
    namespace EL = SdPlayerTab5Layout::Explorer;
    constexpr int ListX = EL::LIST_X, ListY = EL::LIST_Y, ListWidth = EL::LIST_WIDTH, RowHeight = EL::ROW_H, ListHeight = EL::LIST_HEIGHT;
}

bool FileExplorer::validText(const char *value, size_t capacity)
{
    if (!value || !capacity)
        return false;
    const size_t length = strnlen(value, capacity);
    if (length == capacity)
        return false;
    for (size_t i = 0; i < length;)
    {
        const auto first = static_cast<uint8_t>(value[i++]);
        if (first < 0x80)
        {
            if (first < 0x20 || first == 0x7f)
                return false;
            continue;
        }
        unsigned extra;
        uint32_t code, minimum;
        if (first >= 0xc2 && first <= 0xdf)
        {
            extra = 1;
            code = first & 0x1f;
            minimum = 0x80;
        }
        else if (first >= 0xe0 && first <= 0xef)
        {
            extra = 2;
            code = first & 0x0f;
            minimum = 0x800;
        }
        else if (first >= 0xf0 && first <= 0xf4)
        {
            extra = 3;
            code = first & 7;
            minimum = 0x10000;
        }
        else
            return false;
        if (i + extra > length)
            return false;
        while (extra--)
        {
            const auto byte = static_cast<uint8_t>(value[i++]);
            if ((byte & 0xc0) != 0x80)
                return false;
            code = (code << 6) | (byte & 0x3f);
        }
        if (code < minimum || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff))
            return false;
    }
    return true;
}

bool FileExplorer::validPath(const char *path)
{
    if (!validText(path, sizeof(SdFileEntry::path)) || path[0] != '/')
        return false;
    const size_t length = strlen(path);
    if (length == 1)
        return true;
    if (path[length - 1] == '/' || strchr(path, '\\'))
        return false;
    for (const char *part = path + 1; *part;)
    {
        const char *end = strchr(part, '/');
        const size_t n = end ? static_cast<size_t>(end - part) : strlen(part);
        if (!n || (n == 1 && part[0] == '.') || (n == 2 && part[0] == '.' && part[1] == '.'))
            return false;
        if (!end)
            break;
        part = end + 1;
    }
    return true;
}

bool FileExplorer::supportedName(const char *name)
{
    const char *extension = name ? strrchr(name, '.') : nullptr;
    if (!extension)
        return false;
    return !strcasecmp(extension, ".mp3") || !strcasecmp(extension, ".aac") || !strcasecmp(extension, ".flac") || !strcasecmp(extension, ".m4a") || !strcasecmp(extension, ".ogg");
}

bool FileExplorer::supportedFile(const char *path)
{
    return validPath(path) && supportedName(strrchr(path, '/'));
}

bool FileExplorer::begin(lgfx::LGFX_Device &display, FileExplorerCallbacks callbacks)
{
    if (m_ready)
        return m_display == &display;
    m_display = &display;
    m_callbacks = callbacks;
    m_font.unloadFont();
    m_small.unloadFont();
    m_fontData.set(ComfortaaBold27);
    m_smallData.set(ComfortaaBold21);
    if (!m_font.loadFont(&m_fontData) || !m_small.loadFont(&m_smallData))
        return false;
    if (!m_entries)
        m_entries = static_cast<SdFileEntry *>(heap_caps_calloc(MaxEntries, sizeof(SdFileEntry), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!m_collected)
        m_collected = static_cast<SdFileEntry *>(heap_caps_calloc(MaxEntries, sizeof(SdFileEntry), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!m_entries || !m_collected)
        return false;
    if (!m_scene.prepare(EL::W, EL::H) || !m_view.prepare(ListWidth, ListHeight))
        return false;
    m_ok.initButton(&m_scene, EL::OK_CX, EL::BUTTON_CY, EL::BUTTON_W, EL::BUTTON_H, EL::RADIUS, farbe(Rolle::GemRahmen), farbe(Rolle::GemTastenFlaeche), farbe(Rolle::GemDialogText), "ok", nullptr);
    m_cancel.initButton(&m_scene, EL::CANCEL_CX, EL::BUTTON_CY, EL::BUTTON_W, EL::BUTTON_H, EL::RADIUS, farbe(Rolle::GemRahmen), farbe(Rolle::GemTastenFlaeche), farbe(Rolle::GemTextGedaempft), "abbruch", nullptr);
    m_ok.setFont(LeagueGothicCond51);
    m_cancel.setFont(LeagueGothicCond51);
    m_ok.setBorderWidths(RadioTab5Layout::Buttons::BORDER, RadioTab5Layout::Buttons::BORDER_PRESSED);
    m_cancel.setBorderWidths(RadioTab5Layout::Buttons::BORDER, RadioTab5Layout::Buttons::BORDER_PRESSED);
    for (auto *icon : m_icons)
        free(icon);
    // Die Symbole tragen den Zeilengrund eingebrannt, je einmal für normale und gewählte Zeilen.
    const uint16_t ordner = farbe(Rolle::DlgExplorerOrdner), grund = farbe(Rolle::GemGrund), auswahl = farbe(Rolle::GemAuswahl);
    m_icons[0] = IconShapes::draw(IconId::Folder, ordner, grund, EL::ICON_SCALE);
    m_icons[1] = IconShapes::draw(IconId::Folder, ordner, auswahl, EL::ICON_SCALE);
    m_icons[2] = IconShapes::draw(IconId::FolderUp, ordner, grund, EL::ICON_SCALE);
    m_icons[3] = IconShapes::draw(IconId::FolderUp, ordner, auswahl, EL::ICON_SCALE);
    for (auto *icon : m_icons)
        if (!icon)
            return false;
    m_ready = true;
    return true;
}

FileExplorer::~FileExplorer()
{
    heap_caps_free(m_entries);
    heap_caps_free(m_collected);
    for (auto *icon : m_icons)
        free(icon);
}

void FileExplorer::abortRequest()
{
    const bool busy = m_busy;
    m_busy = false;
    if (busy && m_callbacks.request)
    {
        const auto kind = m_request.kind;
        m_request.kind = SdFileRequest::Kind::Cancel;
        m_callbacks.request(m_callbacks.context, m_request);
        m_request.kind = kind;
    }
}

bool FileExplorer::navigate(const char *path)
{
    if (!validPath(path))
    {
        m_message = SdPlayerKey::InvalidPath;
        m_dirty = true;
        return false;
    }
    char copiedPath[256];
    stringutils::safeCopy(copiedPath, path);
    abortRequest();
    m_collecting = false;
    m_count = 0;
    m_skipped = 0;
    m_scroll = 0;
    m_scrollPos = m_scrollVelocity = 0;
    m_lastUpdateTime = millis();
    std::fill(std::begin(m_selected), std::end(m_selected), false);
    stringutils::safeCopy(m_path, copiedPath);
    m_request.kind = SdFileRequest::Kind::List;
    m_request.requestId = ++m_sequence;
    if (!m_request.requestId)
        m_request.requestId = ++m_sequence;
    m_request.mediaGeneration = m_generation;
    m_request.selectionCount = 0;
    stringutils::safeCopy(m_request.path, m_path);
    m_busy = m_callbacks.request && m_callbacks.request(m_callbacks.context, m_request);
    m_message = m_busy ? SdPlayerKey::Scanning : SdPlayerKey::QueueError;
    m_dirty = true;
    return m_busy;
}

bool FileExplorer::open(uint32_t generation, LangIndex language, bool canCollect, const char *path)
{
    if (!m_ready || !generation || !validPath(path))
        return false;
    cancel();
    m_active = true;
    m_generation = generation;
    m_language = language;
    m_canCollect = canCollect;
    m_hit = Hit::None;
    invalidate();
    return navigate(path);
}

void FileExplorer::cancel()
{
    abortRequest();
    m_active = false;
    m_hit = Hit::None;
    m_cancelled = true;
}

bool FileExplorer::acceptResult(const SdFileResult &result)
{
    if (!m_active || !m_busy || result.mediaGeneration != m_generation || result.requestId != m_request.requestId || result.kind != m_request.kind)
        return false;
    if (result.state != SdFileResult::State::Data)
    {
        m_busy = false;
        if (m_collecting)
            m_collectedCount = 0;
        else
            m_count = 0;
        m_message = result.state == SdFileResult::State::Limit ? SdPlayerKey::Limit : SdPlayerKey::ScanError;
        m_dirty = true;
        return true;
    }
    size_t &count = m_collecting ? m_collectedCount : m_count;
    SdFileEntry *target = m_collecting ? m_collected : m_entries;
    if (result.offset < count)
        return false;
    if (result.offset != count || result.count > MaxEntries - count || (result.count && !result.entries))
    {
        abortRequest();
        count = 0;
        m_message = SdPlayerKey::Limit;
        m_dirty = true;
        return false;
    }
    for (size_t i = 0; i < result.count; ++i)
    {
        const auto &entry = result.entries[i];
        bool valid = validPath(entry.path) && validText(entry.name, sizeof(entry.name)) && (!m_collecting || !entry.directory) && (entry.directory || supportedFile(entry.path));
        for (size_t j = 0; valid && j < count; ++j)
            valid = strcmp(entry.path, target[j].path) != 0;
        for (size_t j = 0; valid && j < i; ++j)
            valid = strcmp(entry.path, result.entries[j].path) != 0;
        if (!valid)
        {
            abortRequest();
            count = 0;
            m_message = SdPlayerKey::InvalidPath;
            m_dirty = true;
            return false;
        }
    }
    if (result.count)
        memcpy(target + count, result.entries, result.count * sizeof(SdFileEntry));
    count += result.count;
    if (result.final)
    {
        m_busy = false;
        if (!m_collecting)
            m_skipped = result.skipped;
        m_message = count ? SdPlayerKey::SelectPrompt : SdPlayerKey::EmptyFolder;
        if (m_collecting && count)
        {
            if (m_callbacks.selected && m_callbacks.selected(m_callbacks.context, target, count))
                cancel();
            else
                m_message = SdPlayerKey::Limit;
        }
    }
    m_dirty = true;
    return true;
}

void FileExplorer::collectSelection()
{
    m_request.selectionCount = 0;
    for (size_t i = 0; i < m_count; ++i)
        if (m_selected[i] && m_request.selectionCount < SdFileRequest::MaxSelection)
            m_request.selection[m_request.selectionCount++] = m_entries[i];
}

void FileExplorer::submit()
{
    if (m_busy)
        return;
    collectSelection();
    if (!m_request.selectionCount)
    {
        cancel();
        return;
    }
    bool folders = false;
    for (size_t i = 0; i < m_request.selectionCount; ++i)
        folders |= m_request.selection[i].directory;
    if (!folders)
    {
        if (m_callbacks.selected && m_callbacks.selected(m_callbacks.context, m_request.selection, m_request.selectionCount))
            cancel();
        else
        {
            m_message = SdPlayerKey::Limit;
            m_dirty = true;
        }
        return;
    }
    if (!m_canCollect)
    {
        m_message = SdPlayerKey::NoService;
        m_dirty = true;
        return;
    }
    m_request.kind = SdFileRequest::Kind::Collect;
    m_request.requestId = ++m_sequence;
    if (!m_request.requestId)
        m_request.requestId = ++m_sequence;
    m_request.mediaGeneration = m_generation;
    m_collecting = true;
    m_collectedCount = 0;
    m_busy = m_callbacks.request && m_callbacks.request(m_callbacks.context, m_request);
    m_message = m_busy ? SdPlayerKey::Collecting : SdPlayerKey::QueueError;
    m_dirty = true;
}

void FileExplorer::setLanguage(LangIndex language)
{
    if (m_language != language)
    {
        m_language = language;
        invalidate();
    }
}

void FileExplorer::invalidate()
{
    m_dirty = m_frameDirty = true;
}

FileExplorer::Hit FileExplorer::hitAt(int x, int y, int &row) const
{
    row = -1;
    if (m_cancel.contains(x, y))
        return Hit::Cancel;
    if (m_ok.contains(x, y) && !m_busy)
        return Hit::Add;
    if (x >= ListX && x < ListX + ListWidth && y >= ListY && y < ListY + ListHeight && !m_busy)
    {
        const bool parent = strcmp(m_path, "/");
        int index = (y - ListY + m_scroll) / RowHeight;
        if (parent && index == 0)
            return Hit::Up;
        row = index - (parent ? 1 : 0);
        if (row >= 0 && row < static_cast<int>(m_count))
            return Hit::Row;
    }
    return Hit::None;
}

void FileExplorer::toggle(int row)
{
    if (row < 0 || row >= static_cast<int>(m_count))
        return;
    if (m_entries[row].directory)
    {
        if (!m_canCollect)
            return;
        bool selected = !m_selected[row];
        memset(m_selected, 0, sizeof(m_selected));
        m_selected[row] = selected;
    }
    else
    {
        size_t selected = 0;
        for (size_t i = 0; i < m_count; ++i)
        {
            if (m_selected[i] && m_entries[i].directory)
                return;
            selected += m_selected[i];
        }
        if (!m_selected[row] && selected >= SdFileRequest::MaxSelection)
            m_message = SdPlayerKey::Limit;
        else
            m_selected[row] = !m_selected[row];
    }
    m_dirty = true;
}

bool FileExplorer::touch(const SdTouch &physical)
{
    if (!m_active)
        return false;
    SdTouch e = physical;
    e.x -= EL::X;
    e.y -= EL::Y;
    int row;
    const auto hit = hitAt(e.x, e.y, row);
    if (e.phase == SdTouchPhase::Cancel)
    {
        m_hit = Hit::None;
        m_cancelled = true;
        m_dirty = true;
        m_dragged = false;
        m_scrollVelocity = 0;
        return true;
    }
    if (e.phase == SdTouchPhase::Press)
    {
        m_hit = hit;
        m_pressRow = row;
        m_pressY = e.y;
        m_pressScroll = m_scroll;
        m_pressTime = e.now;
        m_dragged = m_cancelled = m_held = false;
        m_scrollVelocity = 0;
        m_pressX = e.x;
        m_lastTouchY = e.y;
        m_lastTouchTime = e.now;
        m_dirty = true;
    }
    else if (e.phase == SdTouchPhase::Move)
    {
        if (m_hit == Hit::Row || m_hit == Hit::Up)
        {
            constexpr int drag = EL::DRAG_THRESHOLD, over = EL::OVERSCROLL;
            if (!m_dragged && !m_held && (abs(e.y - m_pressY) > drag || abs(e.x - m_pressX) > drag))
            {
                m_dragged = true;
                m_pressY = e.y;
                m_pressScroll = m_scroll;
            }
            // Der GT911 liefert zwischen zwei Abtastungen dieselbe Position, die Schleife läuft schneller: Geschwindigkeit nur bei echter Änderung und über die Zeit seit der letzten Änderung.
            const bool movedY = e.y != m_lastTouchY;
            if (m_dragged)
            {
                int total = (m_count + (strcmp(m_path, "/") ? 1 : 0)) * RowHeight, maximum = std::max(0, total - ListHeight);
                m_scrollPos = m_pressScroll + (m_pressY - e.y) * 1.3f;
                const uint32_t dt = e.now - m_lastTouchTime;
                if (movedY && dt && dt < 100)
                    m_scrollVelocity = std::clamp(((m_lastTouchY - e.y) * 1000.0f / dt * 0.5f + m_scrollVelocity * 0.5f) * 1.5f, -3000.0f, 3000.0f);
                if (m_scrollPos < -over)
                    m_scrollPos = -over + (m_scrollPos + over) * 0.4f;
                else if (m_scrollPos > maximum + over)
                    m_scrollPos = maximum + over + (m_scrollPos - maximum - over) * 0.4f;
                m_scroll = lroundf(m_scrollPos);
                m_dirty = true;
            }
            else if (!m_held && e.now - m_pressTime > 500)
            {
                toggle(m_pressRow);
                m_held = true;
            }
            if (e.x < ListX || e.x >= ListX + ListWidth)
                m_cancelled = true;
            if (movedY)
            {
                m_lastTouchY = e.y;
                m_lastTouchTime = e.now;
            }
        }
        else if (hit != m_hit)
        {
            m_cancelled = true;
            m_dirty = true;
        }
    }
    else if (e.phase == SdTouchPhase::Release)
    {
        auto pressed = m_hit;
        m_hit = Hit::None;
        m_dirty = true;
        const bool dragged = m_dragged;
        m_dragged = false;
        m_lastUpdateTime = e.now;
        if (dragged && e.now - m_lastTouchTime >= 100)
            m_scrollVelocity = 0; // Finger ruhte vor dem Loslassen: kein Wurf
        if (m_cancelled || dragged || m_held || hit != pressed)
            return true;
        if (pressed == Hit::Cancel)
            cancel();
        else if (pressed == Hit::Add)
            submit();
        else if (pressed == Hit::Up)
        {
            char parent[256];
            stringutils::safeCopy(parent, m_path);
            char *slash = strrchr(parent, '/');
            if (slash == parent)
                parent[1] = 0;
            else if (slash)
                *slash = 0;
            navigate(parent);
        }
        else if (pressed == Hit::Row && row == m_pressRow && row >= 0)
        {
            if (e.now - m_pressTime > 500)
                toggle(row);
            else if (e.now - m_pressTime < 400 && m_entries[row].directory)
                navigate(m_entries[row].path);
        }
    }
    return true;
}

void FileExplorer::draw()
{
    // Szene in Dialoggröße. Originalpositionen relativ zu (125,65), nativ 1,5-fach (Werte und Originale in SdPlayerTab5Layout::Explorer).
    constexpr int tx = EL::TEXT_X;
    const uint16_t grund = farbe(Rolle::GemGrund), haupttext = farbe(Rolle::GemText);
    m_scene.fillSmoothRoundRect(0, 0, EL::W, EL::H, EL::RADIUS, farbe(Rolle::GemDialogRahmen));
    m_scene.fillSmoothRoundRect(EL::BORDER, EL::BORDER, EL::W - 2 * EL::BORDER, EL::H - 2 * EL::BORDER, EL::RADIUS, grund);
    m_scene.loadFont(ComfortaaBold24);
    m_scene.setTextDatum(TL_DATUM);
    m_scene.setTextColor(farbe(Rolle::GemTitel), grund);
    m_scene.drawString(I18n::sdPlayer(SdPlayerKey::ExplorerHeading, m_language), tx, EL::TITLE_Y);
    m_scene.setTextColor(haupttext, grund);
    char path[256];
    stringutils::safeCopy(path, m_path);
    while (strlen(path) > 3 && m_scene.textWidth(path) > EL::PATH_W)
    {
        size_t n = 1;
        while ((static_cast<unsigned char>(path[n]) & 0xc0) == 0x80)
            ++n;
        memmove(path, path + n, strlen(path + n) + 1);
    }
    m_scene.drawString(path, tx, EL::PATH_Y);
    const uint16_t trenner = farbe(Rolle::GemDialogTrenner);
    m_scene.fillRect(tx, EL::LINE1_Y, EL::LINE_W, EL::LINE_H, trenner);
    m_scene.fillRect(tx, EL::LINE2_Y, EL::LINE_W, EL::LINE_H, trenner);
    m_scene.setTextColor(farbe(Rolle::GemHinweis), grund);
    m_scene.setTextDatum(ML_DATUM);
    m_scene.drawString(I18n::sdPlayer(SdPlayerKey::ColumnName, m_language), EL::HEADER_NAME_X, EL::HEADER_Y);
    m_scene.setTextDatum(MC_DATUM);
    m_scene.drawString(I18n::sdPlayer(SdPlayerKey::ColumnType, m_language), EL::HEADER_TYPE_X, EL::HEADER_Y);
    m_scene.setTextDatum(MR_DATUM);
    m_scene.drawString(I18n::sdPlayer(SdPlayerKey::ColumnSize, m_language), EL::HEADER_SIZE_X, EL::HEADER_Y);
    m_view.clear(grund);
    m_view.loadFont(ComfortaaBold27);
    const int parent = strcmp(m_path, "/") ? 1 : 0, total = m_count + parent;
    auto sizeText = [](uint64_t bytes, char *out, size_t length)
    {
        const char *units[] = {"B", "KB", "MB", "GB"};
        int unit = 0;
        double value = bytes;
        while (value >= 1024 && unit < 3)
        {
            value /= 1024;
            ++unit;
        }
        if (!unit)
            snprintf(out, length, "%lu B", static_cast<unsigned long>(bytes));
        else
            snprintf(out, length, "%.1f %s", value, units[unit]);
    };
    unsigned dirs = 0, files = 0, selectedDirs = 0, selectedFiles = 0;
    uint64_t selectedSize = 0;
    for (size_t i = 0; i < m_count; ++i)
    {
        const auto &f = m_entries[i];
        if (f.directory)
        {
            ++dirs;
            selectedDirs += m_selected[i];
        }
        else
        {
            ++files;
            if (m_selected[i])
            {
                ++selectedFiles;
                selectedSize += f.size;
            }
        }
    }
    const uint16_t auswahl = farbe(Rolle::GemAuswahl), textHell = farbe(Rolle::GemTextHell), ordner = farbe(Rolle::DlgExplorerOrdner), mp3 = farbe(Rolle::DlgExplorerMp3), flac = farbe(Rolle::DlgExplorerFlac);
    if (m_busy || !total)
    {
        m_view.setTextDatum(MC_DATUM);
        m_view.setTextColor(haupttext, grund);
        m_view.drawString(m_busy ? I18n::sdPlayer(m_message, m_language) : I18n::sdPlayer(SdPlayerKey::DirectoryEmpty, m_language), ListWidth / 2, ListHeight / 2);
    }
    else
        for (int index = 0; index < total; ++index)
        {
            int y = index * RowHeight - m_scroll;
            if (y <= -RowHeight || y >= ListHeight)
                continue;
            const bool up = parent && index == 0;
            int row = index - parent;
            const auto *entry = up ? nullptr : &m_entries[row];
            const bool directory = up || entry->directory, selected = !up && m_selected[row];
            uint16_t bg = selected ? auswahl : grund, fg = selected ? textHell : directory ? ordner
                                                                                           : haupttext;
            if (selected)
                m_view.fillRect(0, y, ListWidth, RowHeight, bg);
            int left = EL::ROW_LEFT;
            if (directory)
            {
                const IconId id = up ? IconId::FolderUp : IconId::Folder;
                m_view.pushImage(left, y + (up ? EL::ICON_UP_Y : EL::ICON_Y), lroundf(IconShapes::width(id) * EL::ICON_SCALE), lroundf(IconShapes::height(id) * EL::ICON_SCALE), m_icons[(up ? 2 : 0) + (selected ? 1 : 0)]);
                left += EL::ICON_GAP;
            }
            char name[128];
            stringutils::safeCopy(name, up ? ".." : entry->name);
            const char *ext = up ? nullptr : strrchr(entry->name, '.');
            if (!directory && ext)
            {
                char *dot = strrchr(name, '.');
                if (dot)
                    *dot = 0;
            }
            bool clipped = false;
            while (name[0] && m_view.textWidth(name) > EL::TYPE_X - left - EL::NAME_RESERVE - (clipped ? m_view.textWidth("...") : 0))
            {
                size_t n = strlen(name);
                do
                {
                    --n;
                } while (n && (static_cast<unsigned char>(name[n]) & 0xc0) == 0x80);
                name[n] = 0;
                clipped = true;
            }
            if (clipped)
                stringutils::safeAppend(name, "...");
            m_view.setTextColor(fg, bg);
            m_view.setTextDatum(ML_DATUM);
            m_view.drawString(name, left, y + EL::ROW_TEXT_Y);
            m_view.setTextDatum(MC_DATUM);
            if (!directory && ext)
            {
                if (!strcasecmp(ext, ".mp3"))
                    m_view.setTextColor(mp3, bg);
                else if (!strcasecmp(ext, ".flac"))
                    m_view.setTextColor(flac, bg);
            }
            m_view.drawString(directory ? "<DIR>" : ext ? ext + 1
                                                        : "",
                              EL::TYPE_X, y + EL::ROW_TEXT_Y);
            if (!directory && entry->size)
            {
                char size[24];
                sizeText(entry->size, size, sizeof(size));
                m_view.setTextColor(fg, bg);
                m_view.setTextDatum(MR_DATUM);
                m_view.drawString(size, EL::SIZE_RIGHT, y + EL::ROW_TEXT_Y);
            }
        }
    // Zeilenweise Speicherkopie statt pixelweisem pushSprite, beide Flächen RGB565 im selben Format
    {
        const auto *src = static_cast<const uint16_t *>(m_view.getBuffer());
        auto *dst = static_cast<uint16_t *>(m_scene.getBuffer());
        for (int row = 0; row < ListHeight; ++row)
            memcpy(dst + (ListY + row) * EL::W + ListX, src + row * ListWidth, ListWidth * sizeof(uint16_t));
    }
    int max = std::max(0, total * RowHeight - ListHeight);
    if (max)
    {
        int height = std::max(EL::SCROLLBAR_MIN_H, static_cast<int>(lroundf(static_cast<float>(ListHeight) * ListHeight / (total * RowHeight))));
        m_scene.fillRect(EL::SCROLLBAR_X, ListY, EL::SCROLLBAR_W, ListHeight, farbe(Rolle::GemScrollSpur));
        m_scene.fillRect(EL::SCROLLBAR_X, ListY + (ListHeight - height) * std::clamp(m_scroll, 0, max) / max, EL::SCROLLBAR_W, height, farbe(m_dragged ? Rolle::GemScrollDaumenGezogen : Rolle::GemScrollDaumen));
    }
    char line[128], size[24];
    m_scene.loadFont(ComfortaaBold21);
    m_scene.setTextDatum(TL_DATUM);
    m_scene.setTextColor(haupttext, grund);
    snprintf(line, sizeof(line), I18n::sdPlayer(SdPlayerKey::StatusOverview, m_language), files, dirs);
    m_scene.drawString(line, tx, EL::STATUS1_Y);
    if (m_busy || m_message == SdPlayerKey::ScanError || m_message == SdPlayerKey::Limit || m_message == SdPlayerKey::QueueError)
        stringutils::safeCopy(line, I18n::sdPlayer(m_message, m_language));
    else if (selectedDirs)
        snprintf(line, sizeof(line), I18n::sdPlayer(SdPlayerKey::StatusFolders, m_language), selectedDirs);
    else if (selectedFiles)
    {
        sizeText(selectedSize, size, sizeof(size));
        snprintf(line, sizeof(line), I18n::sdPlayer(SdPlayerKey::StatusFiles, m_language), size, selectedFiles);
    }
    else if (m_skipped)
        snprintf(line, sizeof(line), I18n::sdPlayer(SdPlayerKey::Skipped, m_language), static_cast<unsigned>(m_skipped));
    else
        stringutils::safeCopy(line, I18n::sdPlayer(SdPlayerKey::NothingSelected, m_language));
    m_scene.drawString(line, tx, EL::STATUS2_Y);
    m_cancel.setLabelText(m_language == LangIndex::DE ? "abbruch" : "cancel");
    m_ok.drawButton(m_hit == Hit::Add && !m_cancelled);
    m_cancel.drawButton(m_hit == Hit::Cancel && !m_cancelled);
    Tab5Render::pushRegion(*m_display, static_cast<const uint16_t *>(m_scene.getBuffer()), EL::W, EL::X, EL::Y, EL::W, EL::H);
    m_frameDirty = false;
}

void FileExplorer::update()
{
    if (m_active && m_ready)
        updateScrollPhysics();
    if (m_active && m_ready && m_dirty)
    {
        draw();
        m_dirty = false;
    }
}

void FileExplorer::updateScrollPhysics()
{
    const uint32_t now = millis();
    const float dt = std::min((now - m_lastUpdateTime) / 1000.0f, 0.1f);
    m_lastUpdateTime = now;
    if (m_dragged || m_hit == Hit::Row || m_hit == Hit::Up || dt <= 0)
        return;
    const int maximum = std::max(0, static_cast<int>(m_count + (strcmp(m_path, "/") ? 1 : 0)) * RowHeight - ListHeight), before = m_scroll;
    if (m_scrollPos < 0 || m_scrollPos > maximum)
    {
        const float target = m_scrollPos < 0 ? 0 : maximum;
        m_scrollPos += (target - m_scrollPos) * 10 * dt;
        m_scrollVelocity = 0;
        if (fabsf(m_scrollPos - target) < 1)
            m_scrollPos = target;
    }
    else if (fabsf(m_scrollVelocity) > 10)
    {
        m_scrollPos += m_scrollVelocity * dt;
        m_scrollVelocity *= expf(-2 * dt);
        if (fabsf(m_scrollVelocity) < 50 && fabsf(m_scrollVelocity) > 10)
            m_scrollVelocity *= 0.98f;
        if (fabsf(m_scrollVelocity) < 10)
        {
            m_scrollVelocity = 0;
            m_scrollPos = std::clamp(m_scrollPos, 0.0f, static_cast<float>(maximum));
        }
    }
    else
        m_scrollVelocity = 0;
    m_scroll = lroundf(m_scrollPos);
    if (before != m_scroll)
        m_dirty = true;
}
