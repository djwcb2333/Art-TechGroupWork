#pragma once
#include "DWSaveGame.h"

// Container version is the deployed-format boundary, not a map/slot/name heuristic.
// Existing v1..v4 saves are never promoted by a capture or by reading a map alias.
namespace DWFormalSaveVersionPolicy
{
 static constexpr int32 LastLegacyVersion=4;
 static constexpr int32 FormalVersion=5;
 enum class EFamily:uint8 { Legacy,LegacyOpening,Trial,Formal };
 inline bool Classify(const UDWSaveGame& S,const FDWFormalLanguageSave& Envelope,EFamily& Out,FString& Error)
 {
  const auto Reject=[&](const TCHAR* Reason){Error=Reason;return false;};
  if(S.Version<1||S.Version>UDWSaveGame::CurrentVersion)
   return Reject(TEXT("Unsupported save container version; original file preserved."));
  const bool Empty=Envelope.Schema==0&&Envelope.SlotIndex==INDEX_NONE&&Envelope.ProgressJson.IsEmpty()&&Envelope.A01StateJson.IsEmpty();
  const bool Trial=S.LanguageTrialSchema!=0||!S.LanguageProgressJson.IsEmpty()||!S.A01StateJson.IsEmpty();
  if(!Empty)
  {
   if(S.Version!=FormalVersion||S.AdventureFlow.Schema!=1||Trial||Envelope.Schema!=2||Envelope.SlotIndex<0||Envelope.SlotIndex>=3||Envelope.ProgressJson.IsEmpty()||Envelope.A01StateJson.IsEmpty())
    return Reject(TEXT("Partial, mixed or wrong-version formal language block."));
   Out=EFamily::Formal;
  }
  else if(Trial)
  {
   if(S.Version>LastLegacyVersion||S.AdventureFlow.Schema!=0)
    return Reject(TEXT("Isolated trial data cannot carry a formal opening/version."));
   Out=EFamily::Trial;
  }
  else if(S.AdventureFlow.Schema==0&&S.Version<=LastLegacyVersion)Out=EFamily::Legacy;
  else if(S.AdventureFlow.Schema==1&&S.Version<=LastLegacyVersion)Out=EFamily::LegacyOpening;
  else return Reject(TEXT("A version-5 adventure must contain its bound Formal2 block."));
  Error.Reset();return true;
 }
 inline bool Classify(const UDWSaveGame& S,EFamily& Out,FString& Error)
 {return Classify(S,S.FormalLanguage,Out,Error);}
}
