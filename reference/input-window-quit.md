# macOS input, focus, quitting

## Keyboard mapping (Wine Mac driver)
- **WineCX 24 (Sikarugir): Command = Alt** for Windows apps. ⌘Q reaches the game as `WM_SYSKEYDOWN`
  `vk 0x51` (Q), `scan 0x10`, Alt context bit set, after an Alt key-down (`vk 0x12`, scan 0x38).
  Option is not Alt unless `HKCU\Software\Wine\Mac Driver\OptionIsAlt` (or Left/Right…) is set.
- Gcenx Wine 11: the same, **Command = Alt** (`winemac.drv/keyboard.c`: `kVK_Command → VK_LMENU`,
  `kVK_RightCommand → VK_RMENU`; `LeftCommandIsCtrl` turns it into Ctrl), Option is nothing without
  `LeftOptionIsAlt`. The app menu's Quit is ⌥⌘Q. So ⌘Q = Alt+Q, ⌘Enter = Alt+Enter, ⌘F4 = Alt+F4 (FoD, verified).
- Match keys by **scan code**, not virtual key: the user's layout may be Russian.
- Command is Alt, so a lone Command press (also the start of ⌘Tab) is an Alt tap, which DefWindowProc
  turns into `WM_SYSCOMMAND SC_KEYMENU` (keyboard menu mode) on a window with `WS_SYSMENU`: the game
  stalls until the next key. Strip WS_SYSMENU from the game window and drop SC_KEYMENU in the shim
  (DoW DE's windows all carry WS_SYSMENU).
- Taking ⌘Q for quitting hides the game's own Alt+Q binding (a hotkey in DoW DE's presets). Say so in the
  README: the user can rebind it in the game.
- Wine's app menu Quit (⌥⌘Q) and Dock → Quit raise `APP_QUIT_REQUESTED`. winemac then sends
  `WM_QUERYENDSESSION`/`WM_ENDSESSION` to the process's windows and terminates the process only if
  all agree. Plain ⌘Q never triggers it.

## Making ⌘Q and Dock → Quit close the game
In a game-side shim (ngfix), on the game's window thread:
- WH_GETMESSAGE: `WM_KEYDOWN/WM_SYSKEYDOWN` with scan 0x10 (or vk 'Q') while Command is down. Check the
  Alt context bit (lParam bit 29), `GetAsyncKeyState(VK_MENU)`, or VK_LWIN/RWIN for other builds. Then
  post `WM_CLOSE` to the top-level window and turn the message into `WM_NULL`.
- WH_CALLWNDPROC: on `WM_QUERYENDSESSION` post `WM_CLOSE` (Dock → Quit).
- `WM_CLOSE` makes Unity quit cleanly (saves, Steam Cloud). Then the launcher's watcher shuts Steam down.
Log the first ~40 key-downs (vk, scan, modifier states) while developing. That is how Command = Alt
was found.

## Focus, foreground, raw input
- Raw input (`WM_INPUT`) goes only to the process owning Wine's **foreground** window, unless a device is
  registered with `RIDEV_INPUTSINK`. Wine's foreground is the desktop window (`0x10020`) whenever a
  non-Wine app (e.g. Claude) is frontmost. That is normal; clicking the game window makes it foreground.
- `scripts/win32/focusprobe.exe 900 UnityWndClass` logs foreground/active/focus/capture and the
  left-button state every 100 ms while the user clicks. It shows whether clicks reach Wine and the
  window, and whether the game captures the mouse.
- `+event` trace (winemac) lists the Cocoa events Wine got: MOUSE_BUTTON, KEY_PRESS, WINDOW_GOT_FOCUS,
  APP_QUIT_REQUESTED…

## Window and display
- Keep `RetinaMode=n` (prefix tweak) unless the game handles HiDPI. Windows then measure in points.
- Wine windows are separate Cocoa windows. A game window can extend under the menu bar.
- A window that covers the whole screen is "fullscreen" for winemac (`updateFullscreen`): while active it is lifted
  to `NSStatusWindowLevel + 1`, above the menu bar — but the menu bar still slides in at the top edge, with the
  system arrow over the game. A borderless window over the **work area** (`GetMonitorInfo rcWork`: below the menu
  bar, above the Dock) avoids it and behaves like a normal Mac window (FoD's "fullscreen" under Wine).
  `CaptureDisplaysForFullscreen=y` (Mac Driver key) would capture the displays instead; not tried.
- Read how the game places its windows before choosing a fix. If its windowed mode moves the window to
  `(max(x, rcWork.left), max(y, rcWork.top))` (DoW DE's Platform.dll), the work-area window is simply that
  mode at the work-area size without a frame: ask for windowed mode and the size through the game's own
  option parser (DoW DE: hooked imports of `Plat::Options::Exist("window")` and `Get("width"/"height")`,
  answered from `GetMonitorInfo` rcWork unless the user gave the option), create the window without frame
  styles, strip them in `WM_STYLECHANGING`, give its `AdjustWindowRectEx` import the frameless style,
  clamp moves while it is in that mode. The back buffer then equals the client rect: no scaling, the
  mouse exact. Its borderless mode was no use: `SetWindowPos(0, 0, mode size)` hardcoded, which only a
  fake display mode could move.
- No 640x480 (or other old) mode on the Mac: `HKCU\Software\Wine\X11 Driver\EmulateModeset=y` (win32u reads it
  for every driver) adds the usual 4:3 and 16:10 modes, and a mode switch scales the image. It works with winemac.
  Wine's virtual desktop (`explorer /desktop=`) does not come up with winemac.
- Exclusive fullscreen D3D9 as a borderless window (Worms): turn the game's CreateDevice/Reset into
  `Windowed=TRUE` on its own window, placed in the work area. Pick an emulated mode with the Mac's aspect that
  scales the game's size up (640x480 menus → 840x525, ×1.71). Translate everything the game asks about screen
  coordinates (cursor, window rects, ClientToScreen, monitor/work area, `GetSystemMetrics`) so it still sees its
  screen at 0,0. Put a black backdrop window behind it. ⌘Tab, the Dock and the menu bar then behave normally.
- **Never-painted Wine windows are click-through on the Mac**: clicks fall to the window behind. MFC games that
  keep invisible dialogs (owner-drawn buttons, found by GetCursorPos) over a D3D window lose every click to the
  main window. That window is disabled during the modal dialog, so it gets `WM_SETCURSOR(HTERROR, button)` and
  beeps. Fix in the shim: subclass the main window, and on that message post the click to the dialog's
  visible, enabled child under the cursor (`WM_NCHITTEST` not `HTTRANSPARENT`), emulating double clicks.
- To keep such a window invisible after it has painted (it turned white and opaque): `WS_EX_LAYERED` +
  `SetLayeredWindowAttributes(alpha 0)`. That makes it invisible and click-through, so the clicks still need
  forwarding. `SetWindowRgn` with an empty region does nothing in winemac (it counts as no region).
- MFC attaches the CWnd being created to the first window created on that thread (CBT hook in `CreateEx`).
  Create the shim's own windows (backdrop) on another thread.
- ⌘Q that always works: a `WH_KEYBOARD_LL` hook in the shim's own thread sees Alt+scan 0x10 even when the
  game's loop is busy. Post WM_CLOSE, and end the process if it is still alive 3 s later (Worms' battle window
  ignores WM_CLOSE).
- A fullscreen game grabs the user's screen. The users here dislike tests that take over the screen.
  Announce every launch that shows windows, prefer windowed/background tests, and never drive the real
  mouse while they are at the computer (FoD used scripted input inside its own DLL instead).

## Cursor
- Games that draw their own cursor call `ShowCursor(FALSE)`. Wine hides the macOS cursor through the
  driver (`macdrv_SetCursor` with NULL). Check `trace+cursor` before blaming the game.
- How winemac hides it (`cocoa_app.m`, `updateCursor:`): an `[NSCursor hide]` counter while the mouse is over a
  Wine window, and `[[NSCursor arrowCursor] set]` + `unhide` over anything else (the menu bar revealed at the top
  edge, another app). ⌘Tab and a click on the menu bar unbalance it: the system cursor shows over the game, a second
  ⌘Tab sometimes hides it again, after the click it stays (FoD). Fix in the game's process: don't let
  `ShowCursor(FALSE)` reach Wine (IAT hook that only counts) and set a fully transparent cursor
  (`CreateCursor` with AND = 0xFF, XOR = 0) on `WM_SETCURSOR`/`HTCLIENT`. winemac turns it into a transparent
  `NSCursor`, which nothing resets; the arrow still shows over the menu bar, as it should.
- `ClipCursor`/`SetCursorPos` move the real Mac cursor. Wine warps it, which feels odd but is expected.
