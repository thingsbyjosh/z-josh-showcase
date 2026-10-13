# Career Engine

This is a sanitised export of the domain layer from a career-mode engine for Automobilista 2: driver
contracts, a transfer market, season calendars, race grids, and a rating system, written in C++17 and
driven by a small React app. It runs on an invented world of fictional series and drivers rather than
the game's own data, so nothing here is content extracted from Automobilista 2. It's a showcase of how
a few specific decisions got made, not a product: there's no installer, and nothing here talks to a
running copy of the game.

## Shape of it

The engine itself doesn't know anything about a UI. Every operation is a method name plus a JSON
payload in, a JSON payload out, which turned out to be exactly the shape needed to put more than one
front end behind it:

```
                     React app (this repo)
                             |
                { method, params } -> JSON payload
                             |
        +--------------------+--------------------+
        |                    |                     |
  WebSocket to a       a WASM build of        JSON fixtures plus
  live game process    the same C++,          a scripted career
  (not published)      run in the browser     (this demo)
```

The version you're looking at runs on the third option. The engine's own test harness built an
invented world — series, drivers, calendars, none of it real — ran the real query and command
handlers against it, and dumped the results as static JSON. What you're browsing is the actual
engine's output, on data that doesn't correspond to anything.

That snapshot is only the starting point. From there the demo simulates a small career in the
browser: you can run a race, read the results, take an offer, sign for a new team and enter special
events. Those results come from a seeded model written in TypeScript for this demo, not from the C++
engine, and the data is invented like the rest. Reloading the page starts the career again.

## Four decisions, checked rather than assumed

A knob in the difficulty settings was supposed to control how consistent the AI drivers were from lap
to lap. Across thirteen races logged with it at different settings, the variation didn't move with the
knob at all — it moved with how much traffic a driver ran into. The setting had been left in for
months on the assumption that it did something, because nothing about using it looked broken.

A watchdog flagged a percentage of races as anomalous, and that number had been quoted as a health
metric for a while. Checked case by case, seventy-one percent of the flagged races weren't unusual at
all — they were an artefact of how the watchdog itself sampled the data. The real anomaly rate was
five percent. The instrument doing the measuring had become the largest source of the thing it claimed
to measure.

Starting grids kept producing a first corner with an outsized share of the race's overtaking — sixty-
four percent of it, at one point. The instinct was that the grid was too tightly bunched, so the fix
tried was to spread it out. That didn't help, because bunching wasn't the problem: a floor value was
holding the whole grid up near the front regardless of the spread between cars, so almost everyone
started close enough together to trade places at turn one no matter what. The floor had to move, not
the spread.

A grid was configured to split three ways — roughly 3, 17, and 5 cars across three skill tiers. The
grid that actually raced came out 9, 8, 8. Something downstream was renormalising the split before it
reached the track, and nobody had noticed because nobody had gone and counted the actual grid against
the number that had been typed in. Writing a value and reading it back are two different tests, and
only one of them tells you what happened.

None of these are subtle bugs in the traditional sense — the code did what it was told. Each one is a
case where the number a person expected and the number the system actually produced quietly parted
ways, and the only way to catch that was to go and measure the output instead of trusting the input.

## How this got built

The private repository this is exported from has 3,242 commits between 5 June and 12 October 2026,
written by one developer, with an engine test harness that runs to 50,426 lines. Most of the
implementation work went through an AI coding agent, and the method was plain. Development was
test-driven, against a harness whose exit code is literally the number of failing tests, so there was
never any doubt about whether a change had worked. A behaviour got measured before it got changed,
because the four cases above are what happens when that step gets skipped. And anything that meant
searching or auditing a large surface — checking a claim against every file that could contradict it,
tallying how often a pattern actually occurs before deciding it's rare — got handed to a subagent that
could run the exhaustive version of the check a person tends to shortcut by the fifth file.

## What the green checkmark means

452 of the private engine's 891 test functions run in this repository's own CI, making 19,254
individual checks — all passing. The other 439 need something this export deliberately doesn't
contain: Automobilista 2's own vehicle and livery catalogue, and the curated championship rosters,
superlicense gates and driver archetypes authored against it. Building a synthetic version of that
data convincing enough to satisfy them would mean inventing a fake game, which is well past what a
portfolio piece needs — so the boundary is drawn instead of blurred: the domain logic ships, is
tested, and is green; the game's own content doesn't ship at all.

The 452 that do run exercise a fully invented world, with no real series, drivers or calendars in the
data they run on, generated from a fixed seed by `tools/gen_world.py`, which is checked in. Clone the repository, run
the harness, and you get the same world and the same 19,254 passing checks.

## What is in the world, and what is not

The world is invented, but it is shaped like the sport. Generic categories of racing are named as
themselves: GT3, GT4, GT1, Formula Vee, Stock Car, Group A, Group C, prototypes, karting, TCR, LMP,
Formula Retro and Formula Classic. The data the demo and the harness run on is invented, and no real
manufacturer, car, circuit, championship or team name appears anywhere in the tree. The one thing the
tree does contain is source comments that occasionally cite real racing drivers as examples. The series
logos in the demo were drawn by the author, and the names written in them are invented. No hash from
the game's own catalogue is present either.

That is checked rather than promised: on every export, the gate compares the whole output tree against
the real catalogue and refuses to write anything that still contains a real name or hash.

## Licence

All rights reserved — published for review, not reuse. See [LICENSE](LICENSE).
