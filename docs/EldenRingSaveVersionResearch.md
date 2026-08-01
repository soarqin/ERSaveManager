# Elden Ring Save Version Research

This document records the version relationships used by ERSaveManager's save downgrade feature.
It is intended as the update checklist when a future Elden Ring patch is released.

## Save layout

The PC `ER0000.sl2` file is a BND4 container. The entries relevant to version downgrade are:

- `UserData000` through `UserData009`: character slots. Each entry contains a 16-byte MD5 followed by a `0x280000`-byte payload.
- `UserData010`: character summary data. Its payload starts with an independent summary format version.
- `UserData011`: embedded regulation data. Its standard payload consists of a 16-byte header followed by up to `0x240000` bytes of encrypted `regulation.bin` data.

The standard `UserData011` header contains four little-endian `uint32_t` fields:

| Offset | Meaning |
| --- | --- |
| `0x00` | Magic `0x52454720` (`" REG"` in file order) |
| `0x04` | Header format version, currently `2` |
| `0x08` | Regulation build number, for example `11611000` for Regulation `1.16.1.1000` |
| `0x0C` | Regulation payload capacity, currently `0x240000` |

The field at `+0x08` is a Regulation build number, not a game version. Game and Regulation
versions can change independently. A full-save downgrade must therefore select an explicitly
verified Game and Regulation combination rather than infer Regulation from the game version.

## Save format versions

Analysis of the game executables identified separate character and `UserData010` summary format
versions. The current table is implemented in `src/common/ersave.c` as `version_targets`.

For character payloads, the leading `uint32_t` and the first two `uint32_t` fields in the
16-byte block immediately before the parsed Steam ID are version-bearing fields. The remaining
two fields in that block are state fields and must be preserved.

## Supported full-save combinations

The full-save downgrade menu is based on explicitly observed compatible combinations:

| Game | Regulation | Regulation build |
| --- | --- | ---: |
| 1.02 | 1.02.1 | 10210038 |
| 1.02.1 | 1.02.1 | 10210038 |
| 1.02.2 | 1.02.1 | 10210038 |
| 1.02.3 | 1.02.1 | 10210038 |
| 1.03 | 1.03.1 | 10310059 |
| 1.03.1 | 1.03.1 | 10310059 |
| 1.03.2 | 1.03.2 | 10320064 |
| 1.03.2 | 1.03.3 | 10330078 |
| 1.04 | 1.04.1 | 10410090 |
| 1.04.1 | 1.04.2 | 10420097 |
| 1.05 | 1.05 | 10501000 |
| 1.06 | 1.06 | 10601000 |
| 1.07 | 1.07.1 | 10710188 |
| 1.07 | 1.07 | 10701000 |
| 1.08 | 1.08 | 10801000 |
| 1.08.1 | 1.08.1 | 10811000 |
| 1.09 | 1.09 | 10901000 |
| 1.09.1 | 1.09.1 | 10911000 |
| 1.10 | 1.10 | 11001000 |
| 1.10.1 | 1.10 | 11001000 |
| 1.12 | 1.12.1 | 11210015 |
| 1.12 | 1.12.2 | 11220021 |
| 1.12.3 | 1.12.4 | 11240023 |
| 1.13 | 1.13.1 | 11310027 |
| 1.13 | 1.13.2 | 11320031 |
| 1.14 | 1.14.1 | 11410033 |
| 1.15 | 1.15 | 11501000 |
| 1.16 | 1.16 | 11601000 |
| 1.16.1 | 1.16.1 | 11611000 |

The current release is Game `1.16.2` with Regulation `1.16.1` build `11611000`. It is recorded
here as the current baseline but is intentionally absent from the downgrade menu.

## Regulation files

Full-save downgrade requires the exact encrypted `regulation.bin` for the selected Regulation
build. ERSaveManager searches `<application directory>\Regulations` first, then allows selection
of a Regulations root. Subdirectories must include the decimal build in parentheses, for example:

```text
1.16.1 (11611000)\regulation.bin
```

If the matching file is unavailable, full-save downgrade must stop. Character-only downgrade does
not use Regulation data and remains independent of this list.

Each supported Regulation build also has an exact encrypted file size and MD5 stored with its
downgrade target. The selected file must match both values before any save changes are prepared.
The folder name alone is not considered proof that a `regulation.bin` belongs to the selected build.

Some saves, including saves produced while using a modified Regulation, store encrypted Regulation
data directly at the start of the `UserData011` payload and therefore do not have the standard
16-byte header. A full-save downgrade may accept this source shape when the BND4 entry has the
expected `0x240020` size because the operation replaces the entire payload. The output is normalized
to the standard header shown above and uses the selected official encrypted `regulation.bin`.

## Future update checklist

When a new game or Regulation release appears:

1. Record the exact Game version, Regulation version, and full Regulation build number separately.
2. Analyze the new executable getters and serializers to determine the character format version,
   `UserData010` summary version, and whether the `UserData011` header format changed.
3. Confirm the exact official Game and Regulation combinations. Do not infer them from matching
   version labels or release order.
4. Obtain the matching encrypted `regulation.bin` and verify that it can be copied byte-for-byte
   into `UserData011 + 0x10`, with zero padding to `0x240000` if the layout is unchanged. Record its
   exact encrypted file size and MD5 in the downgrade target.
5. Add the game save-format entry to `version_targets` and each verified full-save combination to
   `save_downgrade_targets` in `src/common/ersave.c`.
6. Add or update selftests for the new character, summary, Regulation build, payload, and MD5 values.
7. Keep the newest current combination out of the downgrade menu until a later version makes it a
   genuine downgrade target.
