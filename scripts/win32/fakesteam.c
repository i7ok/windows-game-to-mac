/* fakesteam : a stand-in for a hung Steam client, to test a launcher's hung-Steam check (and its
   forced close) without touching a logged-in Steam. Run it in the prefix while Steam is closed. It
   creates Steam's command event (Global\Valve_SteamIPC_Class, manual reset) and writes SteamPID like
   the client, then never takes a command: a real "steam.exe -shutdown" hands its command line over
   (TempAppCmdLine) and exits, and nothing happens. Its name contains "steam.exe", so
   pgrep -if 'steam\.exe' counts it. Afterwards: wineserver -k, and set SteamPID back to 0. */
#include <windows.h>
#include <stdio.h>

int main(void) {
    HANDLE e = CreateEventA(NULL, TRUE, FALSE, "Global\\Valve_SteamIPC_Class");
    DWORD pid = GetCurrentProcessId();
    HKEY k;
    if (!RegCreateKeyExA(HKEY_LOCAL_MACHINE, "Software\\Valve\\Steam", 0, NULL, 0, KEY_SET_VALUE, NULL, &k, NULL)) {
        RegSetValueExA(k, "SteamPID", 0, REG_DWORD, (const BYTE *)&pid, sizeof pid);
        RegCloseKey(k);
    }
    printf("fake hung Steam client: pid %04lx, event %p\n", pid, e);
    fflush(stdout);
    Sleep(INFINITE);
    return 0;
}
