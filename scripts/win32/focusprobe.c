/* focusprobe [SECONDS] [WINDOW_CLASS]: every 100 ms log (on change) Wine's foreground window, the game
 * window's active/focus/capture state and whether Wine sees the left mouse button down.
 * WINDOW_CLASS defaults to UnityWndClass (Unity players); `wtool list` shows other games' classes.
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    int n = (argc > 1 ? atoi(argv[1]) : 60) * 10, i;
    char last[512] = "", cur[512];
    WCHAR cls[256] = L"UnityWndClass";
    if (argc > 2) MultiByteToWideChar(CP_UTF8, 0, argv[2], -1, cls, 256);
    setvbuf(stdout, NULL, _IONBF, 0);
    for (i = 0; i < n; i++)
    {
        HWND game = FindWindowW(cls, NULL), fg = GetForegroundWindow();
        GUITHREADINFO gti = { sizeof(gti) };
        POINT pt;
        SYSTEMTIME st;
        if (game) GetGUIThreadInfo(GetWindowThreadProcessId(game, NULL), &gti);
        GetCursorPos(&pt);
        snprintf(cur, sizeof(cur), "game=%p fg=%p%s active=%p focus=%p capture=%p lbutton=%d",
                 (void *)game, (void *)fg, game && fg == game ? "(GAME)" : "", (void *)gti.hwndActive,
                 (void *)gti.hwndFocus, (void *)gti.hwndCapture, (GetAsyncKeyState(VK_LBUTTON) & 0x8000) ? 1 : 0);
        if (strcmp(cur, last))
        {
            GetLocalTime(&st);
            printf("%02d:%02d:%02d.%03d %s cursor=%ld,%ld\n", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
                   cur, pt.x, pt.y);
            strcpy(last, cur);
        }
        Sleep(100);
    }
    return 0;
}
