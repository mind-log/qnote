# qnote

A lightweight X11 note-taking tool with global hotkey support.

Press a hotkey (default: Super+N) from anywhere to pop open a note window,
type your note, press Ctrl+S to save. It appends timestamped entries to a
per-day markdown journal at `~/.qnote/YYYY-MM-DD.md`.

Single binary, ~43 KB, no runtime dependencies beyond libX11 and libXft.

## Requirements

- Linux with X11
- libX11, libXft (development headers to build)

### Install build dependencies

```bash
# Debian / Ubuntu / Kali
sudo apt install libx11-dev libxft-dev pkg-config

# Fedora
sudo dnf install libX11-devel libXft-devel pkgconf

# Arch
sudo pacman -S libx11 libxft pkgconf
```

## Build & Install

```bash
make
sudo make install
```

This installs the binary to `/usr/local/bin/qnote`, plus a desktop entry
and application icons.

Uninstall:

```bash
sudo make uninstall
```

To install to a different prefix:

```bash
make PREFIX=/usr install
make PREFIX=~/.local install
```

## Usage

### One-shot mode

```bash
qnote
```

Opens a note window. Type your note, then:

| Key | Action |
|-----|--------|
| Ctrl+S | Save and close |
| Ctrl+C / Esc | Close without saving |
| Ctrl+U | Clear the buffer |
| Enter | New line |
| Tab | 4 spaces |

### CLI commands

| Command | Description |
|---------|-------------|
| `qnote --help` | Show help |
| `qnote --all` | Show all notes (newest first) |
| `qnote --today` | Show today's notes |
| `qnote --all N` | Show last N notes |
| `qnote --search text` | Search notes by keyword |
| `qnote --delete` | Interactive menu to delete a note |
| `qnote --setup` | Configure hotkey and journal path |
| `qnote --browse` | Open reader window (all entries) |
| `qnote --browse --today` | Open reader window (today only) |

### Daemon mode (global hotkeys)

```bash
qnote --listen
```

Registers global hotkeys so you can open windows from any application.
By default:

- **Super+N** — open note window (write)
- **Super+B** — open reader window (browse)

Runs in the foreground; add to your window manager's autostart:

```bash
# Openbox: add to ~/.config/openbox/autostart
qnote --listen &
```

No external hotkey daemon (sxhkd, etc.) is required — qnote registers
its own hotkeys via `XGrabKey`.

### Reader window (browse mode)

Both `qnote --browse` and the Super+B hotkey open a keyboard-driven
reader window:

| Key | Action |
|-----|--------|
| `j` / Down | Scroll down |
| `k` / Up | Scroll up |
| `f` / PgDn / Space | Page down |
| `b` / PgUp | Page up |
| `g` / Home | Jump to top |
| `G` / End | Jump to bottom |
| `q` / Esc | Close reader |

### Configuration

Config file: `~/.config/qnote/config`

```
hotkey=super+n
path=/home/user/.qnote
```

Run `qnote --setup` for interactive configuration.

## How it works

- Pure C99 using X11/Xft directly — no toolkits (GTK/Qt), no interpreters.
- Journal files are plain markdown appended to per-day files.
- The global hotkey is registered via `XGrabKey` — no external hotkey daemon.

## License

MIT
