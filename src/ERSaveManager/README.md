# ERSaveManager

ERSaveManager is a Windows desktop tool for Elden Ring save editing. It focuses on character slots, face data, NPC face presets, and Steam ID re-signing through a native Win32 interface.

## Supported Save

- Game: Elden Ring
- Save file: any non-`.bak` file of the Elden Ring save size (normally `ER0000.sl2`)
- Default location: `%APPDATA%\EldenRing\<decimal SteamID>\`
- Character slots: 10
- Face data slots: 15

## Features

- View character slots with name, body type, level, play time, attributes, runes held, and death count.
- Inspect the save-format version of each occupied character slot.
- Import, export, and rename character slots.
- Downgrade one character's version fields or downgrade the complete save to a verified Game and Regulation version combination.
- Import a character slot from another full Elden Ring save file.
- Manage face data slots, including import and export.
- Apply built-in NPC face presets from the base game and Shadow of the Erdtree.
- Detect Steam ID mismatches between the save folder and save file, then offer to re-sign the save.
- Choose Fast, Normal, or Max compression for exported character files.
- Use System, Light, or Dark theme mode.
- Use the UI in 11 languages with automatic system language detection.

## Before You Use It

Close Elden Ring before importing, renaming, re-signing, or downgrading save data. These operations modify the selected save file.

Keep a manual backup of your save folder before editing. Version downgrade uses an atomic temporary-copy replacement, but it is not a substitute for a separate backup.

## Quick Start

1. Launch `ERSaveManager.exe`.
2. If the save folder is not detected correctly, click **Change Save Folder** and select either the folder that contains your Steam ID subfolders or a specific Steam ID folder.
3. Select a detected save file from the dropdown. Parent folders show entries as `<SteamID> / <filename>`; Steam ID folders show only the filename.
4. Select a character in the **Characters** list to view its details.
5. Use **Import Character**, **Export Character**, or **Rename Character** for the selected slot.
6. Click **Face Data...** to manage face slots and NPC presets.

## Version Downgrade

### Downgrade One Character

1. Right-click an occupied character slot.
2. Open **Downgrade Character** and select an older game version.
3. Review and confirm the warning.

This changes only that character's save-format version fields and checksum. It does not replace the embedded Regulation data or change the complete save's summary version.

### Downgrade the Complete Save

1. Close Elden Ring and make a separate backup of the save folder.
2. Open **Options > Downgrade Save**.
3. Select an explicitly listed Game and Regulation combination.
4. Review and confirm the warning.

A complete downgrade updates occupied character slots and `UserData010`, replaces `UserData011` with the matching encrypted `regulation.bin`, and recalculates the changed BND4 checksums. Character slots that are already older than the selected target remain at their existing version.

ERSaveManager first searches for the required file under `Regulations` beside `ERSaveManager.exe`. The expected layout is:

```text
Regulations\1.16.1 (11611000)\regulation.bin
```

If no local file matches the selected Regulation build, select a Regulations root containing the same folder layout. The downgrade is cancelled when the exact Regulation build is unavailable. Game and Regulation versions are independent, so only verified combinations appear in the menu.

The currently verified combinations and the process for adding future game updates are documented in [Elden Ring Save Version Research](../../docs/EldenRingSaveVersionResearch.md).

## Character Workflows

### Export a Character

1. Select an occupied character slot.
2. Click **Export Character**.
3. Choose a destination file.

The exported file can be imported back into ERSaveManager later.

### Import a Character File

1. Select the destination character slot.
2. Click **Import Character**.
3. Pick an exported character file.
4. Confirm the result in the character list.

The destination slot is overwritten.

### Import from Another Save

1. Select the destination character slot.
2. Click **Import Character**.
3. Pick another Elden Ring save file.
4. Choose which character from that save to import.

## Face Data Workflows

1. Click **Face Data...**.
2. Select a face slot.
3. Use **Import Face Data** or **Export Face Data** for external files.
4. Use **Import NPC face data** to apply a built-in preset.

Imported face data overwrites the selected face slot.

## Steam ID Re-signing

When ERSaveManager loads a save, it compares the Steam ID inside the save file with the selected save folder name. If they differ, it asks whether to update the save file's Steam ID so Elden Ring can accept it in that folder.

## Options

- **Language** changes the UI language.
- **Options > Compression Ratio** changes exported character-file compression.
- **Options > Theme** selects System, Light, or Dark mode.
- **Options > Downgrade Save** selects a verified full-save Game and Regulation downgrade target.
