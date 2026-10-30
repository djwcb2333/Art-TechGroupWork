#include "DWOpeningGatheringPolicy.h"
#include "DWSaveGame.h"
#include "DWGatheringWorldPersistence.h"
#include "DWGatheringPersistenceOptIn.h"
#include "DWFormalSaveVersionPolicy.h"
#include "Misc/PackageName.h"

namespace
{
bool Fail(FString& Error,const TCHAR* Text){Error=Text;return false;}
const TCHAR* MainPath(){return TEXT("/Game/DoughWorld/Core/Data/DA_DoughWorldGameplay.DA_DoughWorldGameplay");}
}
bool DWOpeningGatheringPolicy::ValidateSourceFlow(const FDWAdventureFlowSave& F,const FString& Map,const FString& Config,FString& Error)
{
 FGuid Id;
 if(F.Schema!=1||F.ScenarioId!=TEXT("SC0101")||
    F.OpeningMap!=TEXT("/Game/DoughWorld/Maps/Gameplay/Levels/L_SC0101_Awakening")||Map!=F.OpeningMap||
    !FGuid::ParseExact(F.PlaythroughId,EGuidFormats::Digits,Id)||!Id.IsValid()||!F.bStarterGranted||
    (F.Phase!=EDWAdventureOpeningPhase::Pending&&F.Phase!=EDWAdventureOpeningPhase::CompletedPendingDestination)||
    !FPackageName::IsValidLongPackageName(F.DestinationMap)||F.DestinationMap==F.OpeningMap||Config!=MainPath()||
    !UDWGatheringPersistenceOptIn::ProfileAllowed(F.OpeningMap,Config))
  return Fail(Error,TEXT("Only the explicitly audited SC0101 formal source may hand off schema-2 gathering facts."));
 Error.Reset();return true;
}
bool DWOpeningGatheringPolicy::ValidateSourceSnapshot(const UDWSaveGame& S,FString& Error)
{
 if(S.Version!=DWFormalSaveVersionPolicy::FormalVersion||S.FormalLanguage.Schema!=2||
    S.LanguageTrialSchema!=0||!S.LanguageProgressJson.IsEmpty()||!S.A01StateJson.IsEmpty()||S.CombatWorld.bNewAdventure||
    S.CombatWorld.Gathering.Schema!=2||
    !ValidateSourceFlow(S.AdventureFlow,S.MapPackage,MainPath(),Error))
 {if(Error.IsEmpty())Error=TEXT("Opening gathering handoff requires a complete formal source checkpoint.");return false;}
 return FDWGatheringWorldPersistence::Validate(S.CombatWorld.Gathering,S.MapPackage,S.Inventory,false,Error);
}
bool DWOpeningGatheringPolicy::ValidateDestinationBoundary(const UDWSaveGame& Source,const UDWSaveGame& Destination,
 const FDWGatheringWorldSave& Captured,FString& Error)
{
 if(!ValidateSourceSnapshot(Source,Error))return false;
 const auto& F=Source.AdventureFlow;const auto& D=Destination.AdventureFlow;
 if(F.Phase!=EDWAdventureOpeningPhase::CompletedPendingDestination||Destination.CombatWorld.bNewAdventure||
    Destination.Version!=Source.Version||Destination.FormalLanguage.Schema!=2||D.Schema!=F.Schema||
    D.PlaythroughId!=F.PlaythroughId||D.ScenarioId!=F.ScenarioId||D.OpeningMap!=F.OpeningMap||
    D.DestinationMap!=F.DestinationMap||!D.bStarterGranted||D.Phase!=EDWAdventureOpeningPhase::Completed||
    Destination.MapPackage!=F.DestinationMap||Destination.MapPackage==Source.MapPackage||
    (FDWGatheringWorldPersistence::IsSupportedMap(Destination.MapPackage)&&
     !FDWGatheringWorldPersistence::IsApprovedProfileForMap(Destination.MapPackage,MainPath()))||
    !FDWGatheringWorldSave::StaticStruct()->CompareScriptStruct(&Destination.CombatWorld.Gathering,&Captured,PPF_None))
  return Fail(Error,TEXT("Opening destination must retain its independently captured gathering facts, never source pond history."));
 return FDWGatheringWorldPersistence::Validate(Captured,Destination.MapPackage,Destination.Inventory,false,Error);
}
