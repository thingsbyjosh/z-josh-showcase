#pragma once
// Bilingual EN/ES. English by DEFAULT (lang 0 = en, 1 = es). PERMANENT project decision:
// all UI text goes through here; no visible text is ever hardcoded in one language.
void SetLang(int lang);          // 0=en (default), 1=es
int  GetLang();
// Localization to ten languages: the eight that aren't compiled in arrive through strings_locale.h.
void SetLangCode(const char* code);
const char* TR(const char* key); // translates with the current global language; returns the key if it doesn't exist
bool TRHas(const char* key);     // does the key exist? (career_mail: probes the _vN/_subj variants)

// Walks the full table (k, en, es). Used by the external app's exporter (career_test
// dump-strings) to dump the engine's i18n bundle so t() can resolve the overlay's keys.
void ForEachString(void (*cb)(const char* key, const char* en, const char* es, void* ctx), void* ctx);
