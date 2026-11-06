#!/usr/bin/env bash
# Build the plugin and the test suite, and prove the artefacts CHANGED.
#
# A build that reports "build ok, spent 0.062s" and writes nothing is the defect this
# script exists to catch. xmake's success line and a plausible duration are more convincing
# evidence than a timestamp and there is nothing to compare them against, so three no-op
# "rebuilds" in one session looked like successes to me. The gate above this script
# (tools/verify_build.py) trusts the artefact; nothing trusted the BUILD.
#
# The comparison is on artefact identity (sha256 of bytes), not mtime: mtime moves on a
# no-op relink and does not move when a build is skipped, so it is wrong in both directions.
#
# The test suite is built explicitly because target("HelmetOverlaysNG-Tests") is
# set_default(false) in xmake.lua -- a plain `xmake` does NOT build it, so "rebuilt plugin
# beside an unbuilt suite" is the normal state otherwise.
#
# Usage: tools/build.sh [--expect-change] [--start-stamp FILE]
#   --expect-change   fail if either artefact is byte-identical to before (an
#                     edited tree must produce something different)
#   --start-stamp     write the epoch time this build started to FILE, for
#                     verify_build.py --since
set -uo pipefail

cd "$(dirname "$0")/.." || exit 1

BIN=build/windows/x64/releasedbg
DLL=$BIN/HelmetOverlaysNG.dll
TESTS=$BIN/HelmetOverlaysNG-Tests.exe
EXPECT_CHANGE=0
START_STAMP=""

while [ $# -gt 0 ]; do
    case "$1" in
        --expect-change) EXPECT_CHANGE=1; shift ;;
        --start-stamp) START_STAMP="$2"; shift 2 ;;
        *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
done

if [ -n "$START_STAMP" ]; then
    date '+%s' > "$START_STAMP"
fi

hash_of() { [ -f "$1" ] && sha256sum "$1" | cut -d' ' -f1 || echo "missing"; }

BEFORE_DLL=$(hash_of "$DLL")
BEFORE_TESTS=$(hash_of "$TESTS")

# The deployment env vars must not be set: xmake auto-deploys when they are, which would
# copy a half-built binary into a game directory as a side effect of running the tests.
unset SkyrimVRPluginTargets SkyrimPluginTargets SkyrimVRPath

echo "building plugin..."
if ! xmake; then
    echo "build.sh: the plugin build FAILED. Any artefact in $BIN is from an earlier build." >&2
    exit 1
fi

echo "building tests..."
if ! xmake build HelmetOverlaysNG-Tests; then
    echo "build.sh: the tests build FAILED. Any test exe in $BIN is from an earlier build." >&2
    exit 1
fi

AFTER_DLL=$(hash_of "$DLL")
AFTER_TESTS=$(hash_of "$TESTS")

status=0

if [ "$AFTER_DLL" = "missing" ]; then
    echo "build.sh: no plugin DLL at $DLL after a successful build line." >&2
    status=1
elif [ "$AFTER_DLL" = "$BEFORE_DLL" ]; then
    if [ "$EXPECT_CHANGE" -eq 1 ]; then
        echo "build.sh: the plugin DLL is BYTE-IDENTICAL to before the build ($AFTER_DLL)." >&2
        echo "  The build reported success and produced nothing. Rebuild for real, or drop" >&2
        echo "  --expect-change if no source changed." >&2
        status=1
    else
        echo "build.sh: plugin DLL unchanged (${AFTER_DLL:0:12}) -- expected when no source changed."
    fi
else
    echo "build.sh: plugin DLL ${BEFORE_DLL:0:12} -> ${AFTER_DLL:0:12}"
fi

if [ "$AFTER_TESTS" = "missing" ]; then
    echo "build.sh: no test exe at $TESTS after a successful build line." >&2
    status=1
elif [ "$AFTER_TESTS" = "$BEFORE_TESTS" ]; then
    if [ "$EXPECT_CHANGE" -eq 1 ]; then
        echo "build.sh: the test exe is BYTE-IDENTICAL to before the build ($AFTER_TESTS)." >&2
        status=1
    else
        echo "build.sh: test exe unchanged (${AFTER_TESTS:0:12}) -- expected when no source changed."
    fi
else
    echo "build.sh: test exe ${BEFORE_TESTS:0:12} -> ${AFTER_TESTS:0:12}"
fi

exit $status
