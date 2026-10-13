#pragma once
// Language data layer for the EIGHT languages that aren't compiled in (de it ru ko fr zh-Hans pt
// pt-BR). EN and ES stay in the strings.cpp table, which is NOT touched: it's the file main and f0
// touch daily and widening it to ten columns would guarantee a conflict on every rebase.
//
// The layer is only for what the mod draws INSIDE the game (loading cover, tips and the
// ALL SET / CHECK THE SETUP / SHARED MEMORY OFF warnings): about 32 strings, not the whole corpus. The rest of
// the interface lives in the app, which loads its own files.
void SetLangCode(const char* code);              // "de", "pt-BR"...; unknown or null -> English
const char* LangOverlayLookup(const char* key);  // nullptr if there's no layer or the key isn't there
void ResetLangOverlayForTest();
