// Wording and once-per-run policy of the refused-save requester.
#include "platform/amiga/amiga_save_notice.h"

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
    Check(AmigaSaveNotice_Text(SAVED_IMAGE_OK) == NULL, "an accepted image has no notice");
    Check(AmigaSaveNotice_Text(SAVED_IMAGE_BAD_ARGUMENT) == NULL, "a caller error has no notice");
    Check(AmigaSaveNotice_Text(SAVED_IMAGE_BAD_SIZE) == NULL, "a wrong size has no notice");

    const SavedImageStatus refused[] = {SAVED_IMAGE_FOREIGN, SAVED_IMAGE_UNSUPPORTED, SAVED_IMAGE_CORRUPT};
    for (SavedImageStatus status : refused) {
        const char *text = AmigaSaveNotice_Text(status);
        Check(text != NULL && LinesFit(text), "every refusal has a requester text that fits");
    }
    Check(std::strstr(AmigaSaveNotice_Text(SAVED_IMAGE_FOREIGN), "PC version") != NULL,
          "the PC notice says why");
    Check(std::strcmp(AmigaSaveNotice_Text(SAVED_IMAGE_FOREIGN), AmigaSaveNotice_Text(SAVED_IMAGE_CORRUPT)) != 0,
          "the notices differ");

    unsigned announced = 0;
    Check(AmigaSaveNotice_ShouldShow(SAVED_IMAGE_FOREIGN, &announced), "the first PC save is announced");
    Check(!AmigaSaveNotice_ShouldShow(SAVED_IMAGE_FOREIGN, &announced), "the second PC save is not");
    Check(AmigaSaveNotice_ShouldShow(SAVED_IMAGE_CORRUPT, &announced), "another kind is still announced");
    Check(!AmigaSaveNotice_ShouldShow(SAVED_IMAGE_CORRUPT, &announced), "and only once");
    Check(AmigaSaveNotice_ShouldShow(SAVED_IMAGE_UNSUPPORTED, &announced), "the third kind is announced");
    Check(!AmigaSaveNotice_ShouldShow(SAVED_IMAGE_OK, &announced) &&
              !AmigaSaveNotice_ShouldShow(SAVED_IMAGE_BAD_SIZE, &announced),
          "kinds without a text never consume the policy");

    if (failures != 0) {
        std::fprintf(stderr, "amiga_save_notice: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("amiga_save_notice: OK\n");
    return 0;
}
