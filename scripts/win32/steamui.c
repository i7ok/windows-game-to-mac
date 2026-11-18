/* steamui suspend|resume|info : freezes or thaws the running Steam client's UI thread, to see what a
   hung Steam does (e.g. to test a launcher's hung-Steam check on a real, logged-in Steam).
   The client is SteamPID (HKLM\Software\Valve\Steam); its UI thread is the owner of its
   vguiPopupWindow windows, else the process's first thread. A suspended UI thread takes no forwarded
   command (TempAppCmdLine stays set); "resume" lets it run the pending one. "info" prints the thread
   and how many of its windows Wine's IsHungAppWindow calls hung (a suspended thread may show 0). */
#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <string.h>

static DWORD g_pid, g_tid;
static int g_wins, g_hung;

static BOOL CALLBACK find_ui(HWND h, LPARAM lp) {
    DWORD pid = 0, tid = GetWindowThreadProcessId(h, &pid);
    char cls[128] = "";
    (void)lp;
    if (pid != g_pid) return TRUE;
    GetClassNameA(h, cls, sizeof cls);
    if (!strcmp(cls, "vguiPopupWindow") && !g_tid) g_tid = tid;
    return TRUE;
}

static BOOL CALLBACK count_hung(HWND h, LPARAM lp) {
    DWORD pid = 0, tid = GetWindowThreadProcessId(h, &pid);
    (void)lp;
    if (pid == g_pid && tid == g_tid) { g_wins++; if (IsHungAppWindow(h)) g_hung++; }
    return TRUE;
}

int main(int argc, char **argv) {
    DWORD sz = sizeof g_pid, c;
    const char *cmd = argc > 1 ? argv[1] : "info", *how = "vguiPopupWindow owner";
    HANDLE t;
    if (RegGetValueA(HKEY_LOCAL_MACHINE, "Software\\Valve\\Steam", "SteamPID", RRF_RT_REG_DWORD,
                     NULL, &g_pid, &sz) || !g_pid) { printf("no SteamPID: Steam is not running\n"); return 2; }
    EnumWindows(find_ui, 0);
    if (!g_tid) {
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        THREADENTRY32 te;
        te.dwSize = sizeof te;
        for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te))
            if (te.th32OwnerProcessID == g_pid) { g_tid = te.th32ThreadID; break; }
        CloseHandle(snap);
        how = "first thread";
    }
    if (!g_tid) { printf("pid %04lx: no thread found\n", g_pid); return 2; }
    if (!strcmp(cmd, "info")) {
        EnumWindows(count_hung, 0);
        printf("steam pid %04lx, UI thread %04lx (%s): %d windows, %d hung\n", g_pid, g_tid, how, g_wins, g_hung);
        return 0;
    }
    if (strcmp(cmd, "suspend") && strcmp(cmd, "resume")) { printf("usage: steamui suspend|resume|info\n"); return 2; }
    if (!(t = OpenThread(THREAD_SUSPEND_RESUME, FALSE, g_tid))) {
        printf("OpenThread %04lx failed: %lu\n", g_tid, GetLastError());
        return 1;
    }
    c = !strcmp(cmd, "suspend") ? SuspendThread(t) : ResumeThread(t);
    printf("%s thread %04lx of pid %04lx (%s): previous suspend count %ld\n", cmd, g_tid, g_pid, how, (long)c);
    CloseHandle(t);
    return c == (DWORD)-1;
}
