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

bool is_ours(const std::string &line)
{
    return line == kAutoloadName;
}

} // namespace

std::string autoload_with(std::string_view list)
{
    bool crlf = false;
    std::vector<std::string> lines = split(list, crlf);
    for (const std::string &line : lines)
    {
        if (is_ours(line))
            return std::string(list); // already in it
    }
    while (!lines.empty() && lines.back().empty())
        lines.pop_back();
    // A wait after the payload before it; nothing to wait for in an empty list.
    if (!lines.empty())
        lines.push_back("!" + std::to_string(kAutoloadDelayMs));
    lines.emplace_back(kAutoloadName);
    return join(lines, crlf);
}

std::string autoload_without(std::string_view list)
{
    bool crlf = false;
    const std::vector<std::string> lines = split(list, crlf);
    std::vector<std::string> kept;
    for (const std::string &line : lines)
    {
        if (!is_ours(line))
        {
            kept.push_back(line);
            continue;
        }
        // The wait before it was there for it.
        if (!kept.empty() && !kept.back().empty() && kept.back()[0] == '!')
            kept.pop_back();
    }
    return join(kept, crlf);
}

AutoloadState autoload_state(const std::string &directory)
{
    AutoloadState state;
    std::string text;
    if (!save::read_file(list_path(directory), &text, kMaxList))
        return state;
    state.found = true;
    bool crlf = false;
    for (const std::string &line : split(text, crlf))
        state.listed = state.listed || is_ours(line);
    return state;
}

std::string autoload_add(const std::string &directory)
{
    std::string list;
    if (!save::read_file(list_path(directory), &list, kMaxList))
        return "autoload.txt can't be read";
    std::string elf;
    if (!save::read_file(assets_path() + "/payload/ps5-signal.elf", &elf, kMaxPayload))
        return "This build does not include ps5-signal.elf";
    // The copy first: the list must never name a file that isn't there.
    std::string error = save::write_atomic(directory + "/" + kAutoloadName, elf);
    if (!error.empty())
        return error;
    return save::write_atomic(list_path(directory), autoload_with(list));
}

AutoloadRefresh autoload_refresh(const std::string &directory)
{
    AutoloadRefresh refresh;
    if (!autoload_state(directory).listed)
        return refresh;
    std::string elf;
    if (!save::read_file(assets_path() + "/payload/ps5-signal.elf", &elf, kMaxPayload))
        return refresh;
    refresh.to = payload_version(elf);
    if (refresh.to.empty())
        return refresh;
    const std::string path = directory + "/" + kAutoloadName;
    std::string copy;
    const bool found = save::read_file(path, &copy, kMaxPayload);
    refresh.from = found ? payload_version(copy) : std::string();
    if (found && compare_versions(refresh.from, refresh.to) >= 0)
        return refresh;
    refresh.error = save::write_atomic(path, elf);
    refresh.updated = refresh.error.empty();
    return refresh;
}

std::string autoload_remove(const std::string &directory)
{
    std::string list;
    if (!save::read_file(list_path(directory), &list, kMaxList))
        return "autoload.txt can't be read";
    const std::string error = save::write_atomic(list_path(directory), autoload_without(list));
    if (!error.empty())
        return error;
    std::remove((directory + "/" + kAutoloadName).c_str());
    return {};
}

} // namespace hui::signal
