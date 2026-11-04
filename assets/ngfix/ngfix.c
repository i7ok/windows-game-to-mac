/* ngfix: input and window fixes for Unity 6 games under Wine on macOS.
 *
 * Loaded into the game as an app-local version.dll (UnityPlayer.dll imports VERSION.dll); the
 * real version functions are forwarded to kernelbase by version.def.
 *
 * 1. Clicks. Unity 6 calls EnableMouseInPointer(TRUE) and its Input System then takes mouse
 *    buttons from WM_POINTER* messages only. Wine 9 (CrossOver 24) has no mouse-in-pointer mode,
 *    so the game gets legacy WM_LBUTTONDOWN etc. and ignores them. We report the mode as on and
 *    rewrite the legacy client-area mouse messages into WM_POINTERDOWN/UP/UPDATE (and the wheel,
 *    i.e. two-finger trackpad scrolling, into WM_POINTERWHEEL/HWHEEL), the way Windows sends
 *    them. Wine's GetPointerType() already answers PT_MOUSE.
 * 2. Command+Q. This (CrossOver) Mac driver maps Command to Alt, so the game just gets Alt+Q;
 *    we close the window instead. Q is matched by scan code, so any keyboard layout works. Option
 *    is not Alt unless OptionIsAlt is set. Quit from the Dock reaches the game as
 *    WM_QUERYENDSESSION, which we also turn into a normal close.
 * 3. Window (optional, FIT_WORKAREA_DEFAULT / NGFIX_FIT_WORKAREA). A window covering the whole screen
 *    is "fullscreen" for winemac: lifted above the menu bar while active, yet the menu bar still
 *    slides in at the top edge. This gives the game a borderless window over the work area instead
 *    (below the menu bar, above the Dock), like a normal Mac window. Unity sizes its back buffer from
 *    the monitor (FullScreenWindow) or from the resolution it asked for (windowed), so merely
 *    shrinking its window gives a scaled picture with clicks landing above their targets (button
 *    hitboxes below the buttons). So UnityPlayer is shown a
 *    monitor the size of the work area (GetMonitorInfo's rcWork, which winemac fills from NSScreen
 *    visibleFrame): rcMonitor = rcWork, EnumDisplayMonitors' rects, display modes of the monitor's
 *    size and SM_CX/CYSCREEN all become the work area. With -screen-fullscreen 1 -window-mode
 *    borderless (launcher) Unity's own fullscreen window then covers exactly the work area.
 *    Safety net: Unity's top-level window is created without frame styles, WM_STYLECHANGING can't
 *    add them back, and every move/resize is clamped to the work area. Unity creates the window with
 *    a caption and makes it WS_POPUP later, and Wine skips WM_NCCALCSIZE when a clamped move keeps the
 *    old size; that once left the caption's frame (3 px sides, 29 px top: a gray band) inside a
 *    WS_POPUP window, so every clamp sets SWP_FRAMECHANGED.
 *    NGFIX_FIT_WORKAREA=1 or =0 in the environment overrides the build's FIT_WORKAREA_DEFAULT.
 *
 * Only active in GAME_EXE (UnityCrashHandler64.exe sits in the same folder).
 * Wine loads its builtin version.dll even when one sits next to the exe, so the launcher sets
 * HKCU\Software\Wine\AppDefaults\<GAME_EXE>\DllOverrides version=native,builtin.
 * Log: %TEMP%\ngfix.log (C:\users\crossover\Temp in a WineCX prefix).
 *
 * Template from the Northgard Battlegrounds port (~/Macdows/Northgard-Mac/payload/ngfix); the window
 * part comes from the Dimraeth port (~/Macdows/Dimraeth-Mac/payload/ngfix, where it is always on).
 * For another game: set GAME_EXE (or -DGAME_EXE=L\"...\"), check that its
 * UnityPlayer.dll still imports VERSION.dll, and for the window the display functions patched in
 * install() (scripts/pe/imports.py). build.sh links gdi32 for GetDeviceCaps.
 */
#include <windows.h>
#include <stdio.h>

#ifndef GAME_EXE
#define GAME_EXE L"Northgard Battlegrounds.exe"
#endif
#ifndef FIT_WORKAREA_DEFAULT
#define FIT_WORKAREA_DEFAULT 0   /* 1: borderless window over the work area (see 3.) */
#endif

#define MOUSE_POINTER_ID 1
#define MK_ANY_BUTTON (MK_LBUTTON | MK_RBUTTON | MK_MBUTTON | MK_XBUTTON1 | MK_XBUTTON2)
#define SCAN_Q 0x10
#define FRAME_STYLES (WS_CAPTION | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX)

static BOOL pointer_mode;
static DWORD hooked_threads[8];
static int hooked_count;
static LONG converted, wheel_converted, cursor_calls_logged;
static BOOL fit_workarea = FIT_WORKAREA_DEFAULT;
static HWND fit_hwnd;
static WNDPROC fit_prev_proc;
static LONG fits_logged;

static void log_line(const char *fmt, ...)
{
    WCHAR path[MAX_PATH];
    char buf[512];
    va_list args;
    FILE *f;
    SYSTEMTIME st;

    GetTempPathW(MAX_PATH - 16, path);
    lstrcatW(path, L"ngfix.log");
    if (!(f = _wfopen(path, L"a"))) return;
    GetLocalTime(&st);
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    fprintf(f, "%02d:%02d:%02d.%03d [%04lx] %s\n", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
            GetCurrentThreadId(), buf);
    fclose(f);
}

static UINT pointer_flags(WPARAM mk)
{
    UINT flags = POINTER_MESSAGE_FLAG_INRANGE | POINTER_MESSAGE_FLAG_PRIMARY | POINTER_MESSAGE_FLAG_CONFIDENCE;
    if (mk & MK_LBUTTON) flags |= POINTER_MESSAGE_FLAG_FIRSTBUTTON;
    if (mk & MK_RBUTTON) flags |= POINTER_MESSAGE_FLAG_SECONDBUTTON;
    if (mk & MK_MBUTTON) flags |= POINTER_MESSAGE_FLAG_THIRDBUTTON;
    if (mk & MK_XBUTTON1) flags |= POINTER_MESSAGE_FLAG_FOURTHBUTTON;
    if (mk & MK_XBUTTON2) flags |= POINTER_MESSAGE_FLAG_FIFTHBUTTON;
    if (mk & MK_ANY_BUTTON) flags |= POINTER_MESSAGE_FLAG_INCONTACT;
    return flags;
}

/* WM_*BUTTON* bit of the button a legacy message is about */
static WPARAM message_button(UINT msg, WPARAM wp)
{
    switch (msg)
    {
    case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_LBUTTONDBLCLK: return MK_LBUTTON;
    case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_RBUTTONDBLCLK: return MK_RBUTTON;
    case WM_MBUTTONDOWN: case WM_MBUTTONUP: case WM_MBUTTONDBLCLK: return MK_MBUTTON;
    case WM_XBUTTONDOWN: case WM_XBUTTONUP: case WM_XBUTTONDBLCLK:
        return HIWORD(wp) == XBUTTON1 ? MK_XBUTTON1 : MK_XBUTTON2;
    }
    return 0;
}

static void to_pointer_message(MSG *msg)
{
    WPARAM mk = msg->wParam & MK_ANY_BUTTON, button;
    POINT pt = { (short)LOWORD(msg->lParam), (short)HIWORD(msg->lParam) };
    UINT new_msg;

    switch (msg->message)
    {
    case WM_MOUSEMOVE:
        new_msg = WM_POINTERUPDATE;
        break;
    case WM_LBUTTONDOWN: case WM_RBUTTONDOWN: case WM_MBUTTONDOWN: case WM_XBUTTONDOWN:
    case WM_LBUTTONDBLCLK: case WM_RBUTTONDBLCLK: case WM_MBUTTONDBLCLK: case WM_XBUTTONDBLCLK:
        /* a press is POINTERDOWN only when no other button was already held */
        button = message_button(msg->message, msg->wParam);
        new_msg = (mk & ~button) ? WM_POINTERUPDATE : WM_POINTERDOWN;
        break;
    case WM_LBUTTONUP: case WM_RBUTTONUP: case WM_MBUTTONUP: case WM_XBUTTONUP:
        new_msg = mk ? WM_POINTERUPDATE : WM_POINTERUP;
        break;
    default:
        return;
    }
    ClientToScreen(msg->hwnd, &pt);
    msg->message = new_msg;
    msg->wParam = MAKEWPARAM(MOUSE_POINTER_ID, pointer_flags(mk));
    msg->lParam = MAKELPARAM(pt.x, pt.y);
    if (InterlockedIncrement(&converted) == 1) log_line("first mouse message converted to WM_POINTER");
}

/* WM_MOUSEWHEEL/HWHEEL carry screen coordinates already; only the wParam layout changes */
static void to_pointer_wheel(MSG *msg)
{
    msg->message = msg->message == WM_MOUSEWHEEL ? WM_POINTERWHEEL : WM_POINTERHWHEEL;
    msg->wParam = MAKEWPARAM(MOUSE_POINTER_ID, HIWORD(msg->wParam));
    if (InterlockedIncrement(&wheel_converted) == 1) log_line("first wheel message converted to WM_POINTERWHEEL");
}

/* Command arrives as Alt (WM_SYSKEYDOWN with the context bit); accept the Windows key too */
static BOOL command_down(const MSG *msg)
{
    return (msg->lParam & (1 << 29)) || GetAsyncKeyState(VK_MENU) < 0 ||
           (GetAsyncKeyState(VK_LWIN) | GetAsyncKeyState(VK_RWIN)) < 0;
}

static void close_game_window(HWND hwnd, const char *why)
{
    HWND top = GetAncestor(hwnd, GA_ROOT);
    log_line("%s: closing window %p", why, (void *)(top ? top : hwnd));
    PostMessageW(top ? top : hwnd, WM_CLOSE, 0, 0);
}

static LRESULT CALLBACK get_msg_proc(int code, WPARAM wp, LPARAM lp)
{
    MSG *msg = (MSG *)lp;

    if (code == HC_ACTION && msg)
    {
        if (pointer_mode && (msg->message == WM_MOUSEWHEEL || msg->message == WM_MOUSEHWHEEL))
            to_pointer_wheel(msg);
        else if (pointer_mode && msg->message >= WM_MOUSEMOVE && msg->message <= WM_XBUTTONDBLCLK)
            to_pointer_message(msg);
        else if ((msg->message == WM_KEYDOWN || msg->message == WM_SYSKEYDOWN) && wp == PM_REMOVE)
        {
            UINT scan = (msg->lParam >> 16) & 0xff;
            if ((scan == SCAN_Q || msg->wParam == 'Q') && command_down(msg))
            {
                close_game_window(msg->hwnd, "Command+Q");
                msg->message = WM_NULL;
            }
        }
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

/* message hook on every thread that creates a Unity window or turns pointer mode on */
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

/* diagnostics: how Unity shows and hides the system cursor (first calls only) */
static int (WINAPI *real_ShowCursor)(BOOL);
static HCURSOR (WINAPI *real_SetCursor)(HCURSOR);

static int WINAPI fake_ShowCursor(BOOL show)
{
    int count = real_ShowCursor(show);
    if (InterlockedIncrement(&cursor_calls_logged) <= 40) log_line("ShowCursor(%d) -> %d", show, count);
    return count;
}

static HCURSOR WINAPI fake_SetCursor(HCURSOR cursor)
{
    static HCURSOR last = (HCURSOR)-1;
    if (cursor != last && InterlockedIncrement(&cursor_calls_logged) <= 40)
        log_line("SetCursor(%p)", (void *)cursor);
    last = cursor;
    return real_SetCursor(cursor);
}

static BOOL WINAPI fake_EnableMouseInPointer(BOOL enable)
{
    log_line("EnableMouseInPointer(%d)", enable);
    pointer_mode = enable;
    hook_thread(GetCurrentThreadId());
    return TRUE;
}

static BOOL WINAPI fake_IsMouseInPointerEnabled(void)
{
    return pointer_mode;
}

static HWND (WINAPI *real_CreateWindowExW)(DWORD, LPCWSTR, LPCWSTR, DWORD, int, int, int, int, HWND, HMENU,
                                           HINSTANCE, LPVOID);

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

/* Unity's window procedure first, then our geometry wins: every move/resize lands on the work area */
static LRESULT CALLBACK fit_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    LRESULT ret = CallWindowProcW(fit_prev_proc, hwnd, msg, wp, lp);

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
        }
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

static HWND WINAPI fake_CreateWindowExW(DWORD ex, LPCWSTR cls, LPCWSTR name, DWORD style, int x, int y, int w,
                                        int h, HWND parent, HMENU menu, HINSTANCE inst, LPVOID param)
{
    BOOL unity = !IS_INTRESOURCE(cls) && !lstrcmpiW(cls, L"UnityWndClass");
    BOOL fit = unity && fit_workarea && !fit_hwnd && !(style & WS_CHILD);
    DWORD orig_style = style;
    HWND hwnd;

    if (fit) style = (style & ~FRAME_STYLES) | WS_POPUP;   /* born borderless: no title bar, no frame */
    hwnd = real_CreateWindowExW(ex, cls, name, style, x, y, w, h, parent, menu, inst, param);
    if (hwnd && unity)
    {
        hook_thread(GetCurrentThreadId());
        if (fit)
        {
            RECT wa = { 0 };
            fit_hwnd = hwnd;
            fit_prev_proc = (WNDPROC)SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)fit_wndproc);
            get_work_area(hwnd, &wa);
            log_line("window %p (style %08lx -> %08lx) kept over the work area %ld,%ld-%ld,%ld", (void *)hwnd,
                     orig_style, style, wa.left, wa.top, wa.right, wa.bottom);
            fit_now(hwnd);
        }
    }
    return hwnd;
}

/* --- the monitor as UnityPlayer sees it: the size of the work area -------------------------------
 * ngfix's own calls below go through ngfix's imports, so they reach the real functions. */
static LONG display_logged;

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

static BOOL WINAPI fake_GetMonitorInfoA(HMONITOR mon, LPMONITORINFO mi)
{
    BOOL ret = GetMonitorInfoA(mon, mi);
    if (ret && mi) mi->rcMonitor = mi->rcWork;
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

static BOOL WINAPI fake_EnumDisplaySettingsA(LPCSTR dev, DWORD mode, DEVMODEA *dm)
{
    BOOL ret = EnumDisplaySettingsA(dev, mode, dm);
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

static int WINAPI fake_GetDeviceCaps(HDC hdc, int index)
{
    int ret = GetDeviceCaps(hdc, index);
    MONITORINFO mi;
    if ((index == HORZRES || index == VERTRES || index == DESKTOPHORZRES || index == DESKTOPVERTRES) &&
        primary_monitor(&mi))
    {
        BOOL vertical = index == VERTRES || index == DESKTOPVERTRES;
        int full = vertical ? mi.rcMonitor.bottom - mi.rcMonitor.top : mi.rcMonitor.right - mi.rcMonitor.left;
        int work = vertical ? mi.rcWork.bottom - mi.rcWork.top : mi.rcWork.right - mi.rcWork.left;
        if (ret == full)
        {
            if (InterlockedIncrement(&display_logged) <= 30) log_line("GetDeviceCaps(%d): %d reported as %d", index, ret, work);
            ret = work;
        }
    }
    return ret;
}

/* point UnityPlayer.dll's import of dll!name at func; returns the previous target */
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
    HMODULE unity = GetModuleHandleW(L"UnityPlayer.dll");

    if (!unity)
    {
        log_line("UnityPlayer.dll not loaded, nothing patched");
        return;
    }
    log_line("patch EnableMouseInPointer: %s",
             patch_import(unity, "USER32.dll", "EnableMouseInPointer", fake_EnableMouseInPointer) ? "ok" : "missing");
    log_line("patch IsMouseInPointerEnabled: %s",
             patch_import(unity, "USER32.dll", "IsMouseInPointerEnabled", fake_IsMouseInPointerEnabled) ? "ok" : "missing");
    real_CreateWindowExW = patch_import(unity, "USER32.dll", "CreateWindowExW", fake_CreateWindowExW);
    log_line("patch CreateWindowExW: %s", real_CreateWindowExW ? "ok" : "missing");
    if (!real_CreateWindowExW) real_CreateWindowExW = CreateWindowExW;
    if (!(real_ShowCursor = patch_import(unity, "USER32.dll", "ShowCursor", fake_ShowCursor)))
        real_ShowCursor = ShowCursor;
    if (!(real_SetCursor = patch_import(unity, "USER32.dll", "SetCursor", fake_SetCursor)))
        real_SetCursor = SetCursor;
    if (fit_workarea)
    {
        static const struct { const char *dll, *name; void *func; } display[] =
        {
            { "USER32.dll", "GetMonitorInfoW", fake_GetMonitorInfoW },
            { "USER32.dll", "GetMonitorInfoA", fake_GetMonitorInfoA },
            { "USER32.dll", "EnumDisplayMonitors", fake_EnumDisplayMonitors },
            { "USER32.dll", "EnumDisplaySettingsW", fake_EnumDisplaySettingsW },
            { "USER32.dll", "EnumDisplaySettingsA", fake_EnumDisplaySettingsA },
            { "USER32.dll", "GetSystemMetrics", fake_GetSystemMetrics },
            { "GDI32.dll", "GetDeviceCaps", fake_GetDeviceCaps },
        };
        int i;
        for (i = 0; i < ARRAYSIZE(display); i++)
            log_line("patch %s: %s", display[i].name,
                     patch_import(unity, display[i].dll, display[i].name, display[i].func) ? "ok" : "missing");
    }
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, void *reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        WCHAR exe[MAX_PATH], *name, env[8];
        DisableThreadLibraryCalls(inst);
        GetModuleFileNameW(NULL, exe, MAX_PATH);
        name = wcsrchr(exe, '\\');
        if (lstrcmpiW(name ? name + 1 : exe, GAME_EXE)) return TRUE;
        if (GetEnvironmentVariableW(L"NGFIX_FIT_WORKAREA", env, ARRAYSIZE(env)))
            fit_workarea = env[0] != '0';
        log_line("ngfix loaded (work-area window: %s)", fit_workarea ? "on" : "off");
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
