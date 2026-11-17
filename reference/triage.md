# Triage: can this game run on the Mac, and which way

Do this before building anything. Most of the facts are cheapest to collect on the Windows PC where
the game already runs; on the Mac, read them from the copied game folder.

## Facts to collect

| Question | How to find out | Why it matters |
|---|---|---|
| 32- or 64-bit exe | `file Game.exe` (PE32 / PE32+), or pefile | 32-bit code runs in Wine's WoW64 under Rosetta: slower and more fragile (FoD needed a wow64cpu patch) |
| Graphics API | imports (`scripts/pe/imports.py`): d3d9/d3d11/d3d12/ddraw/opengl32/vulkan-1; Unity's `Player.log` line `Direct3D: Version: ...`; Unity `-force-*` strings | Picks the renderer: D3D11 → DXMT; D3D9 → wined3d (OpenGL) or DXVK-macOS; DirectDraw → wined3d or own wrapper DLL; D3D12-only → no free path |
| Engine | `UnityPlayer.dll` + `*_Data/` (Unity; `GameAssembly.dll` = IL2CPP), `*.pak` + `Engine/` (Unreal), else custom | Unity has known Wine quirks with fixes ready (`reference/unity.md`) |
| DRM / copy protection | sections and DLLs: SecuROM (`paul.dll`, wrapper section), StarForce, Denuvo; `steam_api*.dll` (plain Steamworks is fine) | SecuROM 8 fails on Apple Silicon (TDU2): stop early, don't crack |
| Anti-cheat | EasyAntiCheat (`EasyAntiCheat/` folder, `start_protected_game.exe` as Steam's launch option, an install script running `EasyAntiCheat_EOS_Setup.exe`), BattlEye, Vanguard, nProtect folders/drivers | None of them runs on macOS Wine: stop. EAC's Wine mode exists for Linux/Proton only (native `.so` module). On the Mac its bootstrapper logs `Running under Wine on MacOS which is currently unsupported` and exits with 204 (ROB RUN, `case-studies.md`) |
| Store/launcher dependency | `steam_api64.dll` and `SteamAPI_Init` in logs; Epic/EOS SDK; custom launchers (`UpLauncher`) | Steam games need Windows Steam inside Wine (`reference/steam-in-wine.md`); launcher checks can be satisfied by stub DLLs |
| Online/cloud needs | logs (Steam Cloud, UGS, PlayFab...), what happens offline | Network must work inside Wine (TLS = gnutls dylibs) or be blocked on purpose (TDU2 sandbox) |
| Videos | `.mp4/.webm/.bik/.mkv/.usm` files, `VideoClip` in Unity assets | Media Foundation → winegstreamer → GStreamer.framework; Bink is fine natively; WebM decoded by the game's own libvpx (`WebM Project VP9 Decoder` in its strings, DoW DE) needs nothing |
| Input | Unity Input System (`UnityEngine.InputSystem` in metadata), raw input imports, `EnableMouseInPointer` | Unity 6 pointer-mode mouse needs the ngfix shim |
| Window mode | fullscreen by default? command-line switches (`-screen-*`, `-w`) | The user dislikes test runs that grab the screen; plan windowed tests |
| Languages | `language/*.txt` or similar, the store page; bitmap font sheets (`*font*.bmp`, `.fnt` in archives) | A missing language or Cyrillic that shows as accented Latin letters is the game's, not Wine's ("Language and fan translations" below) |

## Decision

- **D3D11, 64-bit, no kernel anti-cheat** → the Northgard recipe: Sikarugir WineCX 24 engine + DXMT +
  GStreamer (+ Windows Steam in Wine if it uses Steamworks). Highest chance of success.
- **D3D9, 64-bit, Steam** → the shared Windows Steam as it is: its WineCX tree keeps Wine's own
  `d3d9.dll` + `wined3d.dll` (OpenGL) next to DXMT's d3d11/dxgi. DoW DE (2025 remaster of a 2004 RTS)
  runs that way at 1440×870 on an M1; the game sees wined3d's stand-in card ("NVIDIA GeForce 8800 GTX"),
  and its `CreateDXGIFactory1` adapter query goes to DXMT's dxgi without trouble.
- **D3D9 / older, 32-bit** → Gcenx Wine 11 (wined3d on OpenGL, or DXVK-macOS on MoltenVK). Expect WoW64
  issues; check the FoD notes (`wow64cpu` patch) before debugging random crashes.
- **DirectDraw / very old 2D** → Wine's ddraw first; if it is slow or wrong, an own wrapper DLL
  (FoD: DirectDraw → SDL3 inside `FODDD.dll`, exe import patched).
- **D3D12-only** → no free route (DXMT is D3D11/10; D3DMetal is Apple's GPTK with its own EULA).
  Tell the user; CrossOver trial is the paid fallback.
- **SecuROM / StarForce / anti-cheat (EasyAntiCheat, BattlEye, kernel drivers)** → stop and tell the
  user what you found. Options are theirs: play on the PC, stream it (Steam Remote Play, Sunshine +
  Moonlight), cloud gaming if the game is offered there, a Windows 11 ARM VM (may also be detected).
  Never crack or patch DRM, never use keygens, never start an anti-cheat game's exe without its
  bootstrapper. When the user wants proof, one launch the official way is cheap: Steam runs the install
  script and the bootstrapper, and EAC's verdict lands in
  `AppData/Roaming/EasyAntiCheat/<productid>/<deploymentid>/anticheatlauncher.log` within seconds.
  APFS-clone a shared prefix first: the install script adds a service (demand start) and VC++ runtimes.

Write the verdict and the facts into the kit's `CLAUDE.md` before moving on, so a later session
does not redo the triage.

## Language and fan translations
- Gcenx Wine 11 takes the code pages and the locale from the Mac's language and region, also for an app started
  from Finder (no `LANG`): a Russian Mac gives ACP 1251, OEM 866, ru-RU (a tiny exe printing `GetACP`, `GetOEMCP`
  and `GetUserDefaultLocaleName` shows it). A non-Unicode game then gets typed Cyrillic as cp1251 bytes.
- Russian that shows as accented Latin letters ("×åðâÿê") or not at all: the game's own 8-bit bitmap fonts have no
  Cyrillic. Look at the font sheets before suspecting Wine (Worms: 162 cells, ASCII + Latin-1). Fan translations
  of such games put Cyrillic glyphs into the Latin-1 slots and cp1251 bytes into the text (Worms: «Русский
  проект»/ENPY Studio). Then Latin-1 symbols in the exe's own strings turn into letters (Worms' `%d×%d` showed
  "1024Ч640"): patch that string in memory from the shim, after checking its bytes.
- Fan patches come as installers from download sites, sometimes with malware reports in the comments. Never run
  them, not even in Wine: Inno Setup installers are listed and extracted by `innoextract` (Homebrew). If its files
  are encrypted and the download page names no password, the installer fills it in itself and it sits among the
  strings of the installer's own setup data; `innoextract --show-password` gives the salted SHA-1 to check
  candidates against (Worms kit: `devtools/inno_pw.py`). Install only data files (text, fonts, archives), compare
  them with the game's first (archives entry by entry, string IDs, printf specifiers), and keep the originals for
  an uninstall script.
