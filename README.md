# windows-game-to-mac

A Claude Code skill for running Windows PC games on Apple Silicon Macs as native-looking `.app` bundles
with a free Wine stack (WineCX or Gcenx Wine, DXMT, GStreamer), including Windows Steam inside Wine. It
grew out of three real ports:
- Northgard Battlegrounds: works;
- Test Drive Unlimited 2: stopped by SecuROM copy protection;
- Three Kingdoms: Fate of the Dragon: remastered through a custom DLL.

## Install

In Claude Code 2.1.275 or later:

```
/plugin install windows-game-to-mac --marketplace i7ok/Macdows-skills
```

Or in two steps, which works in any version:

```
/plugin marketplace add i7ok/Macdows-skills
/plugin install windows-game-to-mac@macdows
```

You need `git`. On a Mac it comes with the Xcode Command Line Tools: `xcode-select --install`.

To update: `/plugin marketplace update macdows`. Or turn on auto-update once:
`/plugin` → Marketplaces → macdows → Enable auto-update.

## Usage

The skill kicks in by itself when you ask something like "port game X from Windows to my Mac", "run this
Windows game on my Mac" or "clicks don't work in the game", in any language. Claude first checks whether
the game can run at all (DirectX version, engine, copy protection, Steam), then builds a kit: installer,
launcher and docs.

The kit template, `~/Macdows/Northgard-Mac`, is not in this repository because it holds game files and
Wine builds. Without it, Claude builds the kit from `reference/app-bundle.md`.

## What's inside

| Path | Contents |
|---|---|
| `SKILL.md` | Workflow, rules and a symptom → cause → fix table |
| `reference/triage.md` | Whether a game will run, and by which route (DirectX, engine, DRM, Steam) |
| `reference/stack.md` | Components: WineCX 24 (Sikarugir), Gcenx Wine 11, DXMT, GStreamer; versions and quirks |
| `reference/app-bundle.md` | How the kit and the `.app` work: installer, launcher, Wine environment, Steam watcher |
| `reference/steam-in-wine.md` | Windows Steam inside Wine: install, login, the transport error, clean shutdown |
| `reference/unity.md` | Unity games: Direct3D 11, logs, clicks and scrolling in Unity 6 (the ngfix shim) |
| `reference/input-window-quit.md` | The Mac keyboard in Wine, ⌘Q, Quit from the Dock, focus, cursor |
| `reference/dll-and-patching.md` | Changing behaviour without source code: DLL shims, byte patches |
| `reference/diagnostics.md` | Logs, Wine traces, helper tools, debugging method, shell pitfalls |
| `reference/case-studies.md` | The three ports: what broke and how it was fixed |
| `scripts/win32/` | Win32 diagnostic helpers that run inside Wine: windows, focus, input, TCP tables (sources and prebuilt `.exe`) |
| `scripts/pe/` | Python tools for Windows binaries (imports, call sites, string references) |
| `scripts/wine/` | Running the helpers in a game's Wine environment; the `nsiproxy.so` patch for Steam |
| `assets/ngfix/` | Input shim for Unity 6 games (clicks, scrolling, ⌘Q) |
| `.claude-plugin/` | Manifests of the plugin and the `macdows` marketplace |

## Updating the skill

The skill lives at the repository root: `SKILL.md` and the folders next to it. The author's working copy
is `~/Macdows/.claude/skills/windows-game-to-mac`, where it is a project skill of the `~/Macdows` folder.
After editing, commit and `git push`. The manifest has no `version` field, so every commit becomes a new
version for everyone who updates the marketplace.
