#!/usr/bin/env python3
"""Refuse to trust an artefact that is older than the sources it is actually built from.

WHY THIS EXISTS
    A content check searches a binary for bytes it expects and passes happily against the
    PREVIOUS build. That is how a failed link reports green: the toolchain left the last good
    DLL in place, `xmake` printed "build ok" for a target it never rebuilt, and every
    byte-level assertion then describes an artefact nobody asked about. A verdict about bytes
    is worthless until the bytes are known to be the ones this build produced.

MTIMES ALONE ARE BOTH TOO STRICT AND NOT STRICT ENOUGH
    Too strict: `git checkout -- file` rewrites an mtime while the bytes are identical, so an
    mtime gate reports a correct no-op build as stale. A gate that cries wolf on a no-op is
    worse than none -- the reader learns to skip it, and the one time it is right they will
    not look. Measured on this tree: `touch src/MaskPublish.h` with no content change made the
    DLL "stale" and refused every content check.

    Not strict enough: a source edited with its own mtime restored passes an mtime gate, and a
    source file replaced by an older copy passes it too. The mtime is a claim ABOUT the
    contents, and it can be wrong in both directions.

    So the comparison is on CONTENT: a digest over the exact input set, recorded after a real
    build. The cost is that recording is a CLAIM the tool cannot verify for you -- see --record.

WHY THE INPUT SET IS PER KIND, AND WHY THAT IS THE HARD PART
    The naive gate compares everything against everything. Comparing the test executable
    against every file under src/ cries wolf on every plugin-only edit, because
    target("HelmetOverlaysNG-Tests") INCLUDES headers from src/ but COMPILES only five named
    .cpp files (xmake.lua:161-167). So the sets are derived from xmake.lua, per target:

        plugin  src/**.cpp, src/**.h, xmake.lua
        tests   tests/**, src/**.h, the five .cpp files the target compiles, xmake.lua

    TESTS_COMPILED_SRC is a literal rather than a glob precisely so it can be wrong in the
    direction that misses rather than the direction that nags. The peer's target compiles ZERO
    src cpps, which is why their set is headers-only for src; the same rule could not have
    been derived from mine. The input set is a measured fact about a target, not a principle.

    Each kind is digested SEPARATELY and keyed by name: a digest over the union of both sets
    never matches either scan, and shipping that bug produced exactly one run of "no record".

Exit codes are distinct on purpose:
    0  every artefact is not older than its sources, and no record is contradicted
    1  a verdict of no: stale, or the inputs have changed since the recorded build
    2  the question could not be asked (missing artefact, no sources, malformed --since)

"I could not ask" and "the answer is no" must not look the same to a caller: a wrapper that
treats every nonzero as staleness retries forever on a typo, and one that treats every
nonzero as an error ships a stale binary.

WHAT --record DOES AND DOES NOT PROVE
    --record asserts "the artefact in front of you was built from these inputs". The tool
    cannot establish that: it can only see what is on disk now. Recording after a build you
    did not actually run is indistinguishable from recording after one you did, so the flag is
    an explicit caller claim rather than a check, and running it is the moment to make sure a
    real build just happened. Without a record the gate falls back to mtimes and says so,
    which is the weaker check: it catches a stale artefact but not a no-op rebuild.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
import time
from pathlib import Path

HEADER_SUFFIXES = {".h", ".hpp", ".hxx", ".inl"}
SOURCE_SUFFIXES = {".cpp", ".cxx", ".cc", ".c"}

# The only src .cpp files target("HelmetOverlaysNG-Tests") compiles (xmake.lua:161-167). A
# glob would be wrong in the other direction: it would mark the suite stale for plugin-only
# edits, which is the false alarm this exists to avoid.
TESTS_COMPILED_SRC = (
    "src/ArtMetrics.cpp",
    "src/HelmetProfile.cpp",
    "src/Motion.cpp",
    "src/SoundEffect.cpp",
    "src/TintColor.cpp",
)

ALL_SOURCE_SUFFIXES = HEADER_SUFFIXES | SOURCE_SUFFIXES

RECORD_NAME = "freshness.json"


def _files(base: Path, suffixes: set[str]) -> list[Path]:
    if not base.is_dir():
        return []
    return [p for p in base.rglob("*") if p.is_file() and p.suffix.lower() in suffixes]


def input_set(root: Path, kind: str) -> tuple[list[Path], list[str]]:
    """Return (files, labels) that feed the artefact of this kind."""
    files: list[Path] = []
    labels: list[str] = []

    xmake = root / "xmake.lua"

    if kind == "plugin":
        files += _files(root / "src", ALL_SOURCE_SUFFIXES)
        labels.append("src (sources and headers)")
        if xmake.is_file():
            files.append(xmake)
            labels.append("xmake.lua")
        return files, labels

    if kind == "tests":
        files += _files(root / "tests", ALL_SOURCE_SUFFIXES)
        labels.append("tests")
        # The suite includes headers from src/ directly, so every header feeds it.
        files += _files(root / "src", HEADER_SUFFIXES)
        labels.append("src headers")
        # ...but only these translation units are compiled into it.
        for relative in TESTS_COMPILED_SRC:
            path = root / relative
            if path.is_file():
                files.append(path)
        labels.append("the .cpp files the suite compiles")
        if xmake.is_file():
            files.append(xmake)
            labels.append("xmake.lua")
        return files, labels

    raise ValueError(f"unknown kind {kind!r}")


def digest(root: Path, files: list[Path]) -> str:
    """A digest over the CONTENT of the input set, names included.

    Names are in the digest because a rename is a change to what the target compiles even when
    no byte of any file moved. Sorted for stability.
    """
    accumulator = hashlib.sha256()
    for path in sorted(files, key=lambda p: p.relative_to(root).as_posix()):
        accumulator.update(path.relative_to(root).as_posix().encode("utf-8"))
        accumulator.update(b"\0")
        accumulator.update(hashlib.sha256(path.read_bytes()).digest())
    return accumulator.hexdigest()


def load_record(root: Path) -> dict:
    path = root / RECORD_NAME
    if not path.is_file():
        return {}
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return {}


def save_record(root: Path, kind: str, value: str) -> None:
    record = load_record(root)
    record[kind] = value
    (root / RECORD_NAME).write_text(json.dumps(record, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def describe(ts: float) -> str:
    return time.strftime("%H:%M:%S", time.localtime(ts))


def check(artifact: Path, root: Path, kind: str, since: float | None, require_record: bool = False, rebootstrap: bool = False) -> int:
    if not artifact.is_file():
        print(f"NOT ASKED: no artefact at {artifact}", file=sys.stderr)
        print("  A missing artefact cannot be fresh. Exit 2, not 1: the build may never have", file=sys.stderr)
        print("  been run, and calling that a stale artefact invites a pointless rebuild.", file=sys.stderr)
        return 2

    files, labels = input_set(root, kind)
    if not files:
        print(f"NOT ASKED: no sources found for kind {kind!r} under {root}", file=sys.stderr)
        return 2

    newest = max(files, key=lambda p: p.stat().st_mtime)
    source_mtime = newest.stat().st_mtime
    artifact_mtime = artifact.stat().st_mtime

    # Content first when a record exists: it is the only check that is neither fooled by a
    # no-op touch nor by an mtime restored over an edit.
    recorded = load_record(root).get(kind)
    current = digest(root, files)
    if recorded:
        if recorded == current:
            print(f"FRESH: {artifact.name} was built from these inputs (content digest {current[:12]})")
            return 0
        if rebootstrap:
            # Without this path the gate could refuse FOREVER: any real source change makes the
            # digest differ from the record, and --record only runs after the gate passes, so a
            # legitimate rebuild left the tree permanently red -- the tool people disable.
            # Measured before adding it: edit to 3.1f, real rebuild of both targets, gate exit 1,
            # --record exit 1, no route back to green.
            #
            # What it is: a CALLER CLAIM that the artefact in front of us was built from these
            # inputs, in the same category as --record. What it is NOT: a bypass of the mtime
            # question.
            #
            # --since is REQUIRED because the mtime guard below is not enough on its own: measured
            # with a source edited to 3.7f and stamped an hour OLD, --rebootstrap exited 0 and
            # re-anchored, i.e. it laundered a backdated edit. An mtime comparison cannot see a
            # backdated write, but a build start time cannot be backdated into existence, so the
            # claim must be accompanied by one.
            if since is None:
                print(
                    "NOT ASKED: --rebootstrap needs --since <build start time>: the only other evidence",
                    file=sys.stderr,
                )
                print("  available is mtimes, and an edit stamped OLDER than the artefact passes them.", file=sys.stderr)
                print("  A build start time cannot be backdated into existence. Re-run with --since.", file=sys.stderr)
                return 2
            if artifact_mtime < source_mtime:
                print(
                    f"NOT ASKED: --rebootstrap was given but {artifact.name} ({describe(artifact_mtime)})"
                    f" is OLDER than its newest input ({newest.relative_to(root)}, {describe(source_mtime)})."
                , file=sys.stderr)
                print("  A claim that a build happened cannot stand against an artefact the sources postdate. Rebuild.", file=sys.stderr)
                return 1
            if artifact_mtime < since:
                print(
                    f"NOT ASKED: --rebootstrap was given but {artifact.name} ({describe(artifact_mtime)})"
                    f" predates the build the caller started ({describe(since)})."
                , file=sys.stderr)
                print("  Nothing was written after that build started, so the claim is contradicted. Rebuild.", file=sys.stderr)
                return 1
            save_record(root, kind, current)
            print(f"REBASED {kind}: {recorded[:12]} -> {current[:12]} over {len(files)} files")
            print("  A caller claim that the artefact was rebuilt from these inputs, not a check.")
            return 0
        print(f"STALE: contents changed since the recorded build ({recorded[:12]} -> {current[:12]})", file=sys.stderr)
        print(f"  artefact: {artifact} ({describe(artifact_mtime)})", file=sys.stderr)
        print(f"  watching: {', '.join(labels)} (kind {kind!r}, {len(files)} files)", file=sys.stderr)
        print("  The source bytes are not the ones that produced this artefact. Rebuild.", file=sys.stderr)
        return 1

    # No record: fall back to mtimes and SAY SO, LOUDLY, and make the caller opt in for a
    # deploy. This fallback cannot see an edit whose mtime was restored -- measured: with no
    # record, a source changed to 3.75f and `touch -d` back to its old stamp passes at exit 0.
    # That is the exact state a deleted or never-created record leaves behind, so a deploy gate
    # that silently degrades here is a gate that can be disabled by deleting a file.
    print(f"NOTE: no content record for kind {kind!r}; falling back to mtimes, which cannot", file=sys.stderr)
    print(f"      tell a no-op touch from an edit. Record one with --record after a real build.", file=sys.stderr)
    if require_record:
        print(f"NOT ASKED: kind {kind!r} has no content record and --require-record was given.", file=sys.stderr)
        print("  An mtime fallback passes an edit whose mtime was restored, so it cannot certify a", file=sys.stderr)
        print("  deploy. Build, then record, then deploy.", file=sys.stderr)
        return 2

    if artifact_mtime < source_mtime:
        print(f"STALE: the build did not produce {artifact.name}.", file=sys.stderr)
        print(f"  artefact: {describe(artifact_mtime)} ({artifact})", file=sys.stderr)
        print(f"  source:   {describe(source_mtime)} ({newest.relative_to(root)})", file=sys.stderr)
        print("  Any content check on it describes an older build. Rebuild before reading bytes.", file=sys.stderr)
        return 1

    if since is not None and artifact_mtime < since:
        # Only --since can tell whether a build happened in this session at all, which matters
        # when a build fails silently and writes nothing. Input mtimes cannot see that.
        print(f"STALE: {artifact.name} predates the build the caller started.", file=sys.stderr)
        print(f"  artefact: {describe(artifact_mtime)} / build started: {describe(since)}", file=sys.stderr)
        return 1

    print(f"FRESH (mtimes only): {artifact.name} ({describe(artifact_mtime)}) is not older than "
          f"{newest.relative_to(root)} ({describe(source_mtime)}) over {len(files)} files")
    return 0


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description="Check that an artefact is not older than the sources that feed it.")
    parser.add_argument("artifact", help="the built DLL or test executable to check")
    parser.add_argument(
        "--kind",
        default="plugin",
        choices=("plugin", "tests"),
        help="which target built the artefact; decides the input set (default: plugin)",
    )
    parser.add_argument(
        "--root",
        default=str(Path(__file__).resolve().parent.parent),
        help="repository root holding src/, tests/ and xmake.lua (default: this script's repo)",
    )
    parser.add_argument("--since", default=None, help="a build start time as epoch seconds; the artefact must be at least this new")
    parser.add_argument(
        "--require-record",
        action="store_true",
        help="refuse (exit 2) when no content record exists instead of falling back to mtimes, which cannot detect a restored mtime. Use this for a gate that certifies a deploy",
    )
    parser.add_argument(
        "--rebootstrap",
        action="store_true",
        help="after a real rebuild, replace a stale content record instead of refusing forever. A CLAIM that the artefact was rebuilt from the present inputs -- not a check, and it still refuses if the sources postdate the artefact",
    )
    parser.add_argument(
        "--record",
        action="store_true",
        help="record the current input digest for this kind. A CLAIM that the artefact was just built from these inputs, not a check",
    )
    args = parser.parse_args(argv)

    since = None
    if args.since is not None:
        try:
            since = float(args.since)
        except ValueError:
            print(f"NOT ASKED: --since is not a number: {args.since!r}", file=sys.stderr)
            return 2

    root = Path(args.root)
    if args.record:
        # --record is reachable ONLY when the gate would pass. Not ordering for tidiness: on its
        # own it records whatever the inputs are at that moment, so `--record` on an edited tree
        # stores a digest for an artefact nobody built and the NEXT plain run says FRESH. Measured
        # before this fix: edit 3.0f -> 3.9f, stamp the file 30 min older, `--record` direct ->
        # exit 0 and a RECORDED line, i.e. laundering. My only protection at the time was that
        # deploy went through verify_build, which is a property of one caller, not of the tool.
        #
        # require_record is deliberately NOT set here. This tool must still be able to write the
        # FIRST record, and with no record the only evidence available is mtimes. That window is
        # unavoidable by construction, so it is made explicit rather than closed: the write says
        # the anchor rests on mtimes, and the deploy gate passes --require-record so a deploy can
        # never rest on it. A record that already exists and DISAGREES is refused outright.
        existing = load_record(root).get(args.kind)
        files, labels = input_set(root, args.kind)
        if not files:
            print(f"NOT ASKED: no sources found for kind {args.kind!r} under {root}", file=sys.stderr)
            return 2
        status = check(Path(args.artifact), root, args.kind, since, require_record=False, rebootstrap=args.rebootstrap)
        if status != 0:
            print("NOT ASKED: --record was given but the freshness gate did not pass, so there is nothing", file=sys.stderr)
            print("  to anchor a claim to. A record written beside a failing gate is indistinguishable", file=sys.stderr)
            print("  from one written after a real build. Build first, then re-anchor with --rebootstrap.", file=sys.stderr)
            return status
        value = digest(root, files)
        save_record(root, args.kind, value)
        print(f"RECORDED {args.kind}: {value[:12]} over {len(files)} files ({', '.join(labels)})")
        print("  This asserts the artefact was built from these inputs. The tool cannot verify that:")
        print("  recording without a real build is indistinguishable from recording after one.")
        if existing is None:
            print("  NOTE: there was no previous record, so this anchor rests on mtimes alone, and an")
            print("  edit whose mtime was restored would have passed. Deploys pass --require-record,")
            print("  so a deploy never rests on a first record written this way.")
        return 0

    return check(Path(args.artifact), root, args.kind, since, args.require_record, args.rebootstrap)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
