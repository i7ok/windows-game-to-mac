/* wtool: inspect and poke Win32 windows inside a Wine session.
 *   wtool list            visible top-level windows with their child controls
 *   wtool click HWND      BM_CLICK a button (radio buttons included)
 *   wtool shot HWND FILE  save a window's GDI contents as a 24-bit BMP
 * HWND is hex as printed by "list". Output goes to stdout as UTF-8.
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

static void put_utf8(const WCHAR *w)
{
    char buf[2048];
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, buf, sizeof(buf), NULL, NULL);
    if (n > 0) fputs(buf, stdout);
}

static BOOL CALLBACK child_proc(HWND h, LPARAM depth)
{
    WCHAR cls[256], text[512];
    RECT r;
    LONG style = GetWindowLongW(h, GWL_STYLE);
    GetClassNameW(h, cls, 256);
    text[0] = 0;
    SendMessageTimeoutW(h, WM_GETTEXT, 512, (LPARAM)text, SMTO_ABORTIFHUNG, 500, NULL);
    GetWindowRect(h, &r);
    printf("    child %p vis=%d style=%08lx ", (void *)h, IsWindowVisible(h) ? 1 : 0, style);
    put_utf8(cls);
    printf(" [");
    put_utf8(text);
    printf("]");
    if (!lstrcmpiW(cls, L"Button"))
    {
        DWORD_PTR st = 0;
        SendMessageTimeoutW(h, BM_GETCHECK, 0, 0, SMTO_ABORTIFHUNG, 500, &st);
        printf(" checked=%d", (int)st);
    }
    printf(" rect=%ld,%ld,%ld,%ld\n", r.left, r.top, r.right, r.bottom);
    return TRUE;
}

static BOOL CALLBACK top_proc(HWND h, LPARAM all)
{
    WCHAR cls[256], text[512];
    RECT r;
    DWORD pid = 0;
    if (!all && !IsWindowVisible(h)) return TRUE;
    GetClassNameW(h, cls, 256);
    text[0] = 0;
    GetWindowTextW(h, text, 512);
    GetWindowRect(h, &r);
    GetWindowThreadProcessId(h, &pid);
    printf("top %p pid=%04lx vis=%d ", (void *)h, pid, IsWindowVisible(h) ? 1 : 0);
    put_utf8(cls);
    printf(" [");
    put_utf8(text);
    printf("] rect=%ld,%ld,%ld,%ld\n", r.left, r.top, r.right, r.bottom);
    EnumChildWindows(h, child_proc, 1);
    return TRUE;
}

static int shot(HWND h, const char *file, int mode)
{
    RECT r;
    int w, ht;
    HDC wdc, mdc;
    HBITMAP bmp;
    BITMAPINFOHEADER bi = {0};
    BITMAPFILEHEADER bf = {0};
    BYTE *bits;
    FILE *f;
    DWORD stride, size;

    GetWindowRect(h, &r);
    w = r.right - r.left;
    ht = r.bottom - r.top;
    if (w <= 0 || ht <= 0) return 1;
    wdc = GetWindowDC(h);
    mdc = CreateCompatibleDC(wdc);
    bmp = CreateCompatibleBitmap(wdc, w, ht);
    SelectObject(mdc, bmp);
    if (mode == 1) BitBlt(mdc, 0, 0, w, ht, wdc, 0, 0, SRCCOPY);
    else if (mode == 2)
    {
        HDC sdc = GetDC(NULL);
        BitBlt(mdc, 0, 0, w, ht, sdc, r.left, r.top, SRCCOPY);
        ReleaseDC(NULL, sdc);
    }
    else if (!PrintWindow(h, mdc, 0)) BitBlt(mdc, 0, 0, w, ht, wdc, 0, 0, SRCCOPY);
    stride = ((w * 3 + 3) & ~3);
    size = stride * ht;
    bits = malloc(size);
    bi.biSize = sizeof(bi);
    bi.biWidth = w;
    bi.biHeight = ht;
    bi.biPlanes = 1;
    bi.biBitCount = 24;
    bi.biCompression = BI_RGB;
    GetDIBits(mdc, bmp, 0, ht, bits, (BITMAPINFO *)&bi, DIB_RGB_COLORS);
    bf.bfType = 0x4d42;
    bf.bfOffBits = sizeof(bf) + sizeof(bi);
    bf.bfSize = bf.bfOffBits + size;
    f = fopen(file, "wb");
    if (!f) return 2;
    fwrite(&bf, sizeof(bf), 1, f);
    fwrite(&bi, sizeof(bi), 1, f);
    fwrite(bits, size, 1, f);
    fclose(f);
    printf("saved %dx%d\n", w, ht);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc >= 2 && !strcmp(argv[1], "list"))
    {
        EnumWindows(top_proc, argc >= 3 && !strcmp(argv[2], "all"));
        return 0;
    }
    if (argc >= 3 && !strcmp(argv[1], "click"))
    {
        HWND h = (HWND)(ULONG_PTR)strtoull(argv[2], NULL, 16);
        DWORD_PTR res;
        if (!IsWindow(h)) { printf("no such window\n"); return 1; }
        SendMessageTimeoutW(h, BM_CLICK, 0, 0, SMTO_ABORTIFHUNG, 2000, &res);
        printf("clicked\n");
        return 0;
    }
    if (argc >= 4 && !strcmp(argv[1], "shot"))
        return shot((HWND)(ULONG_PTR)strtoull(argv[2], NULL, 16), argv[3], argc >= 5 ? atoi(argv[4]) : 0);
    fprintf(stderr, "usage: wtool list [all] | click HWND | shot HWND FILE [0=print|1=windc|2=screen]\n");
    return 1;
}
