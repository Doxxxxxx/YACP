# Dictionary

YACP supports one on-demand StarDict dictionary while reading an EPUB. The
feature performs no dictionary scan, file access, allocation, task creation, or
word lookup during boot, Quick Resume, rendering, or normal page turns.

## First installation and upgrades

Firmware updates do not include dictionary data or create `/.dictionaries/`.
This is expected when upgrading from 1.6.2. Create that folder at the SD-card
root yourself, then copy a prepared dictionary into it as shown below.
Creating an empty folder alone is not enough.

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

You can also assign **Dictionary** in the reader's **Controls** settings to a
short or long Power press, a long Confirm press, or a long Back press. The
shortcut opens word selection on the current EPUB page. Release the shortcut
button, then confirm a word to look it up. Existing button assignments are
preserved when upgrading. Dictionary shortcuts are only available while
reading an EPUB, just like dictionary lookup in the reader menu.

Definitions are limited to 8 KB per lookup and displayed as plain text. Basic
HTML and XDXF tags plus common entities are converted in place. Single-field
StarDict sequences `m`, `l`, `t`, `y`, and `n` are treated as text, while `h`,
`g`, and `x` are treated as markup. Synonyms, lookup history, suggestions,
compressed dictionaries, and background indexing are not part of this
low-power first version.

## Troubleshooting "No files found"

This message means that YACP could not find a compatible, prepared dictionary.
It does not mean your EPUB is missing.

1. Check that the four files above share the same `<stem>` and are in the same
   directory, directly inside `/.dictionaries/<folder>/` or
   `/dictionaries/<folder>/`. Do not leave an extra extracted folder level.
2. If you only have `.dict.dz`, run the preparation helper with `--decompress`.
   If `.idx.oft.cspt` is missing, run the helper to generate it.
3. Keep only one `.idx` file in each dictionary folder. YACP uses the first
   compatible dictionary; there is currently no dictionary selector.
4. Use stable YACP 1.7.0 or later for Cambridge/XDXF support. Earlier betas
   could reject these dictionaries even when their files were present.
5. After copying the files, leave Dictionary and reopen it before retrying a
   word so discovery runs again.
