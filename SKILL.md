---
name: windows-game-to-mac
description: >
  Port, wrap or fix a Windows-only PC game so it runs on an Apple Silicon Mac as a native-looking .app
  with a free Wine stack (WineCX or Gcenx Wine, DXMT, GStreamer). Covers Windows Steam inside Wine (one
  shared Windows Steam app that any game starts, like a Steam shortcut on Windows), the install kits
  (install.sh, launcher, icon), and debugging: "could not exec
  the wine loader", Steam "Unexpected Transport Error 0x3008" or no login window, black or transparent
  game windows, Unity games ignoring clicks or scroll, Cmd+Q not quitting, double cursor, DRM and
  anti-cheat dead ends. Use whenever someone wants to play, run, install, port or move a Windows game
  (exe, Steam, GOG, disc) to a Mac, continues such a port (a kit under ~/Macdows), or reports a problem
  in a Wine-wrapped game, in any language, even without naming the skill. Not for native macOS games or
  ports with source code.
---

# Windows game → Mac (free Wine stack)

Goal: a Windows game the user owns starts on their Apple Silicon Mac from its own `.app` (name, icon,
Launchpad) with one click and plays normally (graphics, video, mouse, keyboard, scroll, saves). It quits
with ⌘Q. A game that needs Windows Steam starts the shared Windows Steam app if it isn't running, like
a Steam shortcut on Windows. Everything comes from free components. A real port is impossible without
source code, so this is a Wine wrapper done well.

This skill holds what was learned porting Northgard Battlegrounds (works), Test Drive Unlimited 2
(blocked by SecuROM), Three Kingdoms: Fate of the Dragon (own DLL remaster), Dimraeth (Unity 6, a
clone of the Northgard kit) and ROB RUN (blocked by EasyAntiCheat). Steam games share one Windows Steam: the kit `~/Macdows/WindowsSteam-Mac`
builds `Windows Steam.app` (Wine, DXMT, Steam and every Steam game), and each game gets a small game kit
and app (model: `~/Macdows/Dimraeth-Mac`). Games without Steam get a self-contained kit with their own
Wine (`~/Macdows/Worms-Mac`). Kits aren't in this repository (Wine builds, game files): on a Mac without
them, build them from `reference/app-bundle.md` and `reference/steam-in-wine.md`.

## Working with the user
- Answer in the user's language and write user docs (`README.md`) in it; `CLAUDE.md` and code comments
  can be English.
- Drive the work and make the technical choices yourself. Ask only when the decision is really the
  user's, or when a physical action is needed: an admin password dialog, Steam login and Steam Guard on
  the phone, clicking a dialog you can't reach, playing to test.
- Announce every launch that will put windows on the user's screen. Prefer background or windowed tests.
  Never drive the real mouse while they are at the computer.
- When the user asks to close the game or Steam, do it right away and gracefully (`wclose`, `--stop`),
  then report.
- Give short progress notes during long steps (Steam updates take minutes). Say plainly what is
  verified and what is only expected.
- Delete only to the Trash. Keep the user's settings files; mark temporary debug lines in them and
  remove them afterwards.

## Workflow

### 1. Triage → `reference/triage.md`
Collect the facts first: bitness, graphics API, engine, DRM/anti-cheat, Steam dependency, videos,
input stack. Then pick a route. Stop early on SecuROM, StarForce, any anti-cheat (EasyAntiCheat too) or D3D12-only, and
explain the options. Record the verdict in the kit's `CLAUDE.md`.

### 2. Kit and bundle → `reference/app-bundle.md`, `reference/stack.md`
A Steam game: a game kit (`game.conf`, `payload/Info.plist`, icon, shims, `game/`) installed with
`WindowsSteam-Mac/add-game.sh`; install Windows Steam first if it's missing. A game without Steam: a
self-contained kit with pinned components in `cache/`, `install.sh`, and the launcher with
prefix/engine-version guard, settings.sh, logs and modes. Install GStreamer before the first prefix
exists, through the native password dialog. For the WineCX engine, add the `Contents/bin -> MacOS`
symlink and run `scripts/wine/patch_nsiproxy.sh` on its `nsiproxy.so` (both are in the Windows Steam
`install.sh`). A kit cloned elsewhere (on the user's PC, say) from an older copy misses fixes made on the
Mac later: diff its scripts against the installed working app first.

### 3. Steam (Steamworks games) → `reference/steam-in-wine.md`
One Windows Steam for all games: silent install, `steam_dev.cfg`, first-start updates (~5–10 min), one
login by the user, then every game app runs `steam -silent -applaunch <appid>` in that prefix. Per-game
settings go to AppDefaults and the game folder, never env (Steam's children inherit Steam's env).
Never `kill -9` a logged-in Steam. The game copied from the PC plus its `appmanifest_<appid>.acf` saves
the download.

### 4. Make the game behave
- Graphics: D3D11 → DXMT in the Wine tree (both arches). Check the game log for the D3D device line.
- Videos: GStreamer. A black screen after the logos means video decode.
- Unity: `reference/unity.md`. Unity 6 needs the ngfix shim for clicks and scroll (`assets/ngfix/`).
- Input, focus, ⌘Q, Dock → Quit, cursor, window size: `reference/input-window-quit.md`.
- Shims and patches: `reference/dll-and-patching.md`.

### 5. Hand over
Put every fix into the kit (re-runnable `install.sh`, launcher, shim source + prebuilt binary), not only
into the installed app. Re-run the installer to prove it, and remove temporary diagnostics. Update
`README.md` (in the user's language: install, first launch, how to quit, what the Dock shows, logs,
reset) and `CLAUDE.md` (findings, rules, risks). Close the game and Steam, and confirm no processes of
the prefix remain.

## Debugging method → `reference/diagnostics.md`
Read the logs first: wine.log, Steam's `logs/*.txt`, Player.log, the shim log. Then test the
hypothesis with the smallest stand-in instead of the real game: Win32 helpers run via
`scripts/wine/appwine`, a stub exe named like the game, a harness that includes the shim source, or a
throwaway prefix. When a closed binary decides something, read its logic (`scripts/pe/*.py` +
objdump) rather than guessing. Ask the user to test only after the stand-in passes, and tell them exactly
what to do while a background loop watches the logs.

## Quick symptom table

| Symptom | Likely cause → fix |
|---|---|
| `wine: could not exec the wine loader` (`wine --version` works) | CX loader re-execs from `lib/../../bin` → `Contents/bin -> MacOS` |
| `Library not loaded: @rpath/libinotify.0.dylib` | wineserver rpath = .app root → dylibs in `Contents/lib` + symlink at the root |
| Gcenx Wine can't find wineserver | add `Contents/MacOS` to `PATH` or set `WINESERVER` |
| Steam never logs in, `secure connections will not be available` | no libgnutls → support dylibs |
| Dialogs without text | no libfreetype |
| No Steam login window; "Unexpected Transport Error (0x3008)"; `Connection rejected` in `transport_client.txt` | nsiproxy TCP-table bug → `patch_nsiproxy.sh`; check with `tcptest.exe` |
| webhelper crash-loops, `[Logged Off]` forever | prefix touched by two Wine builds → `install.sh --reset` (fresh prefix) |
| CM "Try another CM" about every 60 s at the login screen | normal, ignore |
| "Failed to initialize graphics / DirectX 11" | DXMT not in the Wine tree / `winemetal.so` missing |
| Transparent or empty game window (Unity 6) | DXMT 0.74 present path → newer DXMT / notpop fork |
| Hang at `GfxDevice: creating device client` | Steam overlay hook → `gameoverlayrenderer…=d` |
| Black screen after logos, `WindowsVideoMedia error` | GStreamer missing |
| Unity: clicks ignored, scroll dead, `EnableMouseInPointer failed` | Unity 6 pointer mode → ngfix shim |
| Shim in the game folder has no effect; `+loaddll` says `builtin` | Wine prefers builtin → AppDefaults DllOverrides `native,builtin` |
| ⌘Q does nothing | WineCX and Gcenx 11 map Command→Alt; Wine's Quit is ⌥⌘Q → catch Alt+Q (scan 0x10) in the shim |
| Two cursors | game calls ShowCursor(0); check `trace+cursor` / shim log |
| System cursor back after ⌘Tab or a click at the top edge | winemac's `[NSCursor hide]` counter; keep ShowCursor(0) from Wine, use a transparent cursor (`input-window-quit.md`) |
| Menu bar slides over a fullscreen game at the top edge | borderless window over the work area instead of the whole screen (Unity: ngfix's work-area monitor, `unity.md`) |
| Unity window shrunk to the work area: gray band at the top, clicks above their targets | clamped size: stale caption frame, Unity renders the size it asked for → show Unity a work-area-sized monitor (`unity.md`) |
| D3D9 game wants fullscreen 640x480, CreateDevice fails `0x8876086a` in a loop | the Mac has no such mode → `HKCU\Software\Wine\X11 Driver\EmulateModeset=y` |
| Clicks on a menu/dialog reach the window behind it (a disabled main window beeps) | winemac makes never-painted windows click-through → forward the click in the shim (`input-window-quit.md`) |
| An invisible dialog turns white and hides the game | it painted once and became opaque; an empty window region does nothing in winemac → layered, alpha 0 |
| Dock and menu bar say "wine" (Gcenx Wine) | `CFProcessPath` = the bundle's executable + `LSUIElement` (`app-bundle.md`) |
| Dock shows `Game.exe`/`steamwebhelper.exe` with square exe icons (WineCX + Steam) | `assets/dockfix`: dlopen-interposing dylib, each game as its own app, Steam as "Windows Steam" only while a window is up (`app-bundle.md`) |
| A launch stops; Steam waits: "your account is playing X on another computer" | `KickingOtherSession` in console_log: the account is in-game elsewhere; only the user can answer |
| The game's icon does nothing for a while after a cancelled launch | its launcher still waits (LaunchServices sees the app running) → bounded wait + cancel detection (`steam-in-wine.md`) |
| Old 2D game slow, CPU-bound, but D3D/Win32 calls are cheap | sample the game thread; per-pixel x87 code is slow under Rosetta → patch the hot function in memory (`diagnostics.md`, `dll-and-patching.md`) |
| Frame rate far above the display's (vsync ignored in a windowed swap chain) | hold `Present` to the refresh period in the shim while the game asks for vsync |
| Window Retina-sized or off-screen | `RetinaMode=n`, `-screen-width/-height` |
| First launch of a fresh big bundle hangs ~2 min | XProtect scanning it, once per build |
| SecuROM "DSS"/"Suspected Virtual Operating System" | DRM vs Rosetta/LDT → dead end, explain options |
| EasyAntiCheat splash, then no game; `anticheatlauncher.log`: `Running under Wine on MacOS which is currently unsupported`, 204 | EAC has no macOS module → dead end, explain options (`triage.md`); never start the game exe without the bootstrapper |
| Random crashes of 32-bit code (Gcenx 11) | WoW64 under Rosetta → FoD's `wow64cpu` patch |

## Hard rules
- One Wine build per prefix, ever. Store the engine version with the prefix and refuse mismatches.
  All games of the shared Windows Steam live in its prefix, so they all run on its Wine build.
- Never `kill -9` Steam once someone has logged in; use `-shutdown`. `wineserver -k` touches only
  `WINEPREFIX`'s processes, so other Wine sessions of the user (other games) stay alive. Never
  `pkill wineserver` globally.
- No DRM cracking, keygens or anti-cheat bypasses. Patch only our Wine files and add our own shims,
  always verifying the original bytes.
- Don't publish game assets. Copy game files only between the user's own machines.
- The myths in davidesner's notes stay myths for this stack: Windows 7 mode, disabling Steam GPU
  acceleration, `-no-cef-sandbox`.

## Bundled resources
| Path | Use |
|---|---|
| `reference/*.md` | Details per phase (read the one the workflow points to) |
| `scripts/win32/*.c` + `build.sh` (+ prebuilt x86_64 `.exe`) | Win32 helpers: wtool, winfo, wclose, focusprobe, rawtest, tcptest, ptrtest |
| `scripts/pe/*.py` | PE analysis: imports, IAT slot names, call sites of an import, string xrefs (run with `python3 -I`) |
| `scripts/wine/appwine` | Run a helper with a wrapped game's own Wine and prefix |
| `scripts/wine/patch_nsiproxy.sh` | The Steam transport fix for WineCX 24.0.7_7 |
| `assets/ngfix/` | Unity 6 input/quit shim (version.dll proxy), optional borderless work-area window; adapt per game |
| `assets/dockfix/` | macOS dylib for the Dock: each game under its app's name/icon, Steam as "Windows Steam" only with a window up, icon click → Steam window, Quit/⌘Q → clean shutdown |
