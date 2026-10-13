#include "career_dda_offsets.h"
#include "paths.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace career { namespace dda {

namespace {
std::map<std::string, std::map<long, int>> g_offsets;

// Local splitter: doesn't depend on career_fields.cpp's SplitPipe, so this file doesn't drag anything in.
std::vector<std::string> SplitPipeLocal(const char* line) {
    std::vector<std::string> out;
    std::string cur;
    for (const char* p = line; *p; ++p) {
        if (*p == '|') { out.push_back(cur); cur.clear(); }
        else if (*p != '\r' && *p != '\n') cur += *p;
    }
    out.push_back(cur);
    return out;
}

int CapOffset(int v) {
    if (v >  kOffsetCap) return  kOffsetCap;
    if (v < -kOffsetCap) return -kOffsetCap;
    return v;
}
}  // anon

bool LoadClassDifficulty(const char* path) {
    g_offsets.clear();
    FILE* f = paths::FOpen(path, "rb");
    if (!f) return false;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\r' || line[0] == '\n' || line[0] == '\0') continue;
        std::vector<std::string> t = SplitPipeLocal(line);
        if (t.size() < 3) continue;
        if (t[0].empty() || t[0] == "class_id") continue;             // header
        long  th  = strtol(t[1].c_str(), nullptr, 10);
        int   off = CapOffset(atoi(t[2].c_str()));
        g_offsets[t[0]][th] = off;
    }
    fclose(f);
    return true;
}

void ReplaceClassDifficulty(std::map<std::string, std::map<long, int>> m) {
    for (auto& c : m)
        for (auto& t : c.second) t.second = CapOffset(t.second);
    g_offsets = std::move(m);
}

// Sums the INNER maps (class -> track -> offset): the outer map only has one entry per CLASS,
// so counting it would give class counts, not PSV rows (one row per track + one row for the whole class).
int ClassDifficultyRowCount() {
    int n = 0;
    for (const auto& c : g_offsets) n += (int)c.second.size();
    return n;
}

int ClassOffsetFor(const std::string& classId) { return TrackOffsetFor(classId, 0L); }

int TrackOffsetFor(const std::string& classId, long trackHash) {
    auto c = g_offsets.find(classId);
    if (c == g_offsets.end()) return 0;
    auto t = c->second.find(trackHash);
    return t != c->second.end() ? t->second : 0;
}

}}  // namespace career::dda
