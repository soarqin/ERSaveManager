# Elden Ring Save Version Research

This document records the version relationships used by ERSaveManager's save downgrade feature.
It is intended as the update checklist when a future Elden Ring patch is released.

## Save layout

The PC `ER0000.sl2` file is a BND4 container. The entries relevant to version downgrade are:

- `UserData000` through `UserData009`: character slots. Each entry contains a 16-byte MD5 followed by a `0x280000`-byte payload.
- `UserData010`: character summary data. Its payload starts with an independent summary format version.
- `UserData011`: embedded regulation data. Its standard payload consists of a 16-byte header followed by up to `0x240000` bytes of encrypted Regulation data.

The standard `UserData011` header contains four little-endian `uint32_t` fields:

| Offset | Meaning |
| --- | --- |
| `0x00` | Magic `0x52454720` (`" REG"` in file order) |
| `0x04` | Header format version, currently `2` |
| `0x08` | Regulation build number, for example `11611000` for Regulation `1.16.1.1000` |
| `0x0C` | Regulation payload capacity, currently `0x240000` |

The field at `+0x08` is a Regulation build number, not a game version. Game and Regulation
versions can change independently. ERSaveManager therefore does not infer or inject Regulation
data while downgrading.

## Save format versions

Analysis of the game executables identified separate character and `UserData010` summary format
versions. The current table is implemented in `src/common/ersave.c` as `version_targets`.

For character payloads, the leading `uint32_t` and the first two `uint32_t` fields in the
16-byte block immediately before the parsed Steam ID are version-bearing fields. The remaining
two fields in that block are state fields and must be preserved.

## Regulation reset

Both full-save and character-only downgrade clear the complete `UserData011` payload and recompute
its MD5. When Elden Ring does not find the expected magic bytes, it replaces the Regulation region
instead of rejecting the save. No external Regulation archive is required.

If the first four payload bytes are already zero, ERSaveManager leaves the entire entry, including
its existing MD5, untouched. The missing-magic state already tells the game to rebuild the region.

## Future update checklist

When a new game or Regulation release appears:

1. Record the exact Game version and save-format versions.
2. Analyze the new executable getters and serializers to determine the character format version,
   `UserData010` summary version, and whether the `UserData011` header format changed.
3. Confirm that clearing `UserData011` still causes the game to rebuild the Regulation region.
4. Add the game save-format entry to `version_targets` in `src/common/ersave.c`.
5. Add or update selftests for the character, summary, and Regulation reset behavior.
6. Keep the newest current version out of the downgrade menu until a later version makes it a
   genuine downgrade target.
