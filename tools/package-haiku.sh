#!/usr/bin/env bash
# Builds the Haiku package (.hpkg) on Haiku or air/OS. Run from the
# repository root (or `make package`).
#
# airImager goes into /boot/system/apps, with a link in the Deskbar menu's
# Utilities folder. The air/OS build server stages the same layout (airos-ci
# apps/build-app-packages.sh, build_airimager).
set -euo pipefail
ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$ROOT"
INFO=resources/airImager.PackageInfo
NAME=$(awk '$1 == "name" { print $2; exit }' "$INFO")
VERSION=$(awk '$1 == "version" { print $2; exit }' "$INFO")
ARCH=$(getarch 2>/dev/null || awk '$1 == "architecture" { print $2; exit }' "$INFO")
FILE="$ROOT/artifacts/$NAME-$VERSION-$ARCH.hpkg"

make -j"$(nproc 2>/dev/null || echo 4)"
STAGE=$(mktemp -d /tmp/airimager-package-XXXXXX)
trap 'rm -rf -- "$STAGE"' EXIT
DOCS="$STAGE/documentation/packages/airimager"
mkdir -p "$STAGE/apps" "$STAGE/data/deskbar/menu/Utilities" "$DOCS" \
	"$STAGE/data/licenses" "$ROOT/artifacts"

cp build-haiku/airImager "$STAGE/apps/airImager"
strip --strip-debug "$STAGE/apps/airImager"
# GNU strip removes the appended Haiku resources; put them back.
xres -o "$STAGE/apps/airImager" build-haiku/airImager.rsrc
ln -s ../../../../apps/airImager "$STAGE/data/deskbar/menu/Utilities/airImager"
sed "s/^architecture[ \t].*/architecture $ARCH/" "$INFO" > "$STAGE/.PackageInfo"
cp README.md LICENSE "$DOCS/"
cp -R docs "$DOCS/"
cp third_party/xz-embedded/COPYING "$DOCS/xz-embedded-COPYING"

( cd "$STAGE" && mimeset --all -f --mimedb data/mime_db --mimedb /boot/system/data/mime_db apps/airImager )
package create -C "$STAGE" "$FILE"
printf '%s\n' "$FILE"
