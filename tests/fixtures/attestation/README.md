# Attestation fixtures

Copies of real `attestation.json` records written by the SE deploy script, committed so
`tests/test_attestation_schema.cpp` can run anywhere without access to `E:/tmp`.

They are **normalised copies**: `prettier` reformats JSON on commit, so these are not
byte-identical to the files under `E:/tmp/hong-*-deploy-*`. They are semantically
identical, and that is deliberate — a fixture is evidence of the record's _shape and
history_, and reformatting cannot change a key, a value or a null. If you need to prove a
fixture matches the live record, compare parsed values:

```
python -c "import json,sys; a=json.load(open(sys.argv[1])); b=json.load(open(sys.argv[2])); print(a==b)" \
    E:/tmp/hong-alpha255-se-20261031-155010/attestation.json \
    tests/fixtures/attestation/hong-alpha255-se-20261031-155010/attestation.json
```

Do **not** make fixtures byte-comparable by normalising the live records. The live records
are evidence and are not reformatted.

## Why these six

They are the deploys made while the schema was drifting, so they carry the defects the
tests exist to catch:

| record                                        | what it shows                                                                             |
| --------------------------------------------- | ----------------------------------------------------------------------------------------- |
| `hong-reformat-se-20261031-152651` (05a0adf)  | predates `deployedCopiesPreserved`                                                        |
| `hong-alpha255-se-20261031-153313` (31354a1b) | predates it too, and carries two nulls with no provenance until they were annotated       |
| `hong-alpha255-se-20261031-153802` (3cef032)  | still predates it; drops 8 fields the previous record had (`sourceAttestation` flattened) |
| `hong-alpha255-se-20261031-154246` (08c0884b) | first record with `deployedCopiesPreserved`; drops `deploymentArtifactPreservation`       |
| `hong-alpha255-se-20261031-154531` (9f87d1c3) | **loses** `treeBuildPDBSHA256` and `normalisedMatchTree`                                  |
| `hong-alpha255-se-20261031-155010` (9d2622a)  | most recent; still missing both                                                           |

Ordering is by `deployedAt`, never by directory name: `hong-reformat-*` sorts after
`hong-alpha255-*` alphabetically while being the **first** deploy.

## Fields annotated after the fact

Three records were annotated by HONG after they were written, when a checker finally read
them. Each carries a `postHocAnnotations` entry naming exactly what was added. No
measurement was invented: the additions are `...Provenance` strings explaining existing
nulls, plus explicit `null` for `treeBuildPDBSHA256` where the field had been dropped
entirely. A reader can therefore tell a contemporaneous field from a later one.
