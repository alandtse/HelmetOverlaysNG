#!/usr/bin/env python3
"""Run the content checks on artefacts, but only after proving the build produced them.

WHY THE ORDERING IS THE TOOL
    A content check that searches a binary for expected bytes passes against the previous
    build. That is exactly how a failed link reports green, and it is why this script runs the
    freshness gate FIRST and refuses any content check if it fails. A verdict about bytes is
    worthless until the bytes are known to be the ones this build produced; running the checks
    and then caveating them is a remembered rule, and remembered rules are what let this go
    wrong three times in one session.

WHAT IT CHECKS
    Every artefact named is gated before any content check runs, and each keeps its own input
    set (see tools/check_artifact_fresh.py for why a single set cries wolf):
        --dll      the plugin, kind plugin
        --tests    the test executable, kind tests
    Only the plugin has byte content worth searching (the shipped mask shaders), so that check
    runs against the DLL alone.

EXIT CODES, preserved from the stages rather than collapsed:
        0  every artefact is fresh and every content check passed
        1  a verdict of NO: an artefact is stale, or a shipped shader is missing
        2  the question could not be asked: no artefact, unreadable header, bad arguments

    "I could not ask" (2) and "the answer is no" (1) must not look the same to a caller. A
    C1090 in a PCH translation unit can DELETE the DLL, so one cause now produces both a
    stale-green (old artefact intact) and an unaskable one (no artefact); collapsing them
    would hide which happened.

LIMIT, restated because it is the whole point: freshness proves an artefact is not OLDER than
its sources, not that it was built FROM their current contents. A header mutated with its
mtime preserved passes this gate.
"""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent


def run(script: str, argv: list[str]) -> int:
    return subprocess.run([sys.executable, str(HERE / script), *argv]).returncode


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description="Freshness gate for every artefact, then content checks, in that order.")
    parser.add_argument("--dll", help="the built plugin DLL")
    parser.add_argument("--tests", help="the built test executable")
    parser.add_argument("--root", default=str(HERE.parent), help="repository root (default: this script's repo)")
    parser.add_argument("--since", default=None, help="epoch seconds when the build started, if known")
    parser.add_argument("--header", default=None, help="shader header to compare against (default: src/MaskShaders.h)")
    parser.add_argument(
        "--require-record",
        action="store_true",
        help="refuse when a content record is missing instead of falling back to mtimes. Use this where the verdict certifies a deploy",
    )
    parser.add_argument(
        "--rebootstrap",
        action="store_true",
        help="after a real rebuild, re-anchor a stale content record instead of refusing forever. Passed through to the freshness gate, which still refuses if the sources postdate the artefact",
    )
    parser.add_argument(
        "--record",
        action="store_true",
        help="after a passing gate, record the input digests. A CLAIM that these artefacts were built from the inputs present, not a check",
    )
    args = parser.parse_args(argv)

    if not args.dll and not args.tests:
        print("NOT ASKED: name at least one artefact with --dll or --tests", file=sys.stderr)
        return 2

    targets = []
    if args.dll:
        targets.append(("plugin", args.dll))
    if args.tests:
        targets.append(("tests", args.tests))

    # Every artefact is gated before ANY content check runs, and --rebootstrap is applied only
    # after EVERY named artefact has passed. Both restrictions come from measured defects:
    #
    # - Reading bytes out of one artefact while another is merely assumed fresh is how a stale
    #   test exe keeps its pass while the DLL is verified.
    # - Rebootstrap wrote its new record inside the per-artefact loop, so a run where the plugin
    #   re-anchored and the tests then failed left the plugin's record REPLACED for an artefact
    #   whose sibling had not been checked: a HALF-APPLIED state that is worse than either
    #   outcome. Measured: "REBASED plugin" printed, then the tests floor comparison failed, exit
    #   1, plugin record already overwritten. Re-anchoring is therefore a second pass over
    #   artefacts that have all already passed.
    worst = 0
    for kind, artifact in targets:
        fresh_args = [artifact, "--kind", kind, "--root", args.root]
        if args.since is not None:
            fresh_args += ["--since", args.since]
        if args.require_record and not args.record:
            # --record already refuses when an existing record DISAGREES (the laundering case), so
            # demanding a record here would make the FIRST record unwritable: no record -> gate
            # refuses -> record loop never runs -> still no record. Measured: that deadlock made
            # the tests kind permanently unrecordable. Recording is the intent when it is asked
            # for; --require-record governs the DEPLOY path, which never records.
            fresh_args.append("--require-record")
        status = run("check_artifact_fresh.py", fresh_args)
        if status != 0 and not args.rebootstrap:
            print(f"\nverify_build: freshness gate failed for {kind} ({artifact}); content checks were NOT run.", file=sys.stderr)
            return status
        worst = max(worst, status)

    # --since is required for a re-anchor by check_artifact_fresh.py itself; refusing here as well
    # keeps the failure at the top of the output instead of halfway down the artefact list.
    if args.rebootstrap:
        if args.since is None:
            print("NOT ASKED: --rebootstrap needs --since <build start time> (see check_artifact_fresh.py)", file=sys.stderr)
            return 2
        print()
        for kind, artifact in targets:
            rebase_args = [artifact, "--kind", kind, "--root", args.root, "--since", args.since, "--rebootstrap"]
            status = run("check_artifact_fresh.py", rebase_args)
            if status != 0:
                print(f"\nverify_build: re-anchor refused for {kind} ({artifact}).", file=sys.stderr)
                return status
        worst = 0

    header = args.header or str(Path(args.root) / "src" / "MaskShaders.h")
    print()
    if args.dll:
        status = run("check_shipped_shaders.py", [args.dll, header])
        if status != 0:
            return status
    else:
        print("no content checks apply to a test executable; freshness is the whole verdict")

    # Recording happens only AFTER the gate passed, and only for artefacts that were named: a
    # digest recorded for a target nobody built is a claim about nothing, and the peer shipped
    # exactly that for one run by digesting the union of both input sets.
    if args.record:
        print()
        for kind, artifact in targets:
            record_args = [artifact, "--kind", kind, "--root", args.root, "--record"]
            status = run("check_artifact_fresh.py", record_args)
            if status != 0:
                return status

    print(f"\nPASS: every artefact is fresh ({', '.join(k for k, _ in targets)}) and every content check agrees")
    return worst


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
