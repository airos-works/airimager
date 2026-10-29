#!/usr/bin/env bash
# Cross-compiles airImager on Linux with an air/OS cross SDK (airos-ci
# sdk/build-sdk.sh: compiler, sysroot, host rc/xres/mimeset), the way the
# build server does. Produces build-ARCH/airImager.
#
#   tools/build-cross.sh [x86_64|arm64] [make target]
#
# AIROS_SDK defaults to /data1/airos/sdk (the air/OS build server).
set -euo pipefail
ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
ARCH=${1:-x86_64}
SDK=${AIROS_SDK:-/data1/airos/sdk}/$ARCH
[[ -f $SDK/env.sh ]] || { echo "no air/OS SDK at $SDK" >&2; exit 1; }
# shellcheck disable=SC1091
. "$SDK/env.sh"

cd "$ROOT"
exec make BUILD="build-$ARCH" -j"$(nproc)" \
	CXX="${CROSS}g++ --sysroot=$SYSROOT -specs=$UNWIND_SPECS" \
	RC="$TOOLS/rc/rc" XRES="$TOOLS/xres" \
	MIMESET="$TOOLS/mimeset --mimedb $MIMEDB" \
	"${2:-all}"
