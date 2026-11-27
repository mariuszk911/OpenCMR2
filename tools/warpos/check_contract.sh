#!/bin/sh
# WarpOS contract of a relocatable link, checked before conversion:
#   nothing undefined, no COMMON symbols, _start at .text+0, and only the
#   relocation types the converter's 68k stub applies.
# A failure names the offender; the fix is a compiler flag or the link order,
# never the converter.
#   check_contract.sh <tool-prefix> <file.warpelf>
set -eu
prefix=$1
elf=$2

undefined=$("${prefix}nm" --undefined-only "$elf")
if [ -n "$undefined" ]; then
    echo "WarpOS contract: unexpected undefined symbols in $elf:" >&2
    echo "$undefined" >&2
    exit 1
fi

if ! "${prefix}nm" --defined-only --format=posix "$elf" |
        awk '$1 == "_start" && $2 == "T" && $3 == "00000000" { found = 1 } END { exit found ? 0 : 1 }'; then
    echo "WarpOS contract: _start is not defined at .text+0 in $elf" >&2
    exit 1
fi

common=$("${prefix}nm" --format=posix "$elf" | awk '$2 == "C" { print $1 }')
if [ -n "$common" ]; then
    echo "WarpOS contract: unexpected COMMON symbols: $common" >&2
    exit 1
fi

bad=$("${prefix}readelf" --relocs --wide "$elf" |
      awk '$3 ~ /^R_PPC_/ && $3 !~ /^R_PPC_(ADDR32|ADDR16_LO|ADDR16_HA|REL24)$/ { print $3 }' | sort -u)
if [ -n "$bad" ]; then
    echo "WarpOS contract: unsupported relocations: $bad" >&2
    exit 1
fi
