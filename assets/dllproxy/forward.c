/* forward.c: fills the slots of forward_thunks.S (gen_forwarder.py) from the real system DLL.
 *
 * Call forward_init(inst, L"dbghelp.dll") from the shim's DllMain (DLL_PROCESS_ATTACH), before any
 * game code can call through the proxy. The real DLL is loaded by full path from the system
 * directory; Wine then keeps two modules with the same base name (the proxy from the game folder,
 * its builtin from system32), as Windows does. Loading from DllMain is fine here: the loader lock is
 * re-entrant for the loading thread.
 * Slots of names the real DLL lacks keep pointing at fwd_missing (returns 0, ERROR_PROC_NOT_FOUND).
 */
#include <windows.h>

extern void *fwd_slots[];
extern const char *const fwd_names[];
extern const int fwd_count;

ULONG_PTR fwd_missing(void)
{
    SetLastError(ERROR_PROC_NOT_FOUND);
    return 0;
}

/* returns how many exports were forwarded, -1 if the real DLL didn't load */
int forward_init(HINSTANCE self, const WCHAR *dll)
{
    WCHAR path[MAX_PATH];
    UINT n = GetSystemDirectoryW(path, MAX_PATH - 64);
    HMODULE real;
    int i, found = 0;

    if (!n || n >= MAX_PATH - 64 || lstrlenW(dll) > 60) return -1;
    lstrcatW(path, L"\\");
    lstrcatW(path, dll);
    real = LoadLibraryW(path);
    if (!real || real == self) return -1;
    for (i = 0; i < fwd_count; i++)
    {
        void *p = (void *)GetProcAddress(real, fwd_names[i]);
        if (p)
        {
            fwd_slots[i] = p;
            found++;
        }
    }
    return found;
}
