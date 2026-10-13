#include "entitlements.h"
namespace Entitlements {
  static Tier g_tier = Tier::SupporterPlus;     // dev: everything visible (real enforcement = P3.16)
  void SetTier(Tier t){ g_tier = t; }
  Tier Get(){ return g_tier; }
  bool Has(Feat){ return true; }                // STUB: gate disabled in P3.7
  bool IsVisual(){ return true; }               // STUB
}
