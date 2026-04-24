# ERSaveManager

A lightweight Windows desktop application for managing Elden Ring save files. Import and export character slots, manage face data, apply built-in NPC face presets, re-sign Steam IDs, and more — all through a native Win32 GUI.

## Features

- **Character Management** — View all 10 character slots with name, body type, level, and play time. Import, export, and rename characters freely.
- **Face Data Management** — Manage up to 15 face data slots. Import and export face presets to and from files.
- **Built-in NPC Face Presets** — Apply face data from a wide selection of NPCs, including both base game and Shadow of the Erdtree DLC characters (interactable and non-interactable).
- **Cross-Save Character Import** — Import a character directly from another Elden Ring save file by picking which slot to pull from.
- **Steam ID Re-signing** — Automatically detects Steam ID mismatches between save data and folder name, and offers to re-sign so the game accepts the file.
- **Character Detail Panel** — View detailed stats including all 8 attributes (Vigor, Mind, Endurance, Strength, Dexterity, Intelligence, Faith, Arcane), runes held, and death count.
- **Multi-Language UI** — Supports 11 languages with automatic system language detection:
  English, Français, Deutsch, Italiano, Español, Português, Русский, 日本語, 한국어, 简体中文, 繁體中文
- **Persistent Configuration** — Remembers save folder path, language preference, and window position/size across sessions via INI file.

## Usage

1. Launch `ERSaveManager.exe`.
2. The application auto-detects your Elden Ring save folder. Click **Change Save Folder** to select a different location.
3. Pick a Steam ID subfolder from the dropdown to load the corresponding save file.
4. Use the **Characters** panel on the left to view, import, export, or rename character slots.
5. Click **Face Data...** to open the face data management dialog for importing, exporting, or applying NPC face presets.
6. Switch the UI language from the **Language** menu.
