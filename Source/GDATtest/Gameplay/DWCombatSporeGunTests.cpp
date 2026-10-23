#if WITH_DEV_AUTOMATION_TESTS
#include "DWCombatSporeGun.h"
#include "DWCombatCommitLedger.h"
#include "DWCombatRules.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWSporePlanTest,"DoughWorld.Combat.SporeGun.PhaseSnapshotsAndTurnLimit",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWSporePlanTest::RunTest(const FString& Parameters)
{
    TSharedPtr<const FDWCombatRules> Rules;FString Error;FDWSporeGunConfig C;
    if(!TestTrue(TEXT("Strict project rules"),FDWCombatRules::LoadProjectPayload(Rules,Error)&&FDWSporeGunConfig::Build(*Rules,C,Error)))return false;
    double Raw=0.;Rules->TryGetNumber(TEXT("weapon.gun.projectile_speed_mps"),EDWCombatRuleUnit::MetersPerSecond,Raw,Error);
    TestEqual(TEXT("Speed crosses metres to UE centimetres once"),C.SpeedCmPerSecond,Raw*100.);
    FDWSporeShotPlan P;TestTrue(TEXT("Normal cycle begins"),P.Start(C,EDWSporeForm::Normal));double Taken=0.;
    TestTrue(TEXT("Exactly one windup"),P.Advance(C.WindupSeconds/C.NormalRateMultiplier,Taken));
    TestTrue(TEXT("Endpoint release is explicit"),P.EnterNext(EDWSporeForm::Yeast)&&P.GetPhase()==EDWSporePhase::Release);
    TestEqual(TEXT("Pending release consumes no extra time"),P.Remaining(),0.);
    const auto Released=C.Shot(EDWSporeForm::Yeast);
    TestTrue(TEXT("Release adopts current phase form"),P.EnterNext(EDWSporeForm::Yeast));
    TestEqual(TEXT("Recovery snapshots yeast rate"),P.Remaining(),C.RecoverySeconds/C.YeastRateMultiplier);
    P.Advance(P.Remaining(),Taken);TestTrue(TEXT("Gap samples later normal form"),P.EnterNext(EDWSporeForm::Normal));
    TestEqual(TEXT("Already released range unchanged"),Released.RangeCm,C.YeastRangeCm);
    TestEqual(TEXT("Root multiplier directly replaces normal"),C.Shot(EDWSporeForm::MoldRooted).Damage,C.Damage*C.RootDamageMultiplier);
    const auto Turn=DWSporeGun::TurnTowards(FVector::ForwardVector,FVector::RightVector,30.);
    TestTrue(TEXT("A 90-degree target only turns by the allowed 30"),FMath::IsNearlyEqual(FMath::RadiansToDegrees(FMath::Acos(Turn.X)),30.,1.e-9));
    auto Bad=C;Bad.WindupSeconds=C.IntervalSeconds;Bad.RecoverySeconds=C.IntervalSeconds;TestFalse(TEXT("Impossible phase budget rejected"),Bad.IsValid());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWSporeReceiptEmitterTest,"DoughWorld.Combat.SporeGun.AtomicReceiptAndEmitter",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWSporeReceiptEmitterTest::RunTest(const FString& Parameters)
{
    using namespace DWCombatCommit;FLedger L;L.SetWorldEpoch(1);L.SetLife({1,1});L.SetLife({2,1});
    const auto Action=L.BeginAction({1,1});const auto R=L.AdmitResource({Action,1,EResourceKind::WeaponUse,1});
    const auto E=L.CommitWeaponUseWithEmitter(R.Receipt.Handle,1);
    const auto Committed=L.FindReceipt(R.Receipt.Handle);
    TestTrue(TEXT("One commit creates receipt and released identity together"),E.Result==EEmission::Created&&Committed&&Committed->State==EReceiptState::Committed);
    TestTrue(TEXT("Same-key reentry returns identity, never another emission"),L.CommitWeaponUseWithEmitter(R.Receipt.Handle,1).Result==EEmission::Existing);
    L.EndAction(Action);L.SetLife({1,2});
    TestTrue(TEXT("Released source survives action exit and source life change"),L.AdmitDamage({FEmitterDamage{E.Handle,EEmitterDamageKind::ProjectileImpact,1},{2,1}}).Result==EAdmission::Admitted);
    L.RetireEmitter(E.Handle);TestTrue(TEXT("Retired cannot respawn"),L.CommitWeaponUseWithEmitter(R.Receipt.Handle,1).Result==EEmission::Retired);
    const auto NewAction=L.BeginAction({1,2});const auto Cancelled=L.AdmitResource({NewAction,1,EResourceKind::WeaponUse,1});L.EndAction(NewAction);
    TestTrue(TEXT("Cancelled pre-release admission cannot create actor authority"),L.CommitWeaponUseWithEmitter(Cancelled.Receipt.Handle,1).Result==EEmission::Stale);
    return true;
}
#endif
