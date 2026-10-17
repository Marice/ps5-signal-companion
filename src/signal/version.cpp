// Signal Companion - ps5-signal's version, read from an ELF file's marker, and compared.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "signal/version.hpp"

#include <vector>

namespace hui::signal
{

namespace
{

bool version_char(char c)
{
    return (c >= '0' && c <= '9') || c == '.';
}

std::vector<long> parts(std::string_view version)
{
    std::vector<long> numbers;
    long number = 0;
    for (const char c : version)
    {
        if (c == '.')
        {
            numbers.push_back(number);
            number = 0;
        }
        else
        {
            number = number * 10 + (c - '0');
        }
    }
    numbers.push_back(number);
    return numbers;
}

} // namespace

std::string payload_version(std::string_view elf)
{
    const std::size_t at = elf.find(kVersionMarker);
    if (at == std::string_view::npos)
        return {};
    std::size_t end = at + kVersionMarker.size();
    const std::size_t start = end;
    while (end < elf.size() && end - start < 32 && version_char(elf[end]))
        ++end;
    std::string version(elf.substr(start, end - start));
    // A version starts and ends with a digit.
    if (version.empty() || version.front() == '.' || version.back() == '.')
        return {};
    return version;
}

int compare_versions(std::string_view a, std::string_view b)
{
    if (a.empty() || b.empty())
        return a.empty() == b.empty() ? 0 : (a.empty() ? -1 : 1);
    const std::vector<long> left = parts(a);
    const std::vector<long> right = parts(b);
    for (std::size_t i = 0; i < left.size() || i < right.size(); ++i)
    {
        const long l = i < left.size() ? left[i] : 0;
        const long r = i < right.size() ? right[i] : 0;
        if (l != r)
            return l < r ? -1 : 1;
    }
    return 0;
}

} // namespace hui::signal
