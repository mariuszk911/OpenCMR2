#!/bin/sh
# Archives the release drawer dist/amiga as dist/OpenCMR2-amiga-<version>.lha,
# with everything inside a top-level OpenCMR2/ drawer. The version is read
# from the executable's $VER string. Run tools/amiga/build.sh --clean first.
#
# The archive is made with LHa for UNIX from the Amiga toolchain image
# (IMAGE, as in build.sh): distributions often ship Lhasa as "lha", which
# can only extract.
set -eu

REPO=$(cd "$(dirname "$0")/../.." && pwd)
DIST="$REPO/dist/amiga"
IMAGE=${IMAGE:-opencmr2/amiga-toolchain:latest}

fail() { printf 'package.sh: %s\n' "$1" >&2; exit 1; }

command -v docker >/dev/null 2>&1 || fail "docker not found"
[ -f "$DIST/opencmr2" ] || fail "no $DIST/opencmr2; run tools/amiga/build.sh --clean"
for file in opencmr2.info README LICENSE License_MiniGL_Library.txt Libs/minigl.library; do
    [ -f "$DIST/$file" ] || fail "missing $DIST/$file"
done
[ ! -e "$DIST/tests" ] || fail "$DIST/tests is a stale diagnostics drawer; rebuild with --clean"

version=$(strings "$DIST/opencmr2" | sed -n 's/^\$VER: OpenCMR2 \([0-9][0-9.]*\) .*/\1/p' | head -n 1)
[ -n "$version" ] || fail "no \$VER: OpenCMR2 string in $DIST/opencmr2"
name="OpenCMR2-amiga-$version.lha"

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cp -R "$DIST" "$work/OpenCMR2"
docker run --rm --user "$(id -u):$(id -g)" -v "$work":/w -w /w "$IMAGE" \
    sh -c "lha ao5q '$name' OpenCMR2 && lha t '$name' >/dev/null" \
    || fail "lha failed"
cp "$work/$name" "$REPO/dist/$name"
printf 'package.sh: %s (%s files)\n' "$REPO/dist/$name" "$(find "$work/OpenCMR2" -type f | wc -l)"
