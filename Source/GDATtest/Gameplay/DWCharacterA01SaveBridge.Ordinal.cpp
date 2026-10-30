#include "DWCharacterA01SaveBridge.h"
#include "DWCombatPlayerSave.h"

bool DWCharacterA01SaveBridge::ResolveStarvationOrdinal(const DWCombatCommit::FLedgerSnapshot& Ledger,
 DWCombatCommit::FEntityLife Life,bool bFormal,uint64 Saved,uint64& Out,FString& Error)
{
 std::string Why;
 if(!Life.Entity||!Life.Life||Saved==MAX_uint64||!DWCombatCommit::FLedger::ValidateSnapshot(Ledger,Why))
 {Error=TEXT("Cannot derive starvation ordinal from an invalid life/ordinal/authority ledger.");return false;}
 uint64 LowerBound=0;
 for(const auto& K:Ledger.Keys)
  if(K.Key[0]==5&&K.Key[2]==Life.Entity&&K.Key[3]==Life.Life&&
   K.Key[4]==static_cast<uint64>(DWCombatCommit::EVitalsSource::Starvation)&&K.Key[6]==Life.Entity&&K.Key[7]==Life.Life)
   LowerBound=FMath::Max(LowerBound,K.Key[5]);
 // All receipt states retain their key. This is a non-reuse lower bound,
 // never a fabricated count of historical hunger ticks or health damage.
 if(LowerBound==MAX_uint64||(bFormal&&Saved<LowerBound))
 {Error=TEXT("Saved formal starvation ordinal would reuse a same-life authority key.");return false;}
 Out=bFormal?Saved:FMath::Max(Saved,LowerBound);Error.Reset();return true;
}
bool DWCharacterA01SaveBridge::PrepareRespawnResources(const FDWCombatRules& Rules,const FDWCharacterA01Runtime& Current,
 FDWCharacterA01Runtime& Out,FDWCombatPlayerVitalsRuntime& OutLegacy,float& OutHealth,FString& Error)
{
 if(!Current.IsReady())return false;auto Candidate=Current;
 // Use the already-defined death exit. This preserves recorded loss, independent
 // hidden energy facts and ordinal; it never invents a death reset or refunds core.
 if(!Candidate.SyncCurrentHealth(0.))return false;
 FDWCombatPlayerVitalsConfig Config;FDWCombatPlayerVitalsSnapshot Death;
 if(!BuildMirror(Rules,Candidate,Config,Death,Error))return false;
 FDWCombatPlayerVitalsRuntime Reset;if(!Reset.Initialize(Config,EDWCombatVitalsReset::Respawn))return false;
 const auto ResetValues=Reset.Capture(0.);auto State=Candidate.Capture();
 auto EffectiveConfig=Config;EffectiveConfig.Vitals.HealthMax=Candidate.EffectiveMaxHealth();
 const float Health=DWCombatPlayerSave::NativeHealth(Candidate.EffectiveMaxHealth()*Config.Vitals.RespawnHealthFraction,EffectiveConfig);
 if(!FMath::IsFinite(Health)||Health<=DWCombatPlayerSave::HealthFloor(Config))return false;
 State.CurrentHealth=Health;State.Water=ResetValues.Water;State.Satiety=ResetValues.Satiety;
 if(!Candidate.Restore(Current.GetConfig(),State))return false;
 FDWCombatPlayerVitalsSnapshot Snapshot;FDWCombatPlayerVitalsRuntime Legacy;
 if(!BuildMirror(Rules,Candidate,Config,Snapshot,Error)||!Legacy.Restore(Config,Snapshot))return false;
 Out=MoveTemp(Candidate);OutLegacy=MoveTemp(Legacy);OutHealth=Health;Error.Reset();return true;
}
