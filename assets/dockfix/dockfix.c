/* dockfix: Dock names and icons for Wine-wrapped Windows games that share one Windows Steam.
 *
 * Windows Steam.app's launcher injects it into every Wine process of the shared prefix
 * (DYLD_INSERT_LIBRARIES; WineCX passes it on to child processes, so the games Steam starts get it
 * too). Without it, Wine's Mac driver (winemac) turns every process that shows a window into a
 * regular app with a Dock icon named after the Windows exe (WineCX runs each exe from a temp copy
 * of its loader named like it: "Dimraeth.exe", "steamwebhelper.exe"), with the exe's square icon,
 * and shows Windows tray icons (Steam's) in the menu bar.
 *
 * Roles, decided per process from the exe name:
 *   game (an exe listed in game= lines): CFProcessPath = that game app's launcher, set before
 *     CoreFoundation is loaded, so the process is the game's .app: its name and icon in the Dock and
 *     the menu bar. -[NSApplication setApplicationIconImage:] ignores the exe's icon.
 *   Steam (exe name steam*): CFProcessPath = Windows Steam.app's launcher (steam=). That bundle is
 *     LSUIElement: no Dock icon until winemac shows one of Steam's windows; then "Windows Steam"
 *     stays in the Dock until Steam exits. The exe's icon is ignored. Clicking the app's icon while
 *     Steam runs (Launchpad, Finder, Dock) runs "<launcher> --show", and Steam brings up its window.
 *     Quit from the Dock or the app menu, and Cmd+Q in a Steam window, run "<launcher> --stop":
 *     Steam's own -shutdown keeps the login. Logout, restart and shutdown keep Wine's default
 *     (WM_QUERYENDSESSION, which Steam handles like a Windows logoff).
 *   anything else: an inherited CFProcessPath is removed. explorer.exe (Wine's desktop, host of the
 *     tray) never gets a Dock icon. Unregistered exes (a game started from Steam's library before
 *     its app was made) keep Wine's default: a Dock icon named after the exe.
 *   Tray icons (-[NSStatusBar statusItemWithLength:]) are taken off the menu bar in every non-game
 *   process: Steam is reached through its app icon instead.
 *
 * How the code gets to run: in Wine's Windows processes (started through wine-preloader) dyld maps
 * this library but never calls its initializers, so there is no constructor. Instead it interposes
 * dlopen (dyld applies __interpose tuples while binding, initializers or not). Wine's loader dlopens
 * ntdll.so first, which brings in CoreFoundation: before that we decide the role and set or remove
 * CFProcessPath. Later dlopen("winemac.so") brings in AppKit and winemac's own classes: right after
 * a dlopen returns, outside any runtime lock, the methods are replaced, before winemac starts its
 * Cocoa app. objc_* calls from objc_addLoadImageFunc callbacks would trap (runtime lock held).
 *
 * Config: the file named by $DOCKFIX_CONF, else dockfix.conf next to this library. Lines:
 *   game=<exe>|<launcher>   one per game app (exe name, path of the app's CFBundleExecutable)
 *   steam=<launcher>        Windows Steam.app's CFBundleExecutable (identity, --show, --stop)
 *   log=<file>              verbose=1 logs every Windows process
 * Links libobjc only. Build: build.sh.
 */
#include <crt_externs.h>
#include <dlfcn.h>
#include <limits.h>
#include <objc/runtime.h>
#include <objc/message.h>
#include <spawn.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define POLICY_REGULAR 0      /* NSApplicationActivationPolicyRegular */
#define POLICY_ACCESSORY 1    /* NSApplicationActivationPolicyAccessory */
#define TERMINATE_CANCEL 0UL  /* NSTerminateCancel */
#define EVENT_KEY_DOWN 10UL   /* NSEventTypeKeyDown */
#define KEYCODE_Q 12          /* kVK_ANSI_Q: the Q key whatever the keyboard layout */
#define MODS_MASK 0x1E0000UL  /* Shift | Control | Option | Command */
#define MOD_COMMAND 0x100000UL
#define FCC(a, b, c, d) ((unsigned)(a) << 24 | (unsigned)(b) << 16 | (unsigned)(c) << 8 | (unsigned)(d))
#define MAX_GAMES 64

extern char **environ;

static struct { char exe[256], launcher[PATH_MAX]; } games[MAX_GAMES];
static int ngames;
static char cfg_steam[PATH_MAX], cfg_log[PATH_MAX];
static int cfg_verbose, decided, active, is_game, is_steam, is_explorer, hooked_appkit, hooked_wine;

static void dlog(const char *fmt, ...)
{
    char stamp[16];
    time_t now = time(NULL);
    va_list ap;
    FILE *f;

    if (!*cfg_log || !(f = fopen(cfg_log, "a"))) return;
    strftime(stamp, sizeof(stamp), "%H:%M:%S", localtime(&now));
    fprintf(f, "%s [%d %s] ", stamp, (int)getpid(), getprogname());
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}

static void copy_str(char *dst, size_t size, const char *src)
{
    snprintf(dst, size, "%s", src);
}

static void load_config(void)
{
    char path[PATH_MAX + 16], line[2 * PATH_MAX], *bar, *slash;
    const char *conf = getenv("DOCKFIX_CONF");
    Dl_info info;
    FILE *f;

    if (conf && *conf) copy_str(path, sizeof(path), conf);
    else if (dladdr((const void *)load_config, &info) && info.dli_fname && strlen(info.dli_fname) < PATH_MAX)
    {
        copy_str(path, sizeof(path), info.dli_fname);
        if ((slash = strrchr(path, '/'))) copy_str(slash + 1, sizeof(path) - (size_t)(slash + 1 - path), "dockfix.conf");
    }
    else return;
    if (!(f = fopen(path, "r"))) return;
    while (fgets(line, sizeof(line), f))
    {
        line[strcspn(line, "\r\n")] = 0;
        if (!strncmp(line, "game=", 5) && (bar = strchr(line + 5, '|')) && bar > line + 5 && bar[1] && ngames < MAX_GAMES)
        {
            *bar = 0;
            copy_str(games[ngames].exe, sizeof(games[ngames].exe), line + 5);
            copy_str(games[ngames].launcher, sizeof(games[ngames].launcher), bar + 1);
            ngames++;
        }
        else if (!strncmp(line, "steam=", 6)) copy_str(cfg_steam, sizeof(cfg_steam), line + 6);
        else if (!strncmp(line, "log=", 4)) copy_str(cfg_log, sizeof(cfg_log), line + 4);
        else if (!strncmp(line, "verbose=", 8)) cfg_verbose = atoi(line + 8);
    }
    fclose(f);
}

static int ends_with_exe(const char *s)
{
    size_t n = s ? strlen(s) : 0;
    return n > 4 && !strcasecmp(s + n - 4, ".exe");
}

/* basename of a Windows or Unix path */
static const char *base_name(const char *path)
{
    const char *name = path, *p;
    for (p = path; *p; p++)
        if (*p == '\\' || *p == '/') name = p + 1;
    return name;
}

/* the role of this process; runs at its first dlopen, before ntdll.so pulls in CoreFoundation.
 * WineCX names its loader copy after the exe ("Dimraeth.exe"), and Wine puts the Windows path
 * first in argv ("C:\...\Dimraeth.exe -force-d3d11"). */
static void decide(void)
{
    int argc = *_NSGetArgc(), i;
    char **argv = *_NSGetArgv();
    const char *prog = getprogname(), *arg1 = argc > 1 && argv && argv[1] ? argv[1] : "", *game_launcher = NULL;

    decided = 1;
    if (!prog) prog = "";
    if (!ends_with_exe(prog) && !ends_with_exe(arg1)) return;    /* wineserver, the bare loader */
    load_config();
    if (!ngames && !*cfg_steam) return;
    active = 1;
    for (i = 0; i < ngames && !game_launcher; i++)
        if (!strcasecmp(prog, games[i].exe) || !strcasecmp(base_name(arg1), games[i].exe)) game_launcher = games[i].launcher;
    is_game = game_launcher != NULL;
    is_steam = !is_game && (!strncasecmp(prog, "steam", 5) || !strncasecmp(base_name(arg1), "steam", 5));
    is_explorer = !strcasecmp(prog, "explorer.exe") || !strcasecmp(base_name(arg1), "explorer.exe");
    if (cfg_verbose) dlog("Windows process (%s), role %s", arg1, is_game ? "game" : is_steam ? "Steam" : "helper");
    if (is_game)
    {
        setenv("CFProcessPath", game_launcher, 1);
        dlog("game process: shown as %s", game_launcher);
    }
    else if (is_steam && *cfg_steam) setenv("CFProcessPath", cfg_steam, 1);   /* "Windows Steam" */
    else unsetenv("CFProcessPath");
}

/* "<Windows Steam launcher> <arg>", detached: sh puts it in the background and exits at once */
static void run_launcher(const char *arg)
{
    char *argv[] = { "/bin/sh", "-c", "\"$0\" \"$1\" </dev/null >/dev/null 2>&1 &", cfg_steam, (char *)arg, NULL };
    pid_t pid;
    int status;

    if (posix_spawn(&pid, "/bin/sh", NULL, NULL, argv, environ) == 0) waitpid(pid, &status, 0);
    else dlog("could not start %s %s", cfg_steam, arg);
}

static BOOL (*real_set_policy)(id, SEL, long);
static BOOL fake_set_policy(id self, SEL cmd, long policy)
{
    if (policy == POLICY_REGULAR && is_steam)
        dlog("a Steam window is shown: Windows Steam in the Dock");
    else if (policy == POLICY_REGULAR && is_explorer)
    {
        dlog("activation policy Regular -> Accessory (no Dock icon)");
        policy = POLICY_ACCESSORY;
    }
    return real_set_policy(self, cmd, policy);
}

static id (*real_status_item)(id, SEL, double);
static id fake_status_item(id self, SEL cmd, double length)
{
    id item = real_status_item(self, cmd, length);
    if (item)
    {
        ((void (*)(id, SEL, id))objc_msgSend)(self, sel_registerName("removeStatusItem:"), item);
        dlog("tray icon taken off the menu bar");
    }
    return item;
}

static void (*real_set_icon)(id, SEL, id);
static void fake_set_icon(id self, SEL cmd, id image)
{
    if (image)
    {
        if (cfg_verbose) dlog("exe icon ignored, the app's icon stays");
        return;
    }
    real_set_icon(self, cmd, image);
}

/* WineApplicationController: the app's icon was clicked while Steam runs */
static BOOL (*real_reopen)(id, SEL, id, BOOL);
static BOOL fake_reopen(id self, SEL cmd, id app, BOOL visible)
{
    BOOL ret = real_reopen(self, cmd, app, visible);
    dlog("app icon clicked: %s --show", cfg_steam);
    run_launcher("--show");
    return ret;
}

/* the Quit Apple event says why it was sent; logout/restart/shutdown carry a reason */
static int session_ending(void)
{
    Class mgr_class = objc_lookUpClass("NSAppleEventManager");
    id mgr, event, reason;
    unsigned why;

    if (!mgr_class) return 0;
    mgr = ((id (*)(Class, SEL))objc_msgSend)(mgr_class, sel_registerName("sharedAppleEventManager"));
    event = mgr ? ((id (*)(id, SEL))objc_msgSend)(mgr, sel_registerName("currentAppleEvent")) : nil;
    reason = event ? ((id (*)(id, SEL, unsigned))objc_msgSend)(event, sel_registerName("attributeDescriptorForKeyword:"),
                                                                FCC('w', 'h', 'y', '?')) : nil;
    if (!reason) return 0;
    why = (unsigned)((int (*)(id, SEL))objc_msgSend)(reason, sel_registerName("int32Value"));
    return why == FCC('l', 'o', 'g', 'o') || why == FCC('r', 'l', 'g', 'o') || why == FCC('r', 'r', 's', 't') ||
           why == FCC('r', 'e', 's', 't') || why == FCC('r', 's', 'd', 'n') || why == FCC('s', 'h', 'u', 't');
}

static unsigned long (*real_should_terminate)(id, SEL, id);
static unsigned long fake_should_terminate(id self, SEL cmd, id sender)
{
    if (session_ending()) return real_should_terminate(self, cmd, sender);
    dlog("Quit asked: %s --stop", cfg_steam);
    run_launcher("--stop");
    return TERMINATE_CANCEL;
}

/* WineApplication: Cmd+Q would reach Steam as Alt+Q (WineCX maps Command to Alt) and do nothing */
static void (*real_send_event)(id, SEL, id);
static void fake_send_event(id self, SEL cmd, id event)
{
    if (event &&
        ((unsigned long (*)(id, SEL))objc_msgSend)(event, sel_registerName("type")) == EVENT_KEY_DOWN &&
        ((unsigned short (*)(id, SEL))objc_msgSend)(event, sel_registerName("keyCode")) == KEYCODE_Q &&
        (((unsigned long (*)(id, SEL))objc_msgSend)(event, sel_registerName("modifierFlags")) & MODS_MASK) == MOD_COMMAND)
    {
        if (!((BOOL (*)(id, SEL))objc_msgSend)(event, sel_registerName("isARepeat")))
        {
            dlog("Cmd+Q in a Steam window: %s --stop", cfg_steam);
            run_launcher("--stop");
        }
        return;
    }
    real_send_event(self, cmd, event);
}

static IMP replace(const char *cls_name, const char *sel_name, IMP imp)
{
    Class cls = objc_lookUpClass(cls_name);
    Method m = cls ? class_getInstanceMethod(cls, sel_registerName(sel_name)) : NULL;

    if (!m)
    {
        dlog("-[%s %s] not found", cls_name, sel_name);
        return NULL;
    }
    return method_setImplementation(m, imp);
}

/* after a dlopen, outside any runtime lock: AppKit's classes first, then winemac's */
static void install_hooks(void)
{
    int steam_ui = is_steam && *cfg_steam;

    if (!hooked_appkit && objc_lookUpClass("NSApplication") && objc_lookUpClass("NSStatusBar"))
    {
        hooked_appkit = 1;
        if (is_game || steam_ui)
            real_set_icon = (void (*)(id, SEL, id))replace("NSApplication", "setApplicationIconImage:", (IMP)fake_set_icon);
        if (!is_game)
        {
            real_set_policy = (BOOL (*)(id, SEL, long))replace("NSApplication", "setActivationPolicy:",
                                                               (IMP)fake_set_policy);
            real_status_item = (id (*)(id, SEL, double))replace("NSStatusBar", "statusItemWithLength:",
                                                                (IMP)fake_status_item);
        }
        if (cfg_verbose) dlog("AppKit hooks in place");
    }
    if (steam_ui && !hooked_wine && objc_lookUpClass("WineApplicationController") && objc_lookUpClass("WineApplication"))
    {
        hooked_wine = 1;
        real_reopen = (BOOL (*)(id, SEL, id, BOOL))replace("WineApplicationController",
                                                           "applicationShouldHandleReopen:hasVisibleWindows:",
                                                           (IMP)fake_reopen);
        real_should_terminate = (unsigned long (*)(id, SEL, id))replace("WineApplicationController",
                                                                        "applicationShouldTerminate:",
                                                                        (IMP)fake_should_terminate);
        real_send_event = (void (*)(id, SEL, id))replace("WineApplication", "sendEvent:", (IMP)fake_send_event);
        if (cfg_verbose) dlog("winemac hooks in place");
    }
}

static void *dockfix_dlopen(const char *path, int mode)
{
    void *handle;

    if (!decided) decide();
    handle = dlopen(path, mode);                                 /* our own call is not interposed */
    if (active && handle && (!hooked_appkit || (is_steam && *cfg_steam && !hooked_wine))) install_hooks();
    return handle;
}

__attribute__((used)) static const struct { const void *replacement, *replacee; } dockfix_interpose[]
    __attribute__((section("__DATA,__interpose"))) = { { (const void *)dockfix_dlopen, (const void *)dlopen } };
