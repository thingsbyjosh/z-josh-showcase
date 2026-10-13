#pragma once
// INTERNAL header for the career module (not included by career_overlay.cpp): exposes the
// data structs and screen functions. Used by career_ui.cpp, screen_*.cpp, and the harness.
#include "career_state.h"
#include <string>
void DrawTopbar(CareerState& st, float w);
void DrawIdentity(CareerState& st);   // onboarding / player profile (first-launch gate)
void DrawHub(CareerState& st);
void DrawTree(CareerState& st);
void DrawCalendar(CareerState& st);
void DrawGarage(CareerState& st);
void DrawResults(CareerState& st);
void DrawVehicle(CareerState& st);   // vehicle selection screen (ported from screen_vehicle.jsx)
void DrawSeasonSelect(CareerState& st);     // series/season selection (phase-gated)
void SeasonSelectSeries(const std::string& seriesId);   // external selection (tree -> Championships)
void DrawSeasonStandings(CareerState& st);  // active season (calendar + standings + running a round)
void DrawMarket(CareerState& st);           // Driver Market (reputation + direct/open offers)
void DrawInbox(CareerState& st);            // Inbox (info + decisions + livery design)
void DrawProfile(CareerState& st);          // Driver profile (band + ranking + arc + rivals + career record)
void DrawNegotiate(CareerState& st);        // Contract negotiation (% leverage + demands + sponsor)
// PNG-only test harness: seeds a specific negotiation state (demands/salary, forced negotiationLevel,
// relationship for the ding) for the state PNGs (reject / level0 / low relationship). The real overlay NEVER calls it.
namespace career { struct DealTerms; }
void NegotiateProofSeed(const career::DealTerms& terms, int negotiationLevel, double rel);
// PNG-only test harness: forces the qualifier-entry confirmation modal with
// a seeded slot (insolvent=true -> variant blocked by insolvency). The real overlay NEVER calls it.
namespace career { struct CalSlot; }
void CalProofForceQualifierModal(const career::CalSlot& slot, bool insolvent);
// DOUBLE-CONFIRMATION modal for "jumping in time", shared by the calendar
// (jump to date / jump a year, screen_calendar.cpp) and season end (the "next championship event"
// CTA, screen_rollover.cpp). JumpModalOpen ARMS the preview for a 'targetAbsDay' ALREADY RESOLVED by the
// caller (career::CalUiNextEventAbsDayForSeries / CalUiNextEventAbsDayOnOrAfter); it never jumps on its own.
// navigateToCalendar=true makes the screen jump to Screen::Calendar after final confirmation (so
// the season-end CTA takes you to see the event). DrawJumpConfirmModal must be called at the end of the draw for
// ANY screen that can open the modal (it does nothing if it isn't active).
void JumpModalOpen(int targetAbsDay, bool navigateToCalendar = false);
void DrawJumpConfirmModal(CareerState& st);
bool JumpModalIsActive();   // true while the modal (either of its two phases) is on screen; other
                            // controls on the screen that opened it should be hidden/disabled using this
                            // while it's active (the overlay visually covers things, but ImGui's native widgets
                            // don't respect a hand-painted rect -- they have to be explicitly disabled).
void DrawSponsors(CareerState& st);       // sponsors (mine/available, tiers, traits, objectives)
void DrawMemories(CareerState& st);       // era memories (family tree + era detail + relic/inherit)
void DrawChampionships(CareerState& st);  // standings table + movement + points
void DrawRollover(CareerState& st);       // season-end aging (retire/promote/moves + contract)
void DrawTryout(CareerState& st);         // evaluation event (target lap, stakes, pass/ok/fail)
void DrawSettings(CareerState& st);        // Settings — Difficulty section (ported from screen_settings.jsx)
