#pragma once
#include "CoreMinimal.h"
#include "DWCharacterA01.h"
#include "DWCombatPlayerVitals.h"
#include "DWLanguageModel.h"
#include "DWCombatCommitLedgerSnapshot.h"
class FDWCombatRules;
class UDWSaveGame;
class FJsonObject;
struct FDWA01SavedProjection
{
 FDWCharacterA01Runtime Runtime;
 FDWCombatPlayerVitalsConfig LegacyConfig;
 FDWCombatPlayerVitalsSnapshot LegacySnapshot;
 DWLanguage::FState LanguageState;
 double CooldownRemaining=0.,MaximumCooldown=0.;
};
namespace DWCharacterA01SaveBridge
{
 class GDATTEST_API FScopedFormalDefinitions final
 {
 public:
  FScopedFormalDefinitions(const DWLanguage::FCatalog*,const TSharedPtr<FJsonObject>&);
  ~FScopedFormalDefinitions();
  FScopedFormalDefinitions(const FScopedFormalDefinitions&)=delete;
  FScopedFormalDefinitions& operator=(const FScopedFormalDefinitions&)=delete;
  bool HasDefinitions()const{return bAvailable;}
  const DWLanguage::FCatalog& GetCatalog()const{return Catalog;}
  const TSharedPtr<FJsonObject>& GetTrial()const{return Trial;}
 private:
  const FScopedFormalDefinitions* Previous=nullptr;
  DWLanguage::FCatalog Catalog;TSharedPtr<FJsonObject> Trial;bool bAvailable=false;
 };
 // Register immutable definitions after the subsystem has validated its catalog. No live player progress is registered.
 bool BindDefinitions(const DWLanguage::FCatalog&,const TSharedPtr<FJsonObject>& Trial,FString& Error);
 bool HasTrialFields(const UDWSaveGame&);
 bool HasFormalFields(const UDWSaveGame&);
 bool PrepareRespawnResources(const FDWCombatRules&,const FDWCharacterA01Runtime&,FDWCharacterA01Runtime& Out,FDWCombatPlayerVitalsRuntime& OutLegacy,float& OutHealth,FString& Error);
 bool ResolveStarvationOrdinal(const DWCombatCommit::FLedgerSnapshot&,DWCombatCommit::FEntityLife,bool bFormal,uint64 Saved,uint64& Out,FString& Error);
 bool IsExactTrialMap(const FString& Package);
 bool BuildDerivedConfig(const TSharedPtr<FJsonObject>& Trial,const TSet<FName>& Learned,FDWCharacterA01Config&,FString&);
 bool BuildMirror(const FDWCombatRules&,const FDWCharacterA01Runtime&,FDWCombatPlayerVitalsConfig&,FDWCombatPlayerVitalsSnapshot&,FString&);
 bool StateToJson(const FDWCharacterA01State&,double Cooldown,FString& Out,FString& Error);
 bool FormalStateToJson(const FDWCharacterA01State&,double Cooldown,FString& Out,FString& Error);
 bool FormalStateFromJson(const FString&,const FDWCharacterA01Config&,double MaximumCooldown,FDWCharacterA01Runtime& Out,double& OutCooldown,FString& Error);
 bool StateFromJson(const FString&,const FDWCharacterA01Config&,double MaximumCooldown,FDWCharacterA01Runtime& Out,double& OutCooldown,FString& Error);
 // Static/world-independent validation: language is parsed into a candidate, never restored to the live subsystem first.
 bool ValidateProjection(const FDWCombatRules&,const UDWSaveGame&,FDWA01SavedProjection& Out,FString& Error);
 bool ValidateProjectionWithDefinitions(const FDWCombatRules&,const UDWSaveGame&,const DWLanguage::FCatalog&,const TSharedPtr<FJsonObject>& Trial,FDWA01SavedProjection& Out,FString& Error);
}
