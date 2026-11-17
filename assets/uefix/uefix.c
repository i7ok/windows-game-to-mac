/* uefix: window and quit fixes for Unreal Engine 5 games under Wine on macOS (WineCX 24, winemac).
 *
 * Loaded into the game as an app-local version.dll: a monolithic UE shipping exe imports VERSION.dll
 * itself (GetFileVersionInfoSizeW, GetFileVersionInfoW, VerQueryValueW); the real functions are forwarded
 * to kernelbase by version.def. Wine prefers its builtin version.dll even next to the exe, so the launcher
 * sets HKCU\Software\Wine\AppDefaults\<GAME_EXE>\DllOverrides version=native,builtin (game.conf).
 * All engine code is in the exe, so its import table is the one place to patch.
 *
 * 1. Command+Q. winemac maps Command to Alt, so the game gets Alt+Q; we post WM_CLOSE to its window
 *    instead, which Unreal treats like the window's close button or Alt+F4: the engine exits normally.
 *    Q is matched by scan code, so any keyboard layout works. Quit from the Dock reaches the game as
 *    WM_QUERYENDSESSION, which we also turn into a normal close.
 * 2. A lone Command (also the start of Command+Tab) is an Alt tap, which DefWindowProc turns into
 *    WM_SYSCOMMAND SC_KEYMENU (keyboard menu mode) on a window with WS_SYSMENU: dropped.
 * 3. Window over the work area (on unless the command line has -nowindowfix). A window covering the
 *    whole screen is "fullscreen" for winemac: the menu bar still slides in over it at the top edge.
 *    So the game gets a borderless window over the work area (below the menu bar, above the Dock),
 *    like a normal Mac window. Unreal places its "windowed fullscreen" window on the monitor rect of
 *    GetMonitorInfo and sizes it from the display metrics (SM_CX/CYSCREEN, EnumDisplayMonitors,
 *    EnumDisplaySettings), so the exe is shown a monitor the size of the work area: rcMonitor = rcWork,
 *    the monitor's rects, display modes of its size and SM_CX/CYSCREEN all become the work area.
 *    Safety net, as in ngfix: the game window (the first top-level "UnrealWindow" with WS_EX_APPWINDOW)
 *    is born without frame styles, WM_STYLECHANGING can't add them back, and every move/resize is
 *    clamped to the work area (with SWP_FRAMECHANGED, so Wine recomputes the client rect). Whatever
 *    window mode the game picks, its window is the work area; Unreal sizes its back buffer from WM_SIZE.
 *    Unreal knows where its window is only from WM_MOVE (Slate's cached position: mouse hit-testing,
 *    the ClipCursor rect of the captured mouse). So the first fit waits until CreateWindowEx has
 *    returned to Unreal (a posted message), and after every clamp the game gets a WM_MOVE with the real
 *    client origin: a clamped move often changes nothing on screen, and Wine then sends none. Without
 *    this, Unreal kept the 0,0 it had asked for: clicks and the cursor clip 30 px (the menu bar) off.
 *
 * Only active in GAME_EXE (other exes of the game may load a version.dll too).
 * Log: %TEMP%\uefix.log (C:\users\crossover\Temp in a WineCX prefix).
 *
 * Built for MY CAT SCATTERED GAMES: Tidy Videogame Supermarket (UE 5.7, GameKeeper-Win64-Shipping.exe).
 * For another UE game: set GAME_EXE (or -DGAME_EXE=L\"...\") and check with scripts/pe/imports.py that
 * the shipping exe imports VERSION.dll and the USER32 functions patched in install().
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>

#ifndef GAME_EXE
#define GAME_EXE L"GameKeeper-Win64-Shipping.exe"
#endif
#define GAME_WINDOW_CLASS L"UnrealWindow"

#define SCAN_Q 0x10
#define FRAME_STYLES (WS_CAPTION | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX)

static DWORD hooked_threads[8];
static int hooked_count;
static BOOL fit_workarea = TRUE;
static HWND game_hwnd;
static WNDPROC game_prev_proc;
static LONG fits_logged, windows_logged, display_logged, cursor_logged;

static void log_line(const char *fmt, ...)
{
    WCHAR path[MAX_PATH];
    char buf[512];
    va_list args;
    FILE *f;
    SYSTEMTIME st;

    GetTempPathW(MAX_PATH - 16, path);
    lstrcatW(path, L"uefix.log");
    if (!(f = _wfopen(path, L"a"))) return;
    GetLocalTime(&st);
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    fprintf(f, "%02d:%02d:%02d.%03d [%04lx] %s\n", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
            GetCurrentThreadId(), buf);
    fclose(f);
}

/* --- 1. Command+Q and Dock -> Quit ------------------------------------------------------------- */

/* Command arrives as Alt (WM_SYSKEYDOWN with the context bit); accept the Windows key too */
static BOOL command_down(const MSG *msg)
{
    return (msg->lParam & (1 << 29)) || GetAsyncKeyState(VK_MENU) < 0 ||
           (GetAsyncKeyState(VK_LWIN) | GetAsyncKeyState(VK_RWIN)) < 0;
}

static void close_game_window(HWND hwnd, const char *why)
{
    HWND top = game_hwnd ? game_hwnd : GetAncestor(hwnd, GA_ROOT);
    log_line("%s: closing window %p", why, (void *)(top ? top : hwnd));
    PostMessageW(top ? top : hwnd, WM_CLOSE, 0, 0);
}

static LRESULT CALLBACK get_msg_proc(int code, WPARAM wp, LPARAM lp)
{
    MSG *msg = (MSG *)lp;

    if (code == HC_ACTION && msg && wp == PM_REMOVE)
    {
        if (msg->message == WM_KEYDOWN || msg->message == WM_SYSKEYDOWN)
        {
            UINT scan = (msg->lParam >> 16) & 0xff;
            if ((scan == SCAN_Q || msg->wParam == 'Q') && command_down(msg))
            {
                close_game_window(msg->hwnd, "Command+Q");
                msg->message = WM_NULL;
            }
        }
        else if (msg->message == WM_SYSCOMMAND && (msg->wParam & 0xfff0) == SC_KEYMENU)
            msg->message = WM_NULL;   /* posted keyboard-menu requests (a lone Command) */
    }
    return CallNextHookEx(NULL, code, wp, lp);
}

/* sent messages: Quit from the Dock (or Wine's own quit) asks with WM_QUERYENDSESSION */
static LRESULT CALLBACK call_wnd_proc(int code, WPARAM wp, LPARAM lp)
{
    CWPSTRUCT *cwp = (CWPSTRUCT *)lp;

    if (code == HC_ACTION && cwp && cwp->message == WM_QUERYENDSESSION)
        close_game_window(cwp->hwnd, "quit request");
    return CallNextHookEx(NULL, code, wp, lp);
}

/* message hooks on every thread that creates an Unreal window */
static void hook_thread(DWORD tid)
{
    int i;
    for (i = 0; i < hooked_count; i++) if (hooked_threads[i] == tid) return;
    if (hooked_count == ARRAYSIZE(hooked_threads)) return;
    if (SetWindowsHookExW(WH_GETMESSAGE, get_msg_proc, NULL, tid) &&
        SetWindowsHookExW(WH_CALLWNDPROC, call_wnd_proc, NULL, tid))
    {
        hooked_threads[hooked_count++] = tid;
        log_line("message hooks installed on thread %04lx", tid);
    }
    else log_line("SetWindowsHookEx failed: %lu", GetLastError());
}

/* --- 3. the game window over the work area --------------------------------------------------- */

/* the work area of the window's monitor: below the menu bar, above the Dock (if it doesn't hide) */
static BOOL get_work_area(HWND hwnd, RECT *rc)
{
    MONITORINFO mi = { sizeof(mi) };
    if (!GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY), &mi)) return FALSE;
    *rc = mi.rcWork;
    return rc->right > rc->left && rc->bottom > rc->top;
}

static void fit_now(HWND hwnd)
{
    RECT wa;
    if (IsIconic(hwnd) || !get_work_area(hwnd, &wa)) return;
    SetWindowPos(hwnd, NULL, wa.left, wa.top, wa.right - wa.left, wa.bottom - wa.top,
                 SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

/* Unreal keeps the window's screen position from WM_MOVE only (Slate's cached position: mouse
 * hit-testing, ClipCursor rects). A clamped move often changes nothing on screen, so Wine sends no
 * WM_MOVE, and Unreal would keep the position it asked for (0,0 instead of 0,30: everything 30 px off).
 * After each clamp the game gets a WM_MOVE with the real client origin. */
#define WM_UEFIX_FIT (WM_APP + 0x3e1)
static BOOL clamp_pending;
static LONG moves_logged;

static void tell_position(HWND hwnd)
{
    static BOOL telling;
    POINT origin = { 0, 0 };
    if (telling) return;
    telling = TRUE;
    ClientToScreen(hwnd, &origin);
    if (InterlockedIncrement(&moves_logged) <= 10) log_line("game told its position: client at %ld,%ld", origin.x, origin.y);
    CallWindowProcW(game_prev_proc, hwnd, WM_MOVE, 0, MAKELPARAM(origin.x, origin.y));
    telling = FALSE;
}

/* the game's window procedure first, then our rules win */
static LRESULT CALLBACK game_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    LRESULT ret;

    if (msg == WM_SYSCOMMAND && (wp & 0xfff0) == SC_KEYMENU) return 0;   /* a lone Command (= Alt) */
    if (msg == WM_UEFIX_FIT)
    {
        /* posted at creation: Unreal knows its window by now, so it sees the WM_MOVE/WM_SIZE */
        if (fit_workarea) fit_now(hwnd);
        return 0;
    }
    ret = CallWindowProcW(game_prev_proc, hwnd, msg, wp, lp);
    if (!fit_workarea) return ret;

    if (msg == WM_WINDOWPOSCHANGING && lp)
    {
        WINDOWPOS *pos = (WINDOWPOS *)lp;
        RECT wa;
        if ((pos->flags & (SWP_NOMOVE | SWP_NOSIZE)) != (SWP_NOMOVE | SWP_NOSIZE) && !IsIconic(hwnd) &&
            pos->x > -32000 && get_work_area(hwnd, &wa))
        {
            int x = wa.left, y = wa.top, cx = wa.right - wa.left, cy = wa.bottom - wa.top;
            if ((pos->x != x || pos->y != y || pos->cx != cx || pos->cy != cy) &&
                InterlockedIncrement(&fits_logged) <= 20)
                log_line("window %d,%d %dx%d (flags %x) -> work area %d,%d %dx%d", pos->x, pos->y, pos->cx,
                         pos->cy, pos->flags, x, y, cx, cy);
            pos->x = x;
            pos->y = y;
            pos->cx = cx;
            pos->cy = cy;
            /* FRAMECHANGED: Wine sends WM_NCCALCSIZE only when the size changes or this is set */
            pos->flags = (pos->flags & ~(SWP_NOMOVE | SWP_NOSIZE)) | SWP_FRAMECHANGED;
            clamp_pending = TRUE;
        }
    }
    else if (msg == WM_WINDOWPOSCHANGED && clamp_pending)
    {
        clamp_pending = FALSE;
        tell_position(hwnd);
    }
    else if (msg == WM_STYLECHANGING && (INT)wp == GWL_STYLE && lp)
    {
        STYLESTRUCT *ss = (STYLESTRUCT *)lp;
        ss->styleNew = (ss->styleNew & ~FRAME_STYLES) | WS_POPUP;
    }
    else if ((msg == WM_SETTINGCHANGE && wp == SPI_SETWORKAREA) || msg == WM_DISPLAYCHANGE)
        fit_now(hwnd);
    return ret;
}

static HWND (WINAPI *real_CreateWindowExW)(DWORD, LPCWSTR, LPCWSTR, DWORD, int, int, int, int, HWND, HMENU,
                                           HINSTANCE, LPVOID);

static HWND WINAPI fake_CreateWindowExW(DWORD ex, LPCWSTR cls, LPCWSTR name, DWORD style, int x, int y, int w,
                                        int h, HWND parent, HMENU menu, HINSTANCE inst, LPVOID param)
{
    BOOL unreal = !IS_INTRESOURCE(cls) && !lstrcmpiW(cls, GAME_WINDOW_CLASS);
    /* the game window appears in the taskbar; Unreal's menus and tooltips are WS_EX_TOOLWINDOW */
    BOOL main = unreal && !game_hwnd && !(style & WS_CHILD) && (ex & WS_EX_APPWINDOW);
    DWORD orig_style = style;
    HWND hwnd;

    if (main && fit_workarea) style = (style & ~FRAME_STYLES) | WS_POPUP;   /* born borderless */
    hwnd = real_CreateWindowExW(ex, cls, name, style, x, y, w, h, parent, menu, inst, param);
    if (unreal && InterlockedIncrement(&windows_logged) <= 20)
        log_line("UnrealWindow %p \"%ls\": style %08lx%s ex %08lx at %d,%d %dx%d%s", (void *)hwnd,
                 name ? name : L"", orig_style, style != orig_style ? " (frame removed)" : "", ex, x, y, w, h,
                 main ? " = the game window" : "");
    if (hwnd && unreal)
    {
        hook_thread(GetCurrentThreadId());
        if (main)
        {
            game_hwnd = hwnd;
            game_prev_proc = (WNDPROC)SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)game_wndproc);
            if (fit_workarea)
            {
                RECT wa = { 0 };
                get_work_area(hwnd, &wa);
                log_line("game window kept over the work area %ld,%ld-%ld,%ld", wa.left, wa.top, wa.right, wa.bottom);
                /* not now: Unreal learns the window's handle only when CreateWindowEx returns, and
                 * ignores the WM_MOVE/WM_SIZE of a move made before that */
                PostMessageW(hwnd, WM_UEFIX_FIT, 0, 0);
            }
        }
    }
    return hwnd;
}

/* --- the monitor as the game sees it: the size of the work area -------------------------------
 * uefix's own calls below go through uefix's imports, so they reach the real functions. */
static BOOL primary_monitor(MONITORINFO *mi)
{
    POINT origin = { 0, 0 };
    mi->cbSize = sizeof(*mi);
    return GetMonitorInfoW(MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY), mi);
}

static BOOL WINAPI fake_GetMonitorInfoW(HMONITOR mon, LPMONITORINFO mi)
{
    BOOL ret = GetMonitorInfoW(mon, mi);
    if (ret && mi)
    {
        if (InterlockedIncrement(&display_logged) <= 30)
            log_line("GetMonitorInfo: monitor %ld,%ld-%ld,%ld reported as %ld,%ld-%ld,%ld", mi->rcMonitor.left,
                     mi->rcMonitor.top, mi->rcMonitor.right, mi->rcMonitor.bottom, mi->rcWork.left, mi->rcWork.top,
                     mi->rcWork.right, mi->rcWork.bottom);
        mi->rcMonitor = mi->rcWork;
    }
    return ret;
}

struct enum_monitors_ctx { MONITORENUMPROC proc; LPARAM lp; };

static BOOL CALLBACK enum_monitor_as_work_area(HMONITOR mon, HDC hdc, LPRECT rc, LPARAM lp)
{
    struct enum_monitors_ctx *ctx = (struct enum_monitors_ctx *)lp;
    MONITORINFO mi = { sizeof(mi) };
    RECT r = *rc;
    if (GetMonitorInfoW(mon, &mi)) r = mi.rcWork;
    return ctx->proc(mon, hdc, &r, ctx->lp);
}

static BOOL WINAPI fake_EnumDisplayMonitors(HDC hdc, LPCRECT clip, MONITORENUMPROC proc, LPARAM lp)
{
    struct enum_monitors_ctx ctx = { proc, lp };
    if (hdc || !proc) return EnumDisplayMonitors(hdc, clip, proc, lp);
    return EnumDisplayMonitors(NULL, clip, enum_monitor_as_work_area, (LPARAM)&ctx);
}

/* a display mode the size of the primary monitor becomes the size of its work area */
static void mode_as_work_area(DWORD *w, DWORD *h, DWORD mode)
{
    MONITORINFO mi;
    DWORD mw, mh, ww, wh;
    if (!primary_monitor(&mi)) return;
    mw = mi.rcMonitor.right - mi.rcMonitor.left;
    mh = mi.rcMonitor.bottom - mi.rcMonitor.top;
    ww = mi.rcWork.right - mi.rcWork.left;
    wh = mi.rcWork.bottom - mi.rcWork.top;
    if (*w != mw || *h != mh || (ww == mw && wh == mh)) return;
    if (InterlockedIncrement(&display_logged) <= 30)
        log_line("EnumDisplaySettings(%ld): %lux%lu reported as %lux%lu", (long)mode, *w, *h, ww, wh);
    *w = ww;
    *h = wh;
}

static BOOL WINAPI fake_EnumDisplaySettingsW(LPCWSTR dev, DWORD mode, DEVMODEW *dm)
{
    BOOL ret = EnumDisplaySettingsW(dev, mode, dm);
    if (ret && dm) mode_as_work_area(&dm->dmPelsWidth, &dm->dmPelsHeight, mode);
    return ret;
}

static int WINAPI fake_GetSystemMetrics(int index)
{
    MONITORINFO mi;
    if ((index == SM_CXSCREEN || index == SM_CYSCREEN) && primary_monitor(&mi))
        return index == SM_CXSCREEN ? mi.rcWork.right - mi.rcWork.left : mi.rcWork.bottom - mi.rcWork.top;
    return GetSystemMetrics(index);
}

/* --- cursor calls, logged only (two cursors, mouse capture) ------------------------------------ */
static int (WINAPI *real_ShowCursor)(BOOL);
static BOOL (WINAPI *real_ClipCursor)(const RECT *);

static int WINAPI fake_ShowCursor(BOOL show)
{
    int count = real_ShowCursor(show);
    if (InterlockedIncrement(&cursor_logged) <= 40) log_line("ShowCursor(%d) -> %d", show, count);
    return count;
}

static BOOL WINAPI fake_ClipCursor(const RECT *rc)
{
    if (InterlockedIncrement(&cursor_logged) <= 40)
    {
        if (rc) log_line("ClipCursor(%ld,%ld-%ld,%ld)", rc->left, rc->top, rc->right, rc->bottom);
        else log_line("ClipCursor(NULL)");
    }
    return real_ClipCursor(rc);
}

/* point the exe's import of dll!name at func; returns the previous target */
static void *patch_import(HMODULE module, const char *dll, const char *name, void *func)
{
    BYTE *base = (BYTE *)module;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    IMAGE_DATA_DIRECTORY *dir = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    IMAGE_IMPORT_DESCRIPTOR *imp;

    for (imp = (IMAGE_IMPORT_DESCRIPTOR *)(base + dir->VirtualAddress); imp->Name; imp++)
    {
        IMAGE_THUNK_DATA *names, *slots;
        if (lstrcmpiA((char *)(base + imp->Name), dll)) continue;
        names = (IMAGE_THUNK_DATA *)(base + (imp->OriginalFirstThunk ? imp->OriginalFirstThunk : imp->FirstThunk));
        slots = (IMAGE_THUNK_DATA *)(base + imp->FirstThunk);
        for (; names->u1.AddressOfData; names++, slots++)
        {
            DWORD old;
            void *prev;
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            if (strcmp((char *)((IMAGE_IMPORT_BY_NAME *)(base + names->u1.AddressOfData))->Name, name)) continue;
            VirtualProtect(&slots->u1.Function, sizeof(void *), PAGE_READWRITE, &old);
            prev = (void *)slots->u1.Function;
            slots->u1.Function = (ULONG_PTR)func;
            VirtualProtect(&slots->u1.Function, sizeof(void *), old, &old);
            return prev;
        }
    }
    return NULL;
}

static void install(void)
{
    HMODULE exe = GetModuleHandleW(NULL);

    real_CreateWindowExW = patch_import(exe, "USER32.dll", "CreateWindowExW", fake_CreateWindowExW);
    log_line("patch CreateWindowExW: %s", real_CreateWindowExW ? "ok" : "missing");
    if (!real_CreateWindowExW) real_CreateWindowExW = CreateWindowExW;
    real_ShowCursor = patch_import(exe, "USER32.dll", "ShowCursor", fake_ShowCursor);
    if (!real_ShowCursor) real_ShowCursor = ShowCursor;
    real_ClipCursor = patch_import(exe, "USER32.dll", "ClipCursor", fake_ClipCursor);
    if (!real_ClipCursor) real_ClipCursor = ClipCursor;
    if (fit_workarea)
    {
        static const struct { const char *name; void *func; } display[] =
        {
            { "GetMonitorInfoW", fake_GetMonitorInfoW },
            { "EnumDisplayMonitors", fake_EnumDisplayMonitors },
            { "EnumDisplaySettingsW", fake_EnumDisplaySettingsW },
            { "GetSystemMetrics", fake_GetSystemMetrics },
        };
        int i;
        for (i = 0; i < ARRAYSIZE(display); i++)
            log_line("patch %s: %s", display[i].name,
                     patch_import(exe, "USER32.dll", display[i].name, display[i].func) ? "ok" : "missing");
    }
}

/* a whole word of the command line, case-insensitively (Unreal's own switches are, too) */
static BOOL has_switch(const WCHAR *cmdline, const WCHAR *sw)
{
    size_t n = lstrlenW(sw);
    const WCHAR *p;
    for (p = cmdline; *p; p++)
        if ((p == cmdline || p[-1] == ' ' || p[-1] == '"') && !_wcsnicmp(p, sw, n) &&
            (!p[n] || p[n] == ' ' || p[n] == '"'))
            return TRUE;
    return FALSE;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, void *reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        WCHAR exe[MAX_PATH], *name;
        const WCHAR *cmdline = GetCommandLineW();
        DisableThreadLibraryCalls(inst);
        GetModuleFileNameW(NULL, exe, MAX_PATH);
        name = wcsrchr(exe, '\\');
        if (lstrcmpiW(name ? name + 1 : exe, GAME_EXE)) return TRUE;
        if (has_switch(cmdline, L"-nowindowfix")) fit_workarea = FALSE;
        log_line("uefix loaded (work-area window: %s); command line: %ls", fit_workarea ? "on" : "off (-nowindowfix)",
                 cmdline);
        install();
    }
    return TRUE;
}

/* VERSION.dll exports that kernelbase lacks (nobody in the game calls them) */
DWORD WINAPI VerInstallFileA(DWORD flags, LPSTR src_name, LPSTR dest_name, LPSTR src_dir, LPSTR dest_dir,
                             LPSTR cur_dir, LPSTR tmp_name, PUINT tmp_len)
{
    return VIF_CANNOTREADSRC;
}

DWORD WINAPI VerInstallFileW(DWORD flags, LPWSTR src_name, LPWSTR dest_name, LPWSTR src_dir, LPWSTR dest_dir,
                             LPWSTR cur_dir, LPWSTR tmp_name, PUINT tmp_len)
{
    return VIF_CANNOTREADSRC;
}
