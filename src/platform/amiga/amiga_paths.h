/*
 * Pure game-path helpers for the AmigaOS Sys layer.
 *
 * Game paths are the original's DOS-style paths: drive letters are dropped,
 * both separators are accepted, "." is ignored and ".." pops a component.
 * The Amiga filesystems are case-insensitive, so this header only normalizes
 * the path; the lookup itself is a plain AmigaDOS Lock/Open. The fixed caps
 * (8 components of 31 characters) match the original's short paths and keep
 * the stack buffers small.
 */
#ifndef OPENCMR2_AMIGA_PATHS_H
#define OPENCMR2_AMIGA_PATHS_H

#include <stddef.h>
#include <string.h>

#define AMIGA_PATH_MAX_PARTS 8
#define AMIGA_PATH_PART_MAX 32

typedef struct AmigaGamePath {
    char parts[AMIGA_PATH_MAX_PARTS][AMIGA_PATH_PART_MAX];
    int count;
} AmigaGamePath;

static inline char AmigaPath_Lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c + ('a' - 'A')) : c;
}

/* Fills out and returns 1, or returns 0 for an empty/oversized path. */
static inline int AmigaPath_Split(const char *path, AmigaGamePath *out)
{
    const char *p = path;
    char part[AMIGA_PATH_PART_MAX];
    size_t len = 0;

    out->count = 0;
    if (p == NULL)
        return 0;
    if (p[0] != '\0' && p[1] == ':')
        p += 2;
    for (;;) {
        if (*p == '\\' || *p == '/' || *p == '\0') {
            if (len == 1 && part[0] == '.') {
                /* current directory */
            } else if (len == 2 && part[0] == '.' && part[1] == '.') {
                if (out->count > 0)
                    --out->count;
            } else if (len > 0) {
                if (out->count >= AMIGA_PATH_MAX_PARTS || len >= AMIGA_PATH_PART_MAX)
                    return 0;
                memcpy(out->parts[out->count], part, len);
                out->parts[out->count][len] = '\0';
                ++out->count;
            }
            len = 0;
            if (*p == '\0')
                break;
        } else {
            if (len >= AMIGA_PATH_PART_MAX - 1)
                return 0;
            part[len++] = *p;
        }
        ++p;
    }
    return out->count > 0;
}

/* Joins root and parts[start..end) with '/'; returns 1 on success. */
static inline int AmigaPath_Join(char *out, size_t size, const char *root,
                                 const AmigaGamePath *path, int start, int end)
{
    size_t len = 0;

    if (root == NULL)
        root = "";
    while (root[len] != '\0') {
        if (len + 1 >= size)
            return 0;
        out[len] = root[len];
        ++len;
    }
    for (int i = start; i < end; ++i) {
        const char *part;
        if (i < 0 || i >= path->count)
            return 0;
        if (len > 0 && out[len - 1] != ':' && out[len - 1] != '/') {
            if (len + 1 >= size)
                return 0;
            out[len++] = '/';
        }
        part = path->parts[i];
        while (*part != '\0') {
            if (len + 1 >= size)
                return 0;
            out[len++] = *part++;
        }
    }
    if (len >= size)
        return 0;
    out[len] = '\0';
    return 1;
}

/* One '*' at most, compared without regard to case ("*.sav"). */
static inline int AmigaPath_Match(const char *name, const char *pattern)
{
    size_t nameLen = strlen(name);
    size_t patternLen = strlen(pattern);
    size_t star = patternLen;
    size_t headLen;
    size_t tailLen;

    for (size_t i = 0; i < patternLen; ++i) {
        if (pattern[i] == '*') {
            star = i;
            break;
        }
    }
    if (star == patternLen) {
        if (nameLen != patternLen)
            return 0;
        for (size_t i = 0; i < nameLen; ++i)
            if (AmigaPath_Lower(name[i]) != AmigaPath_Lower(pattern[i]))
                return 0;
        return 1;
    }
    headLen = star;
    tailLen = patternLen - star - 1;
    if (nameLen < headLen + tailLen)
        return 0;
    for (size_t i = 0; i < headLen; ++i)
        if (AmigaPath_Lower(name[i]) != AmigaPath_Lower(pattern[i]))
            return 0;
    for (size_t i = 0; i < tailLen; ++i)
        if (AmigaPath_Lower(name[nameLen - tailLen + i]) !=
            AmigaPath_Lower(pattern[star + 1 + i]))
            return 0;
    return 1;
}

/*
 * Transactional directories. A file written below one of them is first
 * written to "<name>.tmp" and only replaces the real file once it was closed
 * without a write error, so an interrupted or failed save never damages the
 * previous one. An interrupted commit leaves "<name>.bak" (the previous file)
 * and/or "<name>.tmp" behind; Sys_ListFiles repairs the directory.
 * Only the saved-game drawer qualifies: other writers (the log, replays,
 * configuration) keep the plain create-and-truncate semantics.
 */
#define AMIGA_STAGING_SUFFIX ".tmp"
#define AMIGA_BACKUP_SUFFIX ".bak"
#define AMIGA_SUFFIX_MAX 4

/* Returns 1 when path (a file or a directory) lies in the saved-game drawer. */
static inline int AmigaPath_InTransactionalDir(const AmigaGamePath *path)
{
    static const char drawer[] = "gamesave";
    size_t i;

    if (path->count < 1)
        return 0;
    for (i = 0; drawer[i] != '\0'; ++i) {
        if (AmigaPath_Lower(path->parts[0][i]) != drawer[i])
            return 0;
    }
    return path->parts[0][i] == '\0';
}

/* The staging and backup names must still be single path components, which
   is also what the directory listing used for repairs can see. */
static inline int AmigaPath_CanStage(const AmigaGamePath *path)
{
    if (path->count < 2)
        return 0;
    return strlen(path->parts[path->count - 1]) + AMIGA_SUFFIX_MAX < AMIGA_PATH_PART_MAX;
}

#endif
