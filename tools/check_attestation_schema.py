#!/usr/bin/env python3
"""Validate a deployment attestation record against the schema.

This exists because the schema was enforced by nothing. The only check was a test
that asserted on the size of a hardcoded list of field names, which compared the
list to itself and never opened a record. Under that check four fields went
missing across four deploys and every run stayed green.

The fix has two halves and they are deliberately separate:

  * this script is the ENFORCEMENT. The deploy script runs it against the record it
    is about to write and refuses to finish on a violation, so a lossy record
    cannot be created in the first place.
  * tests/test_attestation_schema.cpp is the DETECTION. It derives the required
    field set from the records on disk rather than from a list, so a field dropped
    by a future deploy shows up as a red test even if this file is not updated.

Records are HISTORICAL EVIDENCE and are never rewritten by this checker. A record
made before a field existed is reported as `predates`, not `absent`: the difference
between "this deploy failed to record it" and "this deploy happened before the
field was invented" is the whole point, and collapsing them would either launder
the defect or slander the record. A field is required from the first record that
carries it, onward.

Usage:
    check_attestation_schema.py <record.json> [more.json ...]

Records are ordered by `deployedAt`, never by path: sorting by directory name puts
`hong-reformat-*` after `hong-alpha255-*` while being the first deploy, which makes
a chain check report a false break.

Exit codes:
    0  every record is complete from its own point in the schema forward
    1  a field was dropped after being established, or a null is unexplained
    2  a file could not be read or parsed
"""

import argparse
import json
import sys

# Every field the schema asks for. This list is the canonical one: tests/test_attestation_schema.cpp
# cross-checks that every field the newest record carries is named here, so the two cannot
# drift apart without a red test. Kept in step with the record writer in the deploy script.
#
# Deliberately absent because superseded, with the replacement recorded in SUPERSEDES below:
#   normalisedMatchTree    -> dllByteIdenticalToTreeBuild (which the newest record carries and
#                             which is true, so the fact is still recorded)
REQUIRED = [
    # identity of the deploy
    "schemaVersion",
    "deployedAt",
    "backup",
    "gameLaunched",
    # identity of the source
    "commit",
    "commitSubject",
    "branch",
    "change",
    # identity of the bytes, primary and secondary
    "dllSHA256",
    "pdbSHA256",
    "treeBuildDLLSHA256",
    "treeBuildPDBSHA256",
    "treeBuildFieldsProvenance",
    "dllByteIdenticalToTreeBuild",
    "pdbByteIdenticalToTreeBuild",
    "dllNormalisedSHA256",
    "normalisedSource",
    "normalisedDiffersFromPreviousDeploy",
    "freshness",
    "pdbLink",
    # what the artefact is supposed to contain
    "symbolsExpectPresent",
    "symbolsContainer",
    # why anyone should believe the tests were run against it
    "mutationProof",
    "mutationProofProvenance",
    # limits, stated rather than left to be guessed
    "whatThisDoesNotProve",
    "whyThisExists",
    # the backup, and why it is supposed to differ
    "backupFileHashes",
    "backupDiffersFromDeployedAsExpected",
    "backupHolds",
    # the newest deploy's own bytes, so the chain can outlive the install
    "deployedCopiesPreserved",
    "whyDeployedCopyExists",
    # an edit made after the record was written, declared rather than silent
    "postHocAnnotations",
]

# A null is allowed for these, but only alongside its `<key>Provenance` string
# saying why. "Not captured" and "measured, and absent" both render as a hole.
NULLABLE = [
    "treeBuildDLLSHA256",
    "treeBuildPDBSHA256",
    "treeBuildFieldsProvenance",
    "mutationProof",
    "normalisedDiffersFromPreviousDeploy",
    "change",
]

# Fields that deliberately replaced an earlier field. A drop is only allowed when the
# replacement is recorded here AND the record carries it. Verified against the real
# history: `sourceAttestation` was flattened to `commit`/`commitSubject` at 31354a1b,
# and `deploymentArtifactPreservation` was replaced by `deployedCopiesPreserved` plus
# `whyDeployedCopyExists` at 08c0884b when the deploy script began preserving its own
# output. Everything else that vanished was accidental.
SUPERSEDES = {
    "sourceAttestation": ["commit", "commitSubject"],
    "commitProvenance": ["commit"],
    "changeProvenance": ["change"],
    "backupFileProvenance": ["backupFileHashes"],
    "note": ["whatThisDoesNotProve"],
    "pdbLinkLimitations": ["pdbLink"],
    "treeBuildNormalisedSHA256": ["dllNormalisedSHA256"],
    "normalisedDiffersFromPreviousDeployProvenance": ["normalisedDiffersFromPreviousDeploy"],
    "deploymentArtifactPreservation": ["deployedCopiesPreserved", "whyDeployedCopyExists"],
    "treeBuildComparable": ["dllByteIdenticalToTreeBuild", "pdbByteIdenticalToTreeBuild"],
    "treeBuildPDBSHA256": ["pdbByteIdenticalToTreeBuild"],
    "normalisedMatchTree": ["dllByteIdenticalToTreeBuild"],
}


def load(path):
    with open(path, encoding="utf-8-sig") as handle:
        return json.load(handle)


def check_chain(records):
    """Return (problems, notes). `records` is ordered oldest deploy first."""
    problems = []
    notes = []

    if not records:
        return ["no records given"], notes

    for index, record in enumerate(records):
        label = record.get("commit", "?")[:9]

        # A drop is a TRANSITION, reported once where it happens. Reporting it on every
        # later record too buries the one real loss under repetitions of the same fact.
        if index > 0:
            previous = records[index - 1]
            for key in previous:
                if key in record:
                    continue
                # `in` distinguishes absent from null. `.get()` cannot, and neither can
                # any count of a list of field names.
                #
                # A `...Provenance` string exists only to explain a null. When the null is
                # gone the explanation has nothing to explain, so its disappearance is the
                # fix working, not a field being lost. I back-filled provenance strings onto
                # an older record and this rule is what stopped that from reporting a false
                # loss on the next one.
                if key.endswith("Provenance"):
                    # A provenance string explains a null. It is unnecessary when the field it
                    # explains is absent (nothing to explain) or carries a real value (nothing
                    # to excuse), so neither case is a loss of evidence.
                    explaining = key[: -len("Provenance")]
                    if record.get(explaining) is not None:
                        notes.append(f"{label}: {key} dropped, {explaining} is not null")
                        continue
                replacement = SUPERSEDES.get(key)
                if replacement and all(name in record for name in replacement):
                    notes.append(f"{label}: {key} replaced by {', '.join(replacement)}")
                    continue
                problems.append(
                    f"{label}: dropped from previous record: {key}"
                    + (f" (recorded replacement {replacement} is not present)" if replacement else "")
                )

        for key, value in record.items():
            if value is not None:
                continue
            if key not in NULLABLE:
                problems.append(f"{label}: null not permitted: {key}")
                continue
            provenance = f"{key}Provenance"
            reason = record.get(provenance)
            if not isinstance(reason, str) or not reason.strip():
                problems.append(f"{label}: null without {provenance}: {key}")

    # The chain terminus is special: every earlier deploy survives an install overwrite
    # because a later record's backup/ holds it. The newest has no such later record, so
    # it must both preserve its own bytes and satisfy the full canonical schema, or it is
    # the one link nobody can ever check again.
    newest = records[-1]
    newestLabel = newest.get("commit", "?")[:9]
    for key in REQUIRED:
        if key not in newest:
            problems.append(f"{newestLabel}: newest record does not satisfy the schema: {key}")
    if "deployedCopiesPreserved" not in newest:
        problems.append(f"{newestLabel}: newest record does not preserve its own deployed bytes")
    else:
        preserved = newest.get("deployedCopiesPreserved") or {}
        if preserved.get("dllSHA256") != newest.get("dllSHA256"):
            problems.append(f"{newestLabel}: deployedCopiesPreserved.dllSHA256 does not match dllSHA256")

    return problems, notes


def describe(records):
    """Where a field first appears, so the report can tell predating from losing."""
    required_from = {}
    for index, record in enumerate(records):
        for key in record:
            required_from.setdefault(key, (index, record.get("commit", "?")[:9]))
    return required_from


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("records", nargs="+", help="attestation.json files, any order")
    args = parser.parse_args()

    loaded = []
    for path in args.records:
        try:
            loaded.append(load(path))
        except (OSError, json.JSONDecodeError) as error:
            print(f"cannot read {path}: {error}", file=sys.stderr)
            return 2

    loaded.sort(key=lambda record: record.get("deployedAt", ""))
    problems, notes = check_chain(loaded)

    first_seen = describe(loaded)
    print(f"records: {len(loaded)} (ordered by deployedAt)")
    for key, (index, commit) in sorted(first_seen.items(), key=lambda item: item[1][0]):
        print(f"  {key:38s} from #{index} {commit}")

    for note in notes:
        print(f"NOTE {note}")

    if problems:
        print(f"\nFAIL ({len(problems)} problem(s))", file=sys.stderr)
        for problem in problems:
            print(f"  {problem}", file=sys.stderr)
        return 1

    print("\nOK: no field dropped after being established; every null explained")
    return 0


if __name__ == "__main__":
    sys.exit(main())
