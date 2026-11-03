/* wclose HWND: ask a window to close (posts WM_CLOSE) */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
int main(int argc, char **argv)
{
    HWND h = (HWND)(ULONG_PTR)strtoull(argv[1], NULL, 16);
    if (!IsWindow(h)) { printf("no such window\n"); return 1; }
    printf("WM_CLOSE posted: %d\n", PostMessageW(h, WM_CLOSE, 0, 0));
    return 0;
}
