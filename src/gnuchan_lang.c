/*
 * gnuchan_lang.c — shared language implementation for all Gnuchan programs.
 *
 * Reads ~/.config/GnuChanSettings/system.conf (flat "Key = value" lines,
 * capital True/False booleans) and returns translated UI strings. English is
 * the fallback when a key has no Turkish entry or the setting names English.
 */
#include "gnuchan_lang.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Where GnuChanSettings writes system.conf. */
#define LANG_CONFIG_FILE "~/.config/GnuChanSettings/system.conf"

static char g_lang_name[32] = "English";   /* default */
static int  g_lang_turkish = 0;

/* --- read the ApplicationLanguage setting from system.conf --------------- */

static char *read_whole_file(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long sz = ftell(f);
    if (sz < 0) {
        fclose(f);
        return NULL;
    }
    rewind(f);
    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    size_t rd = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[rd] = '\0';
    if (out_len) {
        *out_len = rd;
    }
    return buf;
}

/* Trim leading/trailing whitespace and surrounding quotes in place. */
static void trim_and_unquote(char *s) {
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') {
        s++;
    }
    char *end = s + strlen(s);
    while (end > s && (end[-1] == ' ' || end[-1] == '\t' ||
                       end[-1] == '\r' || end[-1] == '\n')) {
        end--;
    }
    *end = '\0';
    if (s[0] == '"' && end > s + 1 && s[strlen(s) - 1] == '"') {
        memmove(s, s + 1, strlen(s));          /* drop leading quote */
        end = s + strlen(s);
        end[-1] = '\0';                         /* drop trailing quote */
    }
}

/* Parse flat "Key = value" lines from text, return value for `key` or NULL. */
static const char *find_flat_value(const char *text, const char *key,
                                   char *out, unsigned int out_size) {
    const char *p = text;
    size_t klen = strlen(key);
    while (*p) {
        /* Skip to start of line. */
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        if (*p == '#' || *p == '\n' || *p == '\r' || *p == '\0') {
            if (*p) {
                p++;
            }
            continue;
        }
        /* Does this line start with the key? */
        if (strncmp(p, key, klen) == 0 && p[klen] == ' ') {
            const char *eq = p + klen + 1;
            /* Skip spaces around '='. */
            while (*eq == ' ') {
                eq++;
            }
            if (*eq != '=') {
                /* Not a Key = value line; skip to EOL. */
                while (*p && *p != '\n') {
                    p++;
                }
                continue;
            }
            eq++;
            while (*eq == ' ') {
                eq++;
            }
            /* Copy value up to EOL. */
            unsigned int i = 0;
            while (*eq && *eq != '\n' && *eq != '\r' &&
                   i < out_size - 1) {
                out[i++] = *eq++;
            }
            out[i] = '\0';
            trim_and_unquote(out);
            return out;
        }
        /* Skip to end of line. */
        while (*p && *p != '\n') {
            p++;
        }
        if (*p) {
            p++;
        }
    }
    return NULL;
}

void gnuchan_lang_init(void) {
    char path[512];
    const char *xdg = getenv("XDG_CONFIG_HOME");
    if (xdg && xdg[0]) {
        snprintf(path, sizeof(path), "%s/GnuChanSettings/system.conf", xdg);
    } else {
        const char *home = getenv("HOME");
        if (home && home[0]) {
            snprintf(path, sizeof(path), "%s/.config/GnuChanSettings/system.conf",
                     home);
        } else {
            snprintf(path, sizeof(path), "%s", LANG_CONFIG_FILE);
        }
    }

    size_t len = 0;
    char *text = read_whole_file(path, &len);
    if (!text) {
        return;                                    /* keep English default */
    }

    char value[128];
    const char *found = find_flat_value(text, "ApplicationLanguage",
                                        value, sizeof(value));
    if (found) {
        if (strcmp(found, "Turkish") == 0) {
            snprintf(g_lang_name, sizeof(g_lang_name), "Turkish");
            g_lang_turkish = 1;
        } else {
            snprintf(g_lang_name, sizeof(g_lang_name), "English");
            g_lang_turkish = 0;
        }
    }

    free(text);
}

const char *gnuchan_lang_name(void) {
    return g_lang_name;
}

int gnuchan_lang_is_turkish(void) {
    return g_lang_turkish;
}

/* --- translation tables -------------------------------------------------- */

struct tr_entry {
    const char *key;
    const char *en;
    const char *tr;
};

#define TR(k, e, t) { k, e, t }

static const struct tr_entry tr_table[] = {
    /* --- GnuChanSettings panel --- */
    TR("settings_title", "GnuChanSettings", "GnuChanSettings"),
    TR("settings_subtitle", "Settings", "Ayarlar"),
    TR("settings_help",
       "The desktop's control panel: change the settings of the Gnuchan\n"
       "programs by clicking, not by editing their files. A change is\n"
       "kept on screen until Save writes it, so nothing on disk changes\n"
       "until the button is pressed.",
       "Masaüstün denetim paneli: Gnuchan programlarının ayarlarını dosyaları\n"
       "düzenlemek yerine tıklayarak değiştirebilirsiniz. Bir değişiklik,\n"
       "Kaydet'ye basılana kadar ekranda kalır ve diskte bir şey değişmez.\n"),
    TR("settings_usage_open", "open the panel", "paneli aç"),
    TR("settings_usage_help", "this note", "bu notu göster"),
    TR("settings_usage_version", "the version", "sürümü göster"),
    TR("settings_help_note",
       "Each program's settings live under ~/.config, in the file the\n"
       "program itself reads; this panel is a window over those files.",
       "Her programın ayarları ~/.config altında, programın kendi okuduğu\n"
       "dosyada bulunur; bu panel bu dosyaların üzerindeki bir penceredir.\n"),
    TR("switch_on", "On", "Açık"),
    TR("switch_off", "Off", "Kapalı"),
    TR("list_add", "+", "+"),
    TR("list_remove", "-", "-"),
    TR("dropdown_up", "^", "^"),
    TR("dropdown_down", "v", "v"),
    TR("button_revert", "Revert", "Geri al"),
    TR("button_save", "Save", "Kaydet"),
    TR("status_loaded", "%s settings loaded.", "%s ayarları yüklendi."),
    TR("status_no_file",
       "%s has no settings file; defaults are shown and the file will be "
       "created on save.",
       "%s için ayar dosyası yok; varsayılanlar gösteriliyor ve kaydedince "
       "oluşturulacak."),
    TR("status_saved", "%s saved.", "%s kaydedildi."),
    TR("status_save_failed", "Save failed: %.200s", "Kaydedilemedi: %.200s"),
    TR("status_list_limit",
       "At most %d items can be added.", "En fazla %d öğe eklenebilir."),
    TR("status_list_full", "No more room in the list field.",
       "Liste alanında daha fazla yer yok."),
    TR("error_unknown_argument",
       "%s: unknown argument '%s'", "%s: bilinmeyen bağımsız değişken '%s'"),
    TR("error_cannot_open_display",
       "cannot open the display; DISPLAY is '%s'",
       "ekranı açılamadı; DISPLAY='%s'"),
    TR("error_cannot_install_signals",
       "cannot install signal handlers", "sinyal işleyicileri yüklenemedi"),
    TR("error_cannot_create_window",
       "cannot create the %s window", "%s penceresi oluşturulamadı"),
    TR("error_cannot_start_session",
       "could not start the session", "oturum başlatılamadı"),
    TR("error_wrong_password",
       "wrong user name or password", "kullanıcı adı veya şifre hatalı"),
    TR("error_must_run_as_root",
       "must run as root", "root olarak çalıştırılmalıdır"),

    /* --- GnuChanDM (display manager / login screen) --- */
    TR("dm_version", "GnuChanDM %s", "GnuChanDM %s"),
    TR("dm_usage",
       "Usage:\n  %s              open the login screen\n",
       "Kullanım:\n  %s              giriş ekranını açar\n"),
    TR("dm_cannot_start", "cannot start: no usable X display",
       "başlatılamadı: kullanılabilir X ekranı yok"),
    TR("dm_started",
       "gnuchandm: started (pid %ld, DISPLAY=%s)",
       "gnuchandm: başlatıldı (pid %ld, DISPLAY=%s)"),
    TR("dm_must_run_as_root",
       "gnuchandm: must run as root",
       "gnuchandm: root olarak çalıştırılmalıdır"),
    TR("dm_cannot_install_signals",
       "gnuchandm: cannot install signal handlers",
       "gnuchandm: sinyal işleyicileri yüklenemedi"),
    TR("dm_no_display",
       "gnuchandm: cannot start: no usable X display",
       "gnuchandm: başlatılamadı: kullanılabilir bir X ekranı yok"),
    TR("dm_cannot_start_session",
       "could not start the session", "oturum başlatılamadı"),
    TR("dm_wrong_password",
       "wrong user name or password", "kullanıcı adı veya şifre hatalı"),

    /* --- GnuChanSL (lock screen) --- */
    TR("sl_version", "GnuChanSL %s", "GnuChanSL %s"),
    TR("sl_usage",
       "Usage:\n  %s              lock the screen\n",
       "Kullanım:\n  %s              ekranı kilitler\n"),
    TR("sl_cannot_open_font",
       "cannot open a font; install a monospace font.",
       "bir yazı tipi açılamadı; monospace bir yazı tipi yükleyin."),
    TR("sl_already_locked",
       "the screen is already locked (keyboard %d, pointer %d)",
       "ekran zaten kilitli (klavye %d, fare %d)"),
    TR("sl_cannot_work_out_user",
       "could not work out whose password to ask for; there is no LOGNAME, "
       "USER or account.",
       "hangi hesabın şifresi isteneceği çözülemedi; LOGNAME, USER veya "
       "hesap yok."),
    TR("sl_cannot_open_display",
       "cannot open the display; DISPLAY is '%s'",
       "ekranı açılamadı; DISPLAY='%s'"),

    /* --- GnuChanDock --- */
    TR("dock_help_text",
       "A strip of icons along the bottom of the screen: the settings\n"
       "icon and the terminal always, then the programs that are open.",
       "Ekranın altındaki simge şeridi: ayarlar simgesi ve terminal her zaman,\n"
       "sonra açık programlar."),
    TR("dock_usage_show", "show the dock", "dock'u göster"),
    TR("dock_usage_help", "this note", "bu notu göster"),
    TR("dock_usage_version", "the version", "sürümü göster"),

    /* --- GnuChanRunner --- */
    TR("runner_prompt", "Run:", "Çalıştır:"),
    TR("runner_placeholder", "Type to search programs", "Programları aramak için yazın"),
    TR("runner_empty_message", "Nothing matches", "Hiçbir sonuç yok"),
    TR("runner_help_text",
       "Opens the program launcher: type a few letters of a program's\n"
       "name, press Enter, and it starts. Escape closes it.",
       "Program başlatıcısını açar: bir programın isminin birkaç harfini yazın,\n"
       "Enter'a basın ve başlar. Escape ile kapatılır."),
    TR("runner_config_desc",
       "read the settings at FILE instead of the one under ~/.config/GnuChanRunner/",
       "ayarları ~/.config/GnuChanRunner/ altındaki dosya yerine FILE'dan oku"),
    TR("runner_list_desc", "print the programs the scan found and exit",
       "taranın bulunduğu programları yazdır ve çık"),
    TR("runner_version_desc", "print the version and exit", "sürümü yazdır ve çık"),
    TR("runner_config_needs_file",
       "--config needs a file name", "--config bir dosya adı istiyor"),
    TR("runner_using_defaults", "using the defaults", "varsayılanlar kullanılıyor"),

    /* --- GnuChanTerm --- */
    TR("term_name", "gcl-256color", "gcl-256color"),
    TR("term_cannot_start_shell", "cannot start a shell", "kabuk başlatılamadı"),
    TR("term_too_many_modules", "too many modules", "fazla modül"),
    TR("term_cannot_open_display",
       "cannot open a display", "bir ekran açılamadı"),
    TR("term_fallback_shell", "/bin/bash", "/bin/bash"),

    /* --- GnuChanTop --- */
    TR("top_column_cpu", "CPU", "CPU"),
    TR("top_column_mem", "MEM", "BELLEK"),
    TR("top_column_pid", "PID", "PID"),
    TR("top_column_name", "NAME", "AD"),
    TR("top_column_user", "USER", "KULLANICI"),
    TR("top_column_state", "STATE", "DURUM"),
    TR("top_header_processes", "Processes", "İşlemler"),
    TR("top_help_text",
       "A system monitor: CPU and GPU usage across the top, the process\n"
       "list below — the shape btop has.",
       "Sistem izleyici: CPU ve GPU kullanımı üstte, işlem listesi altta —\n"
       "btop şeklinde."),
    TR("top_usage_run", "run the monitor", "izleyiciyi çalıştır"),
    TR("top_usage_help", "this note", "bu notu göster"),
    TR("top_usage_version", "the version", "sürümü göster"),

    /* --- GnuChanWifi --- */
    TR("wifi_title", "Wi-Fi", "Wi-Fi"),
    TR("wifi_connected", "Connected", "Bağlı"),
    TR("wifi_disconnected", "Disconnected", "Bağlı Değil"),
    TR("wifi_secured", "Secured", "Güvenli"),
    TR("wifi_scan", "Scan", "Tara"),

    /* --- GnuChanNetworkManager --- */
    TR("network_title", "Network", "Ağ"),
    TR("network_interface", "Interface", "Arayüz"),
    TR("network_dns", "DNS", "DNS"),

    /* --- GnuChanNotification --- */
    TR("notif_title", "Notification", "Bildirim"),
    TR("notif_body", "You have a new notification", "Yeni bir bildiriniz var"),

    /* --- GnuChanSS --- */
    TR("ss_version", "GnuChanSS %s", "GnuChanSS %s"),
    TR("ss_usage",
       "Usage:\n  %s              start the screen saver\n",
       "Kullanım:\n  %s              ekran koruyucuyu başlatır\n"),
    TR("ss_config_error", "gnuchanss: %s", "gnuchanss: %s"),
    TR("ss_cannot_open_display",
       "cannot open the display; DISPLAY is '%s'",
       "ekranı açılamadı; DISPLAY='%s'"),
    TR("ss_cannot_install_signals",
       "cannot install signal handlers", "sinyal işleyicileri yüklenemedi"),
    TR("ss_cannot_create_window",
       "cannot create the %s window", "%s penceresi oluşturulamadı"),

    /* --- GnuChanFetch --- */
    TR("fetch_os", "OS", "İŞLETİM SİSTEMİ"),
    TR("fetch_kernel", "Kernel", "ÇEKÇEK"),
    TR("fetch_uptime", "Uptime", "ÇALIŞMA SÜRESİ"),
    TR("fetch_packages", "Packages", "PAKETLER"),
    TR("fetch_shell", "Shell", "KABUK"),
    TR("fetch_resolution", "Resolution", "ÇÖZÜNÜRLÜK"),
    TR("fetch_de", "Desktop", "MASAÜSTÜ"),
    TR("fetch_wm", "WM", "WM"),
    TR("fetch_terminal", "Terminal", "TERMINAL"),

    /* --- GnuChanWM --- */
    TR("wm_active_border", "Active border", "Etkin kenar"),
    TR("wm_inactive_border", "Inactive border", "Etkisiz kenar"),
    TR("wm_desktop_colour", "Desktop colour", "Masaüstü rengi"),
    TR("wm_wallpaper", "Wallpaper", "Duvar kağıdı"),
    TR("wm_switcher_background", "Switcher background", "Değiştirici arka planı"),
    TR("wm_startup_failed",
       "gnuchanwm: start-up failed; opening a terminal with the log.",
       "gnuchanwm: başlatma başarısız oldu; günlüğü gösteren bir terminal açılıyor."),
    TR("wm_see_log", "gnuchanwm: see %s for what went wrong.",
       "gnuchanwm: neyin yanlış gittiği için %s'e bakın."),
    TR("wm_started",
       "gnuchanwm: started (pid %ld, DISPLAY=%s)",
       "gnuchanwm: başlatıldı (pid %ld, DISPLAY=%s)"),
    TR("wm_no_display",
       "gnuchanwm: cannot start: no usable X display.",
       "gnuchanwm: başlatılamadı: kullanılabilir bir X ekranı yok."),

    /* --- GnuChanBrowser (Qt) --- */
    TR("browser_new_tab", "New tab", "Yeni sekme"),
    TR("browser_search_placeholder", "Search DuckDuckGo or type an address",
       "DuckDuckGo'da ara veya bir adres yazın"),
    TR("browser_bookmark", "Bookmark this page", "Bu sayfaya yer imi ekle"),
    TR("browser_loading", "Loading %1%", "Yükleniyor %1%"),
    TR("browser_load_failed", "Load failed", "Yükleme başarısız"),

    /* --- GnuChanFetch --- */
    TR("fetch_os", "OS", "İŞLETİM SİSTEMİ"),
    TR("fetch_kernel", "Kernel", "ÇEKÇEK"),
    TR("fetch_uptime", "Uptime", "ÇALIŞMA SÜRESİ"),
    TR("fetch_packages", "Packages", "PAKETLER"),
    TR("fetch_shell", "Shell", "KABUK"),
    TR("fetch_resolution", "Resolution", "ÇÖZÜNÜRLÜK"),
    TR("fetch_de", "Desktop", "MASAÜSTÜ"),
    TR("fetch_wm", "WM", "WM"),
    TR("fetch_terminal", "Terminal", "TERMINAL"),

    { NULL, NULL, NULL }
};

const char *gnuchan_lang_t(const char *key) {
    if (!key) {
        return "";
    }
    for (unsigned int i = 0; tr_table[i].key; i++) {
        if (strcmp(tr_table[i].key, key) == 0) {
            return g_lang_turkish ? tr_table[i].tr : tr_table[i].en;
        }
    }
    return key; /* return the key itself as English fallback */
}
