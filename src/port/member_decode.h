/*
 * Archive members the game reads through raw pointers although their records
 * are little-endian (and, on 64-bit hosts, larger in memory). CGenericFileLoader::
 * FindFile hands every member it finds to MemberDecode_Find; the game then
 * sees native data without any decoding of its own, so the loading code in
 * game/ stays as upstream wrote it.
 *
 * To add a format: write its decoder (port/*_disk.cpp), list its extension in
 * member_decode.cpp, and remove the PORT: AMIGA hook from its load site.
 */
#ifndef OPENCMR2_PORT_MEMBER_DECODE_H
#define OPENCMR2_PORT_MEMBER_DECODE_H

#include "port/types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* name: the member's file name, lower case. data and size: the member in its
 * archive. Returns data for a member without a decoder; a native copy owned by
 * the archive's buffer (Disk_AttachToBuffer) for one with; NULL when the
 * decoder rejects the member, as if it were missing. */
void *MemberDecode_Find(const char *name, void *data, DWORD size);

/* Standalone files (CFileBuffer::GetGenericFileBuffer): the same idea, but the
 * game owns the buffer, so the file is decoded in place. Returns 1 for a file
 * without a decoder or one decoded, 0 when the decoder rejects it (the caller
 * frees the buffer and reports the file as missing). name may be a full path in
 * any case. */
int FileDecode_InPlace(const char *name, void *data, DWORD size);

#ifdef __cplusplus
}
#endif

#endif
