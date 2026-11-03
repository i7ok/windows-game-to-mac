/* rawtest: does Wine deliver raw mouse button input (WM_INPUT) to the foreground window?
 * Registers raw mouse+keyboard like a Unity player, makes its window foreground, then injects a
 * left click with SendInput (no cursor movement) and counts what arrives.
 */
#include <windows.h>
#include <stdio.h>

static int raw_down, raw_up, raw_other, legacy_down, activates, focus_in;
static HWND wnd;

static DWORD WINAPI clicker(void *arg)
{
    INPUT in[2] = {0};
    Sleep(700);
    printf("before click: fg=%p mine=%p\n", (void *)GetForegroundWindow(), (void *)wnd);
    in[0].type = INPUT_MOUSE; in[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    in[1].type = INPUT_MOUSE; in[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
    printf("SendInput sent %u\n", SendInput(2, in, sizeof(INPUT)));
    Sleep(800);
    PostMessageW(wnd, WM_CLOSE, 0, 0);
    return 0;
}

static LRESULT CALLBACK proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_INPUT:
    {
        RAWINPUT ri;
        UINT size = sizeof(ri);
        if (GetRawInputData((HRAWINPUT)lp, RID_INPUT, &ri, &size, sizeof(RAWINPUTHEADER)) != (UINT)-1 &&
            ri.header.dwType == RIM_TYPEMOUSE)
        {
            if (ri.data.mouse.usButtonFlags & RI_MOUSE_LEFT_BUTTON_DOWN) raw_down++;
            else if (ri.data.mouse.usButtonFlags & RI_MOUSE_LEFT_BUTTON_UP) raw_up++;
            else raw_other++;
        }
        break;
    }
    case WM_LBUTTONDOWN: legacy_down++; break;
    case WM_ACTIVATE: if (LOWORD(wp)) activates++; break;
    case WM_SETFOCUS: focus_in++; break;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

int main(void)
{
    WNDCLASSW wc = {0};
    RAWINPUTDEVICE rid[2] = {{1, 2, 0, NULL}, {1, 6, 0, NULL}};
    MSG m;
    wc.lpfnWndProc = proc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"rawtest";
    RegisterClassW(&wc);
    wnd = CreateWindowW(L"rawtest", L"raw input test", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 40, 60, 240, 120,
                        NULL, NULL, wc.hInstance, NULL);
    rid[0].hwndTarget = rid[1].hwndTarget = wnd;
    printf("RegisterRawInputDevices=%d\n", RegisterRawInputDevices(rid, 2, sizeof(rid[0])));
    printf("SetForegroundWindow=%d\n", SetForegroundWindow(wnd));
    CreateThread(NULL, 0, clicker, NULL, 0, NULL);
    while (GetMessageW(&m, NULL, 0, 0) > 0) { TranslateMessage(&m); DispatchMessageW(&m); }
    printf("activates=%d focus=%d raw_down=%d raw_up=%d raw_other=%d legacy_down=%d\n",
           activates, focus_in, raw_down, raw_up, raw_other, legacy_down);
    return 0;
}
