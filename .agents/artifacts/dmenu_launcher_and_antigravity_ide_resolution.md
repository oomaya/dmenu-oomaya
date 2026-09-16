# Root Cause Diagnosis & Resolution: Antigravity IDE Dmenu Launch Failure

**Date**: September 16, 2026  
**Host**: `fedora44` (`dwm-oomaya#2`)  
**Repositories Updated**: `oomaya/dmenu-oomaya` (`faf204b`), `oomaya/dotfiles` (`0c0650e`)  

---

## 1. Executive Summary

When closing Antigravity IDE with `Super + Q` and attempting to re-launch it via `dmenu-desktop` (`Super + D`), nothing appeared to happen. Investigation revealed three interacting root causes across the desktop menu and launcher scripts:
1. **The `sort -u -k2,2` Title-Collision Bug in `dmenu-desktop`**: `dmenu-desktop` sorted application entries with `sort -u -k2,2`. Because whitespace defines sort fields, Field 1 was the Nerd Font icon and Field 2 was **only the first word** of the application name. Any two applications sharing their first word (e.g. "Antigravity IDE" vs "Antigravity Ide", "LibreOffice Writer" vs "LibreOffice Impress", "Bluetooth Manager" vs "Bluetooth Settings") collided—dropping 14 valid applications from the menu entirely.
2. **Conflicting Desktop Entries Pointing to CLI**: `antigravity.desktop` was named "Antigravity IDE" but pointed to the headless CLI wrapper (`antigravity`), while `antigravity-ide.desktop` pointed to the GUI (`antigravity-ide`). The `-k2,2` collision arbitrarily retained the CLI entry and discarded the GUI entry. Clicking it launched a headless script without a TTY, terminating instantly.
3. **The `pgrep -f "/antigravity-ide"` Daemon Trap**: The launcher script checked if the IDE was already running using `pgrep -f "/antigravity-ide"`. However, the background artifact sync daemon runs `inotifywait` watching `/home/rand/.gemini/antigravity-ide/brain`. The substring matched `inotifywait`, falsely signaling that the IDE was already running, causing it to attempt forwarding arguments to a nonexistent GUI window and exit.

---

## 2. Technical Root Cause Breakdown

### Issue 1: `dmenu-desktop` First-Word Deduplication
In `/home/rand/dmenu-oomaya/scripts/dmenu-desktop`:
```awk
# Before:
	}' | sort -u -k2,2
```
Field 1 is the unicode category glyph (e.g., ` `).  
Field 2 is the first word of the app name (`Antigravity`).  
The `-k2,2` switch instructed `sort` to deduplicate strictly on Field 2. As a consequence, 14 applications sharing leading words were silently discarded from the user's desktop menu:
- `Antigravity Ide` (dropped in favor of `Antigravity IDE` pointing to CLI)
- `LibreOffice Impress` & `LibreOffice Writer`
- `KDE Connect`, `KDE Connect Indicator`, `KDE Connect SMS`, `KDE Partition Manager`
- `Bluetooth Manager`
- `System Settings`
- `Thunar Preferences`

### Issue 2: CLI vs GUI Desktop Entry Mismatch
- `antigravity.desktop`: `Name=Antigravity IDE`, `Exec=~/.local/bin/antigravity %F`, `Terminal=false`.
- `antigravity-ide.desktop`: `Name=Antigravity Ide`, `Exec=~/.local/bin/antigravity-ide %F`, `Terminal=false`.

`antigravity` is the fast CLI wrapper for `agy`. Running it with `Terminal=false` runs in the background and exits cleanly with 0 without rendering any window.

### Issue 3: `pgrep -f` Substring Collision with `inotifywait`
In `~/.local/bin/antigravity-ide`:
```bash
# Before:
if pgrep -f "/antigravity-ide" >/dev/null 2>&1; then
    "$IDE_BIN" "$@" &
    ...
    exit 0
fi
```
`antigravity-artifact-sync.service` spawns:
```bash
inotifywait -r -q -t 900 -e close_write,moved_to --include '.*\.(md|json)$' /home/rand/.gemini/antigravity-cli/brain /home/rand/.gemini/antigravity-ide/brain
```
Because `/home/rand/.gemini/antigravity-ide/brain` contains `/antigravity-ide`, `pgrep -f "/antigravity-ide"` returned PID 1229473. The launcher believed an instance was already alive, skipped launching the binary, and exited.

---

## 3. Implemented Fixes

### 1. `dmenu-desktop` Sorting & Deduplication (`oomaya/dmenu-oomaya@faf204b`)
Updated line 148 in `/home/rand/dmenu-oomaya/scripts/dmenu-desktop` and deployed to `~/.local/bin/dmenu-desktop`:
```awk
# After:
	}' | sort -u -f -k2
```
- `-k2`: Sorts starting from Field 2 through the end of the line (evaluating the full application title and command).
- `-f`: Case-insensitive ordering.
- `-u`: Removes exact duplicates while preserving distinct applications.

### 2. Desktop Entries Federation (`oomaya/dotfiles@0c0650e`)
Managed cleanly under `~/dotfiles/antigravity/.local/share/applications/` and stowed to `~/.local/share/applications/`:
- **`antigravity-ide.desktop`**:
  ```ini
  [Desktop Entry]
  Version=1.0
  Type=Application
  Name=Antigravity IDE
  GenericName=Antigravity IDE
  Comment=Antigravity AI-First IDE
  Exec=antigravity-ide %F
  Icon=antigravity-ide
  Terminal=false
  Categories=Development;IDE;
  StartupNotify=true
  StartupWMClass=antigravity ide
  ```
- **`antigravity.desktop`**:
  ```ini
  [Desktop Entry]
  Version=1.0
  Type=Application
  Name=Antigravity CLI
  GenericName=Antigravity CLI
  Comment=Antigravity AI Assistant CLI
  Exec=antigravity
  Icon=antigravity-ide
  Terminal=true
  Categories=Development;Utility;
  StartupNotify=true
  StartupWMClass=antigravity
  ```
- Placed standard high-resolution icon in `~/.local/share/icons/hicolor/512x512/apps/antigravity-ide.png`.

### 3. Resilient Process Matching & Dead-Lock Recovery (`oomaya/dotfiles@0c0650e`)
Updated `~/dotfiles/antigravity/.local/bin/antigravity-ide`:
1. Switched from loose `pgrep -f` to exact process name match (`pgrep -x "antigravity-ide"`) combined with dead-PID verification (`kill -0 "$LOCK_PID"`).
2. Pointed candidates directly to the real ELF binary (`~/.local/share/antigravity-ide/antigravity-ide` and `~/.local/share/antigravity/antigravity-ide`).
3. Restored `/home/rand/.local/share/antigravity-ide/bin/antigravity-ide` to match upstream CLI proxy.

---

## 4. Verification Results

1. **`dmenu-desktop --dry-run`**:
   ```text
      Antigravity CLI                     │  ghostty -e antigravity
      Antigravity IDE                     │  antigravity-ide
   ```
   Both applications now present, correctly categorized, and pointing to proper execution contexts.
2. **Dmenu Launch Simulation**:
   Executed `setsid /bin/sh -c "antigravity-ide" >/dev/null 2>&1 &`. Antigravity IDE launched immediately and mapped window `0x06800004` to tag 1.
3. **Stale Lock Recovery**:
   Simulated crash / kill leaving stale PID in `code.lock`. Next invocation automatically purged the stale lock and spawned cleanly.
4. **Full Test Suite**:
   `~/dotfiles/verify.sh` executed 100% green across all 74 automated checks.
