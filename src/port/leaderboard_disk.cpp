// .nlb leaderboard codec, see port/leaderboard_disk.h.

#include "port/leaderboard_disk.h"

#include "port/endian.h"

#include <stdint.h>
#include <string.h>

namespace {

const size_t kHeaderSize = 8;
const size_t kEntryStride = 8;
const size_t kBoardHeaderSize = 4;
const size_t kWinsInEntry = 4;

size_t WinsOffset(size_t board, size_t entry)
{
    return kHeaderSize + board * LEADERBOARD_DISK_BOARD_STRIDE +
           kBoardHeaderSize + entry * kEntryStride + kWinsInEntry;
}

void StoreHost32(void *destination, uint32_t value)
{
    memcpy(destination, &value, sizeof(value));
}

} // namespace

extern "C" int LeaderboardDisk_Decode(void *nativeFile, size_t nativeSize,
                                      const void *file, size_t size)
{
    const unsigned char *src = (const unsigned char *)file;
    unsigned char *dst = (unsigned char *)nativeFile;
    int32_t id;
    int32_t total;
    size_t board;
    size_t entry;

    if (nativeFile == NULL || file == NULL || size != LEADERBOARD_DISK_SIZE ||
            nativeSize < LEADERBOARD_DISK_SIZE)
        return 0;
    id = (int32_t)ReadLE32(src + 0);
    total = (int32_t)ReadLE32(src + 4);
    if (total < 0 || total > LEADERBOARD_DISK_BOARDS)
        return 0;

    memmove(dst, src, size);
    StoreHost32(dst + 0, (uint32_t)id);
    StoreHost32(dst + 4, (uint32_t)total);
    for (board = 0; board < LEADERBOARD_DISK_BOARDS; board++) {
        for (entry = 0; entry < LEADERBOARD_DISK_PLAYERS; entry++) {
            size_t offset = WinsOffset(board, entry);
            StoreHost32(dst + offset, ReadLE32(src + offset));
        }
    }
    return 1;
}

extern "C" int LeaderboardDisk_Pack(void *file, size_t fileSize,
                                    const void *nativeFile, size_t nativeSize)
{
    const unsigned char *src = (const unsigned char *)nativeFile;
    unsigned char *dst = (unsigned char *)file;
    int32_t id;
    int32_t total;
    size_t board;
    size_t entry;

    if (file == NULL || nativeFile == NULL || fileSize < LEADERBOARD_DISK_SIZE ||
            nativeSize < LEADERBOARD_DISK_SIZE)
        return 0;

    memcpy(&id, src + 0, sizeof(id));
    memcpy(&total, src + 4, sizeof(total));
    memmove(dst, src, LEADERBOARD_DISK_SIZE);
    WriteLE32(dst + 0, (uint32_t)id);
    WriteLE32(dst + 4, (uint32_t)total);
    for (board = 0; board < LEADERBOARD_DISK_BOARDS; board++) {
        for (entry = 0; entry < LEADERBOARD_DISK_PLAYERS; entry++) {
            size_t offset = WinsOffset(board, entry);
            uint32_t wins;
            memcpy(&wins, src + offset, sizeof(wins));
            WriteLE32(dst + offset, wins);
        }
    }
    return 1;
}
