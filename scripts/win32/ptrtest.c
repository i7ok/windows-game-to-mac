#include <windows.h>
#include <stdio.h>
int main(void)
{
    BOOL (WINAPI *gpt)(UINT32, DWORD *) = (void *)GetProcAddress(GetModuleHandleA("user32.dll"), "GetPointerType");
    BOOL (WINAPI *gpi)(UINT32, void *) = (void *)GetProcAddress(GetModuleHandleA("user32.dll"), "GetPointerInfo");
    DWORD t = 0xdead; char buf[256];
    BOOL r = gpt ? gpt(1, &t) : -1;
    printf("GetPointerType present=%d ret=%d type=%lu err=%lu\n", gpt != NULL, r, t, GetLastError());
    printf("GetPointerInfo present=%d ret=%d err=%lu\n", gpi != NULL, gpi ? gpi(1, buf) : -1, GetLastError());
    printf("EnableMouseInPointer ret=%d err=%lu\n", EnableMouseInPointer(TRUE), GetLastError());
    return 0;
}
