# The kit and the .app

The working template is `~/Macdows/Northgard-Mac` (`install.sh`, `payload/NorthgardBG`,
`payload/Info.plist`, `payload/ngfix/`). Copy it and adapt rather than starting from zero.
TDU2 (`~/Macdows/TDU2-Mac`) is the variant with the game moved inside the bundle and an
offline sandbox.

## Kit layout (one folder per game under ~/Macdows)

| Path | What |
|---|---|
| `install.sh` | Builds `/Applications/<Game>.app`, creates the prefix, installs Steam, copies the game. Safe to re-run. `--reset` moves the prefix aside |
| `payload/<Launcher>` | `CFBundleExecutable` bash script (modes below) |
| `payload/Info.plist`, `*.icns`, `settings.sh.example` | Bundle metadata, icon, user knobs |
| `payload/<shim>/` | Game-side shims (source + build script + prebuilt DLL) |
| `cache/` | All pinned downloads (engine, DXMT, dylibs, GStreamer, SteamSetup) |
| `game/` | The game folder copied from the PC (+ Steam `appmanifest_<id>.acf`) |
| `docs/`, `icon/` | Reference notes, the PC's game log, icon pipeline |
| `README.md` | For the user, in their language: install, first launch, how to quit, where logs are |
| `CLAUDE.md` | For Claude: user, goal, stack, findings, rules, known risks. Update it as you learn |

## Bundle layout (WineCX engine)
- Engine `bin/*` → `Contents/MacOS/` next to the launcher, `lib` → `Contents/lib`, `share` → `Contents/share`.
- `Contents/bin -> MacOS` symlink (the CX loader re-execs from `lib/../../bin`).
- Support dylibs in `Contents/lib`, plus a `libinotify.0.dylib` symlink at the .app root.
- DXMT files in `Contents/lib/wine/...`, and the patched `nsiproxy.so`.
- `Contents/Resources/engine-version` = engine `version` file contents.
- `xattr -cr` the bundle (kits arrive quarantined) and `lsregister -f` it. Ad-hoc signatures are enough
  for x86_64 code; re-sign any file you byte-patch.
- A very large bundle (game inside, TDU2: 15 GB) is held ~2 min at the first exec while XProtect
  scans it, once per build. Let the installer's `--prepare` absorb that.

## Launcher (bash, `set -u`)
- Prefix in `~/Library/Application Support/<Game>/prefix`, logs in `~/Library/Logs/<Game>/wine.log`
  (rotated to `.1`), user settings in `…/settings.sh` (sourced every launch: game args, keep-Steam,
  WINEDEBUG channels, extra env).
- Refuse to run if `engine-version` next to the prefix differs from the app's engine.
- `prepare_prefix`: `wineboot --init` → one-time tweaks behind a marker `.ng-prefix-v<N>` (bump N to
  re-run on existing prefixes): de-symlink user folders (no macOS ~/Documents permission prompts),
  `RetinaMode=n`, AppDefaults DllOverrides for shims → silent Steam install (`SteamSetup.exe /S`) →
  `steam_dev.cfg` → copy `settings.sh` template.
- Env: `WINEDLLOVERRIDES` (Steam overlay off: `gameoverlayrenderer,gameoverlayrenderer64=d`;
  `winemenubuilder.exe=d`; TDU2 also `winebrowser.exe=d`, `mscoree=;mshtml=`),
  `DYLD_FALLBACK_LIBRARY_PATH=Contents/lib:/Library/Frameworks/GStreamer.framework/Versions/1.0/lib:/usr/local/lib:/usr/lib`,
  `PATH=Contents/MacOS:…` (Wine finds wineserver through it), `DXMT_LOG_LEVEL=error`, `MVK_CONFIG_LOG_LEVEL=1`.
- Main mode: copy shims into the game folder if they differ (`cmp`); start a background watcher;
  `exec wine steam.exe [-silent] -applaunch <appid> <game args>`. Use `-silent` once
  `config/loginusers.vdf` has an account.
- Watcher (`--watch`): wait for the game exe to appear (`pgrep -if 'Game\.exe'` works because Wine
  puts Windows paths in argv), wait for it to exit, then `wine steam.exe -shutdown` if this click
  started Steam. That is the "Steam closes after the game" behaviour.
- Other modes: `--prepare`, `--steam`, `--stop` (graceful), `--winecfg`, `--logs`.
- For debugging the way the user launches (LaunchServices), use `open "/Applications/<Game>.app"`.
  `open --env VAR=…` passes env, but the launcher's defaults override it, so put temporary
  `NG_WINEDEBUG=…` lines in settings.sh, mark them `# TEMP diagnostics`, and remove them after.

## What the user sees
- Launchpad/Applications icon = your `.icns` (make it from the exe icon: pefile → upscale → squircle).
- Dock while running: the launcher icon (ours) briefly, then `Steam.exe`/`steam.exe` and
  `<Game>.exe`, each with the icon Wine sets from the exe/window. Document it in the README as a
  Wine trait.
- Gcenx Wine 11 without Steam (Worms, FoD): the Dock and menu bar said "wine". `wine game.exe` runs `start.exe`,
  which starts the 32-bit game as a new process from `lib/wine/x86_64-unix/wine` (the preloader), and
  CoreFoundation finds the main bundle from that path, which is not ours. Fix: `export
  CFProcessPath="$CONTENTS/MacOS/<launcher>"` before exec'ing Wine (CoreFoundation's override of the process
  path), and `LSUIElement` in Info.plist. Every Wine process then registers as our bundle. Helpers stay out of the
  Dock, and winemac makes the process with a window a regular app, with our name and icon. Side effect:
  LaunchServices counts the app as running while Wine helpers linger after a quit, so a click on the icon does
  nothing for a few seconds. Symlinking the preloader into `Contents/MacOS` does not work.
- With Windows Steam (WineCX, Dimraeth): one Dock icon with the app's name and icon, Steam hidden →
  `assets/dockfix/` (a dylib the launcher puts in `DYLD_INSERT_LIBRARIES`, settings in
  `Contents/Resources/dockfix.conf`, plus `Contents/Helpers/Steam.app` with LSUIElement as Steam's identity).
  The game process gets `CFProcessPath` = our launcher and keeps the bundle icon (winemac's
  `setApplicationIconImage:` with the square exe icon is ignored); Steam's processes stay out of the Dock until
  one of their windows asks the user (login, cloud conflict), then show as "Steam"; other helpers are forced to
  Accessory; tray icons are taken off the menu bar. Facts behind the design:
  - in Wine's Windows processes (started via wine-preloader) dyld maps inserted libraries but **never runs their
    initializers** → no constructor; interpose `dlopen` instead: decide the role at the first call (before ntdll.so
    brings in CoreFoundation, so `CFProcessPath` can still be set), swizzle AppKit right after `dlopen(winemac.so)`;
  - `objc_addLoadImageFunc` callbacks hold the runtime lock: any objc_* call there traps (SIGILL);
  - DYLD_* reach Wine only when exported inside the bash launcher (SIP prunes them on the way into /bin/bash),
    and WineCX passes them to every child;
  - the launcher should not `exec` Wine: keep the LaunchServices-started process (its bouncing icon) until the
    game process exists, then exit, so the icon goes from the click straight to the game's.
  An earlier constructor-based version made Steam reject its web helper ("Unexpected Transport Error" loop) in a
  real launch; never reproduced, and the dlopen design has not shown it. Test Steam changes in an APFS clone of the
  prefix without `config`/`userdata` (keep `AppData/LocalLow`, or steam.exe crashes): it runs to the login window.
- Wine's own app menu Quit is ⌥⌘Q. Plain ⌘Q goes to the game (as Alt+Q in WineCX) → handle it in a
  shim (`reference/input-window-quit.md`).

## Optional: offline sandbox (TDU2)
If the user wants the game cut off from the internet, run Wine under `sandbox-exec` with a profile that
denies IP and DNS but allows localhost and Unix sockets (Wine needs them). Refuse to start if the sandbox
fails, and set `winebrowser.exe=d` so URLs can't escape through the macOS browser.
