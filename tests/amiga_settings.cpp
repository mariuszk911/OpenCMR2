// opencmr2.ini parsing and line-preserving rewrite, shared by the Amiga Sys
// settings implementation and this host test.
#include "platform/amiga/amiga_settings.h"

#include <cstdio>
#include <string>

namespace {

int failures;

void Check(bool condition, const char *what)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

bool Has(const std::string &text, const char *part)
{
    return text.find(part) != std::string::npos;
}

} // namespace

int main()
{
    std::map<std::string, std::string> settings;
    AmigaSettings::Parse(
        "; OpenCMR2\n"
        "[Install]\n"
        "Language = French\n"
        "; another comment\n"
        "[video]\n"
        "fullscreen = 0\n"
        "[VIDEO]\n"
        "Fullscreen = 1\n"
        "[paths]\n"
        "data = Work:Games/CMR2\n",
        settings);
    Check(settings["install.language"] == "French", "section and key are lowercased");
    Check(settings["video.fullscreen"] == "1", "later duplicate wins");
    Check(settings["paths.data"] == "Work:Games/CMR2", "value keeps its text");
    Check(settings.find(".comment") == settings.end(), "comments are ignored");

    std::string output;
    Check(AmigaSettings::Rewrite(
              "[video]\n; keep\nfullscreen = 0\nfullscreen = 0\n\n[install]\nlanguage = English\n",
              "video", "fullscreen", "1", &output),
          "rewrite an existing key");
    Check(Has(output, "; keep"), "comments are preserved");
    Check(Has(output, "fullscreen = 1\n"), "first match is replaced");
    Check(!Has(output, "fullscreen = 0"), "old values are gone");
    Check(Has(output, "[install]\nlanguage = English\n"), "other sections are preserved");
    Check(output.find("fullscreen = 1") == output.rfind("fullscreen = 1"),
          "duplicates are dropped");

    Check(AmigaSettings::Rewrite("[video]\n; keep\n", "video", "vsync", "1", &output),
          "insert into an existing section");
    Check(output == "[video]\nvsync = 1\n; keep\n", "insert position after the header");

    Check(AmigaSettings::Rewrite("[video]\nfullscreen = 0\n", "input", "port", "123", &output),
          "append a new section");
    Check(output == "[video]\nfullscreen = 0\n\n[input]\nport = 123\n", "appended section text");

    Check(AmigaSettings::Rewrite("", "input", "port", "123", &output), "rewrite empty contents");
    Check(output == "\n[input]\nport = 123\n", "empty contents text");

    Check(!AmigaSettings::Rewrite("[video]\n", "video", "x", "a\nb", &output),
          "line breaks in a value are rejected");

    Check(AmigaSettings::Rewrite("[video]\r\nfullscreen = 0\r\n", "video", "fullscreen", "1", &output),
          "rewrite CRLF input");
    Check(output == "[video]\nfullscreen = 1\n", "CRLF output is normalized");

    Check(AmigaSettings::Rewrite("[video]\nfullscreen = 0", "video", "fullscreen", "1", &output),
          "rewrite without a trailing newline");
    Check(output == "[video]\nfullscreen = 1\n", "unterminated last line text");

    Check(AmigaSettings::Rewrite(
              "[video]\nfullscreen = 0\n[x]\nfullscreen = 9\n[video]\nfullscreen = 2\n",
              "video", "fullscreen", "1", &output),
          "rewrite keys across two blocks");
    Check(!Has(output, "fullscreen = 0") && !Has(output, "fullscreen = 2"),
          "both target-section duplicates are dropped");
    Check(Has(output, "[x]\nfullscreen = 9\n"), "other sections keep the same key");

    if (failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("amiga settings: ok\n");
    return 0;
}
