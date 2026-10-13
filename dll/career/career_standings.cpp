#include "career_standings.h"
#include "career_series.h"   // E6: PointsById (period-correct scoring in StandingsTable)
#include "paths.h"           // Encoding fix (22/07): FOpen/RemoveFile/RenameFile (UTF-8 paths -> wide-char APIs)
#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace career {

// local helper: splits a line on '|' (drops CR/LF).
static std::vector<std::string> SplitPipe(const char* line) {
    std::vector<std::string> out; std::string cur;
    for (const char* p = line; *p; ++p) {
        char c = *p;
        if (c == '\r' || c == '\n') continue;
        if (c == '|') { out.push_back(cur); cur.clear(); }
        else cur += c;
    }
    out.push_back(cur);
    return out;
}

void ApplyRound(Standings& s, int round, const std::vector<RoundScore>& scores) {
    for (const auto& sc : scores) {
        if (!sc.driver_id.empty()) s.ids[sc.name] = sc.driver_id;   // 18/09: who that row is (namesakes)
        // a driver scores ONCE per round. A grid with a duplicated driver (the same name in
        // two cars) landed here twice and, since points accumulate by name, the table added both
        // finishing positions. The duplicate in the grid is an upstream problem, but the table shouldn't
        // amplify it: if the round already has an entry for this name, the BEST result is kept (more
        // points; on a tie, the better position). Rounds already saved with a double entry aren't rewritten.
        auto& rounds = s.drivers[sc.name];
        bool seen = false;
        for (auto& dr : rounds) {
            if (dr.round != round) continue;
            seen = true;
            if (sc.points > dr.points || (sc.points == dr.points && sc.pos < dr.pos))
                dr = { round, sc.pos, sc.points, sc.fl, sc.dnf };
            break;
        }
        if (!seen) rounds.push_back({ round, sc.pos, sc.points, sc.fl, sc.dnf });
    }
    if (round > s.round) s.round = round;
}

bool RenameDriverInStandings(Standings& s, const std::string& oldName, const std::string& newName) {
    // see header. Merges by round with the criterion (best result if the round clashes).
    if (oldName.empty() || newName.empty() || oldName == newName) return false;
    auto itOld = s.drivers.find(oldName);
    if (itOld == s.drivers.end()) {
        // no rounds, but a legacy contact may be left hanging off the old name
        auto c = s.contacts.find(oldName);
        if (c == s.contacts.end()) return false;
        s.contacts[newName] += c->second; s.contacts.erase(c);
        return true;
    }
    std::vector<DriverRound>& dst = s.drivers[newName];        // creates it if it didn't exist
    for (const DriverRound& dr : itOld->second) {
        DriverRound* have = nullptr;
        for (auto& d : dst) if (d.round == dr.round) { have = &d; break; }
        if (!have) { dst.push_back(dr); continue; }
        const bool better = dr.points > have->points ||
                            (dr.points == have->points && dr.pos > 0 &&
                             (have->pos <= 0 || dr.pos < have->pos));
        if (better) *have = dr;
    }
    std::sort(dst.begin(), dst.end(), [](const DriverRound& a, const DriverRound& b){ return a.round < b.round; });
    s.drivers.erase(oldName);
    { auto i = s.ids.find(oldName);                        // 18/09: the id travels with the row
      if (i != s.ids.end()) { if (!s.ids.count(newName)) s.ids[newName] = i->second; s.ids.erase(i); } }
    auto c = s.contacts.find(oldName);
    if (c != s.contacts.end()) { s.contacts[newName] += c->second; s.contacts.erase(c); }
    return true;
}

bool MergePlayerKeyInStandings(Standings& s, const std::string& oldName, const std::string& newName) {
    if (oldName.empty() || newName.empty() || oldName == newName) return false;
    const auto itOld = s.drivers.find(oldName);
    if (itOld == s.drivers.end()) return false;
    const auto idOld = s.ids.find(oldName), idNew = s.ids.find(newName);
    if (idOld != s.ids.end() && idNew != s.ids.end() && !idOld->second.empty() && !idNew->second.empty()
        && idOld->second != idNew->second) return false;                    // two different ids: two people
    const auto itNew = s.drivers.find(newName);
    if (itNew != s.drivers.end())
        for (const DriverRound& a : itOld->second)
            for (const DriverRound& b : itNew->second)
                if (a.round == b.round) return false;                       // same round twice: two people
    return RenameDriverInStandings(s, oldName, newName);
}

// 18/09: a row's id, if the table knows it ("" in tables saved before it was stored)
static std::string IdDeFila(const Standings& s, const std::string& nombre) {
    const auto it = s.ids.find(nombre);
    return it == s.ids.end() ? std::string() : it->second;
}

std::vector<StandingRow> FinalTable(const Standings& s, int dropWorst) {
    std::vector<StandingRow> rows;
    for (const auto& kv : s.drivers) {
        StandingRow row; row.name = kv.first; row.played = (int)kv.second.size(); row.id = IdDeFila(s, kv.first);
        std::vector<int> pts;
        for (const auto& dr : kv.second) { pts.push_back(dr.points); if (dr.pos == 1 && !dr.dnf) row.wins++; }
        std::sort(pts.begin(), pts.end());                 // ascending: worst first
        int start = dropWorst > 0 ? dropWorst : 0;
        if (start > (int)pts.size()) start = (int)pts.size();
        for (size_t i = (size_t)start; i < pts.size(); i++) row.total += pts[i];
        rows.push_back(row);
    }
    std::sort(rows.begin(), rows.end(), [](const StandingRow& a, const StandingRow& b) {
        if (a.total != b.total) return a.total > b.total;
        if (a.wins  != b.wins)  return a.wins  > b.wins;
        return a.name < b.name;                            // deterministic tiebreak (could be improved in CH3)
    });
    return rows;
}

std::vector<ChampRow> ChampStandings(const Standings& s) {
    std::vector<ChampRow> rows;
    for (const auto& kv : s.drivers) {
        ChampRow row; row.name = kv.first; row.id = IdDeFila(s, kv.first);
        // a copy of the rounds sorted by round number (deterministic, independent of insertion order)
        std::vector<DriverRound> rs = kv.second;
        std::sort(rs.begin(), rs.end(),
                  [](const DriverRound& a, const DriverRound& b) { return a.round < b.round; });
        for (const auto& dr : rs) {
            int p = PtsFor(dr.pos);
            row.ptsPerRound.push_back(p);
            row.total += p;
            if (dr.pos == 1) row.wins++;
            if (dr.pos >= 1 && dr.pos <= 3) row.podiums++;
        }
        rows.push_back(row);
    }
    std::sort(rows.begin(), rows.end(), [](const ChampRow& a, const ChampRow& b) {
        if (a.total != b.total) return a.total > b.total;
        if (a.wins  != b.wins)  return a.wins  > b.wins;
        return a.name < b.name;                              // deterministic tiebreak (same as FinalTable)
    });
    for (int i = 0; i < (int)rows.size(); i++) rows[i].rank = i + 1;
    return rows;
}

std::vector<StandingRow> StandingsTable(const Standings& s, int dropWorst) {
    const PointsSystem* sys = PointsById(s.points_system);   // nullptr if the era is unknown
    std::vector<StandingRow> rows;
    for (const auto& kv : s.drivers) {
        StandingRow row; row.name = kv.first; row.played = (int)kv.second.size(); row.id = IdDeFila(s, kv.first);
        std::vector<int> pts;
        for (const auto& dr : kv.second) {
            int p;
            if (sys && !dr.dnf && dr.pos >= 1 && dr.pos <= (int)sys->points.size())
                p = sys->points[dr.pos - 1];                 // period-correct: re-derived from points_id
            else if (sys)
                p = 0;                                       // known era but out of the table / DNF -> 0
            else
                p = dr.points;                               // unknown era -> respects the stored value
            pts.push_back(p);
            if (dr.pos == 1 && !dr.dnf) row.wins++;
        }
        std::sort(pts.begin(), pts.end());                   // ascending: worst first (for dropWorst)
        int start = dropWorst > 0 ? dropWorst : 0;
        if (start > (int)pts.size()) start = (int)pts.size();
        for (size_t i = (size_t)start; i < pts.size(); i++) row.total += pts[i];
        rows.push_back(row);
    }
    std::sort(rows.begin(), rows.end(), [](const StandingRow& a, const StandingRow& b) {
        if (a.total != b.total) return a.total > b.total;
        if (a.wins  != b.wins)  return a.wins  > b.wins;
        return a.name < b.name;
    });
    return rows;
}

bool SaveStandings(const Standings& s, const char* path) {
    char tmp[512]; snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE* f = paths::FOpen(tmp, "wb");
    if (!f) return false;
    fprintf(f, "champ|%s|%s|%d|%d\r\n",
            s.node_id.c_str(), s.points_system.c_str(), s.season_year, s.round);
    for (const auto& kv : s.drivers)
        for (const auto& dr : kv.second)
            fprintf(f, "r|%s|%d|%d|%d|%d|%d\r\n",
                    kv.first.c_str(), dr.round, dr.pos, dr.points, dr.fl ? 1 : 0, dr.dnf ? 1 : 0);
    for (const auto& kv : s.contacts)
        if (kv.second != 0)
            fprintf(f, "c|%s|%d\r\n", kv.first.c_str(), kv.second);   // reserved (E6 legacy, see career_standings.h)
    for (const auto& kv : s.ids)                                             // 18/09: who each row is
        if (!kv.second.empty() && s.drivers.count(kv.first))
            fprintf(f, "i|%s|%s\r\n", kv.first.c_str(), kv.second.c_str());
    fclose(f);
    return paths::ReplaceFileAtomic(tmp, path);   // single-step replace (see paths::ReplaceFileAtomic)
}

bool LoadStandings(Standings& s, const char* path) {
    FILE* f = paths::FOpen(path, "rb");
    if (!f) return false;
    s = Standings{};                    // clean defaults
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        auto t = SplitPipe(line);
        if (t.empty()) continue;
        if (t[0] == "champ" && t.size() >= 5) {
            s.node_id = t[1]; s.points_system = t[2];
            s.season_year = atoi(t[3].c_str()); s.round = atoi(t[4].c_str());
        } else if (t[0] == "r" && t.size() >= 7) {
            DriverRound dr;
            dr.round = atoi(t[2].c_str()); dr.pos = atoi(t[3].c_str());
            dr.points = atoi(t[4].c_str()); dr.fl = atoi(t[5].c_str()) != 0; dr.dnf = atoi(t[6].c_str()) != 0;
            s.drivers[t[1]].push_back(dr);
        } else if (t[0] == "i" && t.size() >= 3) {
            if (!t[2].empty()) s.ids[t[1]] = t[2];                   // 18/09: who that row is
        } else if (t[0] == "c" && t.size() >= 3) {
            s.contacts[t[1]] = atoi(t[2].c_str());                    // reserved (E6 legacy, see career_standings.h)
        }
    }
    fclose(f);
    return true;
}

} // namespace career
