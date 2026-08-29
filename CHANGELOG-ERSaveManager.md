# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [1.1.1] - 2026-08-29

### Added

- ERSaveManager: Support downgrading Elden Ring 1.17 saves to the 1.16.2 save format.

## [1.1.0] - 2026-08-02

### Added

- ERSaveManager: Display the detected Elden Ring save-format version for each character slot.
- ERSaveManager: Downgrade an individual character's version fields from its context menu.
- ERSaveManager: Downgrade a complete save to a known older game version, clearing `UserData011` so the game rebuilds its Regulation data and recalculating all changed BND4 entry checksums.

### Changed

- ERSaveManager: Source files relocated to `src/ERSaveManager/` subdirectory (no behavior change)
- ERSaveManager: Save discovery now accepts either the Steam ID parent directory or a Steam ID directory, lists all non-`.bak` files with the Elden Ring save size, and shows parent-directory matches as `<SteamID> / <filename>`.
- ERSaveManager: Full-save downgrade writes through a verified same-directory temporary copy and atomically replaces the source while rejecting stale loaded snapshots.

### Fixed

- ERSaveManager: Correct ListView background and text colors in Light theme mode.
- ERSaveManager: Allow full-save downgrade when populated character slots contain a mixture of older and newer save-format versions without upgrading already older slots.

## [1.0.0] - 2026-04-04

### Added

- Character slot management: view, import, export, and rename across 10 character slots.
- Face data management: import and export face data for up to 15 face slots.
- Built-in NPC face presets from Elden Ring base game and Shadow of the Erdtree DLC.
- Cross-save character import from another Elden Ring save file.
- Character detail panel displaying level, 8 attributes, runes held, deaths, and play time.
- Steam ID re-signing when the save folder does not match the save file's Steam ID.
- Multi-language UI supporting 11 languages with automatic system language detection.
- INI-based configuration persisting save folder, language, and window layout settings.
- Native Win32 GUI with visual styles.
- BND4 save file format parsing with MD5 checksum validation.

[Unreleased]: https://github.com/soarqin/ERSaveManager/compare/saveman-v1.1.1...HEAD
[1.1.1]: https://github.com/soarqin/ERSaveManager/compare/saveman-v1.1.0...saveman-v1.1.1
[1.1.0]: https://github.com/soarqin/ERSaveManager/compare/v1.0.0...saveman-v1.1.0
[1.0.0]: https://github.com/soarqin/ERSaveManager/releases/tag/v1.0.0
