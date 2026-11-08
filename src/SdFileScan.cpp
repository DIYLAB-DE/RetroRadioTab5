#include "SdFileScan.h"
#include "StringUtils.h"
#include "SystemEntries.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <strings.h>

using State = SdFileResult::State;

void SdFileScan::reset()
{
    m_count = m_pendingCount = 0;
    m_skipped = m_iterations = 0;
}

bool SdFileScan::join(char (&target)[256], const char *parent, const char *name)
{
    const int length = snprintf(target, sizeof(target), "%s/%s", strcmp(parent, "/") ? parent : "", name);
    return length >= 0 && static_cast<size_t>(length) < sizeof(target);
}

SdFileScan::Add SdFileScan::add(const char *parent, const Directory::Item &item)
{
    if (m_count >= FileExplorer::MaxEntries)
        return Add::Full;
    auto &entry = m_entries[m_count];
    entry = {};
    if (!join(entry.path, parent, item.name))
    {
        ++m_skipped;
        return Add::Skipped;
    }
    stringutils::safeCopyUtf8(entry.name, item.name);
    entry.directory = item.directory;
    entry.size = item.directory ? 0 : item.size;
    return keep();
}

SdFileScan::Add SdFileScan::keep()
{
    const auto &entry = m_entries[m_count];
    if (!FileExplorer::validPath(entry.path) || !FileExplorer::validText(entry.name, sizeof(entry.name)))
    {
        ++m_skipped;
        return Add::Skipped;
    }
    for (size_t i = 0; i < m_count; ++i)
        if (!strcmp(m_entries[i].path, entry.path))
            return Add::Added;
    ++m_count;
    return Add::Added;
}

SdFileResult::State SdFileScan::list(Directory &directory, const char *path)
{
    reset();
    if (!FileExplorer::validPath(path) || !directory.open(path))
    {
        directory.close();
        return State::Error;
    }
    State state = State::Data;
    Directory::Item item;
    while (state == State::Data && directory.next(item))
        if (directory.cancelled())
            state = State::Cancelled;
        else if (++m_iterations > MaxIterations)
            state = State::Limit;
        else if (!SystemEntries::hidden(item.name, item.hidden) && (item.directory || FileExplorer::supportedName(item.name)) && add(path, item) == Add::Full)
            state = State::Limit;
    directory.close();
    if (state == State::Data)
        std::sort(m_entries, m_entries + m_count, [](const SdFileEntry &a, const SdFileEntry &b)
        {
            if (a.directory != b.directory)
                return a.directory;
            // Gekürzte Namen können übereinstimmen, der Pfad macht die Reihenfolge eindeutig.
            const int order = strcasecmp(a.name, b.name);
            return order ? order < 0 : strcmp(a.path, b.path) < 0;
        });
    return state;
}

SdFileResult::State SdFileScan::drain(Directory &directory)
{
    while (m_pendingCount)
    {
        // Kopie, weil Unterordner auf den frei gewordenen Platz gelegt werden.
        const Pending current = m_pending[--m_pendingCount];
        if (directory.cancelled())
            return State::Cancelled;
        if (++m_iterations > MaxIterations)
            return State::Limit;
        if (!directory.open(current.path))
        {
            directory.close();
            return State::Error;
        }
        State state = State::Data;
        Directory::Item item;
        while (state == State::Data && directory.next(item))
        {
            if (directory.cancelled())
                state = State::Cancelled;
            else if (++m_iterations > MaxIterations)
                state = State::Limit;
            else if (SystemEntries::hidden(item.name, item.hidden))
                continue;
            else if (item.directory)
            {
                if (current.depth >= MaxDepth)
                    ++m_skipped;
                else if (m_pendingCount >= MaxPending)
                    state = State::Limit;
                else
                {
                    auto &next = m_pending[m_pendingCount];
                    if (join(next.path, current.path, item.name) && FileExplorer::validPath(next.path))
                    {
                        next.depth = current.depth + 1;
                        ++m_pendingCount;
                    }
                    else
                        ++m_skipped;
                }
            }
            else if (FileExplorer::supportedName(item.name) && add(current.path, item) == Add::Full)
                state = State::Limit;
        }
        directory.close();
        if (state != State::Data)
            return state;
    }
    return State::Data;
}

SdFileResult::State SdFileScan::collect(Directory &directory, const SdFileEntry *selection, size_t count)
{
    reset();
    if (count > SdFileRequest::MaxSelection || (count && !selection))
        return State::Error;
    for (size_t i = 0; i < count; ++i)
    {
        const auto &item = selection[i];
        if (!FileExplorer::validPath(item.path) || !FileExplorer::validText(item.name, sizeof(item.name)))
            return State::Error;
        if (item.directory)
        {
            stringutils::safeCopy(m_pending[0].path, item.path);
            m_pending[0].depth = 0;
            m_pendingCount = 1;
            const State state = drain(directory);
            if (state != State::Data)
                return state;
        }
        else if (!FileExplorer::supportedFile(item.path))
            return State::Error;
        else if (m_count >= FileExplorer::MaxEntries)
            return State::Limit;
        else
        {
            m_entries[m_count] = item;
            keep();
        }
    }
    std::sort(m_entries, m_entries + m_count, [](const SdFileEntry &a, const SdFileEntry &b)
    {
        return treeOrder(a.path, b.path);
    });
    return State::Data;
}

bool SdFileScan::treeOrder(const char *a, const char *b)
{
    const char *const fullA = a, *const fullB = b;
    a += *a == '/';
    b += *b == '/';
    for (;;)
    {
        const char *const endA = strchr(a, '/'), *const endB = strchr(b, '/');
        // Der letzte Bestandteil ist die Datei, sie steht vor jedem Unterordner derselben Ebene.
        if (!endA != !endB)
            return !endA;
        const size_t lengthA = endA ? static_cast<size_t>(endA - a) : strlen(a), lengthB = endB ? static_cast<size_t>(endB - b) : strlen(b);
        int order = strncasecmp(a, b, std::min(lengthA, lengthB));
        if (!order)
            order = (lengthA > lengthB) - (lengthA < lengthB);
        if (order)
            return order < 0;
        if (!endA)
            return strcmp(fullA, fullB) < 0;
        a = endA + 1;
        b = endB + 1;
    }
}
