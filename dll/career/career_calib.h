#pragma once
// career_calib.h - THE MEASUREMENT RACE of the onboarding (Kingsmere Uma713 @ Stoneton Bay; iter 2).
//
// A single definition for the DLL (career_overlay.cpp) and the double (tools/zta_fakegame.cpp): both include
// career_ui_bridge.inc, which uses them. Before, each had its own copy, and on 19/09 kCalibMistakes went in only on the
// DLL: the double stopped compiling (C2065) without anything warning about it.
//
// Hashes of the game's LIVE catalog (ams2_track_catalog_live / vehicle_record_specs). Changing the calibration to
// another class = edit here + the app's copy keys calib_class/calib_country + RE-MEASURE kCalibSamples
// (career_difficulty.h): their laps are for THIS car/track combination.
#include "career_balance.h"

constexpr int         kCalibTrackHash = -927015669;          // fairham
constexpr const char* kCalibSeriesId  = "keystone_championship";
constexpr const char* kCalibClassId   = "Beacon_Trophy";       // in-game class (roster + economy + named auto-grid)
constexpr const char* kCalibTrackName = "Stoneton Bay"; // in-game track name
constexpr int         kCalibVehHash   = -573173193;          // keystone_championship
constexpr const char* kCalibRulesId   = "keystone_championship";  // career rules (pit/refuel/…) of the profile grid_nopit_fuel_60
// 19/09: the AI mistakes of the measurement, FIXED and with their own name. They came from HandoffAiForPhase(1) (x5), which
// is the RACE table: if that table is ever touched up, the measurement would stop being comparable with
// kCalibSamples without anyone noticing. Same value as until today, so the measurement does not change.
constexpr int         kCalibMistakes  = career::bal::kMistX5;
