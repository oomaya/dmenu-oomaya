# Architectural Synthesis: Systemic Learnings from Dmenu & DWM-Titus

**Date**: 2026-09-14 22:58 KST  
**Environment**: Fedora Linux 44 (`dwm-oomaya#2`, Kernel 7.2.5)  
**Authors**: Rand & Antigravity Tech Lead Pair  

---

## 1. The Display Manager Path Gap (`/usr/local/bin` vs `~/.local/bin`)

### The Phenomenon
During initial testing or fresh VM spin-ups, user configurations in `~/.local/bin` worked when running inside an active shell or tmux session, but dropped into TTY or crashed on graphical login via display managers (SDDM, LightDM, GDM).

### The Root Mechanism
Display managers initialize graphical Xsessions (`/usr/share/xsessions/dwm.desktop`) in a clean, non-interactive environment with a minimal system `$PATH`:
```text
/usr/local/bin:/usr/bin:/bin
```
User-level shell initializers (`~/.bashrc`, `~/.zshrc`, `~/.profile`) are **not yet sourced** when the display manager spawns the window manager binary or executes early session autostarts. If `dwm`, `dmenu`, or session helpers reside only in `~/.local/bin`, the display manager fails to spawn them.

### The Architectural Standard
1. **Global Canonical Location**: Core window manager binaries, runners, and display helpers must be installed to `/usr/local/bin` (`PREFIX=/usr/local`).
2. **User Convenience**: User-local copies or symlinks in `~/.local/bin` are maintained for non-root development and quick hacking.
3. **Explicit Privilege Escalation**: System-wide installation targets must always be handled through a dedicated `sudo make install` tollgate.

---

## 2. The Upstream Suckless `dmenu_path` Cold-Start Bug

### The Phenomenon
Pressing `Alt + P` (`dmenu-run`) on a fresh system, after cache cleanup, or during a cold start resulted in total silence—no menu appeared and no error was surfaced to the user.

### The Root Mechanism
Upstream suckless `dmenu_path` contains the following logic:
```sh
IFS=:
if stest -dqr -n "$cache" $PATH; then
    stest -flx $PATH | sort -u | tee "$cache"
else
    cat "$cache"
fi
```
The `-n "$cache"` flag tests if any directory in `$PATH` is newer than `$cache`. On a cold start (when `~/.cache/dmenu_run` does not yet exist):
1. `stest` calls `stat("$cache")`, which fails with `ENOENT`.
2. `stest` prints `perror` ("No such file or directory") to `stderr` and exits with code 1.
3. The `if` condition evaluates to false, jumping to `else: cat "$cache"`.
4. `cat "$cache"` fails because the file does not exist, emitting an empty stdout stream.
5. In parent scripts executing under `set -eu` (such as `dmenu-run`), the pipeline fails with a non-zero exit code, silently terminating the process before `dmenu` can map its X11 window.

### The Solution
```sh
[ -d "$cachedir" ] || mkdir -p "$cachedir"

IFS=:
if [ ! -s "$cache" ] || stest -dqr -n "$cache" $PATH 2>/dev/null; then
    stest -flx $PATH 2>/dev/null | sort -u | tee "$cache"
else
    cat "$cache"
fi
```
Checking `[ ! -s "$cache" ]` guarantees that if the cache is missing or zero bytes, the script immediately skips the `stest -n` check, scans `$PATH`, populates the cache file, and streams all executables into dmenu with zero stderr noise.

---

## 3. Multi-Tier Self-Healing Pattern for Desktop Runners

### The Problem
A desktop shortcut (`Alt + P`) is a mission-critical emergency fallback. If helper utilities (`dmenu_path`, `stest`) are temporarily uninstalled, relocated, or corrupted, the hotkey should never be dead.

### The Resilient Architecture
`dmenu-run` implements a 3-tier cascade:
```sh
get_candidates() {
    # Tier 1: Canonical dmenu_path (sub-millisecond cache)
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

    # Tier 2: Direct stest in-line scan (fast filesystem inspection)
    if command -v stest >/dev/null 2>&1; then
        IFS=:
        stest -flx $PATH 2>/dev/null | sort -u
        return
    fi

    # Tier 3: Pure POSIX find directory traversal across $PATH (failsafe)
    OLD_IFS=$IFS
    IFS=:
    for dir in $PATH; do
        [ -d "$dir" ] || continue
        find -L "$dir" -maxdepth 1 -type f -perm /111 -printf '%f\n' 2>/dev/null
    done | sort -u
    IFS=$OLD_IFS
}
```
Even if `dmenu_path` and `stest` are wiped from the machine, Tier 3 guarantees that `Alt + P` will still open dmenu with all runnable system commands.

---

## 4. XDG & Flatpak Application Discovery (Learnings from `dwm-titus`)

Inspecting `/home/rand/.local/share/dwm-titus/scripts/dwm-quickshell-launcher` yielded critical lessons for desktop application parsing:

### 1. The Symlink Invariant
Flatpak applications export their desktop files as symbolic links:
```text
/var/lib/flatpak/exports/share/applications/md.obsidian.Obsidian.desktop ->
../../../app/md.obsidian.Obsidian/current/active/export/share/applications/md.obsidian.Obsidian.desktop
```
A standard file search (`find -type f` or `ls *.desktop` with shallow globbing) skips these files completely. Any robust XDG scanner must traverse symlinks:
```sh
find "$dir" \( -type f -o -type l \) -name '*.desktop' 2>/dev/null
```

### 2. Comprehensive Directory Scraping (`data_dirs`)
Relying on hardcoded paths (`/usr/share/applications`) misses user-installed software, Flatpaks, Snap packages, and Nix profiles. The authoritative set of application directories is dynamically assembled:
- `$XDG_DATA_HOME/applications` (or `$HOME/.local/share/applications`)
- User Flatpak: `$HOME/.local/share/flatpak/exports/share/applications`
- System Flatpak: `/var/lib/flatpak/exports/share/applications`
- Snap & Nix: `/var/lib/snapd/desktop/applications`, `~/.nix-profile/share/applications`
- `$XDG_DATA_DIRS`: Every `${dir}/applications` in `/usr/local/share:/usr/share`

### 3. Application Deduplication
Multiple versions of the same desktop file may exist across directories (e.g. a user customization in `~/.local/share/applications/firefox.desktop` vs system `/usr/share/applications/firefox.desktop`). Deduplicating by desktop ID (`seen_ids=:`) preserves proper XDG override precedence, ensuring user configs take priority.

### 4. Clean Flatpak Execution & Field Code Sanitization
Flatpak `Exec` lines contain proxy arguments and field codes:
```text
Exec=/usr/bin/flatpak run ... --command=obsidian.sh --file-forwarding md.obsidian.Obsidian @@u %U @@
```
- Flatpak forwarder tokens (`@@.*@@`) and URL/file field codes (`%[a-zA-Z]`) must be stripped before shell execution so no empty placeholder parameters are passed.
- `gtk-launch "$desktop_id"` natively resolves Flatpak sandbox arguments, environment variables, and D-Bus activation.

### 5. Terminal Application Wrapping
For desktop entries with `Terminal=true`, `gtk-launch` often fails in minimalist window managers lacking GNOME/KDE terminal brokers. The script must explicitly wrap commands using:
```sh
$TERMINAL -e $cmd
```
With terminal resolution cascading: `$TERMINAL` $\rightarrow$ `dwm-terminal --print-command` $\rightarrow$ `ghostty` $\rightarrow$ `alacritty` $\rightarrow$ `kitty` $\rightarrow$ `${TERM:-xterm}` $\rightarrow$ raw shell.

---

## 5. Process-Aware File Replacement Law (`ETXTBSY`)

### The Problem
During live package compilation and installation (`make install`), using `cp -f` to overwrite an actively running binary (`dmenu`, `dwm`, or daemon scripts) fails with:
```text
cp: cannot create regular file '...': Text file busy (ETXTBSY)
```

### The Solution
Use `install -Dm755` instead of `cp -f`:
```makefile
install -Dm755 dmenu $(DESTDIR)$(PREFIX)/bin/dmenu
install -Dm755 stest $(DESTDIR)$(PREFIX)/bin/stest
```
`install` unlinks the active inode first and writes the new file under a fresh inode, allowing running processes to maintain their open file descriptor in memory while the new binary is immediately available on disk for subsequent invocations.

---

## 6. Summary Matrix of Learnings

| Area | Traditional / Upstream Approach | Hardened Oomaya Standard |
| :--- | :--- | :--- |
| **Binary Paths** | `~/.local/bin` only | `/usr/local/bin` (global) + `~/.local/bin` (convenience) |
| **Cold Start Cache** | `stest -n "$cache"` crashes on missing file | `[ ! -s "$cache" ]` guard forces immediate cache generation |
| **Runner Resilience** | Single binary call (`dmenu_path`) | 3-tier cascade: `dmenu_path` $\rightarrow$ `stest` $\rightarrow$ POSIX walker |
| **Flatpak Discovery** | Standard glob `*.desktop` (misses symlinks) | Symlink-aware find `\( -type f -o -type l \)` across `$XDG_DATA_DIRS` |
| **App Execution** | Raw `nohup /bin/sh -c` | `gtk-launch` / `setsid` with field-code & Flatpak token stripping |
| **File Replacement** | `cp -f` (causes `ETXTBSY`) | `install -Dm755` (safe atomic inode swap) |
