#if WITH_DEV_AUTOMATION_TESTS
#include "DWUIOffscreenComponent.h"
#include "DWSaveGame.h"
#include "Components/Border.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Widgets/SWidget.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWCinematicSaveRoundTrip,"DoughWorld.Cinematics.EventSaveRoundTrip",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWCinematicSaveRoundTrip::RunTest(const FString&)
{
 auto* S=NewObject<UDWSaveGame>();S->CompletedWorldEvents={TEXT("Bridge01")};
 FDWMapSnapshot A;A.MapPackage=TEXT("/Game/MapA");A.CompletedWorldEvents={TEXT("Bridge01"),TEXT("Gate02")};S->VisitedMaps.Add(A);
 FDWMapSnapshot B;B.MapPackage=TEXT("/Game/MapB");S->VisitedMaps.Add(B);
 TArray<uint8> Bytes;TestTrue(TEXT("Serialize without touching disk slots"),UGameplayStatics::SaveGameToMemory(S,Bytes));
 auto* R=Cast<UDWSaveGame>(UGameplayStatics::LoadGameFromMemory(Bytes));if(!TestNotNull(TEXT("Load round trip"),R))return false;
 TestEqual(TEXT("Active event retained"),R->CompletedWorldEvents.Num(),1);TestEqual(TEXT("Map A retains two events"),R->VisitedMaps[0].CompletedWorldEvents.Num(),2);TestEqual(TEXT("Map B stays intact"),R->VisitedMaps[1].CompletedWorldEvents.Num(),0);
 auto* Old=NewObject<UDWSaveGame>();Old->Version=2;Bytes.Reset();UGameplayStatics::SaveGameToMemory(Old,Bytes);auto* Loaded=Cast<UDWSaveGame>(UGameplayStatics::LoadGameFromMemory(Bytes));TestTrue(TEXT("Old version defaults to no completed events"),Loaded&&Loaded->CompletedWorldEvents.IsEmpty());
 auto* Other=NewObject<UDWSaveGame>();TestTrue(TEXT("Separate slot object has separate progress"),Other->CompletedWorldEvents.IsEmpty());return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWCinematicUIRestore,"DoughWorld.Cinematics.UITransformPreserved",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWCinematicUIRestore::RunTest(const FString&)
{
 auto* Widget=NewObject<UBorder>();Widget->SetRenderTranslation(FVector2D(37,-19));Widget->SetRenderScale(FVector2D(.8,1.1));Widget->SetVisibility(ESlateVisibility::HitTestInvisible);
 auto* C=NewObject<UDWUIOffscreenComponent>();C->bAutoExitDistance=false;C->Initialize(Widget);C->PreConstruct(false);auto Wrapper=C->RebuildWidgetWithContent(Widget->TakeWidget());C->Construct();C->HideForCinematic();C->RestoreImmediately();
 TestEqual(TEXT("Authored translation preserved"),Widget->GetRenderTransform().Translation,FVector2D(37,-19));TestEqual(TEXT("Authored scale preserved"),Widget->GetRenderTransform().Scale,FVector2D(.8,1.1));TestEqual(TEXT("Authored visibility preserved"),Widget->GetVisibility(),ESlateVisibility::HitTestInvisible);TestFalse(TEXT("Cleanup stops animation"),C->IsAnimating());TestFalse(TEXT("Cleanup shows group again"),C->IsOffscreen());
 for(float D:{.2f,.65f,1.f})for(int32 I=0;I<=100;++I)TestTrue(TEXT("Spring remains finite"),FMath::IsFinite(UDWUIOffscreenComponent::ReturnFraction(I/100.f,D,1.5f)));
 TestEqual(TEXT("Return settles exactly"),UDWUIOffscreenComponent::ReturnFraction(1,.65f,1.5f),0.f);C->Destruct();return true;
}
#endif
