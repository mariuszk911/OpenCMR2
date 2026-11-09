// render/gfx_api.inc must list exactly the Gfx_* functions port/gfx.h declares,
// with the same return types and parameter lists (compared as text, with
// whitespace folded). The Amiga dispatcher and both backends' prefixed exports
// are generated from that list.
#include <cctype>
#include <cstdio>
#include <fstream>
#include <map>
#include <regex>
#include <sstream>
#include <string>

#ifndef SOURCE_DIR
#error SOURCE_DIR is not defined
#endif

namespace {

int failures;

std::string Slurp(const std::string &path)
{
    std::ifstream in(path.c_str(), std::ios::binary);
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

std::string Fold(const std::string &text)
{
    std::string out;
    bool space = false;
    for (char c : text) {
        if (std::isspace((unsigned char)c)) {
            space = true;
            continue;
        }
        if (space && !out.empty() && c != ',' && c != ')' && out.back() != '(')
            out += ' ';
        space = false;
        out += c;
    }
    return out;
}

struct Entry {
    std::string ret;
    std::string params;
};

} // namespace

int main()
{
    std::string header = Slurp(std::string(SOURCE_DIR) + "/src/port/gfx.h");
    header = std::regex_replace(header, std::regex("/\\*[^]*?\\*/"), "");
    std::map<std::string, Entry> fromHeader;
    const std::regex declaration("([A-Za-z_][\\w \\*]*?)\\s*\\b(Gfx_\\w+)\\s*\\(([^;{}]*?)\\)\\s*;");
    for (std::sregex_iterator it(header.begin(), header.end(), declaration), end; it != end; ++it) {
        if ((*it)[1].str().find("typedef") == 0)
            continue;
        std::string params = Fold((*it)[3].str());
        fromHeader[(*it)[2].str()] = Entry{Fold((*it)[1].str()), params};
    }

    const std::string list = Slurp(std::string(SOURCE_DIR) + "/src/render/gfx_api.inc");
    std::map<std::string, Entry> fromList;
    const std::regex row("GFX_API\\(([^,]+), (Gfx_\\w+), \\(([^\\n]*?)\\), \\(([^\\n]*)\\)\\)");
    for (std::sregex_iterator it(list.begin(), list.end(), row), end; it != end; ++it) {
        const std::string name = (*it)[2].str();
        if (fromList.count(name)) {
            std::fprintf(stderr, "FAIL: %s is listed twice\n", name.c_str());
            ++failures;
        }
        fromList[name] = Entry{Fold((*it)[1].str()), Fold((*it)[3].str())};
    }

    if (fromHeader.size() < 90) {
        std::fprintf(stderr, "FAIL: only %zu declarations parsed from gfx.h\n", fromHeader.size());
        ++failures;
    }
    for (const auto &item : fromHeader) {
        auto found = fromList.find(item.first);
        if (found == fromList.end()) {
            std::fprintf(stderr, "FAIL: %s is in gfx.h but not in gfx_api.inc (run tools/gen_gfx_api.py)\n",
                         item.first.c_str());
            ++failures;
        } else if (found->second.ret != item.second.ret || found->second.params != item.second.params) {
            std::fprintf(stderr, "FAIL: %s differs: header '%s (%s)', list '%s (%s)'\n", item.first.c_str(),
                         item.second.ret.c_str(), item.second.params.c_str(), found->second.ret.c_str(),
                         found->second.params.c_str());
            ++failures;
        }
    }
    for (const auto &item : fromList) {
        if (!fromHeader.count(item.first)) {
            std::fprintf(stderr, "FAIL: %s is in gfx_api.inc but not in gfx.h\n", item.first.c_str());
            ++failures;
        }
    }
    if (failures == 0)
        std::printf("gfx_api_list: %zu entries match gfx.h\n", fromList.size());
    return failures != 0;
}
