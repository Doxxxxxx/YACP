# Dictionary

YACP supports one on-demand StarDict dictionary while reading an EPUB. The
feature performs no dictionary scan, file access, allocation, task creation, or
word lookup during boot, Quick Resume, rendering, or normal page turns.

## Supported files

Place one prepared dictionary in either of these locations:

```text
/.dictionaries/<folder>/<stem>.ifo
/.dictionaries/<folder>/<stem>.idx
/.dictionaries/<folder>/<stem>.dict
/.dictionaries/<folder>/<stem>.idx.oft.cspt
```

`/dictionaries` is also accepted. YACP uses the first compatible dictionary it
finds. A folder with multiple `.idx` files is ignored.

The `.dict` file must be uncompressed. On-device `.dict.dz` decompression is
intentionally unsupported because its 32 KB inflate window would reduce the
largest free heap block on the ESP32-C3.

The `.idx.oft.cspt` accelerator stores a 16-byte headword prefix and a 32-bit
source offset for every 16th `.idx` entry. It stays on the SD card and is never
loaded into a RAM index.

## Preparing a dictionary

Run the helper on a computer before copying the dictionary folder to the SD
card:

```powershell
python scripts/prepare_dictionary.py C:\path\to\dictionary --decompress
```

Omit `--decompress` when an uncompressed `.dict` already exists. Existing
outputs are preserved unless `--force` is supplied.

Prepared `.idx.oft.cspt` files produced by CrossInk use the same format and can
be copied directly.

## Using it

Open an EPUB, enter the reader menu, and select **Dictionary**. Move between
words with the directional controls, press Confirm to look up the highlighted
word, and use Left or Right to change definition pages.

Definitions are limited to 8 KB per lookup and displayed as plain text. Basic
HTML and XDXF tags plus common entities are converted in place. Single-field
StarDict sequences `m`, `l`, `t`, `y`, and `n` are treated as text, while `h`,
`g`, and `x` are treated as markup. Synonyms, lookup history, suggestions,
compressed dictionaries, and background indexing are not part of this
low-power first version.
