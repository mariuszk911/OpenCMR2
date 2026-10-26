/*
 * Host-testable opencmr2.ini parsing and line-preserving rewriting.
 *
 * The format is the host's: [section] headers, "key = value", ';'/'#' comments,
 * later duplicates win when reading. Rewriting replaces the first matching key
 * in the target section, drops later duplicates and keeps every other line and
 * comment.
 */
#ifndef OPENCMR2_AMIGA_SETTINGS_H
#define OPENCMR2_AMIGA_SETTINGS_H

#include <map>
#include <string>
#include <vector>

namespace AmigaSettings {

inline std::string Lower(const std::string &text)
{
    std::string out = text;
    for (char &c : out) {
        if (c >= 'A' && c <= 'Z')
            c = (char)(c + ('a' - 'A'));
    }
    return out;
}

inline void Parse(const std::string &contents, std::map<std::string, std::string> &settings)
{
    settings.clear();
    std::string section;

    size_t position = 0;
    while (position < contents.size()) {
        const size_t newline = contents.find('\n', position);
        std::string line = contents.substr(position, newline == std::string::npos ? std::string::npos
                                                                                  : newline - position);
        position = newline == std::string::npos ? contents.size() : newline + 1;
        std::string text = line;
        while (!text.empty() &&
               (text.back() == '\n' || text.back() == '\r' || text.back() == ' '))
            text.pop_back();
        const size_t start = text.find_first_not_of(" \t");
        if (start == std::string::npos || text[start] == ';' || text[start] == '#')
            continue;
        text = text.substr(start);
        if (text[0] == '[') {
            section = Lower(text.substr(1, text.find(']') - 1));
            continue;
        }
        const size_t equals = text.find('=');
        if (equals == std::string::npos)
            continue;
        std::string key = text.substr(0, equals);
        std::string value = text.substr(equals + 1);
        while (!key.empty() && key.back() == ' ')
            key.pop_back();
        const size_t valueStart = value.find_first_not_of(' ');
        if (valueStart == std::string::npos)
            value.clear();
        else
            value.erase(0, valueStart);
        settings[section + "." + Lower(key)] = value;
    }
}

inline bool Rewrite(const std::string &contents, const std::string &section,
                    const std::string &key, const std::string &value,
                    std::string *output)
{
    if (value.find_first_of("\r\n") != std::string::npos)
        return false;

    const std::string setting = key + " = " + value;
    std::vector<std::string> lines;
    std::string current;
    size_t headerEnd = std::string::npos;
    bool written = false;

    size_t position = 0;
    while (position < contents.size()) {
        const size_t newline = contents.find('\n', position);
        std::string line = contents.substr(position, newline == std::string::npos ? std::string::npos
                                                                                  : newline - position);
        position = newline == std::string::npos ? contents.size() : newline + 1;
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        const size_t start = line.find_first_not_of(" \t");
        const std::string text = start == std::string::npos ? "" : line.substr(start);
        if (!text.empty() && text[0] == '[') {
            current = Lower(text.substr(1, text.find(']') - 1));
            lines.push_back(line);
            if (current == section && headerEnd == std::string::npos)
                headerEnd = lines.size();
            continue;
        }
        const size_t equals = text.find('=');
        if (current == section && equals != std::string::npos) {
            std::string name = text.substr(0, equals);
            const size_t end = name.find_last_not_of(" \t");
            if (end != std::string::npos && Lower(name.substr(0, end + 1)) == key) {
                if (!written)
                    lines.push_back(setting);
                written = true;
                continue;
            }
        }
        lines.push_back(line);
    }

    if (!written && headerEnd != std::string::npos)
        lines.insert(lines.begin() + headerEnd, setting);
    else if (!written)
        lines.insert(lines.end(), {"", "[" + section + "]", setting});

    output->clear();
    for (const std::string &entry : lines)
        *output += entry + '\n';
    return true;
}

} // namespace AmigaSettings

#endif
