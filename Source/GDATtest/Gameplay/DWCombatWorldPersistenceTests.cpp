#if WITH_DEV_AUTOMATION_TESTS
#include "DWCombatWorldPersistence.h"
#include "DWCombatWorldSave.h"
#include "DWCombatEffectsSave.h"
#include "DWCombatAuthoritySubsystem.h"
#include "DWCombatPlayerSave.h"
#include "DWCombatTestWorld.h"
#include "DWCombatRules.h"
#include "DWPlayerCharacter.h"
#include "DWWorldEvent.h"
#include "DWSaveGame.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/DamageType.h"
#include "Misc/AutomationTest.h"
namespace DWAggregateTests
{
struct FFixture
{
 TStrongObjectPtr<UDWGameInstance> GI;UWorld* W=nullptr;ADWPlayerCharacter* P=nullptr;FString Error;bool Ready=false;
 FFixture()
 {
  W=DWCombatTestWorld::Create(GI,TEXT("D22Aggregate"));if(!W)return;
  FActorSpawnParameters Spawn;Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
  P=W->SpawnActor<ADWPlayerCharacter>(ADWPlayerCharacter::StaticClass(),FVector(0,0,110),FRotator::ZeroRotator,Spawn);
  if(!P)return;if(!P->HasActorBegunPlay())P->DispatchBeginPlay();
  auto* S=NewObject<UDWSaveGame>();Ready=DWCombatPlayerSave::MakeNew(*GI->GetCombatRules(),*S,Error)&&P->TryApplySaveData(S);
 }
 ~FFixture(){if(!W)return;for(TActorIterator<AActor> It(W);It;++It)if(It->HasActorBegunPlay())It->RouteEndPlay(EEndPlayReason::EndPlayInEditor);GI->Shutdown();W->DestroyWorld(false);GEngine->DestroyWorldContext(W);}
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWAggregateBootstrapTest,"DoughWorld.Combat.Persistence.Aggregate.StrictFreshSlot",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWAggregateBootstrapTest::RunTest(const FString&)
{
 DWAggregateTests::FFixture F;if(!TestTrue(TEXT("Fixture"),F.Ready))return false;
 TStrongObjectPtr<UDWSaveGame> S(NewObject<UDWSaveGame>());
 if(!TestTrue(TEXT("Make approved defaults"),DWCombatPlayerSave::MakeNew(*F.GI->GetCombatRules(),*S,F.Error))||
    !TestTrue(TEXT("Fresh marker accepted only for pristine slot"),FDWCombatWorldPersistence::MarkNewAdventure(*F.GI->GetCombatRules(),*S,F.Error)))return false;
 S->CombatWorld.Player.ShieldPackets.AddDefaulted();
 TestFalse(TEXT("Schema zero does not conceal running packet data"),FDWCombatWorldPersistence::Validate(*F.GI->GetCombatRules(),*S,F.Error));
 S->CombatWorld.Player={};S->CombatWorld.Player.bRooted=true;
 TestFalse(TEXT("Fresh slot cannot contain a root"),FDWCombatWorldPersistence::Validate(*F.GI->GetCombatRules(),*S,F.Error));
 S->CombatWorld.Player={};S->Health-=1.f;
 TestFalse(TEXT("Fresh marker cannot carry changed resource mirror"),FDWCombatWorldPersistence::Validate(*F.GI->GetCombatRules(),*S,F.Error));
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWAggregateRoundtripTest,"DoughWorld.Combat.Persistence.Aggregate.SerializedWorldRoundtripAndRejectBeforeCommit",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWAggregateRoundtripTest::RunTest(const FString&)
{
 DWAggregateTests::FFixture F;if(!TestTrue(TEXT("Fixture"),F.Ready))return false;
 TStrongObjectPtr<UDWSaveGame> S(NewObject<UDWSaveGame>());
 if(!TestTrue(TEXT("Complete world capture"),FDWCombatWorldPersistence::Capture(*F.P,*S,F.Error))){AddError(F.Error);return false;}
 TArray<uint8> Bytes;if(!TestTrue(TEXT("Actual SaveGame serialization"),UGameplayStatics::SaveGameToMemory(S.Get(),Bytes)))return false;
 TStrongObjectPtr<UDWSaveGame> Loaded(Cast<UDWSaveGame>(UGameplayStatics::LoadGameFromMemory(Bytes)));
 if(!TestTrue(TEXT("Decoded complete schema"),Loaded.IsValid()&&FDWCombatWorldPersistence::Validate(*F.GI->GetCombatRules(),*Loaded,F.Error))){AddError(F.Error);return false;}
 auto* A=UDWCombatAuthoritySubsystem::Get(F.P);const auto Before=A->GetWorldEpoch();F.P->Health=43.f;
 TStrongObjectPtr<UDWSaveGame> Bad(DuplicateObject<UDWSaveGame>(Loaded.Get(),GetTransientPackage()));Bad->CombatWorld.Objects.Add(0);
 bool Committed=true;TestFalse(TEXT("Malformed block rejected"),FDWCombatWorldPersistence::Restore(*F.P,*Bad,Committed,F.Error));
 TestFalse(TEXT("No partial adoption"),Committed);TestEqual(TEXT("Live health retained"),F.P->Health,43.f);TestEqual(TEXT("Live epoch retained"),A->GetWorldEpoch(),Before);
 if(!TestTrue(TEXT("Complete restore"),FDWCombatWorldPersistence::Restore(*F.P,*Loaded,Committed,F.Error))){AddError(F.Error);return false;}
 TestTrue(TEXT("Native facts committed"),Committed);TestTrue(TEXT("Epoch advanced"),A->GetWorldEpoch()>Before);
 TestEqual(TEXT("Saved health restored"),F.P->Health,Loaded->Health);TestFalse(TEXT("Restore gate released after all participants"),F.P->IsCombatRestoreInProgress());
 TestFalse(TEXT("No formal slot ever selected"),F.GI->HasActiveSlot());return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWAggregateShieldKeysTest,"DoughWorld.Combat.Persistence.Aggregate.ShieldPendingKeyBijection",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWAggregateShieldKeysTest::RunTest(const FString&)
{
 using namespace DWCombatCommit;FLedger L;L.SetWorldEpoch(7);L.SetLife({1,1});L.SetLife({2,1});const auto Action=L.BeginAction({2,1});
 const auto Hit=L.AdmitDamage({FActionDamage{Action,3,EActionDamageKind::EnemyMelee,4},{1,1}});
 FDWCombatEffectsSave E;E.Authority.Ledger=L.CaptureSnapshot();E.Authority.LastEntity=2;
 FDWCombatActorIdentitySave Player;Player.StableId=TEXT("player:0");Player.Life={1,1};Player.Kind=EDWCombatSavedActorKind::Player;E.Authority.Actors.Add(Player);
 FDWCombatActorIdentitySave Enemy;Enemy.StableId=TEXT("enemy:test");Enemy.Life={2,1};Enemy.Kind=EDWCombatSavedActorKind::Enemy;E.Authority.Actors.Add(Enemy);
 FDWShieldPacketSave Packet;Packet.Receipt=Hit.Receipt.Handle.Id;Packet.TargetEntity=1;Packet.TargetLife=1;Packet.SourceEntity=2;Packet.SourceLife=1;
 Packet.Action=Action.Id;Packet.Phase=3;Packet.ActionKind=static_cast<uint8>(EActionDamageKind::EnemyMelee);Packet.PacketOrdinal=4;
 Packet.IncomingKind=static_cast<uint8>(EDWShieldIncomingKind::Melee);Packet.Amount=10;Packet.PreparedAmount=Packet.NotificationAmount=5;
 Packet.KnockbackSeconds=.15f;Packet.DamageType=FSoftClassPath(UDamageType::StaticClass());Packet.Remaining=.05;
 FString Error;TArray<FDWShieldPacketSave> Saved{Packet};TestTrue(TEXT("Exactly matching pending packet"),FDWCombatWorldPersistence::ValidateShieldPackets(Saved,E,Error));
 for(int Case=0;Case<4;++Case)
 {
  auto Bad=Saved;if(Case==0)++Bad[0].Phase;if(Case==1)++Bad[0].PacketOrdinal;if(Case==2)Bad[0].ActionKind=static_cast<uint8>(EActionDamageKind::Melee);if(Case==3)Bad.Reset();
  TestFalse(FString::Printf(TEXT("Forged or missing packet %d rejected"),Case),FDWCombatWorldPersistence::ValidateShieldPackets(Bad,E,Error));
 }
 return true;
}
#endif
