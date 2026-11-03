# The free stack: components, versions, why

Everything is free and redistributable. Pin every download by sha256 and ship it in the kit's
`cache/`, so installs don't depend on GitHub availability and nothing changes under you.

## Engines (Wine builds)

| Engine | Use for | Notes |
|---|---|---|
| **Sikarugir `WS12WineCX24.0.7_7`** (github.com/Sikarugir-App/Engines, release v1.0), sha256 `203f9e9f…45fd1c` | D3D11 games with DXMT; Windows Steam | Built from CodeWeavers' LGPL sources ("wine-9.0 (SikarugirCX 24.0.7)"). **Only free build that exports `macdrv_view_*_metal_*`**, which DXMT needs. WoW64 mode (no i386-unix). Ships Mono/Gecko, **no support dylibs**. Quirks below. |
| **Gcenx `wine-stable 11.0_1`** (github.com/Gcenx/macOS_Wine_builds), sha256 `b50dc50e…cb82388` | D3D9/ddraw/OpenGL games (TDU2, FoD); source of the 94 support dylibs | Hides the Metal symbols, so it can't do DXMT. 32-bit code under Rosetta can crash at random without FoD's `wow64cpu` patch. Can't find `wineserver` from a custom bin dir: set `PATH` or `WINESERVER`. |

Never run two Wine builds against one prefix. A prefix touched by two versions gets corrupted for good
(Steam's webhelper crash-loops, no login). Store the engine version next to the prefix and refuse to
start on a mismatch.

### WineCX 24 (Sikarugir) quirks found on the Mac
- **The loader re-executes itself from `<libdir>/../../bin`** (via wine-preloader), not from its own
  folder. If you put `bin/*` into `Contents/MacOS/`, add the symlink `Contents/bin -> MacOS`. Otherwise
  you get `wine: could not exec the wine loader` (`wine --version` still works, which is misleading).
- **Every Windows process runs from a temp copy of the loader named after the exe**
  (`/var/folders/.../T/winetemp-<inode>-<size>-<mtime>-0/Steam.exe`, "CrossOver Hack 10913").
  So the Dock shows `Steam.exe`, `Northgard Battlegrounds.exe` and so on, never your bundle's name.
  Only the LaunchServices-launched launcher process carries the .app identity.
  `WINEPRELOADERAPPNAME` does not change this.
- **`nsiproxy.so` bug: TCP/UDP tables never load on macOS.** `GetExtendedTcpTable` returns 50, which
  breaks Steam. Fix: `scripts/wine/patch_nsiproxy.sh` (byte patch + ad-hoc re-sign).
- **Command maps to Alt** for Windows apps (no `LeftCommandIsCtrl` option in this build; Option becomes
  Alt only with `OptionIsAlt`).
- Wine user name inside the prefix is `crossover` (paths: `C:\users\crossover\...`, TEMP =
  `C:\users\crossover\Temp`). The prefix locale follows macOS (ru-RU → Steam in Russian), Windows 10
  build 19045.

## Graphics
- **DXMT 0.74 builtin** (github.com/3Shain/dxmt), sha256 `2598981a…a0f0ceb`: D3D11/10 → Metal.
  Copy `d3d11.dll dxgi.dll d3d10core.dll winemetal.dll` into **both** `lib/wine/x86_64-windows` and
  `i386-windows`, and `winemetal.so` into `x86_64-unix`: into the Wine tree, never the prefix.
  Drop `nvapi64.dll`/`nvngx.dll`. Healthy Unity log: `Direct3D 11.0 [level 11.1]`, `Renderer: Apple M1`.
  `d3d12: failed to create D3D12 device` alongside it is harmless.
- D3D9: Wine's wined3d (OpenGL) or DXVK-macOS + MoltenVK. Set `MVK_CONFIG_LOG_LEVEL=1`, otherwise
  MoltenVK floods the log.
- Known DXMT 0.74 issue with some Unity 6 titles: transparent/empty window. notpop's fork fixes the
  present path. Northgard did not hit it.

## Support dylibs (from Gcenx wine-stable `Contents/Resources/wine/lib/*.dylib`)
Needed by the WineCX engine, which ships none. `libgnutls` (without it Steam can't log in),
`libfreetype` (dialogs without text otherwise), `libinotify` (wineserver won't start). Put them in
`Contents/lib`. wineserver's rpath `@loader_path/../../` resolves to the .app root, so add a symlink
`libinotify.0.dylib` there too. Unix `.so` files find the rest through `DYLD_FALLBACK_LIBRARY_PATH`.

## Video
**GStreamer 1.28.7 universal pkg**, sha256 `529fdf4a…d73911994`. It installs to
`/Library/Frameworks`. Without it, Unity games that wait for an intro video hang on a black screen.
Install it **before** creating the prefix. `sudo` has no TTY when Claude runs it, so use the native
password dialog:
`osascript -e 'do shell script "/usr/sbin/installer -pkg \"…pkg\" -target /" with administrator privileges'`.
The `pygobject initialization failed` lines in logs are GStreamer's python plugin. Harmless.

## Rosetta 2
Everything is x86_64 under Rosetta. Check with `arch -x86_64 /usr/bin/true`. Install with
`softwareupdate --install-rosetta --agree-to-license`, which needs an admin password.
