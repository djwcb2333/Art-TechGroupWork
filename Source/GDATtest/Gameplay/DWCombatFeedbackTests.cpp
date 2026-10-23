#if WITH_DEV_AUTOMATION_TESTS
#include "DWCombatFeedbackComponent.h"
#include "DWPlayerCharacter.h"
#include "DWGameInstance.h"
#include "DWInventoryComponent.h"
#include "DWCombatTestWorld.h"
#include "Components/SkeletalMeshComponent.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWCombatFeedbackFlashTest,"DoughWorld.Combat.Feedback23.FlashCoversCharacterAndProceduralWeaponThenRestores",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWCombatFeedbackFlashTest::RunTest(const FString&)
{
    TStrongObjectPtr<UDWGameInstance> GI;UWorld* World=DWCombatTestWorld::Create(GI,TEXT("Feedback23Flash"));if(!TestNotNull(TEXT("World"),World))return false;
    FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* P=World->SpawnActor<ADWPlayerCharacter>(ADWPlayerCharacter::StaticClass(),FVector(0,0,500),FRotator::ZeroRotator,Params);
    if(P&&!P->HasActorBegunPlay())P->DispatchBeginPlay();bool Ready=P&&P->CombatFeedback&&P->HasCombatVitalsReady();
    if(TestTrue(TEXT("Real player and feedback ready"),Ready))
    {
        auto* C=P->CombatFeedback.Get();auto* Proc=NewObject<UProceduralMeshComponent>(P);P->AddInstanceComponent(Proc);Proc->RegisterComponent();
        TArray<FVector> Vertices={FVector::ZeroVector,FVector(10,0,0),FVector(0,10,0)};
        Proc->CreateMeshSection_LinearColor(0,Vertices,TArray<int32>{0,1,2},TArray<FVector>(),TArray<FVector2D>(),TArray<FLinearColor>(),TArray<FProcMeshTangent>(),false);
        auto* Original=LoadObject<UMaterialInterface>(nullptr,TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));Proc->SetMaterial(0,Original);
        auto* OriginalOverlay=P->GetMesh()->GetOverlayMaterial();const float Health=P->Health;const int64 Revision=P->Inventory->GetInventoryRevision();
        C->PlayFeedback(EDWCombatFeedbackCue::ChargeReady);
        TestTrue(TEXT("Full-charge cue starts a real flash"),C->IsFlashActive());TestEqual(TEXT("One explicit cue"),C->ChargeReadyCount,1);
        TestTrue(TEXT("Character gets overlay without replacing base atlas"),P->GetMesh()->GetOverlayMaterial()!=OriginalOverlay);
        TestTrue(TEXT("Procedural weapon gets supported material path"),Proc->GetMaterial(0)!=Original);
        C->StopFlash();TestEqual(TEXT("Character overlay restored"),P->GetMesh()->GetOverlayMaterial(),OriginalOverlay);
        TestEqual(TEXT("Procedural original restored"),Proc->GetMaterial(0),Original);
        C->PlayFeedback(EDWCombatFeedbackCue::PerfectBlock);
        const float Initial=C->GetFlashRemainingSeconds();
        C->TickComponent(100.f,LEVELTICK_All,nullptr);
        TestTrue(TEXT("New cue survives a large same-frame component delta"),C->IsFlashActive());
        TestEqual(TEXT("Only time since cue publication is charged"),C->GetFlashRemainingSeconds(),Initial);
        World->Tick(LEVELTICK_All,C->FlashDurationSeconds*.5f);
        TestTrue(TEXT("Natural world tick advances the published flash"),C->IsFlashActive()&&C->GetFlashRemainingSeconds()<Initial);
        TestTrue(TEXT("Half the visual lifetime remains"),FMath::IsNearlyEqual(C->GetFlashRemainingSeconds(),Initial*.5f,.001f));
        World->Tick(LEVELTICK_All,C->FlashDurationSeconds*.5f+.01f);
        // A synchronous second World::Tick shares the automation frame; explicitly evaluate the component at the new real world time.
        C->TickComponent(C->FlashDurationSeconds*.5f+.01f,LEVELTICK_All,nullptr);
        TestFalse(TEXT("Flash expires by actual game time"),C->IsFlashActive());TestEqual(TEXT("Expiration restores weapon"),Proc->GetMaterial(0),Original);
        TestEqual(TEXT("Presentation cannot alter health"),P->Health,Health);TestEqual(TEXT("Presentation cannot mutate inventory"),P->Inventory->GetInventoryRevision(),Revision);
    }
    for(TActorIterator<AActor> It(World);It;++It)if(It->HasActorBegunPlay())It->RouteEndPlay(EEndPlayReason::EndPlayInEditor);
    GI->Shutdown();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);return Ready;
}
#endif
