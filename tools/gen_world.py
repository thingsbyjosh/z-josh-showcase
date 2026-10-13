"""A deterministic synthetic series corpus, shaped like the real catalogue and invented throughout.

The engine loads its world from PSV tables. The real ones are extracted from the game's own files, so
they are derivative content and cannot be published. This generator produces tables with the same
columns and plausible values, from a fixed seed, so that anyone cloning the public repository can
regenerate the exact world the demo runs on.
"""
from __future__ import annotations

import argparse
import random
from pathlib import Path

FILES = ("series.psv", "series_access.psv", "series_calendar.psv",
         "series_points.psv", "series_rosters.psv",
         "career_ladder.psv", "ams2_vehclass.psv", "ams2_car_class.csv",
         "ams2_class_hash.psv", "ams2_liveries.psv", "ams2_series_econ.psv", "ams2_track_slots.psv",
         "circuits_ref.psv", "dlc_products.psv", "feeder_team_map.psv", "livery_team_equiv_packgrids.psv",
         "series_cars.psv", "series_config_profiles.psv", "series_market.psv", "series_roster.psv",
         "career/f3_authored_profiles.psv", "career/seat_index_068.psv", "career/team_cars.psv",
         "career/team_pairing.psv", "skins/livery_resolved.psv", "series_fields.psv")

# Deliberately NOT generated: ams2_vehicle_prices.psv (the engine has per-phase price defaults) and
# ams2_ladder_layout.psv (user drag edits only). The eighteen catalogues in `_catalogues` below exist
# because the harness's tests cross them (class hash <-> livery <-> team <-> seat); they are invented too.
_CARS_PER_CLASS = 3


def disc_for_series(profile_id: str, class_name: str) -> str:
    """The discipline the engine gives a series: career_series.cpp's DiscForSeries, keyword branch.

    The calendar and the season rail colour a series by DiscForSeries; the tree groups its nodes by the
    ladder's own discipline column. If the two disagree the same championship shows as two different
    families on two screens, so the ladder takes exactly what the engine would answer. DiscForSeries
    first consults a curated table keyed by real series ids -- none of which this world uses -- and then
    matches keywords in "<profile_id> <class_name>", lower-cased, in this order, defaulting to single.
    test_dispatch.mjs checks the result against the engine itself, so a drift here fails there."""
    s = f"{profile_id} {class_name}".lower()
    has = lambda *keys: any(k in s for k in keys)                       # noqa: E731
    if has("kart", "academy", "oakfield_motors", "blackley_motors"):
        return "entry"
    if has("endur", "ivory_cup_12573", "nimbus_masters_5890", "pemberton_zel121", "lmp", "dpi", "group_c", "proto", "hypercar", "merrowdown_myr373"):
        return "endurance"
    if has("stock", "harbor_challenge_8227", "copa", "greyham_motors", "truck", "touring", "v8"):
        return "touring"
    if has("formula", "f1", "fhybrid", "ultimate", "indy", "garrowby", "vee", "inter", "fusa", "single"):
        return "single"
    if has("gt"):
        return "gt"
    if has("rally", "dirt", "offroad", "cross"):
        return "offroad"
    return "single"

# Invented vocabulary. Nothing here is taken from the real catalogue.
_PREFIX = ("Meridian", "Northgate", "Sablefield", "Verdant", "Kestrel", "Halcyon",
           "Ironmarch", "Copperline", "Solstice", "Lowtide", "Ravenhurst", "Amberfall")
_KIND = ("Cup", "Trophy", "Challenge", "Series", "Championship")
_CLASS = ("Prototype", "Tourer", "Openwheel", "Roadster", "Sport")
_TRACK = ("Ashcombe", "Bellhaven", "Cindermoor", "Drummond Park", "Ellerby", "Fairwater",
          "Glasshouse", "Holloway", "Ilminster", "Junction Bay", "Kelvedon", "Lyndhurst",
          "Marchwood", "Netherby", "Oakhanger", "Pentridge")
# RULING 66: "Enzo" replaced -- of every real name this invented list could have picked, it is the
# single most motorsport-loaded given name in existence, and this repository's whole premise is that
# nothing in its world is real.
_FIRST = ("Aldo", "Bram", "Cato", "Dara", "Edran", "Fenn", "Gero", "Hale", "Ivo", "Jory",
          "Kell", "Lasse", "Mirko", "Nael", "Osk", "Piet", "Quill", "Roan", "Sten", "Tarn")
_LAST = ("Abbot", "Bragg", "Corliss", "Danforth", "Ewart", "Fairbrass", "Gantry", "Havel",
         "Ibbotson", "Jarrow", "Kemble", "Lorimer", "Mowbray", "Naismith", "Ockley", "Prewitt")

_MONTH = ("Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec")
_MONTH_LEN = (31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31)


def _date_for_doy(doy: int) -> str:
    """Day-of-year -> `7 May`, matching the format the calendar table uses."""
    remaining = doy
    for month, length in zip(_MONTH, _MONTH_LEN):
        if remaining <= length:
            return f"{remaining} {month}"
        remaining -= length
    return f"{remaining} Dec"


def _track_hash(name: str) -> int:
    """A stable hash of the invented name. Never a value copied from the real catalogue."""
    h = 2166136261
    for ch in name:
        h = ((h ^ ord(ch)) * 16777619) & 0xFFFFFFFF
    return h - (1 << 32) if h >= (1 << 31) else h


def _tier_for_level(level: int) -> str:
    return "rookie" if level <= 2 else "semipro" if level <= 4 else "pro" if level <= 6 else "elite"


def _nl(rows: list[str]) -> str:
    return "\n".join(rows) + "\n"


def _ladder_and_catalogue(seed: int, series_meta, display_of, unlockers_of):
    """Invented ladder + car catalogue. A separate RNG keeps the series tables byte-identical to before.

    Each series node agrees with its series row: same level, same display, the discipline the engine
    derives for it, and -- when the series is gated by an unlock -- exactly the series that unlock it
    as its prerequisites, so the tree draws the door the season rail names. An ungated node keeps one
    lower-level node as its prerequisite (the level gate is what actually holds it)."""
    rng = random.Random(seed ^ 0x5EED_CA55)
    cars_of, used_names, used_hashes = {}, set(), set()
    for _sid, class_name, _lvl in series_meta:
        if class_name in cars_of:
            continue
        cars = []
        while len(cars) < _CARS_PER_CLASS:
            name = f"{rng.choice(_PREFIX)} {rng.choice(_LAST)} {rng.randint(2, 9)}0{rng.choice('ABCRS')}"
            h = _track_hash("veh:" + name)
            if name in used_names or h in used_hashes or h == 0:
                continue
            used_names.add(name); used_hashes.add(h)
            cars.append((h, name))
        cars_of[class_name] = cars

    ladder = ["node_id|display|discipline|level|tier|prereqs|classes|note|dlc|pack|vehid"]
    placed = []                                    # (sid, level) of nodes already emitted, by level
    for sid, class_name, level in sorted(series_meta, key=lambda m: m[2]):
        lower = [s for s, lv in placed if lv < level]
        prereqs = rng.choice(lower) if lower else ""          # drawn always: the RNG sequence stays put
        if unlockers_of.get(sid):
            prereqs = ",".join(unlockers_of[sid])
        disc = disc_for_series(str(level), class_name)        # series.psv's profile_id is the bare level
        ladder.append(f"{sid}|{display_of[sid]}|{disc}|{level}|{_tier_for_level(level)}|{prereqs}|"
                      f"{class_name}||base||{cars_of[class_name][0][0]}")
        placed.append((sid, level))

    # One node of a class the catalogue has no car for -- an add-on class that is not installed, which is
    # what a real ladder carries for every DLC the player lacks. LoadLadder counts it zero cars, and the
    # tree has to answer anyway (test_dispatch.mjs holds it to that). Level 6, above every series, so
    # it never shadows a door in. Drawn after everything above, so the other rows stay as they were.
    top = max(lv for _s, _c, lv in series_meta)
    dlc_class = f"{rng.choice(_CLASS)}{top + 1}"
    dlc_prefix = rng.choice(_PREFIX)
    prereq = rng.choice([s for s, lv in placed if lv == top])
    ladder.append(f"{dlc_prefix.lower()}_addon|{dlc_prefix} {dlc_class.rstrip('0123456789')} Masters|"
                  f"{disc_for_series(str(top + 1), dlc_class)}|{top + 1}|{_tier_for_level(top + 1)}|"
                  f"{prereq}|{dlc_class}||dlc||0")

    vehclass = ["veh_hash|class_name|vehicle_name"]
    car_class = ["veh_hash,vehicle_name,class_name"]
    for class_name, cars in cars_of.items():
        for h, name in cars:
            vehclass.append(f"{h}|{class_name}|{name}")
            car_class.append(f"{h},{name},{class_name}")
    return _nl(ladder), _nl(vehclass), _nl(car_class), cars_of


# ---------------------------------------------------------------------------------------------------
# The eighteen cross-referenced catalogues. Every name below is invented; every hash is an FNV-1a of an
# invented string. They close on each other the way the real ones do: a class's hash, its liveries, the
# team behind each livery, the driver seated in it and the car it drives all name the same thing.
# ---------------------------------------------------------------------------------------------------
_TEAM_SUFFIX = ("Works", "Foundry", "Collective", "Syndicate")
_LIVERIES_PER_CLASS = 28          # >= the longest roster the series tables draw (16..27 slots)
_COUNTRY_CODE = ("NRV", "ALD", "KEM", "SOL", "MAR")
_SYNTH_COUNTRY = ("Norvalia", "Aldermere", "Kemmerland")
_ARCHETYPE = ("apex", "metronomo", "cargador", "solido")


def _uhash(name: str) -> int:
    """Unsigned 32-bit hash of an invented string, never 0 (class hashes are unsigned in the real table)."""
    return (_track_hash(name) & 0xFFFFFFFF) or 1


def _livery_team(class_name: str, i: int) -> str:
    t = (i // 2) + sum(map(ord, class_name))          # a different team line-up per class
    return f"{_PREFIX[t % len(_PREFIX)]} {_TEAM_SUFFIX[(t // len(_PREFIX)) % len(_TEAM_SUFFIX)]}"


def _slug(name: str) -> str:
    return name.lower().replace(" ", "")


def _catalogues(seed, series_meta, display_of, cars_of, roster_rows, era_of, cal_rows) -> dict[str, str]:
    rng = random.Random(seed ^ 0xCA7A_106)             # its own stream: the tables above stay byte-identical
    classes = list(cars_of)
    lv_name = {c: [f"{_livery_team(c, i)} #{i + 1}" for i in range(_LIVERIES_PER_CLASS)] for c in classes}
    names_of: dict[str, list] = {}                     # class -> driver per slot, from the pinned rosters
    for row in roster_rows:
        _sid, slot, name, cls, _lid = row.split("|")
        names_of.setdefault(cls, [None] * _LIVERIES_PER_CLASS)
        if int(slot) < _LIVERIES_PER_CLASS and names_of[cls][int(slot)] is None:
            names_of[cls][int(slot)] = name

    def driver(c, i):
        return (names_of.get(c) or [None] * _LIVERIES_PER_CLASS)[i] or ""

    def veh(c, i):
        return cars_of[c][i % len(cars_of[c])][0]

    out: dict[str, str] = {}
    out["ams2_class_hash.psv"] = _nl(["# class_name|hash"] + [f"{c}|{_uhash('cls:' + c)}" for c in classes])
    out["ams2_liveries.psv"] = _nl(
        ["# synthetic livery catalogue", "class_name|xml_name|livery_name|livery_id|display"] +
        [f"{c}|{c}|{lv_name[c][i]}|{50 + i}|{c.rstrip('0123456789')} {c[-1]}"
         for c in classes for i in range(_LIVERIES_PER_CLASS)])

    econ = ["# synthetic series economy",
            "node_id|phase|disc|comprar|alquiler|entry|premio_gan|model|icon|classes|champion"]
    for sid, c, lvl in series_meta:
        disc = disc_for_series(str(lvl), c)
        base = 20000 * lvl * lvl
        econ.append(f"{sid}|{lvl}|{disc}|{base}|{base // 30}|{base // 60}|{base // 8}|"
                    f"{('pocket', 'sponsor', 'factory')[min(2, (lvl - 1) // 2)]}||{c}|")
    out["ams2_series_econ.psv"] = _nl(econ)

    tracks = sorted({r.split("|")[5] for r in cal_rows})
    out["ams2_track_slots.psv"] = _nl(
        ["# hash|slug|grid_slots|pit_boxes"] +
        [f"{_track_hash(t)}|{_slug(t)}|{rng.randint(20, 32)}|{rng.randint(40, 64)}" for t in tracks])
    packed = [t for i, t in enumerate(tracks) if i % 5 == 4]
    out["circuits_ref.psv"] = _nl(
        ["# hash|name|country|continent|type|grade|era|note|slug|dlc|flags"] +
        [f"{_track_hash(t)}|{t}|{_SYNTH_COUNTRY[i % 3]}|Northreach|Asfalto|GP|moderno||{_slug(t)}|"
         f"{'dlc_' + _slug(t) if t in packed else ''}|" for i, t in enumerate(tracks)])
    out["dlc_products.psv"] = _nl(
        ["# synthetic DLC products", "key|kind|product_id|product_display"] +
        [f"dlc_{_slug(t)}|track|{_slug(t)}_pack|{t} Pack" for t in packed] +
        [f"{c}|class|{c.lower()}_pack|{c} Pack" for c in classes[-1:]])

    top = max(series_meta, key=lambda m: m[2])
    out["series_cars.psv"] = _nl(
        ["# series_id|veh_hash|note", f"{top[0]}|{veh(top[1], 0)}|{cars_of[top[1]][0][1]} (single-car series)"])
    out["series_market.psv"] = _nl(
        ["# series_id|mercado|motivo"] +
        [f"{sid}|{'epoca' if int(era_of[sid]) < 2000 else 'viva'}|synthetic" for sid, _c, _l in series_meta])
    out["series_config_profiles.psv"] = _nl(
        ["# profile_id|campos"] +
        [f"{st}_{pit}_{fuel}_{kmh}|start={'stand' if st == 'grid' else 'roll'};pitreq={0 if pit == 'nopit' else 1};"
         f"refok={1 if fuel == 'fuel' else 0};mintyres=0;pitkmh={kmh}"
         for st in ("grid", "roll") for pit in ("nopit", "pit1") for fuel in ("nofuel", "fuel")
         for kmh in (60, 80)])

    rookies = ["# driver_id|name|homage_name|country|tier_min|tier_max|base_skill|aggression|consistency|hot|archetype"]
    for i in range(24):
        n = f"{_FIRST[i % len(_FIRST)]} {_LAST[(i * 5 + 3) % len(_LAST)]}"
        rookies.append(f"rk_{n.split()[0].lower()}|{n}|{n.split()[0]} X|{_COUNTRY_CODE[i % 5]}|{1 + i % 2}|{3 + i % 5}|"
                       f"{0.95 - i * 0.02:.2f}|{0.4 + (i % 5) * 0.1:.2f}|{0.95 - (i % 7) * 0.05:.2f}|{i % 2}|"
                       f"{_ARCHETYPE[i % 4]}")
    out["series_roster.psv"] = _nl(rookies)

    out["feeder_team_map.psv"] = _nl(
        ["# class_name|livery_name|kind|team_label"] +
        [f"{c}|{lv_name[c][i]}|team|{_livery_team(c, i)}" for c in classes for i in range(_LIVERIES_PER_CLASS)])
    out["livery_team_equiv_packgrids.psv"] = _nl(
        ["# class_name|livery_name|real_team|car_number|driver|confidence"] +
        [f"{c}|{lv_name[c][i]}|{_livery_team(c, i)}|{i + 1}|{driver(c, i)}|curado"
         for c in classes for i in range(_LIVERIES_PER_CLASS)])
    out["skins/livery_resolved.psv"] = _nl(
        ["# class_name|livery_name|veh_hash|livery_id|team|team_src|driver|driver_src|country|alt_team"] +
        [f"{c}|{lv_name[c][i]}|{veh(c, i)}|{50 + i}|{_livery_team(c, i)}|humano|{driver(c, i)}|humano|"
         f"{_COUNTRY_CODE[i % 5]}|" for c in classes for i in range(_LIVERIES_PER_CLASS)])

    fields = ["# series_id|order|name|country|iconic|skill|aggr|note"]
    for k, (sid, _c, _l) in enumerate(series_meta):
        for o in range(1, 31):
            nm = f"{_FIRST[(o * 3 + k) % len(_FIRST)]} {_LAST[(o * 7 + k * 5) % len(_LAST)]}"
            fields.append(f"{sid}|{o}|{nm}|{_COUNTRY_CODE[(o + k) % 5]}|0|{0.40 + (o * 17 % 50) / 100:.2f}|"
                          f"{0.30 + (o * 13 % 60) / 100:.2f}|(invented)")
    out["series_fields.psv"] = _nl(fields)

    people = sorted({n for ns in names_of.values() for n in ns if n})
    out["career/f3_authored_profiles.psv"] = _nl(
        ["name|is_real|talent|archetype|affinity_family|history|source_class|era|country"] +
        [f"{n}|0|{0.55 + (j % 9) * 0.05:.3f}|{_ARCHETYPE[j % 4]}|openwheel|Invented career for the demo world|"
         f"{classes[j % len(classes)]}|Season {1990 + j % 30}|{_COUNTRY_CODE[j % 5]}"
         for j, n in enumerate(people)])

    seat_series = list(series_meta)[:7]
    seat_rows = [f"{sid}|{i}|{driver(c, i)}|{lv_name[c][i]}|{driver(c, i)}|{_COUNTRY_CODE[i % 5]}"
                 for sid, c, _l in seat_series for i in range(_LIVERIES_PER_CLASS) if driver(c, i)]
    out["career/seat_index_068.psv"] = _nl(
        ["# seat_index_068.psv -- frozen, invented.",
         f"# ESPERADO: series={len({r.split('|')[0] for r in seat_rows})} filas={len(seat_rows)}"] + seat_rows)

    cars = ["# key|season|unit|strength|reliability|liveries|note"]
    pair = ["# series_id|slot|driver|livery|team|country"]
    for c in classes:
        for t in range(_LIVERIES_PER_CLASS // 2):
            cars.append(f"{c}|*|{_livery_team(c, 2 * t)}|{rng.uniform(-0.6, 0.8):.3f}|{rng.uniform(0.3, 0.9):.3f}|"
                        f"{';'.join(lv_name[c][2 * t:2 * t + 2])}|invented")
    for sid, c, _l in series_meta:
        pair += [f"{sid}|{i}|{driver(c, i)}|{lv_name[c][i]}|{_livery_team(c, i)}|{_COUNTRY_CODE[i % 5]}"
                 for i in range(_LIVERIES_PER_CLASS) if driver(c, i)]
    out["career/team_cars.psv"] = _nl(cars)
    out["career/team_pairing.psv"] = _nl(pair)
    return out


def generate(seed: int = 20260827) -> dict[str, str]:
    rng = random.Random(seed)          # never the module-level RNG: that would break determinism
    levels = [1, 2, 3, 4, 5] + [rng.choice((1, 2, 3, 4, 5)) for _ in range(7)]
    rng.shuffle(levels)

    series_base, series_meta = [], []
    access_rows, cal_rows, roster_rows = [], [], []
    points_rows = ["standard|25,18,15,12,10,8,6,4,2,1|1|10|1|flagship scoring",
                   "compact|15,12,10,8,6,4,2,1|1|8|0|support scoring"]

    for idx, level in enumerate(levels):
        sid = f"{_PREFIX[idx].lower()}_{_CLASS[idx % len(_CLASS)].lower()}"
        display = f"{_PREFIX[idx]} {_CLASS[idx % len(_CLASS)]} {rng.choice(_KIND)}"
        class_name = f"{_CLASS[idx % len(_CLASS)]}{level}"
        era = rng.choice(("1970", "1985", "1998", "2012", "2024"))
        # Level 1 is the way in, and an era year (< 2000) makes a series an era-memory series, which the
        # calendar opens only to a player who owns a car of its class (IsEraMemorySeries, BuildMasterCalendar).
        # A new career owns none, so an era door at level 1 is a locked door. Remapped after the draw, so
        # the RNG sequence -- and every other column -- stays what it was.
        if level == 1 and int(era) < 2000:
            era = {"1970": "2012", "1985": "2024", "1998": "2012"}[era]
        rounds = rng.randint(6, 12)
        points_id = "standard" if level >= 4 else "compact"
        # profile_id holds the bare level: series.psv's own class-level test parses this column as int.
        series_meta.append((sid, class_name, level))
        series_base.append(
            f"{sid}|{class_name}|{display}|INT|{era}|{level}|{points_id}|{rounds}|")

        if level >= 4:
            access_rows.append(f"{sid}|closed")

        doy = rng.randint(20, 60)
        for rnd in range(1, rounds + 1):
            track = _TRACK[(idx * 3 + rnd) % len(_TRACK)]
            cal_rows.append(
                f"{sid}|{rnd}|{doy}|{_date_for_doy(doy)}|{_track_hash(track)}|{track}|"
                f"race|{rng.randint(8, 24)}|{rng.randint(20, 45)}|{rng.randint(11, 17)}|clear|")
            doy += rng.randint(14, 28)

        for slot in range(rng.randint(16, 28)):
            name = f"{_FIRST[(idx + slot) % len(_FIRST)]} {_LAST[(idx * 2 + slot) % len(_LAST)]}"
            roster_rows.append(f"{sid}|{slot}|{name}|{class_name}|{50 + slot}")

    # Unlocks only ever point at a STRICTLY higher level, so no cycle can form for any seed. The gated
    # series carries the feeder (the class of the series that unlocks it), as career_calendar.cpp reads it.
    unlock_of, feeder_of = {}, {}
    for sid, class_name, level in series_meta:
        higher = [m for m in series_meta if m[2] > level]
        if higher and (sid == series_meta[0][0] or rng.random() < 0.7 or level == 1):
            tgt = rng.choice(higher)
            top = rng.randint(1, 3)
            unlock_of[sid] = (tgt[0], top)
            feeder_of.setdefault(tgt[0], (class_name, top))
    series_rows = []
    for (sid, _c, level), base in zip(series_meta, series_base):
        tgt, top = unlock_of.get(sid, ("", 0))
        fclass, ftop = feeder_of.get(sid, ("", 0))
        series_rows.append((level, f"{base}|{level}|champ|{tgt}|{top or ''}|{fclass}|{ftop or ''}"))
    # Written by level, lowest first. The engine keeps series.psv's order (AllSeries), and the season
    # screen opens on the first championship of its rail, so a file in draw order could open a new
    # career on a level-4 championship it cannot enter. A stable sort after every draw: the RNG
    # sequence, and each row's content, stay exactly what they were.
    series_rows = [row for _lv, row in sorted(series_rows, key=lambda r: r[0])]

    def table(header: str, rows: list[str]) -> str:
        return f"# {header}\n" + "\n".join(rows) + "\n"

    display_of = {b.split("|")[0]: b.split("|")[2] for b in series_base}
    unlockers_of: dict[str, list[str]] = {}
    for src, (tgt, _top) in unlock_of.items():
        unlockers_of.setdefault(tgt, []).append(src)
    ladder, vehclass, car_class, cars_of = _ladder_and_catalogue(seed, series_meta, display_of, unlockers_of)
    extra = _catalogues(seed, series_meta, display_of, cars_of, roster_rows,
                        {b.split("|")[0]: b.split("|")[4] for b in series_base}, cal_rows)

    return {
        "career_ladder.psv": ladder,
        "ams2_vehclass.psv": vehclass,
        "ams2_car_class.csv": car_class,
        "series.psv": table(
            "series_id|class_name|display|region|era|profile_id|points_id|rounds|branch_of|level|kind|"
            "unlocks|gate_top|gate_feeder_class|gate_feeder_top",
            series_rows),
        "series_access.psv": table("series_id|access", access_rows),
        "series_calendar.psv": table(
            "series_id|round|doy|date|track_hash|track_name|fmt|laps|durationMin|hour|weather|note",
            cal_rows),
        "series_points.psv": table(
            "points_id|puntos|fl_point|fl_top_n|pole_point|nota", points_rows),
        "series_rosters.psv": table("series_id|slot|driver|class_name|livery_id", roster_rows),
        **extra,
    }


def write_world(out_dir: Path, seed: int = 20260827) -> list[Path]:
    out_dir.mkdir(parents=True, exist_ok=True)
    written = []
    for name, content in generate(seed).items():
        path = out_dir / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content, encoding="utf-8", newline="\n")
        written.append(path)
    return written


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--seed", type=int, default=20260827)
    args = parser.parse_args()
    for path in write_world(args.out, args.seed):
        print(path)
