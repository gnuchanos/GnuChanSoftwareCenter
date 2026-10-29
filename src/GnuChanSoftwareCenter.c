/*
 * GnuChanSoftwareCenter.c — GnuchanOS software center.
 *
 * Linux Mint App Market benzeri bir arayuz: Debian paketlerini listeler,
 * yuklenmis paketleri goster, guncellemeleri kontrol eder ve GnuChan
 * X11 programlarini otomatik olarak gunceller.
 *
 * Her baslatisida GitHub'daki tum GnuChan depolarindaki version.txt'lari
 * kontrol eder, kurulu programların config dosyasindaki versiyonla
 * karsilastirir ve guncelleme gorunsun diye isaretler.
 *
 * Islevler:
 *   - Butun Debian paketlerini tarama (dpkg-query)
 *   - Yuklenmis paketleri listeleme
 *   - Guncelleme denetimi (apt-get -s upgrade)
 *   - Tekli paket kurma / kaldirma (apt-get install / remove)
 *   - Tum guncellemeleri tek seferde uygulama
 *   - GnuChan programlarini GitHub'dan versiyon denetimi ile guncelleme
 *   - Arama kutusu ve kategori secimi (Tum, Yuklenmis, Guncellemeler, GnuChan)
 *
 * Arka uclarda popen() ile dpkg-query, apt-cache, apt-get, curl cagrilir.
 * Arayuz sade X11 cizimleri ile yapilir.
 */
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <pwd.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include "gnuchan_lang.h"

#define GNUCHANSOFTWARECENTER_VERSION "0.2.0"

/* --- boyutlar ------------------------------------------------------------ */

#define INITIAL_WIN_WIDTH  1000
#define INITIAL_WIN_HEIGHT 700

static int window_width = INITIAL_WIN_WIDTH;
static int window_height = INITIAL_WIN_HEIGHT;

#define WIN_WIDTH  window_width
#define WIN_HEIGHT window_height

#define SIDEBAR_W  220
#define HEADER_H   44
#define FOOTER_H   64
#define ITEM_H     26
#define LIST_TOP   (HEADER_H + 10)
#define LIST_LEFT  (SIDEBAR_W + 10)
#define LIST_RIGHT (WIN_WIDTH - 10)
#define LIST_BOTTOM (WIN_HEIGHT - FOOTER_H - 10)

#define MAX_PACKAGES 4096
#define MAX_NAME_LEN 256
#define MAX_VERSION_LEN 64
#define MAX_DESC_LEN 512
#define CMD_BUF_LEN 8192
#define LINE_BUF_LEN 2048
#define CONFIG_PATH_LEN 512

/* --- renkler ------------------------------------------------------------- */

#define COL_BG        0x160D20U
#define COL_SIDEBAR   0x21102FU
#define COL_HEADER    0x241433U
#define COL_FOOTER    0x241433U
#define COL_TEXT      0xF5EAFFU
#define COL_TEXT_DIM  0xB9A2C9U
#define COL_BORDER    0x513768U
#define COL_SELECT    0x392150U
#define COL_BUTTON    0x8B5CB8U
#define COL_BUTTON_H  0xA572D1U
#define COL_DANGER    0xFF6B8AU
#define COL_SUCCESS   0x6EA879U
#define COL_WARN      0xD7A85AU
#define COL_UPDATE    0xD7A85AU

/* --- paket modeli -------------------------------------------------------- */

typedef enum PkgState {
    PKG_STATE_UNKNOWN,
    PKG_STATE_INSTALLED,
    PKG_STATE_NOT_INSTALLED,
} PkgState;

typedef enum PackageCategory {
    CATEGORY_SYSTEM,
    CATEGORY_INTERNET,
    CATEGORY_GRAPHICS,
    CATEGORY_MULTIMEDIA,
    CATEGORY_DEVELOPMENT,
    CATEGORY_OTHER,
} PackageCategory;

typedef struct Package {
    char name[MAX_NAME_LEN];
    char version[MAX_VERSION_LEN];
    char remote_version[MAX_VERSION_LEN];
    char desc[MAX_DESC_LEN];
    PkgState state;
    PackageCategory category;
    int is_gnuchan;      /* 1: GnuChan programi */
    int has_update;      /* 1: guncelleme mevcut (apt veya github) */
    int github_update;   /* 1: GitHub'da yeni versiyon var */
    int selected;        /* UI secim durumu */
    struct Package *next;
} Package;

static Package *pkg_head = NULL;
static int pkg_count = 0;

/* --- UI durumu ----------------------------------------------------------- */

typedef enum ViewId {
    VIEW_ALL,
    VIEW_INSTALLED,
    VIEW_UPDATES,
    VIEW_GNUCHAN,
    VIEW_SYSTEM,
    VIEW_INTERNET,
    VIEW_GRAPHICS,
    VIEW_MULTIMEDIA,
    VIEW_DEVELOPMENT,
    VIEW_OTHER,
} ViewId;

static ViewId current_view = VIEW_ALL;
static char search_text[256] = {0};
static int scroll_offset = 0;
static int selected_index = -1;
static char status_msg[512] = {0};
static int busy = 0;
static int update_available = 0;
static int gnuchan_update_count = 0;
static int github_check_done = 0;
static int github_check_failures = 0;
static int show_installed_by_default = 1;
static int check_updates_on_start = 1;
static int require_authentication = 1;
static int update_checks_disabled = 0;

static volatile sig_atomic_t should_stop = 0;

/* --- GnuChan program listesi (repo adi -> program adi) ------------------ */

typedef struct GnuchanRepo {
    char repo[128];      /* GitHub repo adi (gnuchanos/<repo>) */
    char program[128];   /* Program adi */
    char config_dir[128]; /* ~/.config/<config_dir>/ */
    char config_file[128]; /* Config dosya adi */
    char local_version[MAX_VERSION_LEN];
    char remote_version[MAX_VERSION_LEN];
    int has_remote;
    int update_available;
} GnuchanRepo;

static GnuchanRepo gnuchan_repos[] = {
    {.repo = "GnuChanWM", .program = "GnuChanWM", .config_dir = "GnuChanWM",
     .config_file = "GnuChanWM.py"},
    {.repo = "GnuChanSS", .program = "GnuChanSS", .config_dir = "GnuChanSS",
     .config_file = "GnuChanSS.py"},
    {.repo = "GnuChanSettings", .program = "GnuChanSettings",
     .config_dir = "GnuChanSettings", .config_file = "system.conf"},
    {.repo = "GnuChanDock", .program = "GnuChanDock",
     .config_dir = "GnuChanDock", .config_file = "GnuChanDock.py"},
    {.repo = "GnuChanFetch", .program = "GnuChanFetch",
     .config_dir = "GnuChanFetch", .config_file = "GnuChanFetch.py"},
    {.repo = "GnuChanTerm", .program = "GnuChanTerm", .config_dir = "GnuChanTerm",
     .config_file = "GnuChanTerm.py"},
    {.repo = "GnuChanTop", .program = "GnuChanTop", .config_dir = "GnuChanTop",
     .config_file = "GnuChanTop.py"},
    {.repo = "GnuChanWifi", .program = "GnuChanWifi", .config_dir = "GnuChanWifi",
     .config_file = "GnuChanWifi.py"},
    {.repo = "GnuChanLockScreen", .program = "GnuChanLockScreen",
     .config_dir = "GnuChanSL", .config_file = "GnuChanSL.py"},
    {.repo = "GnuChanNetworkManager", .program = "GnuChanNetworkManager",
     .config_dir = "GnuChanNetworkManager", .config_file = "config.py"},
    {.repo = "GnuChanNotification", .program = "GnuChanNotification",
     .config_dir = "GnuChanNotification",
     .config_file = "GnuChanNotification.py"},
    {.repo = "GnuChanRunner", .program = "GnuChanRunner",
     .config_dir = "GnuChanRunner", .config_file = "config.py"},
    {.repo = "GnuChanScreenShoot", .program = "GnuChanScreenShoot",
     .config_dir = "GnuChanScreenShoot",
     .config_file = "GnuChanScreenShoot.py"},
    {.repo = "GnuChanSoftwareCenter", .program = "GnuChanSoftwareCenter",
     .config_dir = "GnuChanSoftwareCenter",
     .config_file = "GnuChanSoftwareCenter.py"},
    {.repo = "GnuChanBrowser", .program = "GnuChanBrowser",
     .config_dir = "GnuChanBrowser", .config_file = "config.py"},
    {.repo = "GnuChanDM", .program = "GnuChanDM", .config_dir = "GnuChanDM",
     .config_file = "GnuChanDM.py"},
};
#define GNUCHAN_REPO_COUNT (sizeof(gnuchan_repos) / sizeof(gnuchan_repos[0]))

/* --- yardimcilar --------------------------------------------------------- */

static void set_status(const char *msg) {
    snprintf(status_msg, sizeof(status_msg), "%s", msg);
}

static void set_status_fmt(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(status_msg, sizeof(status_msg), fmt, ap);
    va_end(ap);
}

static int parse_setting_bool(const char *value, int fallback) {
    if (strcmp(value, "True") == 0 || strcmp(value, "true") == 0) return 1;
    if (strcmp(value, "False") == 0 || strcmp(value, "false") == 0) return 0;
    return fallback;
}

static void load_center_settings(const char *home) {
    const char *xdg = getenv("XDG_CONFIG_HOME");
    char path[CONFIG_PATH_LEN];
    if (xdg && xdg[0]) {
        snprintf(path, sizeof(path),
                 "%s/GnuChanSoftwareCenter/GnuChanSoftwareCenter.py", xdg);
    } else {
        snprintf(path, sizeof(path),
                 "%s/.config/GnuChanSoftwareCenter/GnuChanSoftwareCenter.py",
                 home);
    }

    FILE *f = fopen(path, "r");
    if (!f) {
        return;
    }
    char line[LINE_BUF_LEN];
    while (fgets(line, sizeof(line), f)) {
        char *equals = strchr(line, '=');
        if (!equals) {
            continue;
        }
        char key[64] = {0}, value[128] = {0};
        size_t key_length = (size_t)(equals - line);
        while (key_length > 0 && isspace((unsigned char)line[key_length - 1])) {
            key_length--;
        }
        size_t key_start = 0;
        while (key_start < key_length &&
               isspace((unsigned char)line[key_start])) {
            key_start++;
        }
        size_t key_size = key_length - key_start;
        if (key_size == 0 || key_size >= sizeof(key)) {
            continue;
        }
        memcpy(key, line + key_start, key_size);
        const char *value_start = equals + 1;
        while (isspace((unsigned char)*value_start)) value_start++;
        size_t value_length = strcspn(value_start, ", )#\t\r\n");
        if (value_length == 0 || value_length >= sizeof(value)) {
            continue;
        }
        memcpy(value, value_start, value_length);
        if (strcmp(key, "ShowInstalled") == 0) {
            show_installed_by_default = parse_setting_bool(
                value, show_installed_by_default);
        } else if (strcmp(key, "CheckUpdates") == 0) {
            check_updates_on_start = parse_setting_bool(
                value, check_updates_on_start);
        } else if (strcmp(key, "RequireAuth") == 0) {
            require_authentication = parse_setting_bool(
                value, require_authentication);
        }
    }
    fclose(f);
}

static int run_cmd(const char *cmd, char *out, size_t out_size) {
    FILE *fp = popen(cmd, "r");
    if (!fp) {
        snprintf(out, out_size, "popen failed: %s", strerror(errno));
        return -1;
    }
    size_t n = 0;
    char line[LINE_BUF_LEN];
    while (fgets(line, sizeof(line), fp)) {
        size_t len = strlen(line);
        if (n + len < out_size) {
            memcpy(out + n, line, len);
            n += len;
        }
    }
    out[n] = '\0';
    int rc = pclose(fp);
    return WIFEXITED(rc) ? WEXITSTATUS(rc) : -1;
}

static void check_apt_updates(void);
static void check_github_versions(const char *home);

static void refresh_update_checks(const char *home) {
    if (!check_updates_on_start) {
        update_checks_disabled = 1;
        github_check_done = 1;
        update_available = 0;
        gnuchan_update_count = 0;
        set_status("Automatic update checks are disabled in settings");
        return;
    }
    update_checks_disabled = 0;
    check_apt_updates();
    check_github_versions(home);
}

/* --- versiyon karsilastirma ---------------------------------------------- */

/* "0.1.0" -> 10000, "0.2.0" -> 20000, "1.10.3" -> 101003 */
static int version_to_int(const char *v) {
    int major = 0, minor = 0, patch = 0;
    sscanf(v, "%d.%d.%d", &major, &minor, &patch);
    return major * 1000000 + minor * 1000 + patch;
}

static int version_newer(const char *a, const char *b) {
    return version_to_int(a) > version_to_int(b);
}

static int version_valid(const char *version) {
    int major, minor, patch;
    char extra;
    return version && strlen(version) < MAX_VERSION_LEN &&
           sscanf(version, "%d.%d.%d%c",
                             &major, &minor, &patch, &extra) == 3 &&
           major >= 0 && minor >= 0 && patch >= 0;
}

/* --- config dosyasindan versiyon okuma ---------------------------------- */

static void read_config_version(const char *home, const char *config_dir,
                                const char *config_file, char *out,
                                size_t out_size) {
    char path[CONFIG_PATH_LEN];
    snprintf(path, sizeof(path), "%s/.config/%s/%s", home, config_dir, config_file);

    FILE *f = fopen(path, "r");
    if (!f) {
        snprintf(out, out_size, "0.0.0");
        return;
    }

    char line[LINE_BUF_LEN];
    out[0] = '\0';
    while (fgets(line, sizeof(line), f)) {
        /* Version = "0.1.0"  veya  Version="0.1.0" */
        char *eq = strchr(line, '=');
        if (!eq) continue;
        char *key = line;
        while (*key && (*key == ' ' || *key == '\t')) key++;
        char *ke_end = eq - 1;
        while (ke_end > key && (*ke_end == ' ' || *ke_end == '\t')) ke_end--;
        *(ke_end + 1) = '\0';

        if (strcmp(key, "Version") != 0) continue;

        /* value parala */
        char *val = eq + 1;
        while (*val && (*val == ' ' || *val == '\t')) val++;
        if (*val == '"') val++;
        char *end = val;
        while (*end && *end != '"' && *end != '\n' && *end != '\r') end++;
        *end = '\0';
        snprintf(out, out_size, "%s", val);
        break;
    }
    fclose(f);
    if (out[0] == '\0') {
        snprintf(out, out_size, "0.0.0");
    }
}

/* --- paket veritabani ---------------------------------------------------- */

static void pkg_free_all(void) {
    Package *p = pkg_head;
    while (p) {
        Package *n = p->next;
        free(p);
        p = n;
    }
    pkg_head = NULL;
    pkg_count = 0;
}

static Package *pkg_find(const char *name) {
    for (Package *p = pkg_head; p; p = p->next) {
        if (strcmp(p->name, name) == 0) return p;
    }
    return NULL;
}

static void pkg_add(const char *name, const char *version,
                    const char *desc, PkgState state,
                    PackageCategory category,
                    int is_gnuchan, int has_update, int github_update) {
    if (pkg_find(name)) return;
    Package *p = calloc(1, sizeof(*p));
    if (!p) return;
    snprintf(p->name, sizeof(p->name), "%s", name);
    snprintf(p->version, sizeof(p->version), "%s", version ? version : "");
    snprintf(p->remote_version, sizeof(p->remote_version), "%s", version ? version : "");
    snprintf(p->desc, sizeof(p->desc), "%s", desc ? desc : "");
    p->state = state;
    p->category = category;
    p->is_gnuchan = is_gnuchan;
    p->has_update = has_update;
    p->github_update = github_update;
    p->next = pkg_head;
    pkg_head = p;
    pkg_count++;
}

static int is_gnuchan_binary(const char *name) {
    if (strncmp(name, "gnuchan", 7) == 0) return 1;
    if (strncmp(name, "GnuChan", 7) == 0) return 1;
    if (strcmp(name, "gcl") == 0) return 1;
    return 0;
}

static PackageCategory category_for_section(const char *section) {
    if (strncmp(section, "net", 3) == 0 ||
        strncmp(section, "web", 3) == 0 ||
        strncmp(section, "mail", 4) == 0) {
        return CATEGORY_INTERNET;
    }
    if (strncmp(section, "graphics", 8) == 0 ||
        strncmp(section, "x11", 3) == 0) {
        return CATEGORY_GRAPHICS;
    }
    if (strncmp(section, "sound", 5) == 0 ||
        strncmp(section, "video", 5) == 0) {
        return CATEGORY_MULTIMEDIA;
    }
    if (strncmp(section, "devel", 5) == 0) {
        return CATEGORY_DEVELOPMENT;
    }
    if (strncmp(section, "admin", 5) == 0 ||
        strncmp(section, "utils", 5) == 0 ||
        strncmp(section, "kernel", 6) == 0) {
        return CATEGORY_SYSTEM;
    }
    return CATEGORY_OTHER;
}

static void load_installed_packages(void) {
    char cmd[CMD_BUF_LEN];
    snprintf(cmd, sizeof(cmd),
             "dpkg-query -W -f='${Package}\\t${Version}\\t${db:Status-Status}\\t${Section}\\n' 2>/dev/null");
    char out[CMD_BUF_LEN * 4];
    if (run_cmd(cmd, out, sizeof(out)) != 0) {
        set_status("dpkg-query calismadi");
        return;
    }

    char *line = out;
    while (*line) {
        char *end = strchr(line, '\n');
        if (end) *end = '\0';

        char name[MAX_NAME_LEN] = {0};
        char version[MAX_VERSION_LEN] = {0};
        char status[32] = {0};
        char section[128] = {0};
        sscanf(line, "%255[^\t]\t%63[^\t]\t%31[^\t]\t%127[^\n]",
               name, version, status, section);

        int installed = strcmp(status, "installed") == 0;
        int is_gc = is_gnuchan_binary(name);
        pkg_add(name, version, "",
                installed ? PKG_STATE_INSTALLED : PKG_STATE_NOT_INSTALLED,
                category_for_section(section), is_gc, 0, 0);

        if (end) line = end + 1;
        else break;
    }
}

/* Guncelleme denetimi: apt-get -s upgrade */
static void check_apt_updates(void) {
    char cmd[CMD_BUF_LEN];
    snprintf(cmd, sizeof(cmd), "apt-get -s upgrade 2>/dev/null | grep '^Inst' | head -50");
    char out[CMD_BUF_LEN * 4];
    if (run_cmd(cmd, out, sizeof(out)) != 0) {
        set_status("apt guncelleme denetimi calismadi");
        return;
    }

    int cnt = 0;
    char *line = out;
    while (*line) {
        char *end = strchr(line, '\n');
        if (end) *end = '\0';
        char pkg_name[MAX_NAME_LEN] = {0};
        if (sscanf(line, "Inst %255s", pkg_name) == 1) {
            Package *p = pkg_find(pkg_name);
            if (p) {
                p->has_update = 1;
                cnt++;
            }
        }
        if (end) line = end + 1;
        else break;
    }
    update_available = (cnt > 0);
}

/* GitHub'dan tum GnuChan repolarinin version.txt'ini al */
static void check_github_versions(const char *home) {
    github_check_failures = 0;
    for (size_t i = 0; i < GNUCHAN_REPO_COUNT; i++) {
        GnuchanRepo *r = &gnuchan_repos[i];
        r->has_remote = 0;
        r->update_available = 0;
        r->remote_version[0] = '\0';

        /* Yerel versiyonu config dosyasindan oku */
        read_config_version(home, r->config_dir, r->config_file,
                            r->local_version, sizeof(r->local_version));

        /* Uzak versiyonu GitHub'dan al */
        char url[512];
        snprintf(url, sizeof(url),
                 "https://raw.githubusercontent.com/gnuchanos/%s/main/version.txt", r->repo);
        char cmd[CMD_BUF_LEN];
        snprintf(cmd, sizeof(cmd), "curl -fsS --connect-timeout 5 '%s' 2>/dev/null", url);
        char out[CMD_BUF_LEN];
        if (run_cmd(cmd, out, sizeof(out)) == 0 && out[0]) {
            /* Bosluklari temizle */
            char *s = out;
            while (*s && (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r')) s++;
            char *e = s + strlen(s);
            if (e > s) {
                e--;
            }
            while (e > s && (*e == ' ' || *e == '\t' || *e == '\n' || *e == '\r')) *e-- = '\0';
            if (version_valid(s) && version_valid(r->local_version)) {
                memcpy(r->remote_version, s, strlen(s) + 1);
                r->has_remote = 1;
                if (version_newer(r->remote_version, r->local_version)) {
                    r->update_available = 1;
                }
            } else {
                github_check_failures++;
            }
        } else {
            github_check_failures++;
        }
    }

    /* GnuChan guncelleme sayisi */
    gnuchan_update_count = 0;
    for (size_t i = 0; i < GNUCHAN_REPO_COUNT; i++) {
        if (gnuchan_repos[i].update_available) gnuchan_update_count++;
    }
    github_check_done = 1;
    if (github_check_failures > 0) {
        set_status_fmt("GitHub update check incomplete: %d of %lu repositories unavailable",
                       github_check_failures,
                       (unsigned long)GNUCHAN_REPO_COUNT);
    } else {
        set_status_fmt("GitHub checked %lu repositories; %d GnuChan update(s)",
                       (unsigned long)GNUCHAN_REPO_COUNT,
                       gnuchan_update_count);
    }

    /* Paket listesindeki GnuChan entry'leri guncelle */
    for (size_t i = 0; i < GNUCHAN_REPO_COUNT; i++) {
        Package *p = pkg_find(gnuchan_repos[i].program);
        if (p) {
            snprintf(p->remote_version, sizeof(p->remote_version), "%s",
                     gnuchan_repos[i].remote_version);
            p->github_update = gnuchan_repos[i].update_available;
            if (gnuchan_repos[i].update_available) {
                p->has_update = 1;
            }
        }
    }
}

/* --- paket islemleri ----------------------------------------------------- */

static int do_install(const char *name) {
    char cmd[CMD_BUF_LEN];
    const char *auth = require_authentication && getuid() != 0
                           ? "pkexec " : "";
    snprintf(cmd, sizeof(cmd),
             "%sapt-get install -y --no-install-recommends '%s' 2>&1",
             auth, name);
    char out[CMD_BUF_LEN * 4];
    int rc = run_cmd(cmd, out, sizeof(out));
    if (rc == 0) {
        set_status_fmt("%s kuruldu", name);
        return 0;
    }
    set_status_fmt("%s kurulamadi: %.200s", name, out);
    return -1;
}

static int do_remove(const char *name) {
    char cmd[CMD_BUF_LEN];
    const char *auth = require_authentication && getuid() != 0
                           ? "pkexec " : "";
    snprintf(cmd, sizeof(cmd), "%sapt-get remove -y '%s' 2>&1",
             auth, name);
    char out[CMD_BUF_LEN * 4];
    int rc = run_cmd(cmd, out, sizeof(out));
    if (rc == 0) {
        set_status_fmt("%s kaldirildi", name);
        return 0;
    }
    set_status_fmt("%s kaldirilamadi: %.200s", name, out);
    return -1;
}

static int do_upgrade_all(void) {
    char cmd[CMD_BUF_LEN];
    const char *auth = require_authentication && getuid() != 0
                           ? "pkexec " : "";
    snprintf(cmd, sizeof(cmd), "%sapt-get upgrade -y 2>&1", auth);
    char out[CMD_BUF_LEN * 4];
    int rc = run_cmd(cmd, out, sizeof(out));
    if (rc == 0) {
        set_status("tum guncellemeler uygulandi");
        return 0;
    }
    set_status_fmt("guncelleme basarisiz: %.200s", out);
    return -1;
}

static int do_update_gnuchan_all(void) {
    const struct passwd *account = getpwuid(getuid());
    if (!account || !account->pw_name || !account->pw_name[0]) {
        set_status("GnuChan update failed: cannot identify the current account");
        return -1;
    }
    char cmd[CMD_BUF_LEN];
    for (const char *p = account->pw_name; *p; p++) {
        if (!(isalnum((unsigned char)*p) || *p == '_' || *p == '-' ||
              *p == '.')) {
            set_status("GnuChan update failed: invalid current account name");
            return -1;
        }
    }
    const char *auth = getuid() == 0 || !require_authentication
                           ? "" : "pkexec ";
    int written = snprintf(cmd, sizeof(cmd),
                           "%s/usr/local/libexec/gnuchanos-programs-update "
                           "--update '%s' 2>&1", auth, account->pw_name);
    if (written < 0 || (size_t)written >= sizeof(cmd)) {
        set_status("GnuChan update command is too long");
        return -1;
    }
    size_t pos = (size_t)written;
    for (size_t i = 0; i < GNUCHAN_REPO_COUNT; i++) {
        Package *p = pkg_find(gnuchan_repos[i].program);
        if (p && p->state == PKG_STATE_INSTALLED && p->github_update) {
            int added = snprintf(cmd + pos, sizeof(cmd) - pos, " '%s'",
                                 gnuchan_repos[i].program);
            if (added < 0 || (size_t)added >= sizeof(cmd) - pos) {
                set_status("GnuChan update command is too long");
                return -1;
            }
            pos += (size_t)added;
        }
    }
    if (pos == (size_t)written) {
        set_status("guncellenecek GnuChan paketi yok");
        return 0;
    }

    char out[CMD_BUF_LEN * 4];
    int rc = run_cmd(cmd, out, sizeof(out));
    if (rc == 0) {
        set_status("GnuChan source updates installed successfully");
        return 0;
    }
    set_status_fmt("GnuChan update failed (exit %d): %.180s", rc, out);
    return -1;
}

/* --- UI cizim yardimcilari --------------------------------------------- */

static void fill_rect(Display *dpy, Window win, GC gc, unsigned long color,
                      int x, int y, unsigned int w, unsigned int h) {
    XSetForeground(dpy, gc, color);
    XFillRectangle(dpy, win, gc, x, y, w, h);
}

static void draw_rect(Display *dpy, Window win, GC gc, unsigned long color,
                      int x, int y, unsigned int w, unsigned int h) {
    XSetForeground(dpy, gc, color);
    XDrawRectangle(dpy, win, gc, x, y, w, h);
}

static void draw_text(Display *dpy, Window win, GC gc, unsigned long color,
                      int x, int y, const char *text) {
    XSetForeground(dpy, gc, color);
    XDrawString(dpy, win, gc, x, y, text, (int)strlen(text));
}

static unsigned long get_color(Display *dpy, unsigned long rgb) {
    XColor c;
    c.red = (rgb >> 16) & 0xFF;
    c.red = c.red * 256 | c.red;
    c.green = (rgb >> 8) & 0xFF;
    c.green = c.green * 256 | c.green;
    c.blue = rgb & 0xFF;
    c.blue = c.blue * 256 | c.blue;
    c.flags = DoRed | DoGreen | DoBlue;
    Colormap cmap = DefaultColormap(dpy, DefaultScreen(dpy));
    if (!XAllocColor(dpy, cmap, &c)) {
        /* Fallback: if we can't allocate, use the closest pixel directly. */
        return rgb;
    }
    return c.pixel;
}

static GC create_gc(Display *dpy, Window win) {
    GC gc = XCreateGC(dpy, win, 0, NULL);
    Font f = XLoadFont(dpy, "fixed");
    if (!f) {
        f = XLoadFont(dpy, "6x13");
    }
    if (!f) {
        f = XLoadFont(dpy, "cursor");
    }
    XSetFont(dpy, gc, f);
    return gc;
}

/* --- sekme / buton durumu ------------------------------------------------ */

typedef struct SidebarItem {
    const char *label;
    ViewId view;
} SidebarItem;

static SidebarItem sidebar_items[] = {
    {"Tum Paketler", VIEW_ALL},
    {"Yuklenmisler", VIEW_INSTALLED},
    {"Guncellemeler", VIEW_UPDATES},
    {"GnuChan Programlari", VIEW_GNUCHAN},
    {"System", VIEW_SYSTEM},
    {"Internet", VIEW_INTERNET},
    {"Graphics", VIEW_GRAPHICS},
    {"Multimedia", VIEW_MULTIMEDIA},
    {"Development", VIEW_DEVELOPMENT},
    {"Other", VIEW_OTHER},
};
static int sidebar_count =
    (int)(sizeof(sidebar_items) / sizeof(sidebar_items[0]));

/* --- filtreleme / gorunur liste ------------------------------------------ */

typedef struct VisibleEntry {
    Package *pkg;
    int index;
} VisibleEntry;

static VisibleEntry visible_list[MAX_PACKAGES];
static int visible_len = 0;

static int pkg_matches_search(const Package *p, const char *search) {
    if (!search || !search[0]) return 1;
    char haystack[MAX_NAME_LEN + MAX_VERSION_LEN + MAX_DESC_LEN + 3];
    snprintf(haystack, sizeof(haystack), "%s %s %s",
             p->name, p->version, p->desc);
    for (char *c = haystack; *c; c++) *c = (char)tolower((unsigned char)*c);
    char needle[256];
    snprintf(needle, sizeof(needle), "%s", search);
    for (char *c = needle; *c; c++) *c = (char)tolower((unsigned char)*c);
    return strstr(haystack, needle) != NULL;
}

static int view_matches(ViewId view, const Package *p) {
    switch (view) {
    case VIEW_ALL: return 1;
    case VIEW_INSTALLED: return p->state == PKG_STATE_INSTALLED;
    case VIEW_UPDATES: return p->has_update;
    case VIEW_GNUCHAN: return p->is_gnuchan;
    case VIEW_SYSTEM: return p->category == CATEGORY_SYSTEM;
    case VIEW_INTERNET: return p->category == CATEGORY_INTERNET;
    case VIEW_GRAPHICS: return p->category == CATEGORY_GRAPHICS;
    case VIEW_MULTIMEDIA: return p->category == CATEGORY_MULTIMEDIA;
    case VIEW_DEVELOPMENT: return p->category == CATEGORY_DEVELOPMENT;
    case VIEW_OTHER: return p->category == CATEGORY_OTHER;
    }
    return 0;
}

static void rebuild_visible_list(void) {
    visible_len = 0;
    for (Package *p = pkg_head; p; p = p->next) {
        if (!view_matches(current_view, p)) continue;
        if (!pkg_matches_search(p, search_text)) continue;
        visible_list[visible_len].pkg = p;
        visible_list[visible_len].index = visible_len;
        visible_len++;
    }
    if (selected_index >= visible_len) selected_index = visible_len - 1;
    if (selected_index < 0 && visible_len > 0) selected_index = 0;

    int item_visible = (LIST_BOTTOM - LIST_TOP) / ITEM_H + 1;
    if (item_visible < 1) item_visible = 1;
    int max_scroll = visible_len - item_visible;
    if (max_scroll < 0) max_scroll = 0;
    if (scroll_offset > max_scroll) scroll_offset = max_scroll;
    if (scroll_offset < 0) scroll_offset = 0;
    if (selected_index >= 0 && selected_index < visible_len) {
        if (selected_index < scroll_offset) {
            scroll_offset = selected_index;
        } else if (selected_index >= scroll_offset + item_visible) {
            scroll_offset = selected_index - item_visible + 1;
        }
    }
}

/* --- cizim --------------------------------------------------------------- */

static void draw_sidebar(Display *dpy, Window win, GC gc) {
    fill_rect(dpy, win, gc, get_color(dpy, COL_SIDEBAR), 0, 0,
              (unsigned int)SIDEBAR_W, (unsigned int)WIN_HEIGHT);
    draw_rect(dpy, win, gc, get_color(dpy, COL_BORDER),
              (unsigned int)SIDEBAR_W, 0, 1, (unsigned int)WIN_HEIGHT);

    int yy = HEADER_H + 20;
    for (int i = 0; i < sidebar_count; i++) {
        int sel = (current_view == sidebar_items[i].view);
        int item_h = 34;
        if (sel) {
            fill_rect(dpy, win, gc, get_color(dpy, COL_SELECT),
                      4, yy - 4, (unsigned int)(SIDEBAR_W - 8), (unsigned int)item_h);
        }
        draw_text(dpy, win, gc, get_color(dpy, COL_TEXT),
                  14, yy, sidebar_items[i].label);

        /* hint */
        char hint[64] = {0};
        if (sidebar_items[i].view == VIEW_UPDATES && update_available) {
            snprintf(hint, sizeof(hint), "(%d)", update_available > 99 ? 99 : update_available);
        } else if (sidebar_items[i].view == VIEW_GNUCHAN && gnuchan_update_count > 0) {
            snprintf(hint, sizeof(hint), "(%d)", gnuchan_update_count);
        }
        if (hint[0]) {
            draw_text(dpy, win, gc, get_color(dpy, COL_TEXT_DIM),
                      SIDEBAR_W - 10 - (int)(strlen(hint) * 6), yy, hint);
        }
        yy += item_h;
    }

    char ver[64];
    snprintf(ver, sizeof(ver), "v%s", GNUCHANSOFTWARECENTER_VERSION);
    draw_text(dpy, win, gc, get_color(dpy, COL_TEXT_DIM),
              14, WIN_HEIGHT - 30, ver);
}

static void draw_header(Display *dpy, Window win, GC gc) {
    fill_rect(dpy, win, gc, get_color(dpy, COL_HEADER), 0, 0,
              (unsigned int)WIN_WIDTH, (unsigned int)HEADER_H);
    draw_rect(dpy, win, gc, get_color(dpy, COL_BORDER),
              0, (unsigned int)HEADER_H, (unsigned int)WIN_WIDTH, 1);

    const char *title = "GnuChanSoftwareCenter";
    draw_text(dpy, win, gc, get_color(dpy, COL_TEXT), 14, HEADER_H / 2 + 6, title);

    int sx = WIN_WIDTH - 300;
    fill_rect(dpy, win, gc, get_color(dpy, COL_BG), sx, 10, 280, 24);
    draw_rect(dpy, win, gc, get_color(dpy, COL_BORDER), sx, 10, 280, 24);
    draw_text(dpy, win, gc, get_color(dpy, COL_TEXT), sx + 6, 10 + 18, search_text);
    if (search_text[0] == '\0') {
        draw_text(dpy, win, gc, get_color(dpy, COL_TEXT_DIM), sx + 6, 10 + 18, "ara...");
    }
}

static void draw_package_list(Display *dpy, Window win, GC gc) {
    int yy = LIST_TOP - scroll_offset * ITEM_H;

    for (int i = 0; i < visible_len; i++) {
        Package *p = visible_list[i].pkg;
        if (yy + ITEM_H < LIST_TOP) { yy += ITEM_H; continue; }
        if (yy > LIST_BOTTOM) break;

        int sel = (i == selected_index);
        if (sel) {
            fill_rect(dpy, win, gc, get_color(dpy, COL_SELECT),
                      (unsigned int)LIST_LEFT, (unsigned int)yy,
                      (unsigned int)(LIST_RIGHT - LIST_LEFT), (unsigned int)ITEM_H);
        } else if (i % 2 == 1) {
            fill_rect(dpy, win, gc, get_color(dpy, 0x20112EU),
                      (unsigned int)LIST_LEFT, (unsigned int)yy,
                      (unsigned int)(LIST_RIGHT - LIST_LEFT), (unsigned int)ITEM_H);
        }

        /* durum noktasi */
        unsigned long dot_col = get_color(dpy, COL_TEXT_DIM);
        if (p->state == PKG_STATE_INSTALLED) dot_col = get_color(dpy, COL_SUCCESS);
        else if (p->has_update) dot_col = get_color(dpy, COL_UPDATE);
        fill_rect(dpy, win, gc, dot_col, LIST_LEFT + 2, yy + 9, 6, 6);

        /* gnuchan isareti */
        if (p->is_gnuchan) {
            draw_text(dpy, win, gc, get_color(dpy, COL_BUTTON),
                      LIST_LEFT + 12, yy + 18, "[G]");
        }

        /* paket adi ve yerel versiyon */
        char label[512];
        if (p->remote_version[0] && version_newer(p->remote_version, p->version)) {
            snprintf(label, sizeof(label), "%s  %s -> %s", p->name, p->version, p->remote_version);
            draw_text(dpy, win, gc, get_color(dpy, COL_TEXT_DIM),
                      LIST_LEFT + (p->is_gnuchan ? 40 : 18), yy + 18, label);
            /* guncelleme var isareti */
            draw_text(dpy, win, gc, get_color(dpy, COL_UPDATE),
                      LIST_RIGHT - 100, yy + 18, "GUNCELLEME");
        } else {
            snprintf(label, sizeof(label), "%s  %s", p->name, p->version);
            draw_text(dpy, win, gc, get_color(dpy, COL_TEXT),
                      LIST_LEFT + (p->is_gnuchan ? 40 : 18), yy + 18, label);
        }

        yy += ITEM_H;
    }

    if (visible_len == 0) {
        draw_text(dpy, win, gc, get_color(dpy, COL_TEXT_DIM),
                  LIST_LEFT, LIST_TOP, "gosterilecek paket yok");
    }
}

static void draw_footer(Display *dpy, Window win, GC gc) {
    int fy = WIN_HEIGHT - FOOTER_H;
    fill_rect(dpy, win, gc, get_color(dpy, COL_FOOTER), 0, fy,
              (unsigned int)WIN_WIDTH, (unsigned int)FOOTER_H);
    draw_rect(dpy, win, gc, get_color(dpy, COL_BORDER),
              0, (unsigned int)fy, (unsigned int)WIN_WIDTH, 1);

    Package *sel_pkg = (selected_index >= 0 && selected_index < visible_len)
        ? visible_list[selected_index].pkg : NULL;

    int bx = LIST_LEFT;
    int by = fy + 6;
    int bw = 100;
    int bh = 24;

    if (sel_pkg) {
        if (sel_pkg->state == PKG_STATE_INSTALLED) {
            fill_rect(dpy, win, gc, get_color(dpy, COL_DANGER),
                      bx, by, bw, bh);
            draw_text(dpy, win, gc, get_color(dpy, 0xFFFFFFU),
                      bx + 14, by + 18, "Kaldir");
        } else {
            fill_rect(dpy, win, gc, get_color(dpy, COL_BUTTON),
                      bx, by, bw, bh);
            draw_text(dpy, win, gc, get_color(dpy, 0xFFFFFFU),
                      bx + 20, by + 18, "Yukle");
        }
        bx += bw + 8;
    }

    int up_w = 140;
    if (update_available) {
        fill_rect(dpy, win, gc, get_color(dpy, COL_SUCCESS),
                  bx, by, up_w, bh);
        draw_text(dpy, win, gc, get_color(dpy, 0xFFFFFFU),
                  bx + 10, by + 18, "Tumunu Guncelle");
    } else {
        fill_rect(dpy, win, gc, get_color(dpy, 0x392150U),
                  bx, by, up_w, bh);
        draw_text(dpy, win, gc, get_color(dpy, COL_TEXT_DIM),
                  bx + 10, by + 18, "Guncelleme Yok");
    }
    bx += up_w + 8;

    int gc_w = 140;
    if (gnuchan_update_count > 0) {
        fill_rect(dpy, win, gc, get_color(dpy, COL_BUTTON),
                  bx, by, gc_w, bh);
        char lbl[64];
        snprintf(lbl, sizeof(lbl), "GnuChan Guncelle (%d)", gnuchan_update_count);
        draw_text(dpy, win, gc, get_color(dpy, 0xFFFFFFU),
                  bx + 8, by + 18, lbl);
    } else if (update_checks_disabled) {
        fill_rect(dpy, win, gc, get_color(dpy, 0x392150U),
                  bx, by, gc_w, bh);
        draw_text(dpy, win, gc, get_color(dpy, COL_TEXT_DIM),
                  bx + 8, by + 18, "Denetim kapali");
    } else if (github_check_done && github_check_failures == 0) {
        fill_rect(dpy, win, gc, get_color(dpy, 0x392150U),
                  bx, by, gc_w, bh);
        draw_text(dpy, win, gc, get_color(dpy, COL_TEXT_DIM),
                  bx + 8, by + 18, "GnuChan Guncel");
    } else if (github_check_done) {
        fill_rect(dpy, win, gc, get_color(dpy, COL_WARN),
                  bx, by, gc_w, bh);
        draw_text(dpy, win, gc, get_color(dpy, COL_BG),
                  bx + 8, by + 18, "GitHub Denetlenemedi");
    } else {
        fill_rect(dpy, win, gc, get_color(dpy, 0x392150U),
                  bx, by, gc_w, bh);
        draw_text(dpy, win, gc, get_color(dpy, COL_TEXT_DIM),
                  bx + 8, by + 18, "GnuChan Denetleniyor...");
    }

    if (status_msg[0]) {
        draw_text(dpy, win, gc, get_color(dpy, COL_TEXT_DIM),
                  LIST_LEFT, fy + FOOTER_H - 8, status_msg);
    }
}

static void redraw(Display *dpy, Window win, GC gc) {
    XClearWindow(dpy, win);
    draw_sidebar(dpy, win, gc);
    draw_header(dpy, win, gc);
    draw_package_list(dpy, win, gc);
    draw_footer(dpy, win, gc);
    XFlush(dpy);
}

/* --- olay dongusu -------------------------------------------------------- */

static void handle_keypress(Display *dpy, Window win, GC gc, XKeyEvent *ev) {
    KeySym ks = XLookupKeysym(ev, 0);
    char buf[16];
    int n = XLookupString(ev, buf, sizeof(buf), &ks, NULL);

    if (busy) return;

    if (ks == XK_Escape) {
        search_text[0] = '\0';
        rebuild_visible_list();
        selected_index = visible_len > 0 ? 0 : -1;
        redraw(dpy, win, gc);
        return;
    }

    if (ks == XK_BackSpace) {
        int len = (int)strlen(search_text);
        if (len > 0) {
            search_text[len - 1] = '\0';
            rebuild_visible_list();
            redraw(dpy, win, gc);
        }
        return;
    }

    if (ks == XK_Return || ks == XK_KP_Enter) {
        if (selected_index >= 0 && selected_index < visible_len) {
            Package *p = visible_list[selected_index].pkg;
            if (p->state == PKG_STATE_INSTALLED) {
                do_remove(p->name);
            } else {
                do_install(p->name);
            }
            pkg_free_all();
            load_installed_packages();
            refresh_update_checks(getenv("HOME") ? getenv("HOME") : "/root");
            rebuild_visible_list();
            redraw(dpy, win, gc);
        }
        return;
    }

    if (ks == XK_Up) {
        if (selected_index > 0) selected_index--;
        if (selected_index < scroll_offset) scroll_offset = selected_index;
        rebuild_visible_list();
        redraw(dpy, win, gc);
        return;
    }
    if (ks == XK_Down) {
        if (selected_index < visible_len - 1) selected_index++;
        int item_visible = (LIST_BOTTOM - LIST_TOP) / ITEM_H + 1;
        if (selected_index >= scroll_offset + item_visible)
            scroll_offset = selected_index - item_visible + 1;
        rebuild_visible_list();
        redraw(dpy, win, gc);
        return;
    }
    if (ks == XK_Page_Up) {
        int item_visible = (LIST_BOTTOM - LIST_TOP) / ITEM_H + 1;
        selected_index -= item_visible;
        if (selected_index < 0) selected_index = 0;
        scroll_offset = selected_index;
        rebuild_visible_list();
        redraw(dpy, win, gc);
        return;
    }
    if (ks == XK_Page_Down) {
        int item_visible = (LIST_BOTTOM - LIST_TOP) / ITEM_H + 1;
        selected_index += item_visible;
        if (selected_index >= visible_len) selected_index = visible_len - 1;
        scroll_offset = selected_index - item_visible + 1;
        rebuild_visible_list();
        redraw(dpy, win, gc);
        return;
    }

    if (n > 0 && isprint((unsigned char)buf[0])) {
        int len = (int)strlen(search_text);
        if (len < (int)sizeof(search_text) - 1) {
            search_text[len] = buf[0];
            search_text[len + 1] = '\0';
            rebuild_visible_list();
            selected_index = visible_len > 0 ? 0 : -1;
            scroll_offset = 0;
            redraw(dpy, win, gc);
        }
    }
}

static void handle_button(Display *dpy, Window win, GC gc, XButtonEvent *ev) {
    if (busy) return;

    int x = ev->x, y = ev->y;

    if (x < SIDEBAR_W && y > HEADER_H) {
        int yy = HEADER_H + 20;
        for (int i = 0; i < sidebar_count; i++) {
            if (y >= yy - 4 && y < yy - 4 + 34) {
                current_view = sidebar_items[i].view;
                selected_index = -1;
                scroll_offset = 0;
                rebuild_visible_list();
                redraw(dpy, win, gc);
                return;
            }
            yy += 34;
        }
        return;
    }

    if (x > WIN_WIDTH - 300 && x < WIN_WIDTH - 20 && y > 10 && y < 34) {
        return;
    }

    if (x >= LIST_LEFT && x < LIST_RIGHT && y >= LIST_TOP && y < LIST_BOTTOM) {
        int rel_y = y - LIST_TOP + scroll_offset * ITEM_H;
        int idx = rel_y / ITEM_H;
        if (idx >= 0 && idx < visible_len) {
            selected_index = idx;
            redraw(dpy, win, gc);
        }
        return;
    }

    int fy = WIN_HEIGHT - FOOTER_H + 6;
    if (y >= fy && y < fy + 24) {
        Package *sel_pkg = (selected_index >= 0 && selected_index < visible_len)
            ? visible_list[selected_index].pkg : NULL;

        if (sel_pkg && x >= LIST_LEFT && x < LIST_LEFT + 100) {
            if (sel_pkg->state == PKG_STATE_INSTALLED) {
                do_remove(sel_pkg->name);
            } else {
                do_install(sel_pkg->name);
            }
            pkg_free_all();
            load_installed_packages();
            refresh_update_checks(getenv("HOME") ? getenv("HOME") : "/root");
            rebuild_visible_list();
            redraw(dpy, win, gc);
            return;
        }

        int bx = LIST_LEFT + 108;
        if (x >= bx && x < bx + 140 && update_available) {
            busy = 1;
            set_status("tum guncellemeler uygulaniyor...");
            redraw(dpy, win, gc);
            do_upgrade_all();
            busy = 0;
            pkg_free_all();
            load_installed_packages();
            refresh_update_checks(getenv("HOME") ? getenv("HOME") : "/root");
            rebuild_visible_list();
            redraw(dpy, win, gc);
            return;
        }

        bx += 148;
        if (x >= bx && x < bx + 140 && gnuchan_update_count > 0) {
            busy = 1;
            set_status("GnuChan programlari guncelleniyor...");
            redraw(dpy, win, gc);
            do_update_gnuchan_all();
            busy = 0;
            pkg_free_all();
            load_installed_packages();
            refresh_update_checks(getenv("HOME") ? getenv("HOME") : "/root");
            rebuild_visible_list();
            redraw(dpy, win, gc);
            return;
        }
    }
}

/* --- ana ----------------------------------------------------------------- */

static void handle_signal(int signum) {
    (void)signum;
    should_stop = 1;
}

int main(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--version") == 0) {
            printf("GnuChanSoftwareCenter %s\n", GNUCHANSOFTWARECENTER_VERSION);
            return 0;
        }
        if (strcmp(argv[i], "--help") == 0) {
            printf("usage: GnuChanSoftwareCenter [--version] [--help]\n"
                   "\nGnuchanOS paket yoneticisi. Tum Debian paketlerini listeler,\n"
                   "guncellemeleri denetler ve GnuChan programlarini gunceller.\n"
                   "Her baslatisida GitHub'dan GnuChan versiyonlarini kontrol eder.\n");
            return 0;
        }
    }

    gnuchan_lang_init();

    /* --- X11 pencere once acilir, arkaplan islemleri sonra ----------------- */

    Display *dpy = XOpenDisplay(NULL);
    if (!dpy) {
        fprintf(stderr, "GnuChanSoftwareCenter: X display acilamadi\n");
        return 1;
    }

    int screen = DefaultScreen(dpy);
    Window root = RootWindow(dpy, screen);

    Window win = XCreateSimpleWindow(dpy, root, 60, 60,
                                     (unsigned int)window_width,
                                     (unsigned int)window_height,
                                     1, BlackPixel(dpy, screen),
                                     get_color(dpy, COL_BG));
    XSizeHints size_hints = {0};
    size_hints.flags = PMinSize;
    size_hints.min_width = 720;
    size_hints.min_height = 480;
    XSetWMNormalHints(dpy, win, &size_hints);
    XSelectInput(dpy, win,
                 ExposureMask | KeyPressMask | ButtonPressMask |
                 StructureNotifyMask);
    XMapWindow(dpy, win);
    XStoreName(dpy, win, "GnuChanSoftwareCenter");
    XFlush(dpy);

    /* Wait for the first Expose event so the window is visible before we
       block on network/dpkg calls. */
    XEvent ev;
    while (XPending(dpy) == 0) {
        struct timespec ts = {0, 10 * 1000 * 1000L};
        nanosleep(&ts, NULL);
    }
    XNextEvent(dpy, &ev);

    GC gc = create_gc(dpy, win);
    redraw(dpy, win, gc);

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_signal;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);

    /* --- paket veritabani ve GitHub versiyon denetimi ---------------------- */

    const char *home = getenv("HOME");
    if (!home || !home[0]) {
        home = "/root";
    }

    pkg_free_all();
    load_installed_packages();
    load_center_settings(home);
    if (show_installed_by_default) {
        current_view = VIEW_INSTALLED;
    }

    refresh_update_checks(home);

    rebuild_visible_list();
    redraw(dpy, win, gc);

    while (!should_stop) {
        if (XPending(dpy) > 0) {
            XNextEvent(dpy, &ev);
            switch (ev.type) {
            case Expose:
                redraw(dpy, win, gc);
                break;
            case KeyPress:
                handle_keypress(dpy, win, gc, &ev.xkey);
                break;
            case ButtonPress:
                handle_button(dpy, win, gc, &ev.xbutton);
                break;
            case ConfigureNotify:
                if (window_width != ev.xconfigure.width ||
                    window_height != ev.xconfigure.height) {
                    window_width = ev.xconfigure.width;
                    window_height = ev.xconfigure.height;
                    rebuild_visible_list();
                    redraw(dpy, win, gc);
                }
                break;
            default:
                break;
            }
        } else {
            struct timespec ts = {0, 50 * 1000 * 1000L};
            nanosleep(&ts, NULL);
        }
    }

    XFreeGC(dpy, gc);
    XDestroyWindow(dpy, win);
    XCloseDisplay(dpy);
    pkg_free_all();
    return 0;
}
