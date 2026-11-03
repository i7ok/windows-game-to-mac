# Changing behaviour without source: DLL shims and byte patches

Prefer, in this order: Wine settings and registry → a shim DLL next to the game → a byte patch of a
Wine file in our bundle. Never patch the game's own exe if it has DRM integrity checks (SecuROM). Never
patch or crack DRM at all.

## App-local proxy DLL (how ngfix gets into the game)
1. Pick a DLL that the game module imports, that has few exports, and that no Wine builtin imports
   earlier: `scripts/pe/imports.py UnityPlayer.dll`. `VERSION.dll` (3 functions) worked for Unity.
2. Export the same names. Forward to a module with a *different* name that implements them (e.g.
   `GetFileVersionInfoA = kernelbase.GetFileVersionInfoA` in the `.def`). A forwarder to the same base
   name (`version.X`) would resolve to yourself. Stub what has no other home.
3. **Wine prefers its builtin DLL even when a native one sits next to the exe.** `+loaddll` prints the
   app path but says `: builtin`. Force native only for the game exe:
   `HKCU\Software\Wine\AppDefaults\<Game>.exe\DllOverrides` `"<dll>"="native,builtin"`. A global
   `WINEDLLOVERRIDES` would work too but affects every process.
4. In DllMain: check the process exe name (other exes in the folder, like crash handlers, load it too),
   then do the work. Dependencies' imports are already resolved when your DllMain runs, so you can patch
   the importing module's IAT right there.
5. The game folder belongs to Steam: have the launcher re-copy the DLL when it differs (`cmp -s`).
   Steam doesn't delete extra files, but "verify files" or a reinstall can.
6. Verify loading with a stand-in before involving the user: a stub exe with the game's exe name that
   imports the same module, run with `WINEDEBUG=+loaddll`, and `native` in the output.

## Stub DLLs with exact exports (TDU2's CrashHook.dll)
To satisfy a launcher check or remove crash-reporter UI, replace the game's DLL with a stub exporting
the same names, keeping the original as `.orig`. For 32-bit stdcall, export names must keep their
decoration (`_crInstallA@4`). mingw `.def` handling differs from MSVC's `EXPORTAS`, so verify the export
table against the original after building.

## IAT patching and message hooks
- IAT patch: walk the importing module's import descriptors, find the thunk by name (OriginalFirstThunk),
  `VirtualProtect` and swap the pointer. Keep the original for pass-through. Logging wrappers cost
  nothing and save a test round (ShowCursor/SetCursor in ngfix).
- Rewrite messages: `SetWindowsHookExW(WH_GETMESSAGE, proc, NULL, tid)` on the window's thread. The hook
  may modify the MSG before GetMessage/PeekMessage returns (`wp == PM_REMOVE` when it is removed).
  Sent messages need WH_CALLWNDPROC, which can only observe, not change the result.
- Install hooks lazily on the right thread: at `CreateWindowExW` of the game's window class (IAT-hook it)
  or when the engine calls the API you fake.

## Byte-patching a Wine file in our bundle
1. Find the code: `strings`/`otool -tV` (Mach-O unix libs) or `x86_64-w64-mingw32-objdump -d` (PE).
   For PE, `scripts/pe/xref.py` finds code referencing a string, `scripts/pe/callsites.py` finds calls to
   an import, `scripts/pe/iat.py` names IAT slots.
2. Look for room: tail calls with `push rbp; mov rbp,rsp; …; pop rbp; jmp` free 5 bytes plus padding.
   Recompute RIP-relative displacements when instructions move.
3. Patch only if the original bytes match exactly (the install script must refuse other builds). Make
   it idempotent: accept already-patched bytes.
4. `codesign -f -s - file`. Verify with `codesign -v` and by disassembling the patched range.
5. Put the patch in `install.sh` (or a script it calls). A hand-patched installed app is lost on the
   next rebuild. Re-run the installer and compare the result (`codesign --remove-signature` copies, then `cmp`).
Example: `scripts/wine/patch_nsiproxy.sh`.

## Reading a closed binary's logic (Steam, UnityPlayer)
Find the error string in the logs, then `xref.py` gives the referencing function, then
`objdump --start-address/--stop-address` around it, then `iat.py` names the imported calls in that code.
That is how the Steam check (GetExtendedTcpTable → EqualSid → ProcessIdToSessionId → PID compare) and
Unity's `IsMouseInPointerEnabled → EnableMouseInPointer(TRUE)` were confirmed in minutes, without guessing.
