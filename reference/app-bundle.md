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
- Wine's own app menu Quit is ⌥⌘Q. Plain ⌘Q goes to the game (as Alt+Q in WineCX) → handle it in a
  shim (`reference/input-window-quit.md`).

## Optional: offline sandbox (TDU2)
If the user wants the game cut off from the internet, run Wine under `sandbox-exec` with a profile that
denies IP and DNS but allows localhost and Unix sockets (Wine needs them). Refuse to start if the sandbox
fails, and set `winebrowser.exe=d` so URLs can't escape through the macOS browser.
