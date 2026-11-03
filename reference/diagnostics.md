# Diagnostics toolbox

## Logs (read before guessing)
| Log | Where | Shows |
|---|---|---|
| Launcher/Wine | `~/Library/Logs/<Game>/wine.log` (`.1` = previous run) | launcher steps (`-- …`), Wine `err:`, traces you enabled, Steam bootstrapper output |
| Steam | `prefix/drive_c/Program Files (x86)/Steam/logs/` | `bootstrap_log` (updates), `connection_log` (CM, `Logged On`), `transport_client`/`transport_steamui` (webhelper ↔ client), `webhelper_js`, `cef_log`, `console_log` |
| Game | Unity `Player.log` (see `unity.md`), others in the game folder or `AppData` | engine init, D3D device, Steam init, exceptions, scene/meta flow |
| Shim | `C:\users\crossover\Temp\ngfix.log` | patches applied, hooks, first conversions, keys, cursor calls |

`WINEDEBUG` channels that paid off: `+loaddll` (which DLL loaded, native/builtin), `+event` (winemac's
Cocoa events), `trace+cursor` (cursor set/hide), `trace+nsi,err+nsi` (network tables),
`trace+rawinput`, `err+module` (missing DLLs/dylibs, on by default). Driver services (winedevice for
nsiproxy) log only if they start under your WINEDEBUG, so stop the prefix first (`wineserver -w` / `-k`).

## Seeing Wine windows without screen access
Claude has no Screen Recording permission: CGWindowList gives owners and bounds but no titles, and
`screencapture` would prompt the user. Instead run Win32 helpers inside the session with
`scripts/wine/appwine` (same Wine, same prefix):
- `wtool list [all]`: every top-level window with class, title, rect, child controls and checkbox state.
  `wtool click HWND`: BM_CLICK a real Win32 button.
- `winfo HWND`: style, client origin, foreground/active/focus/capture.
- `wclose HWND`: graceful WM_CLOSE.
- `focusprobe SEC CLASS`: timeline of focus and mouse button changes while the user acts.
- `rawtest`, `tcptest`, `ptrtest`: isolate Wine behaviour (raw input, TCP owner table, pointer stubs).
Limits: Steam's VGUI dialogs have no child controls, and other processes' surfaces read back black
(PrintWindow/BitBlt), so ask the user for a screenshot. Build the helpers with
`scripts/win32/build.sh` (`i686` for 32-bit prefixes).

Wine puts Windows paths in process args (`C:\Program Files (x86)\Steam\steam.exe`). CX runs every
exe from `/var/folders/.../winetemp-*`. `lsappinfo list` shows how macOS registered each process
(name, bundle, Foreground/BackgroundOnly).

## Method that worked
1. Read the logs end to end around the failure. Steam and Unity usually name the problem
   (`Connection rejected`, `EnableMouseInPointer failed`).
2. Form a hypothesis and test it with the smallest possible stand-in: a helper exe, a stub named like the
   game, a harness that includes the shim's source, a throwaway prefix in the scratchpad (same engine).
   Each real-game round trip costs the user minutes and patience.
3. Confirm with the binary's own logic (`reference/dll-and-patching.md`, "Reading a closed binary") rather
   than forum lore. davidesner's SKILL.md lists fixes that are myths for this stack (Windows 7 mode,
   disabling Steam GPU acceleration, `-no-cef-sandbox`).
4. Fix in the kit (install.sh / launcher / shim source), install, verify, then write the finding into
   the kit's `CLAUDE.md`.
5. Only then ask the user to try. Tell them exactly what to do (click X, press ⌘Q) and what you will
   watch. Watch with a background loop (`run_in_background`) on the log or process, not by polling.

## Shell pitfalls (Claude's Bash tool runs zsh)
- `kill $pids` with several pids fails ("illegal pid"). Use `${=pids}` or a bash script.
- `ps … | grep pattern` matches your own `zsh -c` (its args contain the pattern). Use `ps -Ao comm`
  or `pgrep` with care.
- A bare `sleep N` is blocked. Use until-loops with a condition, or background tasks.
- Heredocs mangle backslashes in Windows paths and C escapes. Write source files with the Write tool.
- Wrapper scripts that `export WINEDLLOVERRIDES=…` overwrite yours. Append instead.
