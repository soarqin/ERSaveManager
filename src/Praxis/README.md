# Praxis

Praxis is a practice save tool for Elden Ring (and potentially other games) that allows you to quickly backup and restore save files using global hotkeys. It features a tree-structured save library and automatic ring backups to ensure you never lose progress.

## Features

- **Full + Slot Backup/Restore** — Backup or restore the entire save file or just the currently active character slot.
- **Global Hotkeys** — Perform backups and restores instantly from within the game.
- **Tree-Structured Save Library** — Organize your saves in a hierarchical tree view. Rename, move, and delete saves with ease.
- **Recycle Bin Support** — Deleted saves are moved to the Windows Recycle Bin.
- **Ring Backup System** — Automatically maintains a 5-slot FIFO ring of backups every time you restore, allowing you to undo the last restore operation.
- **Multi-Game Ready** — Designed with a backend interface to support multiple games (currently implements Elden Ring).

## Default Hotkeys

| Action | Default Hotkey |
|--------|---------------|
| Backup Full Save | Ctrl+Shift+F5 |
| Restore Full Save | Ctrl+Shift+F9 |
| Backup Current Slot | Ctrl+Shift+F6 |
| Restore Current Slot | Ctrl+Shift+F10 |
| Undo Last Restore | Ctrl+Shift+Z |

## Tree Storage

Saves are stored in a tree structure on disk, mirroring the organization in the UI. This allows for easy management and categorization of practice states.

## Ring Backup

The Ring Backup system (located in the `.praxis_ring/` hidden directory) automatically captures the state of your save file before any restore operation. If you accidentally restore the wrong save, you can use the Undo command to revert to the previous state.

## Build & Run

Refer to the root [README.md](../../README.md) for build instructions. Once built, launch `Praxis.exe`.

## Backend Interface

Praxis uses a compile-time vtable (`game_backend_t`) defined in `src/Praxis/game_backend.h`. This allows the core logic to remain game-agnostic while specific backends (like `er_backend.c`) handle the details of save file locations and slot manipulation.
