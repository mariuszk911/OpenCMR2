// .nlb leaderboards: round-trip of the native image and LE file form.
#include "NetworkLeaderboards.h"
#include "port/endian.h"
#include "port/leaderboard_disk.h"

#include <stdio.h>
#include <string.h>

static void PutLE32(unsigned char *p, unsigned int value)
{
#if defined(OPENCMR2_TEST_FORCE_BIG_ENDIAN) && defined(OPENCMR2_LITTLE_ENDIAN)
    p[0] = (unsigned char)((value >> 24) & 0xff);
    p[1] = (unsigned char)((value >> 16) & 0xff);
    p[2] = (unsigned char)((value >> 8) & 0xff);
    p[3] = (unsigned char)(value & 0xff);
#else
    p[0] = (unsigned char)(value & 0xff);
    p[1] = (unsigned char)((value >> 8) & 0xff);
    p[2] = (unsigned char)((value >> 16) & 0xff);
    p[3] = (unsigned char)((value >> 24) & 0xff);
#endif
}

static unsigned int GetHost32(const unsigned char *p)
{
    unsigned int value;
    memcpy(&value, p, sizeof(value));
    return value;
}

static int Fail(const char *what, int line)
{
    printf("leaderboard: FAIL %s (line %d)\n", what, line);
    return 1;
}

#define CHECK(expr) do { if (!(expr)) return Fail(#expr, __LINE__); } while (0)

static void FillNative(NetworkLeaderboardsFile *image)
{
    int board;
    int entry;

    memset(image, 0, sizeof(*image));
    image->leaderboardID = 3;
    image->totalLeaderboards = 2;
    for (board = 0; board < LEADERBOARD_DISK_BOARDS; board++) {
        image->leaderboards[board].isLoaded = (BYTE)(board < 2 ? 1 : 0);
        for (entry = 0; entry < LEADERBOARD_DISK_PLAYERS; entry++) {
            image->leaderboards[board].entries[entry].name[0] = (char)('A' + (board + entry) % 26);
            image->leaderboards[board].entries[entry].name[1] = (char)('a' + entry % 26);
            image->leaderboards[board].entries[entry].name[2] = '\0';
            image->leaderboards[board].entries[entry].wins = board * 100 + entry;
        }
    }
}

static int CheckNative(const NetworkLeaderboardsFile *image)
{
    int board;
    int entry;

    CHECK(image->leaderboardID == 3);
    CHECK(image->totalLeaderboards == 2);
    for (board = 0; board < LEADERBOARD_DISK_BOARDS; board++) {
        CHECK(image->leaderboards[board].isLoaded == (BYTE)(board < 2 ? 1 : 0));
        for (entry = 0; entry < LEADERBOARD_DISK_PLAYERS; entry++) {
            CHECK(image->leaderboards[board].entries[entry].name[0] == (char)('A' + (board + entry) % 26));
            CHECK(image->leaderboards[board].entries[entry].name[1] == (char)('a' + entry % 26));
            CHECK(image->leaderboards[board].entries[entry].wins == board * 100 + entry);
        }
    }
    return 0;
}

int main(void)
{
    NetworkLeaderboardsFile native;
    NetworkLeaderboardsFile decoded;
    unsigned char file[LEADERBOARD_DISK_SIZE];
    unsigned char inplace[LEADERBOARD_DISK_SIZE];
    int board;
    int entry;

    CHECK(sizeof(NetworkLeaderboardEntry) == 8);
    CHECK(sizeof(NetworkLeaderboard) == LEADERBOARD_DISK_BOARD_STRIDE);
    CHECK(sizeof(NetworkLeaderboardsFile) == LEADERBOARD_DISK_SIZE);

    FillNative(&native);
    memset(file, 0, sizeof(file));
    CHECK(LeaderboardDisk_Pack(file, sizeof(file), &native, sizeof(native)) != 0);

    // The packed header and every wins word are little-endian on the wire.
    CHECK(ReadLE32(file + 0) == 3);
    CHECK(ReadLE32(file + 4) == 2);
    for (board = 0; board < LEADERBOARD_DISK_BOARDS; board++) {
        for (entry = 0; entry < LEADERBOARD_DISK_PLAYERS; entry++) {
            size_t offset = 8 + (size_t)board * LEADERBOARD_DISK_BOARD_STRIDE + 4 +
                            (size_t)entry * 8 + 4;
            CHECK(ReadLE32(file + offset) == (unsigned)(board * 100 + entry));
        }
    }

    memset(&decoded, 0, sizeof(decoded));
    CHECK(LeaderboardDisk_Decode(&decoded, sizeof(decoded), file, sizeof(file)) != 0);
    if (CheckNative(&decoded))
        return 1;

    // In-place pack and decode (the file buffer path).
    memcpy(inplace, &native, sizeof(inplace));
    CHECK(LeaderboardDisk_Pack(inplace, sizeof(inplace), inplace, sizeof(inplace)) != 0);
    CHECK(LeaderboardDisk_Decode(inplace, sizeof(inplace), inplace, sizeof(inplace)) != 0);
    memcpy(&decoded, inplace, sizeof(decoded));
    if (CheckNative(&decoded))
        return 1;

    // A total outside 0..32 is rejected without touching the output.
    memcpy(file, &native, sizeof(file));
    memset(file, 0, sizeof(file));
    PutLE32(file + 0, 3);
    PutLE32(file + 4, 33);
    memset(&decoded, 0x5a, sizeof(decoded));
    CHECK(LeaderboardDisk_Decode(&decoded, sizeof(decoded), file, sizeof(file)) == 0);
    CHECK(decoded.leaderboardID == 0x5a5a5a5a);
    PutLE32(file + 4, (unsigned)-1);
    CHECK(LeaderboardDisk_Decode(&decoded, sizeof(decoded), file, sizeof(file)) == 0);

    CHECK(LeaderboardDisk_Decode(&decoded, sizeof(decoded), file, sizeof(file) - 1) == 0);
    CHECK(LeaderboardDisk_Decode(NULL, sizeof(decoded), file, sizeof(file)) == 0);
    CHECK(LeaderboardDisk_Decode(&decoded, sizeof(decoded), NULL, sizeof(file)) == 0);
    CHECK(LeaderboardDisk_Pack(NULL, sizeof(file), &native, sizeof(native)) == 0);
    CHECK(LeaderboardDisk_Pack(file, sizeof(file) - 1, &native, sizeof(native)) == 0);

    printf("leaderboard: OK\n");
    return 0;
}
