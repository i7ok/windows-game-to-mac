/* dockfix: one Dock icon for a Wine-wrapped Steam game, with the app's name and icon.
 *
 * The launcher injects it into the Wine processes of a game session (DYLD_INSERT_LIBRARIES).
 * Wine's Mac driver (winemac) turns every process that shows a window into a regular app with a
 * Dock icon named after the Windows exe (WineCX runs each exe from a temp copy of its loader named
 * like it: "Dimraeth.exe", "steam.exe", "steamwebhelper.exe"), sets the exe's icon as the app icon,
 * and shows Windows tray icons (Steam's) as status items in the menu bar.
 *
 * In the game process (its exe = game= in dockfix.conf):
 *   - CFProcessPath = the app's launcher before CoreFoundation is loaded, so the process is this
 *     .app: its name and icon in the Dock and the menu bar;
 *   - -[NSApplication setApplicationIconImage:] ignores the exe's icon, so the bundle's icon stays.
 * Steam's own programs (exe name steam*): CFProcessPath = Contents/Helpers/Steam.app (LSUIElement,
 *   named "Steam"): no Dock icon until winemac shows one of their windows (login, cloud conflict);
 *   then Regular goes through, so a Steam window that asks the user is findable in the Dock.
 * Every other Windows process (Wine's helpers, the crash handler):
 *   - an inherited CFProcessPath is removed, so they never register as the app;
 *   - -[NSApplication setActivationPolicy:] turns Regular into Accessory: windows still work, no Dock icon.
 * All non-game processes: -[NSStatusBar statusItemWithLength:] items are taken off the menu bar
 *   (Steam's tray icon, shown by explorer.exe).
 *
 * How the code gets to run: in Wine's Windows processes (started through wine-preloader) dyld maps
 * this library but never calls its initializers, so there is no constructor. Instead it interposes
 * dlopen (dyld applies __interpose tuples while binding, initializers or not). Wine's loader dlopens
 * ntdll.so first, which brings in CoreFoundation: before that we decide the role and set or remove
 * CFProcessPath. Later dlopen("winemac.so") brings in AppKit: right after it returns, outside any
 * runtime lock, the AppKit methods are replaced, before winemac starts its Cocoa app.
 * Settings: dockfix.conf next to this library (game=, launcher=, logs= or log=, verbose=), written by
 * install.sh. Log: ~/Library/Logs/<logs>/dockfix.log. Links libobjc only. Build: build.sh.
 */
#include <crt_externs.h>
#include <dlfcn.h>
#include <limits.h>
#include <objc/runtime.h>
#include <objc/message.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

#define POLICY_REGULAR 0     /* NSApplicationActivationPolicyRegular */
#define POLICY_ACCESSORY 1   /* NSApplicationActivationPolicyAccessory */

static char cfg_game[256], cfg_bundle_exe[PATH_MAX], cfg_steam_exe[PATH_MAX], cfg_log[PATH_MAX];
static int cfg_verbose, decided, active, is_game, is_steam, hooked;

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

/* dockfix.conf in the same folder as this library (Contents/Resources) */
static void load_config(void)
{
    char dir[PATH_MAX], path[PATH_MAX + 16], line[PATH_MAX + 16], launcher[256] = "", logs[256] = "";
    const char *home = getenv("HOME");
    Dl_info info;
    char *slash;
    FILE *f;

    if (!dladdr((const void *)load_config, &info) || !info.dli_fname || strlen(info.dli_fname) >= sizeof(dir)) return;
    copy_str(dir, sizeof(dir), info.dli_fname);
    if ((slash = strrchr(dir, '/'))) *slash = 0;                  /* .../Contents/Resources */
    snprintf(path, sizeof(path), "%s/dockfix.conf", dir);
    if ((f = fopen(path, "r")))
    {
        while (fgets(line, sizeof(line), f))
        {
            line[strcspn(line, "\r\n")] = 0;
            if (!strncmp(line, "game=", 5)) copy_str(cfg_game, sizeof(cfg_game), line + 5);
            else if (!strncmp(line, "launcher=", 9)) copy_str(launcher, sizeof(launcher), line + 9);
            else if (!strncmp(line, "logs=", 5)) copy_str(logs, sizeof(logs), line + 5);
            else if (!strncmp(line, "log=", 4)) copy_str(cfg_log, sizeof(cfg_log), line + 4);
            else if (!strncmp(line, "verbose=", 8)) cfg_verbose = atoi(line + 8);
        }
        fclose(f);
    }
    if ((slash = strrchr(dir, '/'))) *slash = 0;                  /* .../Contents */
    if (*launcher) snprintf(cfg_bundle_exe, sizeof(cfg_bundle_exe), "%s/MacOS/%s", dir, launcher);
    snprintf(path, sizeof(path), "%s/Helpers/Steam.app/Contents/MacOS/Steam", dir);
    if (!access(path, F_OK)) copy_str(cfg_steam_exe, sizeof(cfg_steam_exe), path);
    if (!*cfg_log && *logs && home) snprintf(cfg_log, sizeof(cfg_log), "%s/Library/Logs/%s/dockfix.log", home, logs);
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
    int argc = *_NSGetArgc();
    char **argv = *_NSGetArgv();
    const char *prog = getprogname(), *arg1 = argc > 1 && argv && argv[1] ? argv[1] : "";

    decided = 1;
    if (!ends_with_exe(prog) && !ends_with_exe(arg1)) return;    /* wineserver, the bare loader */
    load_config();
    if (!*cfg_game) return;
    active = 1;
    is_game = (prog && !strcasecmp(prog, cfg_game)) || !strcasecmp(base_name(arg1), cfg_game);
    if (cfg_verbose) dlog("Windows process (%s), role %s", arg1, is_game ? "game" : "helper");
    /* Steam's own programs (steam.exe, steamwebhelper.exe, ...): no Dock icon until one of their
     * windows asks the user something; then winemac's Regular goes through (see the hook). Their
     * identity is Contents/Helpers/Steam.app (LSUIElement), so that icon is named "Steam". */
    is_steam = !is_game && (!strncasecmp(prog ? prog : "", "steam", 5) || !strncasecmp(base_name(arg1), "steam", 5));
    if (is_game && *cfg_bundle_exe)
    {
        setenv("CFProcessPath", cfg_bundle_exe, 1);
        dlog("game process: shown as %s", cfg_bundle_exe);
    }
    else if (is_steam && *cfg_steam_exe) setenv("CFProcessPath", cfg_steam_exe, 1);  /* "Steam" in the Dock */
    else unsetenv("CFProcessPath");
}

static BOOL (*real_setActivationPolicy)(id, SEL, long);
static BOOL fake_setActivationPolicy(id self, SEL cmd, long policy)
{
    if (policy == POLICY_REGULAR && is_steam)
        dlog("a Steam window wants the user: Steam shown in the Dock");
    else if (policy == POLICY_REGULAR)
    {
        dlog("activation policy Regular -> Accessory (no Dock icon)");
        policy = POLICY_ACCESSORY;
    }
    return real_setActivationPolicy(self, cmd, policy);
}

static id (*real_statusItemWithLength)(id, SEL, double);
static id fake_statusItemWithLength(id self, SEL cmd, double length)
{
    id item = real_statusItemWithLength(self, cmd, length);
    if (item)
    {
        ((void (*)(id, SEL, id))objc_msgSend)(self, sel_registerName("removeStatusItem:"), item);
        dlog("tray icon taken off the menu bar");
    }
    return item;
}

static void (*real_setApplicationIconImage)(id, SEL, id);
static void fake_setApplicationIconImage(id self, SEL cmd, id image)
{
    if (image)
    {
        dlog("exe icon ignored, the app's icon stays");
        return;
    }
    real_setApplicationIconImage(self, cmd, image);
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

/* after a dlopen, outside any runtime lock: acts once AppKit's classes are there */
static void install_hooks(void)
{
    if (!objc_lookUpClass("NSApplication") || !objc_lookUpClass("NSStatusBar")) return;
    hooked = 1;
    if (is_game)
        real_setApplicationIconImage = (void (*)(id, SEL, id))replace("NSApplication", "setApplicationIconImage:",
                                                                      (IMP)fake_setApplicationIconImage);
    else
    {
        real_setActivationPolicy = (BOOL (*)(id, SEL, long))replace("NSApplication", "setActivationPolicy:",
                                                                    (IMP)fake_setActivationPolicy);
        real_statusItemWithLength = (id (*)(id, SEL, double))replace("NSStatusBar", "statusItemWithLength:",
                                                                     (IMP)fake_statusItemWithLength);
    }
    if (cfg_verbose) dlog("AppKit hooks in place (%s)", is_game ? "game" : "helper");
}

static void *dockfix_dlopen(const char *path, int mode)
{
    void *handle;

    if (!decided) decide();
    handle = dlopen(path, mode);                                 /* our own call is not interposed */
    if (active && !hooked && handle) install_hooks();
    return handle;
}

__attribute__((used)) static const struct { const void *replacement, *replacee; } dockfix_interpose[]
    __attribute__((section("__DATA,__interpose"))) = { { (const void *)dockfix_dlopen, (const void *)dlopen } };
