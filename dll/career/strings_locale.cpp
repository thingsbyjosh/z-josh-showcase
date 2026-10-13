#include "strings_locale.h"
#include "strings.h"
#include "paths.h"
#include "json_min.h"
#include <map>
#include <string>

// The layer lives in memory for the whole session: it's dozens of strings and it's queried on every
// frame of the cover. A std::map<std::string,std::string> is plenty and drags in no dependencies.
static std::map<std::string, std::string> g_overlay;

void ResetLangOverlayForTest() { g_overlay.clear(); }

const char* LangOverlayLookup(const char* key) {
    if (g_overlay.empty() || !key) return nullptr;   // EN/ES: goes out through here, zero cost
    auto it = g_overlay.find(key);
    return it == g_overlay.end() ? nullptr : it->second.c_str();
}

void SetLangCode(const char* code) {
    g_overlay.clear();
    std::string c = code ? code : "";
    // The two compiled-in ones keep going through the table; nobody else changes path.
    if (c.empty() || c == "en") { SetLang(0); return; }
    if (c == "es") { SetLang(1); return; }

    // Any other: base English (the table) + the layer on top. If the file isn't there, the language
    // comes out in English, which is exactly what's wanted from a half-translated language.
    // jsonm::ParseFile opens through the wide API (paths::FOpen), so a path with accents doesn't break.
    SetLang(0);
    jsonm::ValuePtr root = jsonm::ParseFile(paths::Data("i18n\\" + c + ".json").c_str());
    if (!root || !root->isObject()) return;
    for (const auto& kv : root->obj)
        if (kv.second && kv.second->isString() && !kv.second->str.empty())
            g_overlay[kv.first] = kv.second->str;
}
