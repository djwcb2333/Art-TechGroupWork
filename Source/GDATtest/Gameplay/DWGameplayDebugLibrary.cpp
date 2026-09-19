#include "DWGameplayDebugLibrary.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "InputKeyEventArgs.h"
#include "DWPlayerCharacter.h"
bool UDWGameplayDebugLibrary::SetTestKey(APlayerController* PC,FName Name,bool bPressed)
{
#if WITH_EDITOR
    if(!PC||!PC->GetWorld()||PC->GetWorld()->WorldType!=EWorldType::PIE)return false;
    const FKey Key(Name);if(!Key.IsValid())return false;
    return PC->InputKey(FInputKeyEventArgs::CreateSimulated(Key,bPressed?IE_Pressed:IE_Released,bPressed?1.f:0.f));
#else
    return false;
#endif
}
bool UDWGameplayDebugLibrary::SendTestAxis(APlayerController* PC,FName Name,float Delta)
{
#if WITH_EDITOR
    if(!PC||!PC->GetWorld()||PC->GetWorld()->WorldType!=EWorldType::PIE)return false;
    const FKey Key(Name);if(!Key.IsValid()||!Key.IsAxis1D())return false;
    return PC->InputKey(FInputKeyEventArgs::CreateSimulated(Key,IE_Axis,Delta));
#else
    return false;
#endif
}
bool UDWGameplayDebugLibrary::SetPlayerTestState(APlayerController* PC,float Health,float Transformation,float SprintElapsed,float DecayElapsed)
{
#if WITH_EDITOR
    auto* P=PC?Cast<ADWPlayerCharacter>(PC->GetPawn()):nullptr;
    if(!P||!P->GetWorld()||P->GetWorld()->WorldType!=EWorldType::PIE||P->IsDead())return false;
    if(!FMath::IsFinite(Health)||!FMath::IsFinite(Transformation)||!FMath::IsFinite(SprintElapsed)||!FMath::IsFinite(DecayElapsed))return false;
    P->Health=FMath::Max(1.f,Health);P->Transformation=FMath::Max(0.f,Transformation);
    P->SprintAlcoholElapsed=FMath::Max(0.f,SprintElapsed);P->TransformationDecayElapsed=FMath::Max(0.f,DecayElapsed);
    return true;
#else
    return false;
#endif
}
