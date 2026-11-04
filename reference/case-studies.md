# Case studies (tested on a MacBook Air M1, macOS 26.6.2, November 2026)

## Northgard Battlegrounds: works (kit: `~/Macdows/Northgard-Mac`)
Unity 6000.2.7f2 IL2CPP, D3D11, Steam (app 4487060, Early Access), Steam Cloud saves, Unity Cloud Code,
no anti-cheat. Stack: WineCX 24.0.7 (Sikarugir) + DXMT 0.74 + Gcenx dylibs + GStreamer 1.28.7 +
Windows Steam in the prefix. `-applaunch 4487060 -force-d3d11`, overlay off.

| Symptom | Cause | Fix |
|---|---|---|
| `install.sh` aborts: `wine: could not exec the wine loader` | CX loader re-execs from `lib/../../bin`; bin was moved to `Contents/MacOS` | `Contents/bin -> MacOS` symlink |
| `sudo` for GStreamer fails in Claude's shell | no TTY | `osascript … with administrator privileges` (native password dialog) |
| Steam: no login window, then "Unexpected Transport Error (0x3008)" | nsiproxy can't read TCP tables → GetExtendedTcpTable fails → Steam rejects its webhelper | `patch_nsiproxy.sh` (2×32 bytes + ad-hoc sign) |
| Steam stuck on that dialog, `-shutdown` ignored | modal VGUI dialog | `wineserver -k` for the prefix (nobody logged in yet) |
| Login screen: CM drops every ~60 s ("Try another CM") | normal behaviour | none; the user logged in with QR/password |
| Game renders, clicks do nothing (sometimes a click sound), scroll dead | Unity 6 mouse-in-pointer mode not in Wine 9 | ngfix `version.dll`: fake EnableMouseInPointer + legacy→WM_POINTER conversion |
| ngfix copied but never loaded | Wine loads builtin version.dll despite the app-local one | AppDefaults DllOverrides `version=native,builtin` for the game exe |
| ⌘Q does nothing | CX maps Command → Alt; Wine's Quit is ⌥⌘Q | ngfix: Alt+Q (scan 0x10) → WM_CLOSE; WM_QUERYENDSESSION → WM_CLOSE (Dock → Quit) |
| Two cursors (system + game) | reported before the input fixes; the game calls ShowCursor(0) in battle | gone after the fixes; trace+cursor showed Wine hiding the cursor |
Healthy end state: one click → Steam logs in silently → game; ⌘Q → game saves to Steam Cloud and quits →
watcher shuts Steam down; all processes of the prefix exit within ~30 s.

## Test Drive Unlimited 2: stopped at DRM (kit: `~/Macdows/TDU2-Mac`)
2011, D3D9, 32-bit, Russian retail v086, **SecuROM 8.03** + PA activation. Gcenx Wine 11, game moved
into the bundle, offline `sandbox-exec` (user's hard rule), CrashHook.dll stub for the UpLauncher mutex
check. Every launch dies in the SecuROM wrapper ("DSS": security module can't be activated). Its own
analysis log says "Suspected Virtual Operating System": Wine runs 32-bit code in LDT segments and
Rosetta emulates SIDT/SGDT/SLDT. No Wine setting changes that, and cracking is out. Verdict given to
the user: play on the PC, stream it, or try a Windows ARM VM.
Lessons: check DRM in triage; XProtect scans a 15 GB bundle ~2 min on the first exec; `winebrowser.exe=d`
when sandboxing.

## Three Kingdoms: Fate of the Dragon: remaster via own DLL, works (kit + project: `~/Macdows/ThreeKingdoms-FoD`)
2001, 32-bit, DirectDraw. Their own `FODDD.dll` replaces the platform layer (DirectDraw → SDL3, videos via
pl_mpeg, input, UI) and patches the game in memory, checking the original bytes first. The exe import is
byte-patched from DDRAW.dll to FODDD.dll. Stack: Gcenx Wine 11.0_1 in the bundle (`Fate of the Dragon.app`,
`install.sh` like Northgard's: engine `bin/*` → `Contents/MacOS`, `lib`, `share`; `WINESERVER` set by the
launcher), SDL3 Vulkan renderer via MoltenVK, GStreamer for the music (DirectShow MP3). Tests run in the
background with scripted input inside the DLL and an off-screen window, never driving the user's mouse; the dev
prefix (`~/.wine-fod`, `game/working`) is separate from the app's.

| Symptom | Cause | Fix |
|---|---|---|
| Crash or hang at start in ~half of the runs, `page fault on read access to 00004DC9 at 7BF2123D` | WoW64 under Rosetta: the far jump into wow64cpu's 64-bit thunk sometimes doesn't switch modes (WineHQ 60494); OpenGL/wined3d start-up is the main trigger | byte patch of `wow64cpu.dll` (far returns instead of far jumps, after MagicKettle-Runtime patch 0002): `patches/wine_wow64cpu.py`, same bytes in `install.sh` |
| Out of memory before the first battle (SDL Vulkan renderer) | Wine 11 `win32u_vkAllocateMemory` loses the 32-bit host pointer (`memory->vm_map` stays NULL), `vkFreeMemory` never frees it; SDL allocates a staging buffer per texture upload | the DLL wraps SDL's `vkGetInstanceProcAddr`/`vkMapMemory`/`vkFreeMemory` and frees the leaked block (MagicKettle patch 0017 fixes it in win32u) |
| SDL's OpenGL renderer: `Could not create GL context` | child window under winemac | Vulkan renderer instead |
| ⌘Q does nothing | Gcenx 11 maps Command → Alt too (`keyboard.c`: `kVK_Command → VK_LMENU`): ⌘Q = Alt+Q; Wine's Quit is ⌥⌘Q | the DLL quits on Q (scan 0x10) with Alt on the game window |
| A second (system) cursor after ⌘Tab; after a click at the top edge it stays | the game hides the cursor once (`ShowCursor(0)`); winemac hides with an `[NSCursor hide]` counter and shows the arrow + `unhide` over anything that is not a Wine window (the menu bar sliding in at the top edge); a click there and ⌘Tab unbalance it | don't pass `ShowCursor(FALSE)` to Wine (IAT hook); a fully transparent cursor image over the client area (`WM_SETCURSOR`), which macOS keeps |
| Menu bar slides over the game's top bar | a borderless window covering the whole screen is lifted above the menu bar (NSStatusWindowLevel+1) but the menu bar still reveals at the top edge | "fullscreen" = borderless window over the work area (below the menu bar, above the Dock); battle size = work area (1440×870 on the M1 Air) |
| The REMASTER tab's widgets doubled around the cursor | own bug: the wide UI cut the cursor out of the frame after our page was pasted in | cursor first, then the page |
Lesson: for old 2D games an own wrapper DLL gives a better result than fighting Wine's ddraw, and it
doubles as a test harness.

## Worms World Party Remastered: works (kit: `~/Macdows/Worms-Mac`)
2015 remaster of a 2001 game, 32-bit MFC, Direct3D 9 used as a screen: the game draws every frame in software
into a 32-bit buffer and uploads it as one texture. No Steam client in the prefix.
Stack: Gcenx Wine 11.0_1 in the bundle with FoD's `wow64cpu` patch, wined3d on OpenGL, and the
shim `wwpfix` as an app-local `xinput1_3.dll` (imported by ordinal 2, forwarded to Wine's xinput1_4).

| Symptom | Cause | Fix |
|---|---|---|
| Nothing on screen, CreateDevice `0x8876086a` (D3DERR_NOTAVAILABLE) in a loop | the menus want exclusive fullscreen 640x480; the Mac has no such mode | `EmulateModeset=y` (X11 Driver key, read by win32u for all drivers) |
| The game takes the whole screen and the Mac is unusable | exclusive fullscreen; a window covering the screen is lifted above everything | windowed swap chain on the game's window in the work area, emulated mode with the Mac's aspect to scale small sizes, coordinate translation, black backdrop (`input-window-quit.md`) |
| Clicks in the menus do nothing, the main window beeps | the menus are invisible MFC dialogs that never paint, so they are click-through on the Mac; clicks hit the disabled D3D window | forward `WM_SETCURSOR(HTERROR)` clicks to the dialog's child under the cursor |
| White screen after the setup dialog | the dialog painted white once and became opaque; an empty window region does nothing in winemac | dialogs layered with alpha 0 (clicks still forwarded) |
| The shim's backdrop became the game's main window | MFC's CBT hook attaches the CWnd to the first window created on the thread | create the backdrop on the shim's own thread |
| Battle zoomed and cropped | the game reuses the parameters we changed for its Reset (Windowed=TRUE), and the saved battle size was the full screen | treat every CreateDevice/Reset as a new size; default battle size = the work area (1440x870) |
| ⌘Q sometimes ignored; ⌘K wanted too, also on the Russian layout | the battle loop doesn't always pump messages; its window ignores WM_CLOSE | `WH_KEYBOARD_LL` in the shim's thread, scan codes 0x10/0x25 with Alt/Command, WM_CLOSE + 3 s watchdog |
| Dock says "wine" | the game process is started from Wine's preloader | `CFProcessPath` + `LSUIElement` (`app-bundle.md`) |
| Launcher exits at once ("already running") | `pgrep -if 'w2\.exe'` matched Claude's own shell | match `^[a-z]:\\...\\w2.exe`: Wine puts the Windows path first in argv |
| Battle 12–14 fps at 1440x870, menus ~45; freezes of 10–15 s on the CPU's turn | 85% of a frame was the copy into the texture: one call per pixel to a fade function with x87 code (slow under Rosetta), even with no fade on | in-memory patch: `memcpy` rows while no fade runs, the game's code during fades, checked pixel-exact → 60–80 fps, no freezes (`dll-and-patching.md`) |
| 130 fps in the menus, 80 in battle on a 60 Hz screen | wined3d's windowed swap chain on the Mac ignores the game's vsync | the shim holds each `Present` to the refresh period while the game asks for vsync |
Tests ran as scripted quick battles with a fake cursor read by the shim from a file (`WWPFIX_CURSOR_FILE`), clicks
injected as `WM_SETCURSOR` messages, and Alt+Q through SendInput, so the user's mouse was never touched. Each run
was announced, because the game window still shows.

## Dimraeth: works (kit: `~/Macdows/Dimraeth-Mac`)
Unity 6000.0 IL2CPP, Direct3D 11, Windows Steam, Steam Cloud saves, early-access dev builds. The kit was
cloned from the Northgard kit on the PC, from a copy older than the Northgard fixes made on the Mac.

| Symptom | Cause | Fix |
|---|---|---|
| (avoided) | the clone lacked the `Contents/bin` symlink, the nsiproxy patch and ngfix | diff `install.sh` and the launcher against the installed Northgard app and port the fixes before the first run; no transport-error dialog then |
| `Curl error 35: Cert verify failed ... UnityTls error code: 7` | the game's news-feed host has no DNS record any more; the PC's log has `Curl error 6` at the same point | none, harmless |
| Menu bar slides over the game; the user wants a window between the menu bar and the Dock | Unity's FullScreenWindow covers the whole screen | ngfix shows UnityPlayer a work-area-sized monitor + `-screen-fullscreen 1 -window-mode borderless` (`unity.md`) |
| Gray band under the menu bar, button hitboxes below the buttons | first try clamped Unity's window: the caption's frame stayed inside the WS_POPUP window, and Unity kept rendering 1440×900 into 1440×870 | frame styles stripped at creation + SWP_FRAMECHANGED; the work-area monitor instead of a size clamp |
| The Air gets warm while playing | ~11 W for the whole Mac on battery (GPU ~60%, the game ~1.1 cores, 1440×870 on a 60 Hz screen), thermal state fair → nominal, no throttling, ~9 GB footprint (unified memory) | normal for a fanless Air at this load; a frame cap or lower settings should help (not tried) |
