# Implementation Plan: Dmenu Ecosystem Resilience & Application Discovery (v2 - Hardened)

## Goal Description
Resolve user-reported friction points in the suckless `dmenu-oomaya` desktop suite:
1. **Dmenu Run (`Alt + P`) Failure**: `dmenu-run` fails silently due to missing `dmenu_path` and `stest` binaries in the system `$PATH`, compounded by an upstream cold-start cache bug in `dmenu_path`.
2. **Missing Applications in App Launcher (`dmenu-desktop`)**: Applications installed via Flatpak (Obsidian, Gear Lever, Warehouse, Parabolic, NewPipe) and user-local XDG paths are invisible because directory scanning is hardcoded rather than dynamically discovering all XDG and Flatpak directories.
3. **Display Manager & System-Wide Binary Path Gap**: Binaries placed only in `~/.local/bin` cause SDDM/display managers to fail or drop into TTY because system display manager sessions initialize with a minimal `/usr/local/bin:/usr/bin:/bin` environment before user shell profiles are evaluated.
4. **Architectural Upgrades Inspired by `dwm-titus`**: Adopt the battle-tested XDG traversal, symlink resolution, application deduplication, and `gtk-launch` execution patterns from `/home/rand/.local/share/dwm-titus/scripts/dwm-quickshell-launcher`.

---

## User Review Required

> [!IMPORTANT]
> **System-Wide Installation Target (`/usr/local/bin`) via `sudo`**:
> To eliminate the display manager login gap (where SDDM/LightDM failed to launch dwm from user `~/.local/bin`), all core binaries and scripts will be deployed to **`/usr/local/bin`** (`sudo make install PREFIX=/usr/local`). 
> - Binaries: `/usr/local/bin/dmenu`, `/usr/local/bin/stest`
> - Suckless Runners: `/usr/local/bin/dmenu_path`, `/usr/local/bin/dmenu_run`
> - Tokyo Night Suite: `/usr/local/bin/dmenu-run`, `/usr/local/bin/dmenu-desktop`, `/usr/local/bin/dmenu-windows`, `/usr/local/bin/dmenu-hub`, `/usr/local/bin/dmenu-power`, `/usr/local/bin/dmenu-clip`, `/usr/local/bin/dmenu-scrot`, `/usr/local/bin/dmenu-omacorn`
> - User Symlinks: Kept in `~/.local/bin` for dual user/system consistency.
> **Action**: We will explicitly prompt you when ready to run the `sudo make install` command.

> [!NOTE]
> **Terminal Hierarchy & Generic Fallback**:
> When a desktop application requires a terminal (`Terminal=true`), the resolution hierarchy is:
> 1. `$TERMINAL` (if set and executable)
> 2. `dwm-terminal --print-command 2>/dev/null` (Ghostty on this system)
> 3. Fallback GUI emulators: `ghostty` $\rightarrow$ `alacritty` $\rightarrow$ `kitty` $\rightarrow$ `xterm`
> 4. Generic TTY fallback: `${TERM:-xterm}` (or direct execution if non-GUI environment)

---

## Technical Deep-Dive: Root Causes & Self-Healing Architecture

### 1. How the `dmenu_path` Cold-Start Bug Occurred & How the Fix Solves It
In upstream suckless `dmenu_path`:
```sh
IFS=:
if stest -dqr -n "$cache" $PATH; then
    stest -flx $PATH | sort -u | tee "$cache"
else
    cat "$cache"
fi
```
- **The Bug**: On a cold start (first run, fresh VM, or when `~/.cache/dmenu_run` is deleted), `$cache` does not exist. `stest -n "$cache"` attempts to `stat()` a non-existent file, prints `perror` ("No such file or directory") to stderr, and exits with code 1. This forces the script into the `else` branch: `cat "$cache"`. `cat` also fails because the file does not exist, emitting zero output. Because `dmenu-run` is executed with `set -eu`, the pipeline fails with a non-zero exit code and silently aborts without opening dmenu.
- **The Solution**: Adding `[ ! -s "$cache" ] || stest -dqr -n "$cache" $PATH 2>/dev/null` ensures that if `$cache` is missing or empty (size 0), the condition evaluates to `true` immediately without invoking `stest -n`. It directly runs `stest -flx $PATH | sort -u | tee "$cache"`, populating the cache and outputting commands in sub-5ms.

### 2. Self-Healing `dmenu-run`
`dmenu-run` will no longer depend on a single binary existing in `$PATH`. It will incorporate a 3-tier self-healing fallback:
1. **Tier 1 (Fast Cached Path)**: Calls `dmenu_path` if available in `$PATH` or `/usr/local/bin` / `~/.local/bin`.
2. **Tier 2 (In-Line `stest` Direct Scan)**: If `dmenu_path` is missing or corrupted, runs `stest -flx $PATH | sort -u` directly.
3. **Tier 3 (POSIX Fallback Directory Walker)**: If `stest` is also missing, falls back to a POSIX directory walker (`find -L ... -maxdepth 1 -type f -perm /111 -printf '%f\n' 2>/dev/null | sort -u`).
4. **Auto-Restoration**: If `/home/rand/dmenu-oomaya/dmenu_path` is detected on disk while missing from `/usr/local/bin`, it logs an advisory and can self-relink.

---

## Proposed Changes

### Component 1: Core Suckless Runners

#### [MODIFY] `dmenu_path`
Fix the cache cold-start bug:
```sh
#!/bin/sh

cachedir="${XDG_CACHE_HOME:-"$HOME/.cache"}"
cache="$cachedir/dmenu_run"

[ -d "$cachedir" ] || mkdir -p "$cachedir"

IFS=:
if [ ! -s "$cache" ] || stest -dqr -n "$cache" $PATH 2>/dev/null; then
	stest -flx $PATH 2>/dev/null | sort -u | tee "$cache"
else
	cat "$cache"
fi
```

#### [MODIFY] `scripts/dmenu-run`
Add multi-tier self-healing execution:
```sh
#!/bin/sh
# dmenu-run — Centered Tokyo Night PATH Command Runner (Self-Healing)
set -eu

PROMPT="  Run >"

get_candidates() {
	# Tier 1: Canonical dmenu_path
	if command -v dmenu_path >/dev/null 2>&1; then
		dmenu_path
		return
	elif [ -x "/usr/local/bin/dmenu_path" ]; then
		/usr/local/bin/dmenu_path
		return
	elif [ -x "$HOME/.local/bin/dmenu_path" ]; then
		"$HOME/.local/bin/dmenu_path"
		return
	fi

	# Tier 2: Direct stest in-line scan
	if command -v stest >/dev/null 2>&1; then
		IFS=:
		stest -flx $PATH 2>/dev/null | sort -u
		return
	fi

	# Tier 3: Pure POSIX find directory traversal across $PATH
	OLD_IFS=$IFS
	IFS=:
	for dir in $PATH; do
		[ -d "$dir" ] || continue
		find -L "$dir" -maxdepth 1 -type f -perm /111 -printf '%f\n' 2>/dev/null
	done | sort -u
	IFS=$OLD_IFS
}

cmd=$(get_candidates | dmenu -p "$PROMPT" -l 10 "$@")
[ -n "$cmd" ] || exit 0

if command -v setsid >/dev/null 2>&1; then
	setsid ${SHELL:-"/bin/sh"} -c "$cmd" >/dev/null 2>&1 &
else
	nohup ${SHELL:-"/bin/sh"} -c "$cmd" >/dev/null 2>&1 &
fi
```

---

### Component 2: Application Discovery & Launcher (`dmenu-desktop`)

#### [MODIFY] `scripts/dmenu-desktop`
Adopt `dwm-quickshell-launcher` architectural strengths:
1. **Dynamic XDG & Flatpak Directory Scraper**:
   - Discovers `$XDG_DATA_HOME/applications`, `$HOME/.local/share/flatpak/exports/share/applications`, `/var/lib/flatpak/exports/share/applications`, `/var/lib/snapd/desktop/applications`, and all `${dir}/applications` in `$XDG_DATA_DIRS`.
   - Traverses symlinks (`-type f -o -type l`), critical for Flatpaks.
2. **Desktop File Deduplication**:
   - Deduplicates by desktop file ID (`seen_ids=:`) so user overrides in `~/.local/share/applications` supersede system definitions.
3. **Execution Engine (`gtk-launch` + `setsid`)**:
   - If `gtk-launch` is present, uses `gtk-launch "$desktop_id"` (natively handles Flatpak sandboxes, working directories, and field codes).
   - Fallback parses `Exec`, strips Flatpak `@@.*@@` tokens and `%[a-zA-Z]` field codes.
4. **Terminal Wrapping**:
   - Queries `dwm-terminal --print-command 2>/dev/null` $\rightarrow$ `ghostty` $\rightarrow$ `alacritty` $\rightarrow$ `${TERM:-xterm}`.
5. **Tokyo Night Glyphs**:
   - Rich icon palette covering Browsers, Terminals, Editors, File Managers, Audio/Media, Chat/Communication, Notes/Obsidian, System/Tools, and Settings.

---

### Component 3: Build & System Installation

#### [MODIFY] `Makefile`
Update install targets so `make install PREFIX=/usr/local` cleanly installs:
- Binaries: `dmenu`, `stest` $\rightarrow$ `$(DESTDIR)$(PREFIX)/bin`
- Scripts & Runners: `dmenu_path`, `dmenu_run`, and all `scripts/dmenu-*`
- Man pages: `dmenu.1`, `stest.1` $\rightarrow$ `$(DESTDIR)$(MANPREFIX)/man1`
- Sets permissions to `755`.

---

## Verification Plan

### Phase 1: Automated Health Tests
```bash
# 1. Compile cleanly with strict flags
cd /home/rand/dmenu-oomaya
make clean && make

# 2. Test cold-start dmenu_path
rm -f ~/.cache/dmenu_run
./dmenu_path | head -n 10
test -s ~/.cache/dmenu_run && echo "PASS: Cold start cache generated cleanly"

# 3. Test self-healing dmenu-run
PATH="/usr/bin:/bin" ./scripts/dmenu-run </dev/null || true

# 4. Verify Application Discovery Coverage
./scripts/dmenu-desktop --dry-run | grep -E 'Obsidian|Gear Lever|Warehouse|Parabolic|Brave Origin|Ghostty|Omacorn'
```

### Phase 2: System Installation & Sudo Tollgate
Prompt the user for permission to execute:
```bash
sudo make -C /home/rand/dmenu-oomaya install PREFIX=/usr/local
# Also update ~/.local/bin symlinks
make -C /home/rand/dmenu-oomaya install PREFIX="$HOME/.local"
```

### Phase 3: Live Manual Verification
1. **Alt + P (`dmenu-run`)**: Press `Alt + P`, type `uname -r`, press Enter. Verify smooth execution.
2. **Super + C / Super + D (`dmenu-desktop`)**: Launch Obsidian or Gear Lever. Verify Flatpak apps spawn instantly.
3. **Super + Shift + H (`dmenu-hub`)**: Verify all 8 sub-launchers open smoothly.
4. **Alt + Tab (`dmenu-windows`)**: Verify window switcher lists active windows and shifts focus immediately.
