#!/bin/sh
# Archives the WarpOS release drawer dist/warpos as
# dist/OpenCMR2-warpos-<version>.lha and .zip, with everything inside a
# top-level OpenCMR2/ drawer. The version is read from the executable's $VER
# string. Run tools/warpos/build.sh --clean first.
#
# The MiniGL PPC stack (minigl_ppc.dll, minigl_ppc_host) is a requirement
# named in the README, not part of the archive.
#
# The archive is made with LHa for UNIX from the Amiga toolchain image
# (IMAGE, as in tools/amiga/build.sh): distributions often ship Lhasa as
# "lha", which can only extract.
set -eu

REPO=$(cd "$(dirname "$0")/../.." && pwd)
DIST="$REPO/dist/warpos"
IMAGE=${IMAGE:-opencmr2/amiga-toolchain:latest}

fail() { printf 'package.sh: %s\n' "$1" >&2; exit 1; }

command -v docker >/dev/null 2>&1 || fail "docker not found"
[ -f "$DIST/opencmr2" ] || fail "no $DIST/opencmr2; run tools/warpos/build.sh --clean"
for file in OpenCMR2-WOS README LICENSE; do
    [ -f "$DIST/$file" ] || fail "missing $DIST/$file"
done
for file in minigl_ppc.dll minigl_ppc_host; do
    [ ! -e "$DIST/$file" ] || fail "$DIST/$file must not be shipped (the MiniGL stack is a user requirement)"
done

version=$(strings "$DIST/opencmr2" | sed -n 's/.*\$VER: OpenCMR2 \([0-9][0-9.]*\) .*/\1/p' | head -n 1)
[ -n "$version" ] || fail "no \$VER: OpenCMR2 string in $DIST/opencmr2"
name="OpenCMR2-warpos-$version.lha"

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cp -R "$DIST" "$work/OpenCMR2"
docker run --rm --user "$(id -u):$(id -g)" -v "$work":/w -w /w "$IMAGE" \
    sh -c "lha ao5q '$name' OpenCMR2 && lha t '$name' >/dev/null" \
    || fail "lha failed"
cp "$work/$name" "$REPO/dist/$name"
zipname="OpenCMR2-warpos-$version.zip"
(cd "$work" && python3 -I -m zipfile -c "$zipname" OpenCMR2) || fail "zip failed"
cp "$work/$zipname" "$REPO/dist/$zipname"
printf 'package.sh: %s, %s (%s files)\n' "$REPO/dist/$name" "$REPO/dist/$zipname" "$(find "$work/OpenCMR2" -type f | wc -l)"
