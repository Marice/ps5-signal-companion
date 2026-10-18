// Signal Companion - ps5-signal in ps5-autoloader's list, so it starts with the console.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "signal/autoload.hpp"

#include "core/save_file.hpp"
#include "signal/service.hpp"
#include "signal/version.hpp"

#include <cstdio>
#include <vector>

namespace hui::signal
{

namespace
{

constexpr std::size_t kMaxList = 64 * 1024;
constexpr std::size_t kMaxPayload = 4u << 20;

std::string list_path(const std::string &directory)
{
    return directory + "/autoload.txt";
}

// The lines without their endings; crlf says whether the file used "\r\n".
std::vector<std::string> split(std::string_view text, bool &crlf)
{
    crlf = text.find("\r\n") != std::string_view::npos;
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start < text.size())
    {
        std::size_t end = text.find('\n', start);
        if (end == std::string_view::npos)
            end = text.size();
        std::string line(text.substr(start, end - start));
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        lines.push_back(std::move(line));
        start = end + 1;
    }
    return lines;
}

std::string join(const std::vector<std::string> &lines, bool crlf)
{
    std::string out;
    for (const std::string &line : lines)
        out += line + (crlf ? "\r\n" : "\n");
    return out;
}

// The suite's order in the list.
constexpr std::string_view kSuiteOrder[] = {kExporterPayload.name, kHomeAssistantPayload.name,
                                            kSignalPayload.name};

int suite_rank(std::string_view name)
{
    for (int i = 0; i < 3; ++i)
    {
        if (kSuiteOrder[i] == name)
            return i;
    }
    return -1;
}

bool is_wait(const std::string &line)
{
    return !line.empty() && line[0] == '!';
}

} // namespace

std::string autoload_with(std::string_view list, std::string_view name)
{
    bool crlf = false;
    std::vector<std::string> lines = split(list, crlf);
    for (const std::string &line : lines)
    {
        if (line == name)
            return std::string(list); // already in it
    }
    while (!lines.empty() && lines.back().empty())
        lines.pop_back();
    const std::string wait = "!" + std::to_string(kAutoloadDelayMs);
    // Before the first suite payload that comes after this one: it and a wait go
    // where that payload's line is, so the wait before that payload stays in front.
    const int rank = suite_rank(name);
    if (rank >= 0)
    {
        for (std::size_t i = 0; i < lines.size(); ++i)
        {
            if (suite_rank(lines[i]) > rank)
            {
                lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(i),
                             {std::string(name), wait});
                return join(lines, crlf);
            }
        }
    }
    // Last: a wait after the payload before it; nothing to wait for in an empty list.
    if (!lines.empty())
        lines.push_back(wait);
    lines.emplace_back(name);
    return join(lines, crlf);
}

std::string autoload_without(std::string_view list, std::string_view name)
{
    bool crlf = false;
    const std::vector<std::string> lines = split(list, crlf);
    std::vector<std::string> kept;
    for (const std::string &line : lines)
    {
        if (line != name)
        {
            kept.push_back(line);
            continue;
        }
        // The wait before it was there for it.
        if (!kept.empty() && is_wait(kept.back()))
            kept.pop_back();
    }
    // Taken from the front: the wait after it has nothing to wait for.
    while (!kept.empty() && is_wait(kept.front()))
        kept.erase(kept.begin());
    return join(kept, crlf);
}

AutoloadState autoload_state(const std::string &directory, const AutoloadPayload &payload)
{
    AutoloadState state;
    std::string text;
    if (!save::read_file(list_path(directory), &text, kMaxList))
        return state;
    state.found = true;
    bool crlf = false;
    for (const std::string &line : split(text, crlf))
        state.listed = state.listed || line == payload.name;
    return state;
}

std::string autoload_add(const std::string &directory, const AutoloadPayload &payload)
{
    std::string list;
    if (!save::read_file(list_path(directory), &list, kMaxList))
        return "autoload.txt can't be read";
    std::string elf;
    if (!save::read_file(assets_path() + "/payload/" + payload.name, &elf, kMaxPayload))
        return std::string("This build does not include ") + payload.name;
    // The copy first: the list must never name a file that isn't there.
    std::string error = save::write_atomic(directory + "/" + payload.name, elf);
    if (!error.empty())
        return error;
    return save::write_atomic(list_path(directory), autoload_with(list, payload.name));
}

AutoloadRefresh autoload_refresh(const std::string &directory, const AutoloadPayload &payload)
{
    AutoloadRefresh refresh;
    if (!autoload_state(directory, payload).listed)
        return refresh;
    std::string elf;
    if (!save::read_file(assets_path() + "/payload/" + payload.name, &elf, kMaxPayload))
        return refresh;
    refresh.to = payload_version(elf, payload.marker);
    if (refresh.to.empty())
        return refresh;
    const std::string path = directory + "/" + payload.name;
    std::string copy;
    const bool found = save::read_file(path, &copy, kMaxPayload);
    refresh.from = found ? payload_version(copy, payload.marker) : std::string();
    if (found && compare_versions(refresh.from, refresh.to) >= 0)
        return refresh;
    refresh.error = save::write_atomic(path, elf);
    refresh.updated = refresh.error.empty();
    return refresh;
}

std::string autoload_remove(const std::string &directory, const AutoloadPayload &payload)
{
    std::string list;
    if (!save::read_file(list_path(directory), &list, kMaxList))
        return "autoload.txt can't be read";
    const std::string error =
        save::write_atomic(list_path(directory), autoload_without(list, payload.name));
    if (!error.empty())
        return error;
    std::remove((directory + "/" + payload.name).c_str());
    return {};
}

} // namespace hui::signal
