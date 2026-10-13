#pragma once
#include <map>
// Bridge between the Settings UI <-> the overlay (difficulty persistence). Implemented in career_overlay.cpp.
namespace career {
int  SettingsUiMode();            // current mode (0..5) read from the save
void SettingsUiSetMode(int mode); // sets and persists the mode
int  SettingsUiTrim();            // current trim (-5..+5)
void SettingsUiSetTrim(int trim); // sets and persists the trim (clamped)
// FIXED difficulty picked by hand (DM_CUSTOM mode): the AMS2 slider value that always runs, regardless of
// category level, trim, or class/track offsets moving it. It's saved and changed even if
// the active mode is something else -- turning it on selects the card, not the number.
int  SettingsUiCustomAi();               // current save value (prog.custom_difficulty)
void SettingsUiSetCustomAi(int value);   // fixed and persisted (clamped to dda::kAiMin..kAiMax)
// AI aggression chosen by the user. 0 = AUTO (by phase); 40/60/80/100 = override.
int  SettingsUiAggr();            // current save value (prog.ai_aggression)
void SettingsUiSetAggr(int aggr); // fixed and persisted (0 or 40/60/80/100; any other value clamps to the nearest)
// toggles for automatic scaling of Gameplay multipliers (Wear / Consumption).
bool SettingsUiScaleTyre();          void SettingsUiSetScaleTyre(bool on);
bool SettingsUiScaleFuel();          void SettingsUiSetScaleFuel(bool on);
int  SettingsUiFcyMode();            void SettingsUiSetFcyMode(int mode);   // FCY 0=No 1=Yes 2=Inherited
// 0.7.0: race PACE (prog.reward_mode 0/1/2). Sets the save and the "difficulty" of profile.json, which
// takes effect on load; false if the mode doesn't exist or profile.json couldn't be rewritten (then it does NOT touch the save).
bool SettingsUiSetReward(int mode);
// Block D (spec 2026-07-24): race telemetry submission consent. Off by default.
// SettingsUiTelemetryAsked = has consent already been asked for? (the question appears ONCE, in front of the
// Play button); any explicit answer closes it, and from then on the Settings toggle governs it.
bool SettingsUiTelemetry();          void SettingsUiSetTelemetry(bool on);
bool SettingsUiTelemetryAsked();

// AUTO/Inherited per field (prog.inherit_mask, kInh* bits from career_engine.h).
// SettingsInheritBit translates the app contract's token ("weather"/"hour"/"timeProg"/"start"/
// "pit"/"ai"/"sessions") to its bit; 0 = unknown token. It's the ONLY token<->bit table.
int  SettingsInheritBit(const char* field);
int  SettingsUiInheritMask();                        // current save mask
bool SettingsUiSetInherit(const char* field, bool inherit);   // fixes+persists one field; false = unknown token

// Block C (spec C1) -- WEATHER. Mode: 0 random, 1 inherited from your Custom Race, 2 rotation of your
// selection. The weights are ONLY read by mode 2 (AMS2 catalog code -> weight; absent or 0 = forbidden);
// empty = factory weights. An event's authored weather beats all three.
int  SettingsUiWxMode();                             void SettingsUiSetWxMode(int mode);
std::map<int,int> SettingsUiWxWeights();             void SettingsUiSetWxWeights(const std::map<int,int>& w);

}
