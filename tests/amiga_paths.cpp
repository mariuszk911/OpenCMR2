// Pure game-path helpers for the Amiga Sys layer: split, join and the
// one-'*' pattern match used by Sys_ListFiles.
#include "platform/amiga/amiga_paths.h"

#include <cstdio>
#include <cstring>

namespace {

int failures;

void Check(bool condition, const char *what)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

} // namespace

int main()
{
    AmigaGamePath path;
    char out[256];

    Check(AmigaPath_Split("c:\\Frontend\\menu.tga", &path) == 1, "split a drive path");
    Check(path.count == 2 && std::strcmp(path.parts[0], "Frontend") == 0 &&
              std::strcmp(path.parts[1], "menu.tga") == 0,
          "drive component and separators");

    // The game builds the saved controller path with a leading ".\".
    Check(AmigaPath_Split(".\\Configuration\\Controller.rcf", &path) == 1,
          "split controller config path");
    Check(path.count == 2 && std::strcmp(path.parts[0], "Configuration") == 0 &&
              std::strcmp(path.parts[1], "Controller.rcf") == 0,
          "controller config components");
    Check(AmigaPath_Join(out, sizeof(out), "PROGDIR:", &path, 0, path.count),
          "join controller config path");
    Check(std::strcmp(out, "PROGDIR:Configuration/Controller.rcf") == 0,
          "controller config join result");

    Check(AmigaPath_Split("a//b/./c/../d", &path) == 1, "split mixed separators");
    Check(path.count == 3 && std::strcmp(path.parts[0], "a") == 0 &&
              std::strcmp(path.parts[1], "b") == 0 && std::strcmp(path.parts[2], "d") == 0,
          "dot and dot-dot components");

    Check(AmigaPath_Split("..\\x", &path) == 1, "dot-dot at the root is dropped");
    Check(path.count == 1 && std::strcmp(path.parts[0], "x") == 0, "dot-dot at the root result");

    Check(AmigaPath_Split("", &path) == 0, "empty path is rejected");
    Check(AmigaPath_Split("c:", &path) == 0, "drive-only path is rejected");

    char longPart[AMIGA_PATH_PART_MAX + 1];
    std::memset(longPart, 'a', sizeof(longPart));
    longPart[sizeof(longPart) - 1] = '\0';
    Check(AmigaPath_Split(longPart, &path) == 0, "oversized component is rejected");

    Check(AmigaPath_Split("a/b/c", &path) == 1 && AmigaPath_Join(out, sizeof(out), "PROGDIR:", &path, 0, path.count),
          "join a relative path to an assign");
    Check(std::strcmp(out, "PROGDIR:a/b/c") == 0, "assign join result");

    Check(AmigaPath_Join(out, sizeof(out), "Work:Games/CMR2", &path, 0, path.count),
          "join to a volume path");
    Check(std::strcmp(out, "Work:Games/CMR2/a/b/c") == 0, "volume join result");

    Check(AmigaPath_Join(out, sizeof(out), "PROGDIR:Data/", &path, 0, path.count),
          "join to a directory with a trailing slash");
    Check(std::strcmp(out, "PROGDIR:Data/a/b/c") == 0, "trailing slash join result");

    Check(AmigaPath_Join(out, 4, "PROGDIR:", &path, 0, path.count) == 0,
          "short output buffer is rejected");

    Check(AmigaPath_Match("Slot1.SAV", "*.sav"), "extension match ignores case");
    Check(AmigaPath_Match("a.sav", "*.sav"), "extension match");
    Check(!AmigaPath_Match("a.sav.bak", "*.sav"), "suffix match is anchored");
    Check(AmigaPath_Match("save?.sav", "save?.sav"), "question mark is literal");
    Check(!AmigaPath_Match("save1.sav", "save?.sav"), "question mark is not a wildcard");
    Check(AmigaPath_Match("anything", "*"), "bare star matches everything");
    Check(AmigaPath_Match("abcdef", "abc*"), "prefix star match");
    Check(!AmigaPath_Match("ab", "abc*"), "short prefix does not match");
    Check(!AmigaPath_Match("x", "*.sav"), "empty head does not match a short name");

    if (failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("amiga paths: ok\n");
    return 0;
}
