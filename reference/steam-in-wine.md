# Windows Steam inside Wine

A Steamworks game (`steam_api64.dll`, `[Steamworks.NET] Initialized`, `Steam Login success`) needs a
running **Windows** Steam client logged into the user's account. Mac Steam can't serve a Windows
process. Steam Cloud saves then sync with the PC. Copying the game from the PC plus its
`appmanifest_<appid>.acf` into `steamapps/` makes Steam treat it as installed (it may still patch it).

## One Windows Steam for all games
A game reaches Steam only from inside Steam's own prefix: `steam_api` finds the client through the
prefix's registry (`HKCU\Software\Valve\Steam\ActiveProcess`) and talks to it over Windows IPC that lives
in one wineserver. So instead of a Steam inside every game app, all Steam games share one prefix:
`~/Macdows/WindowsSteam-Mac` builds `Windows Steam.app` (Wine, DXMT, dockfix, Steam, every game), and
each game gets a ~1 MB app (icon, `game.conf`, shims) whose executable only runs
`WindowsSteam --launch <its .app>`. Like a Steam shortcut on Windows, a game started while Steam is
closed starts it. Model game kit: `~/Macdows/Dimraeth-Mac` (`game.conf` with name, appid, exe, dir, args,
dlloverrides, shims; `payload/`; `game/`), installed with `WindowsSteam-Mac/add-game.sh`.
- `steam [-silent] -applaunch <appid> <args>` is the same call whether Steam runs or not: a running
  Steam gets it forwarded.
- A game started by a running Steam inherits **Steam's** environment, not its own app's. Per-game
  settings go to `HKCU\Software\Wine\AppDefaults\<exe>` and to the game folder (shims); Steam-wide env
  applies to every game and takes effect when Steam starts.
- Closing: a Steam that a game started shuts down after the last game; one the user opened stays.
- All games of one Steam run on one Wine build. Another engine needs another prefix (one more login);
  move `steamapps` over instead of downloading again.
- Steam can stop a launch to ask the user, and only the user answers: "your account is playing <game>
  on another computer" (`KickingOtherSession` in `console_log.txt`), interstitials, cloud conflicts. A
  cancel writes no line of its own: it shows as `SlowJSMethodCall Apps.CancelGameAction` (console_log)
  and, for Auto-Cloud games, `[AppID N] Starting sync (up,AC Exit,)` (cloud_log). Bound the launcher's
  wait: while the process LaunchServices started for a game app lives, clicks on its icon do nothing.
- Moving per-game apps over: adopt one logged-in prefix of the same engine (`install.sh --adopt`), then
  per game move `steamapps/common/<dir>` + `appmanifest_<appid>.acf`, copy `userdata/<account>/<appid>`
  (Cloud cache) and `AppData/LocalLow/<studio>/<game>`, and carry its HKCU key (Unity PlayerPrefs) with
  `reg export`/`reg import` under the same engine.
- Verified on an M1 (Nov 2026): Steam auto-start and silent login from a game click, Dock identities,
  an icon click on a running Steam → its window, Cmd+Q in Steam → clean shutdown with the login kept.
  Not yet: a game run end to end from the shared Steam (the account was in-game elsewhere).

## Install and first start
1. Silent install: `wine SteamSetup.exe /S`, then `wineserver -w`. `Steam.exe` lands in
   `C:\Program Files (x86)\Steam`.
2. `steam_dev.cfg` with `@nClientDownloadEnableHTTP2PlatformWindows 0`, because HTTP/2 downloads stall.
3. First launch: the bootstrapper downloads ~336 MB, restarts, may download ~240 MB more, then
   starts `steamwebhelper` (CEF) and shows the "Sign in to Steam" window (an `SDL_app` window, titled
   «Войти в Steam» in Russian Steam). Watch progress in `logs/bootstrap_log.txt`, or with `wtool list`
   (`BootstrapUpdateUIClass` window with progress text).
4. The user logs in (password or QR from the Steam app, Steam Guard on the phone). Then `-applaunch`
   starts the game. The next launches run with `-silent`, so Steam logs in by itself without a window.

## "Unexpected Transport Error (0x3008)" («Непредвиденная ошибка транспорта» in Russian Steam)
- Symptom: no login window. `logs/transport_client.txt` repeats `WebUITransport: Connection rejected`.
  `webhelper_js.txt` shows websocket retries 0..8, then a VGUI dialog with radio buttons appears.
- Root cause (WineCX 24): `CWebUITransport` accepts a websocket only after
  `GetExtendedTcpTable(AF_INET, TCP_TABLE_OWNER_PID_CONNECTIONS)` finds the client-side row (ESTABLISHED,
  local addr:port = peer). Its owner PID must then pass `EqualSid` + `ProcessIdToSessionId` and equal
  Steam's PID or belong to the webhelper. This Wine's `nsiproxy.so` can't read the TCP table at all, so
  every connection is "from unknown source".
- Fix: `scripts/wine/patch_nsiproxy.sh` (built into the Northgard `install.sh`). Verify with
  `scripts/win32/tcptest.exe`: it should print `ret=0`, and your own connection gets a PID after it sends data.
- Owner PIDs exist only for sockets whose fd the Windows process already holds. Wine creates sockets in
  wineserver and hands the fd over lazily, on first I/O. The webhelper sends its upgrade request right
  away, so Steam's check passes.
- The dialog's "VO: Continue Anyway" is «Только для Valve: продолжить» in Russian Steam. Picking it
  does not fix the cause; the patch does. The dialog is VGUI: no Win32 child controls, and it can't be
  captured from another process. Ask the user for a screenshot.

## Login notes
- At the login screen the CM connection is dropped about every 60 s with "Try another CM" /
  "Disconnected By Remote Host", and Steam reconnects. That is normal: login works within it.
- Wine ignores the macOS system proxy (e.g. Hiddify at 127.0.0.1:12334). Steam connects directly,
  which worked in Russia (Nov 2026). Suspect the network only if logins keep failing.
- `cef_log.txt` noise to ignore: `WSALookupServiceBegin failed`, `Failed to set crash key`,
  `gldriverquery.exe` missing SDL2.dll, `Disabling GPU acceleration: Disabled/CommandLine`.

## Closing Steam safely
- Never `kill -9` Steam while logged in: it loses the session token and the user has to log in again
  with phone approval.
- Graceful: `wine steam.exe -shutdown` (the launcher's `--stop`). Steam ignores it while a modal VGUI
  dialog is up. Before any login has happened, `WINEPREFIX=<prefix> wineserver -k` is fine. It kills
  only that prefix's processes, so other Wine sessions of the user survive.
- Close a running game gracefully from outside: post WM_CLOSE to its window
  (`scripts/win32/wclose.exe <hwnd>`; get the hwnd with `wtool list`). The watcher then shuts Steam down.
- Count processes: Steam restarts itself as lowercase `steam.exe`, so match case-insensitively.
  In zsh, `ps args | grep pattern` matches your own shell. Count with `ps -Ao comm | grep -c winetemp-<inode>`.

## Overlay
Disable `gameoverlayrenderer,gameoverlayrenderer64=d`. Its D3D11 hooks can hang games at
`GfxDevice: creating device client`. Unchecking it in Steam's settings is not enough.
