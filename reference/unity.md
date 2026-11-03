# Unity games under Wine on the Mac

## Basics
- `UnityPlayer.dll` + `<Game>_Data/`. `GameAssembly.dll` means IL2CPP (game code is native x64; no
  managed DLLs to patch). Engine version is in `Player.log` (`Initialize engine version: 6000.2.7f2`).
- Pass `-force-d3d11` (DXMT is D3D11-only). Unity 6 also probes D3D12 (`d3d12: failed to create D3D12
  device` is harmless).
- `Player.log`: `prefix/drive_c/users/<user>/AppData/LocalLow/<Company>/<Product>/Player.log` (user is
  `crossover` on WineCX). Keep the PC's `Player.log` in the kit as the reference for a healthy run.
- Videos (`VideoClip`, `.mp4`) go through Media Foundation → winegstreamer → GStreamer.framework.
  A black screen after the logos with `WindowsVideoMedia error` in the log means GStreamer is missing.
- Window size: with `RetinaMode=n` the window is sized in points (e.g. 1440×842 client on a 1440×900
  display). If a Retina-sized window shows up, pass `-screen-fullscreen 0 -screen-width W -screen-height H`.
- `Curl error 35: Recv failure: Connection was reset` in Player.log means some web service (analytics)
  is unreachable. Harmless if login and cloud work.

## Unity 6: clicks and scroll ignored → mouse-in-pointer mode
Symptom: the game renders and the cursor moves, but UI buttons don't react, sometimes with only a click
sound. Two-finger scroll/zoom does nothing. `Player.log` has
`EnableMouseInPointer failed with the following error: …`.

Why: Unity 6's Windows player calls `if (!IsMouseInPointerEnabled()) EnableMouseInPointer(TRUE)`.
Its Input System then takes mouse buttons and the wheel from `WM_POINTERDOWN/UP/UPDATE/WHEEL` (it loads
`GetPointerType` etc. dynamically). Wine 9 (CrossOver 24) has no mouse-in-pointer mode, so the game
only gets legacy `WM_LBUTTONDOWN`. Unity even calls `SetCapture` on them, but the UI never sees a click.
Raw input itself works: registered with flags 0, delivered to the foreground process.

Fix: the **ngfix shim** (`assets/ngfix/`, deployed in Northgard as `payload/ngfix/version.dll`):
- An app-local `version.dll` (UnityPlayer imports 3 functions from VERSION.dll). 14 exports forward to
  `kernelbase`, `VerInstallFileA/W` are stubs.
- Wine loads its builtin version.dll even next to the exe. Set
  `HKCU\Software\Wine\AppDefaults\<Game>.exe\DllOverrides` `version=native,builtin` (launcher prefix tweak).
- DllMain (only in the game exe) IAT-patches UnityPlayer's `EnableMouseInPointer` → TRUE and
  `IsMouseInPointerEnabled`, plus `CreateWindowExW`/`ShowCursor`/`SetCursor` for logging. It installs
  WH_GETMESSAGE + WH_CALLWNDPROC hooks on the thread that creates `UnityWndClass` or enables pointer mode.
- The message hook rewrites client mouse messages: move → `WM_POINTERUPDATE`; a press with no other
  button held → `WM_POINTERDOWN`, else UPDATE; a release of the last button → `WM_POINTERUP`, else
  UPDATE. wParam = `MAKEWPARAM(1, flags)` (INRANGE|PRIMARY|CONFIDENCE, +INCONTACT and FIRST..FIFTHBUTTON
  from MK_* state); lParam = screen coords. The wheel → `WM_POINTERWHEEL/HWHEEL`
  (`MAKEWPARAM(1, delta)`, lParam already screen). Wine's `GetPointerType` stub answers `PT_MOUSE`,
  and Unity needs nothing more for a mouse.
- The same shim handles quit keys (`reference/input-window-quit.md`). Log: `C:\users\crossover\Temp\ngfix.log`.
- To reuse it: change the exe name check in `DllMain`, check that UnityPlayer still imports VERSION.dll
  (`scripts/pe/imports.py UnityPlayer.dll | grep -i version`). If it doesn't, proxy another small DLL
  it imports that no Wine builtin loads first (verify with `+loaddll`).
- Test without the real game. A stub exe named like the game that imports `UnityPlayer.dll` (dlltool
  import lib for `UnityMain`, never called) shows whether the shim loads natively and patches the IAT.
  A harness that `#include`s ngfix.c and posts legacy messages to its own window shows the conversion.
  Only then ask the user to click.

## Cursor
In battle the game draws its own cursor and calls `ShowCursor(0)` (count -1). Wine passes the hide to
the Mac driver (`trace+cursor`: `setting cursor with cursor_name (null)`). A "second system cursor" was
reported once before the input fixes and was gone afterwards. If it comes back, compare ngfix.log's
ShowCursor/SetCursor lines with a `trace+cursor` run while the user reproduces it.
