#!/usr/bin/env python3
"""Compute the build-identity values an attestation record needs.

Three values that a deploy record asks for cannot be observed by copying files, and
leaving them null with a "not computed" note is the shape of a field that exists to be
filled in. So they are computed here from the artefacts themselves:

  * dllNormalisedSHA256 — the DLL's hash with the values that change on every link
    regardless of source erased: the COFF TimeDateStamp, the PE CheckSum, and for each
    debug-directory entry the TimeDateStamp plus the CV_INFO_PDB70 GUID and age. Two
    builds of the same source therefore normalise to the same value, and a source change
    does not. Validated across BOTH PDB lifecycles (incremental append and full-rebuild
    recreate) with identical source; keeping the GUID produced a value that moved when only
    the lifecycle changed, which is the defect that removed it here.

  * pdbLink — the RSDS GUID and age from the DLL's debug directory, plus the same pair
    from the PDB's info streams. This is a SECONDARY check and it rules out an unrelated
    build only; it proves nothing about which build produced the DLL.

    The GUID and age are properties of the PDB's LIFECYCLE, not of the source. Measured on
    this machine and independently on a peer's, with the SAME numbers: an INCREMENTAL relink
    appends, so the GUID holds and the age increments; a FULL rebuild recreates, so the GUID is
    fresh and the age returns to 1. Both of us first tried to name the trigger from one
    experiment each -- one guessed "depends whether a PDB exists", the other "recreates every
    link" -- and neither had varied the build kind, which is the actual trigger. A value that
    moves with the lifecycle cannot identify a build, which is why dllNormalisedSHA256 zeroes
    the GUID as well as the age, and why the hash is unchanged across that transition with
    identical source: the strongest single check on this tool.

  * symbolsExpectPresent — how many times each declared symbol appears in the PDB.

Offset handling is spec-driven, never hardcoded, because hardcoded offsets in a normaliser
are how a "normalised" hash silently stops covering what it claims to:
  * COFF header TimeDateStamp is at e_lfanew + 8; CheckSum at e_lfanew + 0x58.
  * The debug data directory is data-directory entry 6, at opt + 112 + 6 * 8.
  * IMAGE_DEBUG_DIRECTORY is 28 bytes: Characteristics, TimeDateStamp, MajorVersion,
    MinorVersion, Type, SizeOfData, AddressOfRawData, PointerToRawData.
  * CV_INFO_PDB70 is "RSDS", then a 16-byte GUID, then a 4-byte age. The GUID and age
    are at DIFFERENT offsets than in a PDB 7.0 info stream, where the order is
    Version, Signature, Age, GUID — age BEFORE GUID. Getting that backwards is an easy
    and silent mistake; assert on values that only parse if the offset is right.
  * IMAGE_SECTION_HEADER order at +8 is VirtualSize, VirtualAddress, SizeOfRawData,
    PointerToRawData.

Usage:
    normalise_build.py --dll <dll> --pdb <pdb> [--symbol Name ...] [--json]

Exit codes:
    0  values computed
    2  an artefact could not be parsed
"""

import argparse
import hashlib
import json
import struct
import sys

PDB70_VERSION = 20000404


def read(path):
    with open(path, "rb") as handle:
        return bytearray(handle.read())


def pe_layout(data):
    """Return (e_lfanew, checksum_offset, data_dir_offset, sections)."""
    if data[:2] != b"MZ":
        raise ValueError("not a PE file: no MZ header")
    e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
    if data[e_lfanew : e_lfanew + 4] != b"PE\0\0":
        raise ValueError("not a PE file: no PE signature")

    coff = e_lfanew + 4
    number_of_sections = struct.unpack_from("<H", data, coff + 2)[0]
    optional_size = struct.unpack_from("<H", data, coff + 16)[0]
    opt = coff + 20
    magic = struct.unpack_from("<H", data, opt)[0]
    if magic not in (0x10B, 0x20B):
        raise ValueError(f"unexpected optional header magic 0x{magic:x}")

    checksum_offset = opt + 64

    # Data directories follow the optional header's fixed part: 96 bytes for PE32,
    # 112 for PE32+. Hardcoding either is how a normaliser silently stops covering the
    # debug directory on the other format.
    data_dir = opt + (112 if magic == 0x20B else 96)

    sections = []
    section_start = opt + optional_size
    for index in range(number_of_sections):
        base = section_start + index * 40
        virtual_size, virtual_address, raw_size, raw_pointer = struct.unpack_from("<IIII", data, base + 8)
        sections.append((virtual_address, virtual_size, raw_pointer, raw_size))

    return e_lfanew, checksum_offset, data_dir, sections


def rva_to_offset(sections, rva):
    for virtual_address, virtual_size, raw_pointer, raw_size in sections:
        span = max(virtual_size, raw_size)
        if virtual_address <= rva < virtual_address + span:
            return raw_pointer + (rva - virtual_address)
    raise ValueError(f"RVA 0x{rva:x} is not inside any section")


DEBUG_DIRECTORY_ENTRY = 28  # IMAGE_DEBUG_DIRECTORY: Characteristics, TimeDateStamp,
# MajorVersion, MinorVersion, Type, SizeOfData, AddressOfRawData, PointerToRawData.
#
# SizeOfData is at +16 and AddressOfRawData at +20, and Type is at +12. Getting one of those
# wrong does NOT fail loudly: the loop finds no RSDS, returns nothing, and the tool still
# prints a successful hash computed over bytes it never touched. That is a tool reporting
# success over nothing, so the size is asserted to be a whole number of entries here rather
# than assumed, and callers refuse to report a link they did not find.


class NormaliserError(Exception):
    """The artefact did not contain what the normaliser needs to answer the question."""


def debug_entries(data, data_dir, sections):
    """Parse the debug directory, insisting that its size is a whole number of entries."""
    debug_rva, debug_size = struct.unpack_from("<II", data, data_dir + 6 * 8)
    if not debug_rva or not debug_size:
        raise NormaliserError("no debug directory: the DLL carries no CodeView entry to identify it by")
    if debug_size % DEBUG_DIRECTORY_ENTRY:
        raise NormaliserError(
            f"debug directory size {debug_size} is not a multiple of {DEBUG_DIRECTORY_ENTRY}: "
            "the entries would be misparsed, and a misparse here reports an empty result rather than an error"
        )
    start = rva_to_offset(sections, debug_rva)
    for index in range(debug_size // DEBUG_DIRECTORY_ENTRY):
        base = start + index * DEBUG_DIRECTORY_ENTRY
        yield base


def codeview_entries(data, data_dir, sections):
    """Debug entries that are CodeView by EITHER read, insisting the two reads agree.

    IMAGE_DEBUG_DIRECTORY says what an entry IS in two independent places: `Type` (2 =
    CodeView) and the RSDS signature of the blob it points at. Locating by only one of them
    makes the other decorative -- measured: an entry with Type patched to 9 and its signature
    intact was rejected by a Type-only locator as ABSENT, which is a plausible-looking answer
    to a question nobody asked, rather than the contradiction it actually is.

    So both reads happen here, once, and a disagreement is an error: if the record cannot
    agree with itself then neither field can be trusted, and every downstream answer -- a
    normalised hash, a GUID and age -- would be built on one of them arbitrarily.
    """
    for base in debug_entries(data, data_dir, sections):
        _, _, _, _, kind, size, _, raw_pointer = struct.unpack_from("<IIHHIIII", data, base)
        is_typed = kind == 2
        if not size or not raw_pointer:
            if is_typed:
                raise NormaliserError(
                    f"debug entry at 0x{base:x} claims Type=2 (CodeView) but points at no data "
                    f"(SizeOfData={size}, AddressOfRawData=0x{raw_pointer:x})"
                )
            continue
        signature = bytes(data[raw_pointer : raw_pointer + 4])
        is_signed = signature == b"RSDS"
        if is_typed == is_signed:
            if is_typed:
                yield base, raw_pointer
            continue
        raise NormaliserError(
            f"debug entry at file offset 0x{base:x} disagrees with itself: Type={kind} but "
            f"signature {signature!r}; one of the two reads is misparsed, so neither can be trusted"
        )


def normalise(data, label):
    """Zero the values that change on every link, in place. Returns (bytes, report)."""
    e_lfanew, checksum_offset, data_dir, sections = pe_layout(data)
    report = {}

    coff_stamp = e_lfanew + 8
    report["coffTimeDateStamp"] = struct.unpack_from("<I", data, coff_stamp)[0]
    struct.pack_into("<I", data, coff_stamp, 0)

    report["checkSum"] = struct.unpack_from("<I", data, checksum_offset)[0]
    struct.pack_into("<I", data, checksum_offset, 0)

    debug_size = struct.unpack_from("<I", data, data_dir + 6 * 8 + 4)[0]
    debug_rva = struct.unpack_from("<I", data, data_dir + 6 * 8)[0]
    report["debugDirectory" ] = {"rva": debug_rva, "size": debug_size}

    entries = []
    if debug_rva and debug_size:
        # Zero the timestamp on EVERY debug entry, not only the CodeView one: the types here are
        # 2 (CodeView), 12 (PDB) and 13 (VC feature), and a non-CodeView stamp left in place
        # would put a per-link value back into the hash. Measured: narrowing this loop to
        # CodeView entries alone moved the normalised hash 5958d8cc -> 3444c7e8 without any
        # source change, which would have invalidated every recorded digest.
        codeview = dict(codeview_entries(data, data_dir, sections))
        for base in debug_entries(data, data_dir, sections):
            characteristic, stamp, major, minor, kind, size, raw_rva, raw_pointer = struct.unpack_from("<IIHHIIII", data, base)
            entries.append({"type": kind, "timeDateStamp": stamp, "size": size, "pointer": raw_pointer})
            struct.pack_into("<I", data, base + 4, 0)  # TimeDateStamp
            if base not in codeview:
                continue
            # The GUID is minted when the linker CREATES a PDB and the age is the slot
            # it occupies in a created or appended one, so both move with the PDB's
            # LIFECYCLE rather than with the source. Measured across two machines and
            # three behaviours; neither is a property of the code, so both are zeroed.
            guid_offset = raw_pointer + 4
            age_offset = guid_offset + 16
            entries[-1]["rsdsGUID"] = bytes(data[guid_offset:age_offset]).hex()
            entries[-1]["rsdsAge"] = struct.unpack_from("<I", data, age_offset)[0]
            data[guid_offset:age_offset] = b"\0" * 16
            struct.pack_into("<I", data, age_offset, 0)

    if not any(entry["type"] == 2 for entry in entries):
        # Zero debug entries means the timestamp and checksum were all this normalisation
        # covered, which is less than the name promises. Say so loudly: this is the silent
        # success the peer's tool printed while returning an empty list.
        raise NormaliserError(
            f"no CodeView entry among {len(entries)} debug entries: the normalised hash would "
            "claim to have erased a link identity that was never found"
        )

    # A READ AND A WRITE OVER THE SAME RECORDS ARE NOT THE SAME LOOP. The read above wants to be
    # narrow (only CodeView yields a GUID and an age); the write wants to stay wide (every entry
    # carries a per-link TimeDateStamp -- types 2, 12 and 13 all do here). They happen to be
    # separate loops, which is a fact about how this function is written and not a property of
    # it: narrowing the write to CodeView alone moved the hash 5958d8cc -> 3444c7e8 with no
    # source change. The peer reproduced it from the other side (9f6a04c6 -> 266b8fc5), and the
    # general lesson is that a per-link stamp leaking into the hash invalidates every recorded
    # digest rather than producing a wrong answer anyone would notice.
    #
    # So the write is ASSERTED rather than trusted. Re-reading every entry after zeroing is the
    # only check that can see this defect, because the hash moves without failing anything.
    # EVERY ONE OF THE FIVE WRITES ABOVE IS GUARDED SEPARATELY, because a guard on one of them is
    # not evidence about the others. Measured by skipping each in turn and rehashing this DLL:
    # skipping the COFF TimeDateStamp gave 7549bdcf8800 and skipping the RSDS age gave
    # 143765c3b098, both with exit 0 and no message -- a clean-looking hash that had merely moved.
    # Each guard is a different claim: a skipped write is SILENT, a moved hash is its only
    # symptom, and nothing downstream fails on either.
    if struct.unpack_from("<I", data, coff_stamp)[0]:
        raise NormaliserError("COFF TimeDateStamp was not zeroed: the per-link stamp would leak into the hash")
    if struct.unpack_from("<I", data, checksum_offset)[0]:
        # Inert on this linker, which already writes CheckSum 0. Kept and checked anyway: "inert
        # here" is a fact about a toolchain, not about the format.
        raise NormaliserError("OptionalHeader CheckSum was not zeroed: the per-link checksum would leak into the hash")

    for index, base in enumerate(debug_entries(data, data_dir, sections)):
        stamp = struct.unpack_from("<I", data, base + 4)[0]
        if stamp:
            raise NormaliserError(
                f"debug entry {index} at 0x{base:x} still carries a TimeDateStamp "
                f"(0x{stamp:08x}) after normalisation; the per-link stamp would leak into the hash"
            )

    # The GUID and the age live inside the CodeView blob rather than in the entry itself, so they
    # are re-read through the same locator that did the write. Skipping either was silent above.
    for base, raw_pointer in codeview_entries(data, data_dir, sections):
        guid = bytes(data[raw_pointer + 4 : raw_pointer + 20])
        if guid != b"\0" * 16:
            raise NormaliserError(
                f"RSDS GUID at 0x{raw_pointer + 4:x} was not zeroed ({guid.hex()[:16]}...): "
                "the per-link GUID would leak into the hash"
            )
        age = struct.unpack_from("<I", data, raw_pointer + 20)[0]
        if age:
            raise NormaliserError(
                f"RSDS age at 0x{raw_pointer + 20:x} was not zeroed ({age}): the per-link age would leak into the hash"
            )

    report["debugEntries"] = entries
    report["label"] = label
    return bytes(data), report


def pdb_link(data):
    """(guid, age) from the DLL's CV_INFO_PDB70 entry, as hex and int.

    Raises rather than returning None: a caller that prints a hash and omits this line looks
    identical to a caller whose DLL has no link, and the difference between "there is no
    link" and "I did not find the link" is the difference between a fact and a bug.
    """
    e_lfanew, checksum_offset, data_dir, sections = pe_layout(bytearray(data))
    for base, raw_pointer in codeview_entries(data, data_dir, sections):
        guid = data[raw_pointer + 4 : raw_pointer + 20]
        age = struct.unpack_from("<I", data, raw_pointer + 20)[0]
        if age == 0:
            raise NormaliserError("CodeView age is 0, which no link produces: the entry is misparsed")
        return {"rsdsGUID": guid.hex(), "dllAge": age}
    raise NormaliserError("no CodeView entry in the debug directory")


def pdb_info_streams(data):
    """Every (version, age, guid) the PDB carries. Version must be 20000404 to parse."""
    streams = []
    position = 0
    while True:
        position = data.find(b"Microsoft C/C++ MSF 7.00\r\n", position)
        if position < 0:
            break
        position += 1
    # The info streams are not located by scanning for a magic string in general, so use
    # the documented version constant as the anchor and require it to match exactly.
    for offset in range(0, len(data) - 28):
        if struct.unpack_from("<I", data, offset)[0] != PDB70_VERSION:
            continue
        signature = struct.unpack_from("<I", data, offset + 4)[0]
        age = struct.unpack_from("<I", data, offset + 8)[0]
        guid = data[offset + 12 : offset + 28]
        if signature == 0 or age == 0:
            continue
        streams.append({"version": PDB70_VERSION, "age": age, "guid": guid.hex()})
    # Deduplicate: the same stream can match at overlapping offsets.
    unique = []
    for stream in streams:
        if stream not in unique:
            unique.append(stream)
    return unique


def count_symbol(data, name):
    needle = name.encode("ascii", "replace")
    total = 0
    start = 0
    while True:
        found = data.find(needle, start)
        if found < 0:
            return total
        total += 1
        start = found + 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dll", required=True)
    parser.add_argument("--pdb")
    parser.add_argument("--symbol", action="append", default=[])
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()

    try:
        dll = read(args.dll)
    except (OSError, ValueError, NormaliserError) as error:
        print(f"cannot read {args.dll}: {error}", file=sys.stderr)
        return 2

    try:
        link = pdb_link(bytes(dll))
        normalised, report = normalise(bytearray(dll), "dll")
    except (NormaliserError, ValueError) as error:
        # Exit 2, not 1: the question could not be answered, which is not the same verdict as
        # "the hashes differ". A caller that treats every nonzero as a mismatch would report a
        # reproducibility failure for a DLL this tool simply could not read.
        print(f"NOT ASKED: {error}", file=sys.stderr)
        return 2
    result = {
        "dllNormalisedSHA256": hashlib.sha256(normalised).hexdigest(),
        "dllRawSHA256": hashlib.sha256(bytes(dll)).hexdigest(),
        "pdbLink": link,
        "normalisationReport": report,
    }

    if args.pdb:
        try:
            pdb = read(args.pdb)
        except OSError as error:
            print(f"cannot read {args.pdb}: {error}", file=sys.stderr)
            return 2
        streams = pdb_info_streams(bytes(pdb))
        result["pdbInfoStreams"] = streams
        if args.symbol:
            result["symbolsExpectPresent"] = {name: count_symbol(bytes(pdb), name) for name in args.symbol}

    if args.json:
        print(json.dumps(result, indent=2, sort_keys=True))
    else:
        print(f"dllNormalisedSHA256 {result['dllNormalisedSHA256']}")
        print(f"dllRawSHA256        {result['dllRawSHA256']}")
        print(f"pdbLink             {link['rsdsGUID']} age {link['dllAge']}")
        for stream in result.get("pdbInfoStreams", []):
            print(f"pdb info stream     age {stream['age']} guid {stream['guid']}")
        for name, count in result.get("symbolsExpectPresent", {}).items():
            print(f"symbol              {name} x{count}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
