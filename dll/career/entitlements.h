#pragma once
enum class Tier { Free, Supporter, SupporterPlus };
enum class Feat { DlcIntegration, VisualCards, Contracts, Sponsors, Scouts, EnduranceAdvanced, EditorTools };

namespace Entitlements {
  void SetTier(Tier t);          // dev: SupporterPlus
  Tier Get();
  bool Has(Feat f);              // P3.7 stub: always true
  bool IsVisual();               // visual cards vs. text; P3.7 stub: true
}
