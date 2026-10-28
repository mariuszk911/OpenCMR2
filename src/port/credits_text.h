#ifndef OPENCMR2_PORT_CREDITS_TEXT_H
#define OPENCMR2_PORT_CREDITS_TEXT_H

#include <stddef.h>

/*
 * Credits files are a sequence of quoted "role", "name" pairs, one pair per
 * line. Parsing is in place: each quoted string is terminated and entries
 * receives pointers into the buffer. The scan is bounded by size, so a missing
 * final NUL, a truncated pair or a missing closing quote cannot read past the
 * file. Returns the number of complete pairs written (at most capacity); a
 * malformed or truncated pair ends the scan.
 *
 * Fork-owned: game/GameInfo.cpp only binds the result to its tables.
 */
size_t CreditsText_Parse(char *buffer, size_t size, char **entries, size_t capacity);

#endif
