#include "DWCharacterA01SaveBridge.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "DWCombatRules.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWFormalA01OrdinalTest,"DoughWorld.FormalLanguage.StarvationOrdinal",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWFormalA01OrdinalTest::RunTest(const FString& Parameters)
{
 using namespace DWCombatCommit;FString Error;FDWCharacterA01Config C;C.NormalRegenAmount=0.;
 FDWCharacterA01Runtime Runtime;TestTrue(TEXT("A01 fixture initializes"),Runtime.Initialize(C));
 auto State=Runtime.Capture();State.CurrentHealth=50.;State.Satiety=0.;State.StarvationOrdinal=9007199254740993ULL;
 TestTrue(TEXT("Exact uint64 state is accepted"),Runtime.Restore(C,State));FString Formal,Trial;
 TestTrue(TEXT("Formal exact codec writes"),DWCharacterA01SaveBridge::FormalStateToJson(State,.2,Formal,Error));
 FDWCharacterA01Runtime Decoded;double Cooldown=0.;
 TestTrue(TEXT("Formal exact codec reads"),DWCharacterA01SaveBridge::FormalStateFromJson(Formal,C,2.,Decoded,Cooldown,Error));
 TestTrue(TEXT("Ordinal above double integer precision is retained exactly"),Decoded.GetState().StarvationOrdinal==State.StarvationOrdinal);
 TestTrue(TEXT("Legacy trial codec writes"),DWCharacterA01SaveBridge::StateToJson(State,.2,Trial,Error));
 TSharedPtr<FJsonObject> J;TestTrue(TEXT("Trial JSON parses"),FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Trial),J));
 TestTrue(TEXT("Trial remains exactly 15 fields without formal ordinal"),J&&J->Values.Num()==15&&!J->HasField(TEXT("starvation_ordinal")));
 TestFalse(TEXT("Formal reader refuses a trial envelope"),DWCharacterA01SaveBridge::FormalStateFromJson(Trial,C,2.,Decoded,Cooldown,Error));
 FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Formal),J);J->SetStringField(TEXT("starvation_ordinal"),TEXT("18446744073709551615"));
 FString Invalid;FJsonSerializer::Serialize(J.ToSharedRef(),TJsonWriterFactory<>::Create(&Invalid));
 TestFalse(TEXT("MAX ordinal rejects before any wrap"),DWCharacterA01SaveBridge::FormalStateFromJson(Invalid,C,2.,Decoded,Cooldown,Error));

 FLedger Ledger;const FEntityLife Player{1,1},Other{2,1};Ledger.SetWorldEpoch(1);Ledger.SetLife(Player);Ledger.SetLife(Other);
 auto Hunger=[&](FEntityLife Life,uint64 Ordinal){return Ledger.AdmitDamage({FVitalsDamage{1,Life,EVitalsSource::Starvation,Ordinal},Life});};
 auto Prior=Hunger(Player,7);TestTrue(TEXT("Old hunger admitted"),Prior.Result==EAdmission::Admitted);Ledger.Resolve(Prior.Receipt.Handle,EOutcome::Committed);
 auto Rejected=Hunger(Player,9);Ledger.Resolve(Rejected.Receipt.Handle,EOutcome::Rejected);
 auto DifferentPlayer=Hunger(Other,100);Ledger.Resolve(DifferentPlayer.Receipt.Handle,EOutcome::Committed);
 uint64 Resolved=88;const auto Saved=Ledger.CaptureSnapshot();
 TestTrue(TEXT("Old trial derives a non-reuse lower bound from all same-life receipt states"),DWCharacterA01SaveBridge::ResolveStarvationOrdinal(Saved,Player,false,0,Resolved,Error));
 TestTrue(TEXT("Rejected key retained; another entity excluded"),Resolved==9);
 TestFalse(TEXT("Formal ordinal below its own ledger fails"),DWCharacterA01SaveBridge::ResolveStarvationOrdinal(Saved,Player,true,8,Resolved,Error));
 TestTrue(TEXT("Formal saved ordinal may be ahead of committed ledger"),DWCharacterA01SaveBridge::ResolveStarvationOrdinal(Saved,Player,true,11,Resolved,Error)&&Resolved==11);

 FLedger Restored;FLedgerRestorePlan Plan;std::string Why;
 TestTrue(TEXT("Native ledger prepares exact epoch remap"),Restored.PrepareSnapshotRestore(Saved,2,{},Plan,Why));
 if(!Restored.CanCommitSnapshotRestore(Plan)){AddError(TEXT("Native ledger restore plan rejected."));return false;}
 Restored.CommitSnapshotRestoreUnchecked(std::move(Plan));
 TestTrue(TEXT("Trial lower bound survives epoch remap"),DWCharacterA01SaveBridge::ResolveStarvationOrdinal(Restored.CaptureSnapshot(),Player,false,0,Resolved,Error)&&Resolved==9);
 State.StarvationOrdinal=Resolved;Runtime.Restore(C,State);FDWCharacterA01Step Step;
 TestTrue(TEXT("Natural resource slice advances without a setter"),Runtime.AdvanceNext(.1,{},Step));
 TestTrue(TEXT("The new hunger tick has a positive damage amount and strictly new ordinal"),Step.HungerDamageDue>0.&&Runtime.GetState().StarvationOrdinal==10);
 const auto NewDamage=Restored.AdmitDamage({FVitalsDamage{2,Player,EVitalsSource::Starvation,Runtime.GetState().StarvationOrdinal},Player});
 TestTrue(TEXT("New hunger receipt admits after restore rather than deduplicating"),NewDamage.Result==EAdmission::Admitted);
 TestTrue(TEXT("New hunger receipt commits"),Restored.Resolve(NewDamage.Receipt.Handle,EOutcome::Committed)==EResolve::Resolved);
 State.StarvationOrdinal=MAX_uint64-1;Runtime.Restore(C,State);
 TestFalse(TEXT("Exhausted ordinal rejects a slice without wrapping"),Runtime.AdvanceNext(.1,{},Step));
 TestTrue(TEXT("Exhausted state remains unchanged"),Runtime.GetState().StarvationOrdinal==MAX_uint64-1);
 TSharedPtr<const FDWCombatRules> Rules;if(!FDWCombatRules::LoadProjectPayload(Rules,Error)){AddError(Error);return false;}
 C.HealthyMax=120.;C.WaterMax=150.;C.SatietyMax=120.;Runtime.Initialize(C);State=Runtime.Capture();
 State.RecordedCapLoss=40.;State.CurrentHealth=0.;State.Gas=13.;State.StarvationOrdinal=10;Runtime.Restore(C,State);
 FDWCharacterA01Runtime Respawned;FDWCombatPlayerVitalsRuntime Legacy;float RespawnHealth=0.f;
 TestTrue(TEXT("Existing respawn fractions prepare A01 resources"),DWCharacterA01SaveBridge::PrepareRespawnResources(*Rules,Runtime,Respawned,Legacy,RespawnHealth,Error));
 TestTrue(TEXT("Respawn preserves recorded loss and does not exceed effective health cap"),Respawned.GetState().RecordedCapLoss==40.&&double(RespawnHealth)<=Respawned.EffectiveMaxHealth());
 TestTrue(TEXT("Independent hidden energy and ordinal retain their existing death facts"),Respawned.GetState().Gas==13.&&Respawned.GetState().StarvationOrdinal==10);
 TestTrue(TEXT("Normal HUD special remains zero"),Legacy.GetSpecial()==0.&&Respawned.GetState().Form==EDWA01Form::Normal);
 TestTrue(TEXT("Current approved water and food respawn halves use talent-derived maxima"),Respawned.GetState().Water==75.&&Respawned.GetState().Satiety==60.);
 return true;
}
#endif
