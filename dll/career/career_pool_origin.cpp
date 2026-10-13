#include "career_pool_origin.h"
#include "paths.h"
#include "career_text.h"   // 23/09: CleanNameSpaces — the name, without hard spaces
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace career {

// Port 0.7.0: in f0 this was `DriverProfileHash`, from `career_profiles` -- a module that 0.7.0
//  retired for not having a single consumer in production. It is the SAME FNV-1a-32 and gives the SAME number: the
//  curation of `pool_origin.psv` was done with it, and changing it would silently throw its 1,488 rows on the floor.
unsigned PoolOriginHash(const std::string& driverId) {
    unsigned h = 2166136261u;
    for (char c : driverId) { h ^= (unsigned char)c; h *= 16777619u; }
    return h;
}

namespace {

std::map<unsigned, PoolOrigin> g_origen;

// The same idiom as the engine's other loaders: fopen rb -> fgets -> split '|' -> guard.
std::vector<std::string> SplitPipe(const char* line) {
    std::vector<std::string> out;
    std::string cur;
    for (const char* p = line; *p; ++p) {
        if (*p == '\r' || *p == '\n') break;
        if (*p == '|') { out.push_back(cur); cur.clear(); }
        else cur.push_back(*p);
    }
    out.push_back(cur);
    for (auto& s : out) {   // trim whitespace on both sides
        size_t a = 0, b = s.size();
        while (a < b && (s[a] == ' ' || s[a] == '\t')) ++a;
        while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t')) --b;
        s = s.substr(a, b - a);
    }
    return out;
}

std::string Minus(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

int ConfianzaDe(const std::string& s) {
    const std::string m = Minus(s);
    if (m == "alta" || m == "high")  return 3;
    if (m == "media" || m == "medium") return 2;
    if (m == "baja" || m == "low")   return 1;
    return 0;
}

// "era", "retirado/a", "leyenda" as WORDS (not as part of another: "carrera" doesn't count).
bool NotaDeEpoca(const std::string& note) {
    const std::string m = Minus(note);
    std::string w;
    auto es = [&](const std::string& x) { return x == "era" || x == "retirado" || x == "retirada" || x == "leyenda" || x == "retired" || x == "legend"; };
    for (size_t i = 0; i <= m.size(); ++i) {
        const bool fin = (i == m.size()) || !(std::isalnum((unsigned char)m[i]) || m[i] == '_');
        if (fin) { if (!w.empty() && es(w)) return true; w.clear(); }
        else w.push_back(m[i]);
    }
    return false;
}

}  // anon

bool LoadPoolOrigin(const char* path) {
    FILE* f = paths::FOpen(path, "rb");
    g_origen.clear();
    if (!f) return false;   // doesn't open -> empty table (the world as before), no crash
    char line[2048];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#') continue;
        const std::vector<std::string> t = SplitPipe(line);
        if (t.size() < 3) continue;                         // key, name and class at the very least
        if (t[0] == "name_hash" || t[0].empty() || t[2].empty()) continue;
        char* end = nullptr;
        const unsigned long key = strtoul(t[0].c_str(), &end, 10);
        if (end == t[0].c_str() || *end != '\0') continue;  // col0 isn't a clean integer -> skip
        PoolOrigin o;
        o.name       = CleanNameSpaces(t[1]);
        o.home_class = t[2];
        o.confidence = t.size() > 3 ? ConfianzaDe(t[3]) : 0;
        o.note       = t.size() > 4 ? t[4] : std::string();
        o.era        = NotaDeEpoca(o.note);
        g_origen[(unsigned)key] = o;
    }
    fclose(f);
    return true;
}

int PoolOriginCount() { return (int)g_origen.size(); }

const PoolOrigin* PoolOriginForHash(unsigned nameHash) {
    const auto it = g_origen.find(nameHash);
    return it == g_origen.end() ? nullptr : &it->second;
}

const PoolOrigin* PoolOriginFor(const std::string& driverId) {
    if (g_origen.empty() || driverId.empty()) return nullptr;
    return PoolOriginForHash(PoolOriginHash(driverId));
}

}  // namespace career
