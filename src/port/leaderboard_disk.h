/*
 * NetworkLeaderboards\leaderboards.nlb (0x2088 bytes).
 *
 * Native layout: two ints (leaderboard id, total boards) followed by 32
 * boards of { BYTE isLoaded; BYTE reserved[3]; 32 x { char name[4]; int wins } }.
 * Only the two header ints and every wins word are little-endian; the board
 * flags and names are bytes.
 */
#ifndef OPENCMR2_PORT_LEADERBOARD_DISK_H
#define OPENCMR2_PORT_LEADERBOARD_DISK_H

#include <stddef.h>

#define LEADERBOARD_DISK_SIZE 0x2088
#define LEADERBOARD_DISK_BOARDS 32
#define LEADERBOARD_DISK_PLAYERS 32
#define LEADERBOARD_DISK_BOARD_STRIDE 0x104

#ifdef __cplusplus
extern "C" {
#endif

/* Decodes a file image into the native NetworkLeaderboardsFile layout.
 * Returns 1 on success; 0 for a NULL/wrong-size input or a totalLeaderboards
 * outside 0..32 (the output is then untouched). */
int LeaderboardDisk_Decode(void *nativeFile, size_t nativeSize,
                           const void *file, size_t size);

/* Writes the native image as the little-endian file form. The buffers may be
 * the same pointer. Returns 1 on success, 0 for NULL/short buffers. */
int LeaderboardDisk_Pack(void *file, size_t fileSize,
                         const void *nativeFile, size_t nativeSize);

#ifdef __cplusplus
}
#endif

#endif
