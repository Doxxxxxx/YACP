#!/usr/bin/env python3
"""Prepare an uncompressed StarDict dictionary for YACP."""

from __future__ import annotations

import argparse
import gzip
import os
import shutil
import struct
from pathlib import Path


CSPT_MAGIC = b"CSPT"
CSPT_VERSION = 1
PREFIX_BYTES = 16
STRIDE = 16


def resolve_idx(source: Path) -> Path:
    if source.is_dir():
        indexes = sorted(source.glob("*.idx"))
        if len(indexes) != 1:
            raise ValueError(f"Expected exactly one .idx in {source}, found {len(indexes)}")
        return indexes[0]
    if source.suffix.lower() == ".ifo":
        return source.with_suffix(".idx")
    if source.suffix.lower() != ".idx":
        raise ValueError("Source must be a dictionary directory, .ifo, or .idx file")
    return source


def read_offset_bytes(ifo_path: Path) -> int:
    if not ifo_path.is_file():
        raise FileNotFoundError(f"Missing StarDict metadata: {ifo_path}")
    metadata = ifo_path.read_text(encoding="utf-8", errors="replace")
    if "idxoffsetbits=64" in metadata:
        raise ValueError("YACP does not support StarDict dictionaries with 64-bit index offsets")
    return 4


def decompress_dictzip(idx_path: Path, force: bool) -> Path:
    dict_path = idx_path.with_suffix(".dict")
    compressed_path = idx_path.with_suffix(".dict.dz")
    if dict_path.exists() and not force:
        return dict_path
    if not compressed_path.is_file():
        raise FileNotFoundError(f"Missing {dict_path.name} and {compressed_path.name}")
    temporary = Path(str(dict_path) + ".tmp")
    if temporary.exists():
        raise FileExistsError(f"Temporary output already exists: {temporary}")
    with gzip.open(compressed_path, "rb") as source, temporary.open("xb") as destination:
        shutil.copyfileobj(source, destination, length=1024 * 1024)
    os.replace(temporary, dict_path)
    return dict_path


def read_c_string(index) -> bytes | None:
    value = bytearray()
    while True:
        byte = index.read(1)
        if not byte:
            if not value:
                return None
            raise ValueError("Truncated .idx headword")
        if byte == b"\0":
            return bytes(value)
        value.extend(byte)


def build_cspt(idx_path: Path, force: bool) -> Path:
    if not idx_path.is_file():
        raise FileNotFoundError(f"Missing StarDict index: {idx_path}")
    offset_bytes = read_offset_bytes(idx_path.with_suffix(".ifo"))
    output = Path(str(idx_path) + ".oft.cspt")
    if output.exists() and not force:
        raise FileExistsError(f"Refusing to overwrite {output}; pass --force")

    temporary = Path(str(output) + ".tmp")
    if temporary.exists():
        raise FileExistsError(f"Temporary output already exists: {temporary}")

    source_entries = 0
    samples = 0
    with idx_path.open("rb") as index, temporary.open("xb") as prepared:
        prepared.write(struct.pack("<4sBBHI", CSPT_MAGIC, CSPT_VERSION, PREFIX_BYTES, STRIDE, 0))
        while True:
            source_offset = index.tell()
            headword = read_c_string(index)
            if headword is None:
                break
            location = index.read(offset_bytes + 4)
            if len(location) != offset_bytes + 4:
                raise ValueError(f"Truncated .idx location at byte {source_offset}")
            if source_entries % STRIDE == 0:
                prefix = headword[:PREFIX_BYTES].ljust(PREFIX_BYTES, b"\0")
                prepared.write(prefix)
                prepared.write(struct.pack("<I", source_offset))
                samples += 1
            source_entries += 1
        prepared.seek(0)
        prepared.write(struct.pack("<4sBBHI", CSPT_MAGIC, CSPT_VERSION, PREFIX_BYTES, STRIDE, samples))

    os.replace(temporary, output)
    print(f"Prepared {output} with {samples} samples for {source_entries} words")
    return output


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Generate YACP's low-RAM .idx.oft.cspt accelerator for a StarDict dictionary."
    )
    parser.add_argument("source", type=Path, help="Dictionary directory, .ifo, or .idx path")
    parser.add_argument("--decompress", action="store_true", help="Expand .dict.dz to .dict when needed")
    parser.add_argument("--force", action="store_true", help="Replace existing prepared outputs")
    args = parser.parse_args()

    idx_path = resolve_idx(args.source.resolve())
    if args.decompress:
        dict_path = decompress_dictzip(idx_path, args.force)
    else:
        dict_path = idx_path.with_suffix(".dict")
        if not dict_path.is_file():
            raise FileNotFoundError(f"Missing uncompressed dictionary data: {dict_path}")
    build_cspt(idx_path, args.force)
    print(f"Dictionary data ready at {dict_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
