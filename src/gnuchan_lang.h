/*
 * gnuchan_lang.h — shared language for all Gnuchan programs.
 *
 * Reads the ApplicationLanguage setting from
 * ~/.config/GnuChanSettings/system.conf (written by GnuChanSettings) and
 * provides translated UI strings so every program automatically speaks the
 * language the person chose in the settings panel.
 *
 * Include this header and link gnuchan_lang.c into any program that wants
 * translated UI text. Call gnuchan_lang_init() once at startup, then use
 * gnuchan_lang_t("key") wherever a hardcoded English string was used.
 */
#ifndef GNUCHAN_LANG_H
#define GNUCHAN_LANG_H

#ifdef __cplusplus
extern "C" {
#endif

/* Read ~/.config/GnuChanSettings/system.conf and set the current language.
   Safe to call before opening a display. */
void gnuchan_lang_init(void);

/* The current language name: "English" or "Turkish". */
const char *gnuchan_lang_name(void);

/* Nonzero when Turkish is active. */
int gnuchan_lang_is_turkish(void);

/* Translate a UI string key to the current language. Returns the English
   original when no translation exists or the key is unknown. */
const char *gnuchan_lang_t(const char *key);

#ifdef __cplusplus
}
#endif

#endif /* GNUCHAN_LANG_H */
