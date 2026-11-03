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
| Anti-cheat | EasyAntiCheat, BattlEye, Vanguard, nProtect folders/drivers | Kernel anti-cheat never works under Wine: stop |
| Store/launcher dependency | `steam_api64.dll` and `SteamAPI_Init` in logs; Epic/EOS SDK; custom launchers (`UpLauncher`) | Steam games need Windows Steam inside Wine (`reference/steam-in-wine.md`); launcher checks can be satisfied by stub DLLs |
| Online/cloud needs | logs (Steam Cloud, UGS, PlayFab...), what happens offline | Network must work inside Wine (TLS = gnutls dylibs) or be blocked on purpose (TDU2 sandbox) |
| Videos | `.mp4/.webm/.bik/.mkv/.usm` files, `VideoClip` in Unity assets | Media Foundation → winegstreamer → GStreamer.framework; Bink is fine natively |
| Input | Unity Input System (`UnityEngine.InputSystem` in metadata), raw input imports, `EnableMouseInPointer` | Unity 6 pointer-mode mouse needs the ngfix shim |
| Window mode | fullscreen by default? command-line switches (`-screen-*`, `-w`) | The user dislikes test runs that grab the screen; plan windowed tests |

## Decision

- **D3D11, 64-bit, no kernel anti-cheat** → the Northgard recipe: Sikarugir WineCX 24 engine + DXMT +
  GStreamer (+ Windows Steam in Wine if it uses Steamworks). Highest chance of success.
- **D3D9 / older, 32-bit** → Gcenx Wine 11 (wined3d on OpenGL, or DXVK-macOS on MoltenVK). Expect WoW64
  issues; check the FoD notes (`wow64cpu` patch) before debugging random crashes.
- **DirectDraw / very old 2D** → Wine's ddraw first; if it is slow or wrong, an own wrapper DLL
  (FoD: DirectDraw → SDL3 inside `FODDD.dll`, exe import patched).
- **D3D12-only** → no free route (DXMT is D3D11/10; D3DMetal is Apple's GPTK with its own EULA).
  Tell the user; CrossOver trial is the paid fallback.
- **SecuROM / StarForce / kernel anti-cheat** → stop and tell the user what you found. Options are
  theirs: play on the PC, stream it (Sunshine + Moonlight), a Windows 11 ARM VM (may also be detected).
  Never crack or patch DRM, never use keygens.

Write the verdict and the facts into the kit's `CLAUDE.md` before moving on, so a later session
does not redo the triage.
