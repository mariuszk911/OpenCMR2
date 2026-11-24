// Wording and version rule of the requester shown when MiniGL cannot start.
#include "platform/amiga/amiga_minigl_notice.h"

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

// The requester is 400 pixels wide and the system font is 8 pixels.
bool LinesFit(const char *text)
{
    size_t length = 0;
    int lines = 1;
    for (const char *p = text; *p != '\0'; ++p) {
        if (*p == '\n') {
            ++lines;
            length = 0;
        } else if (++length > 46) {
            return false;
        }
    }
    return lines <= 16;
}

} // namespace

int main()
{
    Check(AmigaMiniGLNotice_IsTooOld(28, 9), "28.9 is too old");
    Check(AmigaMiniGLNotice_IsTooOld(29, 0), "29.0 is too old (dispatch table one slot short)");
    Check(AmigaMiniGLNotice_IsTooOld(29, 1), "29.1 is too old (present path can stall)");
    Check(AmigaMiniGLNotice_IsTooOld(29, 5), "a later 29.x is too old");
    Check(!AmigaMiniGLNotice_IsTooOld(30, 0), "30.0 is accepted");
    Check(!AmigaMiniGLNotice_IsTooOld(31, 0), "a later 30.x/31.x is accepted");

    char missing[256];
    char old[256];
    char other[256];
    AmigaMiniGLNotice_Text(missing, sizeof(missing), 0, 0, 0);
    AmigaMiniGLNotice_Text(old, sizeof(old), 1, 29, 0);
    AmigaMiniGLNotice_Text(other, sizeof(other), 1, 30, 0);
    Check(LinesFit(missing) && LinesFit(old) && LinesFit(other), "every text fits the requester");
    Check(std::strstr(missing, "not found") != NULL && std::strstr(missing, "LIBS:") != NULL,
          "the missing text says where to install it");
    Check(std::strstr(missing, "Aminet") != NULL, "the missing text names the Aminet package");
    Check(std::strstr(old, "29.0 is too old") != NULL, "the old text names the version found");
    Check(std::strstr(old, "30.0") != NULL, "the old text names the version needed");
    Check(std::strstr(other, "30.0 could not") != NULL, "a recent library that fails says so");

    char tiny[8];
    AmigaMiniGLNotice_Text(tiny, sizeof(tiny), 0, 0, 0);
    Check(std::strlen(tiny) == sizeof(tiny) - 1, "a short buffer is truncated, not overrun");

    if (failures == 0)
        std::printf("amiga_minigl_notice: OK\n");
    return failures == 0 ? 0 : 1;
}
