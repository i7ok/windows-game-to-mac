# macOS input, focus, quitting

## Keyboard mapping (Wine Mac driver)
- **WineCX 24 (Sikarugir): Command = Alt** for Windows apps. ⌘Q reaches the game as `WM_SYSKEYDOWN`
  `vk 0x51` (Q), `scan 0x10`, Alt context bit set, after an Alt key-down (`vk 0x12`, scan 0x38).
  Option is not Alt unless `HKCU\Software\Wine\Mac Driver\OptionIsAlt` (or Left/Right…) is set.
- Gcenx Wine 11: the same, **Command = Alt** (`winemac.drv/keyboard.c`: `kVK_Command → VK_LMENU`,
  `kVK_RightCommand → VK_RMENU`; `LeftCommandIsCtrl` turns it into Ctrl), Option is nothing without
  `LeftOptionIsAlt`. The app menu's Quit is ⌥⌘Q. So ⌘Q = Alt+Q, ⌘Enter = Alt+Enter, ⌘F4 = Alt+F4 (FoD, verified).
- Match keys by **scan code**, not virtual key: the user's layout may be Russian.
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
