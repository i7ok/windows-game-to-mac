/* winfo: foreground/focus/capture state and geometry of a window (HWND hex) */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
int main(int argc, char **argv)
{
    HWND h = argc > 1 ? (HWND)(ULONG_PTR)strtoull(argv[1], NULL, 16) : GetForegroundWindow();
    RECT wr, cr; POINT p = {0, 0}, cur;
    GUITHREADINFO gti = { sizeof(gti) };
    GetWindowRect(h, &wr); GetClientRect(h, &cr); ClientToScreen(h, &p); GetCursorPos(&cur);
    GetGUIThreadInfo(GetWindowThreadProcessId(h, NULL), &gti);
    printf("hwnd=%p fg=%p visible=%d enabled=%d iconic=%d\n", (void *)h, (void *)GetForegroundWindow(),
           IsWindowVisible(h), IsWindowEnabled(h), IsIconic(h));
    printf("style=%08lx exstyle=%08lx\n", GetWindowLongW(h, GWL_STYLE), GetWindowLongW(h, GWL_EXSTYLE));
    printf("window=%ld,%ld-%ld,%ld client=%ldx%ld at %ld,%ld cursor=%ld,%ld\n", wr.left, wr.top, wr.right, wr.bottom,
           cr.right, cr.bottom, p.x, p.y, cur.x, cur.y);
    printf("thread: active=%p focus=%p capture=%p flags=%lx\n", (void *)gti.hwndActive, (void *)gti.hwndFocus,
           (void *)gti.hwndCapture, gti.flags);
    printf("screen=%dx%d virtual=%dx%d\n", GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN),
           GetSystemMetrics(SM_CXVIRTUALSCREEN), GetSystemMetrics(SM_CYVIRTUALSCREEN));
    return 0;
}
