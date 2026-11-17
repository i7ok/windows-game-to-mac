# Unreal Engine 5 games

Learned on MY CAT SCATTERED GAMES: Tidy Videogame Supermarket (UE 5.7, Steam, Nov 2026): it runs on the
shared Windows Steam (WineCX 24 + DXMT 0.74) with `-dx11`, the uefix shim and one Wine setting for
Unreal's bootstrapper.

## Layout and triage
- Steam starts the **bootstrapper** `<Project>.exe` at the top of the install (BootstrapPackagedGame,
  ~300 KB). It checks prerequisites, then starts `<Project>/Binaries/Win64/<Project>-Win64-Shipping.exe`
  with the same arguments and waits for it. The shipping exe is monolithic (all engine code, ~150 MB) and
  owns the window: make it the kit's `exe` (Dock identity, watcher) and put shims next to it (`exedir`).
- Version: the UTF-16 string `++UE5+Release-5.x` in the shipping exe.
- **Renderer:** UE5 starts on D3D12 (`d3d12.dll` delay import, Agility SDK in `Binaries/Win64/D3D12`).
  DXMT is D3D11 only. The game runs on D3D11 if it was cooked with SM5 shaders: look for
  `ShaderArchive-…-PCD3D_SM5` in the `.utoc` file names (`strings -a Paks/*.utoc`); only
  `PCD3D_SM6` = D3D12-only, no free route. Pass **`-dx11`** (kit `args`). Wine's `d3d12.dll` still gets
  loaded (a probe), harmless.
- No DRM in plain sections (.text/.rdata/.data/.pdata/_RDATA/.rsrc/.reloc); Denuvo adds odd big sections.
  EAC/EOS anti-cheat: `EasyAntiCheat/` folder → `triage.md`.
- Videos: `Content/Movies/*.mp4` (H.264/AAC) through Media Foundation → winegstreamer → GStreamer libav.
  `gst_video_info_from_caps: assertion 'gst_caps_is_fixed (caps)' failed` once per movie is harmless.
- Web widgets: CEF (`Engine/Binaries/ThirdParty/CEF3`, `EpicWebHelper.exe`) runs; its processes stay
  BackgroundOnly (nothing in the Dock).
- Shipping builds usually write **no Unreal log**. Check `AppData/Local/<Project>/Saved/Logs/` for the
  game's own logs. To see which renderer and media modules loaded, `lsof -p <pid>`: DXMT's `d3d11.dll`,
  `dxgi.dll`, `winemetal.so` and its Metal shader cache `~/Library/Caches/dxmt/<exe>/` mean D3D11 on
  Metal; `winegstreamer.so` + `libgstlibav` mean the movies decode.
- Settings: `AppData/Local/<Project>/Saved/Config/Windows/GameUserSettings.ini` (written at exit):
  `ResolutionSizeX/Y`, `FullscreenMode` (0 fullscreen, 1 windowed fullscreen, 2 windowed),
  `[ScalabilityGroups] sg.*Quality` (3 = Epic when no benchmark ran). Saves: `Saved/SaveGames/*.sav`
  (often Steam Auto-Cloud `WinAppDataLocal`).
- Arguments: `-dx11`, `-windowed`, `-fullscreen`, `-ResX=`, `-ResY=`, `-WinX=`, `-WinY=`.

## The bootstrapper says "Microsoft Visual C++ 2015-2022 Redistributable (x64)" is required
"The following component(s) are required to run this program … Would you like to install them now?"
appears although Microsoft's runtime is in the prefix (Steamworks Common Redistributables install it).
Yes runs `Engine\Extras\Redist\en-us\vc_redist.x64.exe` and relaunches; No exits (0x232a) and no game starts.
- The check (UE 5.7, read from the disassembly; the minimum version is a constant in `.rdata`, 14.44.35211
  here): app-local `msvcp140_2.dll`/`vcruntime140_1.dll`, else `HKLM\SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64`
  Major/Minor/Bld, then for `%SystemRoot%\system32\msvcp140_2.dll` and `vcruntime140_1.dll`:
  `GetFileVersionInfoSizeW`/`VerQueryValueW("\")` ≥ the minimum, and `LoadLibraryW` succeeds.
- Under WineCX the registry passes and the file check fails: `GetFileVersionInfoSizeExW` opens the file
  with `LOAD_LIBRARY_AS_IMAGE_RESOURCE`, and for these names Wine maps its **builtin** DLLs (system32 →
  builtin load order), which carry no version resource → error 1812. A byte-identical copy under another
  name reports its version. Test exe: the same steps fail by default and pass with
  `msvcp140,msvcp140_2,vcruntime140,vcruntime140_1=n,b`.
- Fix: those four `native,builtin` in `AppDefaults\<Project>.exe\DllOverrides`, **for the bootstrapper
  only** (shared launcher: `dlloverrides=…;<Project>.exe:msvcp140=native,builtin;…`). Nothing gets
  installed; the game exe keeps Wine's default runtime. Verify with a stand-in exe named like the bootstrapper.
- Without the Microsoft runtime in system32, let the bootstrapper run `vc_redist.x64.exe` once (Yes), then
  apply the same overrides.

## Window and quit: uefix (`assets/uefix/`)
App-local `version.dll` next to the shipping exe (it imports VERSION.dll itself) +
`AppDefaults\<exe>\DllOverrides version=native,builtin`. Patches the exe's own imports.
- **Cmd+Q** = Alt+Q (scan 0x10) → WM_CLOSE to the game window: Unreal exits normally (verified, 3 s,
  saves and Steam Cloud fine). WM_QUERYENDSESSION → WM_CLOSE. SC_KEYMENU dropped (lone Command).
- **Work-area window** (`-nowindowfix` turns it off): Unreal puts its windowed-fullscreen window on
  GetMonitorInfo's `rcMonitor` and sizes it from the display metrics, so the exe sees a monitor the size of
  the work area (rcMonitor = rcWork, EnumDisplayMonitors rects, EnumDisplaySettingsW modes, SM_CX/CYSCREEN).
  The game window is the first top-level `UnrealWindow` with WS_EX_APPWINDOW (menus and tooltips are
  WS_EX_TOOLWINDOW). Born WS_POPUP, frame styles stripped in WM_STYLECHANGING, every move/resize clamped
  to the work area. Unreal then saves `ResolutionSizeX/Y` = the work area and renders at that size.
- **Unreal knows its window's position only from WM_MOVE** (Slate's cached screen position: mouse
  hit-testing and the `ClipCursor` rect of the captured mouse). Two traps, both seen: a move made inside
  the CreateWindowExW hook happens before Unreal has the handle, so its WM_MOVE is ignored; and a clamped
  move that changes nothing on screen (Unreal's `MoveWindowTo(0,0)` when it enters windowed fullscreen)
  makes Wine send no WM_MOVE at all. Unreal then believed its client area was at 0,0: clicks 30 px (the
  menu bar) off and `ClipCursor(1,1-1439,869)` instead of `(1,31-1439,899)`. Fix in uefix: post the first
  fit (runs after CreateWindowEx returned), and send the game a WM_MOVE with the real client origin after
  every clamp. uefix logs `ClipCursor` calls: their rect must sit inside the work area.
- Unreal's SetWindowPos for exclusive fullscreen uses SWP_NOSENDCHANGING, which skips the clamp. Keep the
  game in windowed fullscreen (its default; `FullscreenMode=1`).
