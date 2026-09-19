#include "DWInteractionPromptComponent.h"
#include "DWUserSettings.h"
#include "DWInteractionPromptStyle.h"
#include "DWInteractionPromptWidget.h"
#include "DWGameplayConfig.h"
#include "DWGameplayHUD.h"
#include "DWLocalizationLibrary.h"
#include "DWPlayerCharacter.h"
#include "DWPlayerController.h"
#include "DWResourceNode.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/SoftObjectPath.h"

namespace
{
    // Different nearby prompts cannot chorus on the same entry frame. Weak world keys keep PIE
    // sessions independent and never retain a world after it is closed.
    TMap<TWeakObjectPtr<UWorld>, double> NextWorldPromptSoundTime;
}

UDWInteractionPromptComponent::UDWInteractionPromptComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = true;
    PrimaryComponentTick.bTickEvenWhenPaused = true;
    PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
    bAutoActivate = true;
    SetWidgetSpace(EWidgetSpace::Screen);
    SetDrawSize(FVector2D(320, 150));
    SetPivot(FVector2D(.5, 1));
    SetTickMode(ETickMode::Enabled);
    SetTickWhenOffscreen(true);
    SetWindowFocusable(false);
    bReceiveHardwareInput = false;
    SetCollisionEnabled(ECollisionEnabled::NoCollision);
    SetGenerateOverlapEvents(false);
    SetCanEverAffectNavigation(false);
    SetUsingAbsoluteScale(true);
}

void UDWInteractionPromptComponent::OnRegister()
{
    Super::OnRegister();
    RefreshWorldPlacement();
}

void UDWInteractionPromptComponent::BeginPlay()
{
    // These assets are created after the native class is compiled. Never load them on the CDO.
    if (!Style)
        Style = Cast<UDWInteractionPromptStyle>(FSoftObjectPath(TEXT("/Game/DoughWorld/Maps/Gameplay/UI/Interaction/DA_DWInteractionPromptStyle.DA_DWInteractionPromptStyle")).TryLoad());
    if (!Style) Style = NewObject<UDWInteractionPromptStyle>(this);
    if (!GetWidgetClass())
    {
        const FSoftClassPath DefaultWidget(TEXT("/Game/DoughWorld/Maps/Gameplay/UI/Interaction/WBP_DWInteractionPrompt.WBP_DWInteractionPrompt_C"));
        SetWidgetClass(DefaultWidget.TryLoadClass<UDWInteractionPromptWidget>());
    }
    if (GetWidgetClass() && !GetWidgetClass()->IsChildOf(UDWInteractionPromptWidget::StaticClass()))
    {
        UE_LOG(LogTemp, Warning, TEXT("%s: Widget Class must inherit DWInteractionPromptWidget; prompt disabled."), *GetPathName());
        SetWidgetClass(nullptr);
        bPromptEnabled = false;
    }
    else if (!GetWidgetClass())
    {
        UE_LOG(LogTemp, Warning, TEXT("%s: WBP_DWInteractionPrompt is missing. Assign a DWInteractionPromptWidget-derived Widget Class."), *GetPathName());
    }
    SetTickMode(ETickMode::Enabled);
    PrimaryComponentTick.bTickEvenWhenPaused = true;
    Super::BeginPlay();
    RefreshWorldPlacement();
    PrepareWidget();
}

void UDWInteractionPromptComponent::RefreshWorldPlacement()
{
    const AActor* Owner = GetOwner();
    if (!Owner || IsTemplate()) return;
    // A component used as the actor's root cannot offset from its own location each frame.
    // Attach it below the mesh/default scene root to use an overhead offset.
    if (Owner->GetRootComponent() == this) return;
    SetWorldLocation(Owner->GetActorLocation() + WorldOffset, false, nullptr, ETeleportType::TeleportPhysics);
    SetWorldScale3D(FVector::OneVector);
}

void UDWInteractionPromptComponent::PrepareWidget()
{
    if (!GetUserWidgetObject() && GetWidgetClass()) InitWidget();
    UDWInteractionPromptWidget* Prompt = GetPromptWidget();
    if (!Prompt) return;
    if (InitializedWidget.Get() != Prompt)
    {
        InitializedWidget = Prompt;
        AppliedStyle = Style.Get();
        Prompt->InitializePrompt(Style);
        // Both the wrapper and all its children are display only; they must never intercept LMB.
        Prompt->SetVisibility(ESlateVisibility::HitTestInvisible);
        Prompt->SetPromptVisible(false, true);
        if (bPromptDesiredVisible) Prompt->SetPromptVisible(true, true);
    }
    else if (AppliedStyle.Get() != Style.Get())
    {
        AppliedStyle = Style.Get();
        Prompt->InitializePrompt(Style);
    }
}

UDWInteractionPromptWidget* UDWInteractionPromptComponent::GetPromptWidget() const
{
    return Cast<UDWInteractionPromptWidget>(GetUserWidgetObject());
}

void UDWInteractionPromptComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    UWorld* World = GetWorld();
    if (!World || !World->IsGameWorld() || World->bIsTearingDown) return;
    RefreshWorldPlacement();
    PrepareWidget();
    UDWInteractionPromptWidget* Prompt = GetPromptWidget();
    if (!Prompt) return;

    AActor* Owner = GetOwner();
    APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
    APawn* Pawn = PC ? PC->GetPawn() : nullptr;
    const ADWPlayerCharacter* Player = Cast<ADWPlayerCharacter>(Pawn);
    const ADWPlayerController* DWPC = Cast<ADWPlayerController>(PC);
    const ADWGameplayHUD* HUD = PC ? Cast<ADWGameplayHUD>(PC->GetHUD()) : nullptr;
    const bool bBlocked = !bPromptEnabled || !IsActive() || !IsVisible() || !IsValid(Owner) || Owner->IsHidden()
        || !IsValid(PC) || !IsValid(Pawn) || !PC->IsLocalPlayerController() || World->IsPaused()
        || (Player && Player->IsDead()) || (DWPC && DWPC->IsGameplayBlocked())
        || (HUD && (!HUD->IsSessionStarted() || HUD->IsBlockingGameplay()));

    bool bShouldShow = false;
    if (!bBlocked)
    {
        const FVector Delta = Pawn->GetActorLocation() - Owner->GetActorLocation();
        const bool bWithinHeight = FMath::Abs(Delta.Z) <= FMath::Max(0.f, HeightTolerance);
        if (const ADWResourceNode* Resource = Cast<ADWResourceNode>(Owner))
        {
            bShouldShow = bWithinHeight && Player && Player->GetFocusedResource() == Resource;
            if (bShouldShow)
            {
                const UDWGameplayConfig* Config = Player->GetGameplayConfig();
                const FDWItemDefinition* Item = Config ? Config->GetItemDefinition(Resource->ItemId) : nullptr;
                const FText Title = Item ? UDWLocalizationLibrary::GetItemDisplayName(this, *Item) : FText::FromName(Resource->ItemId);
                const FText HarvestKey=DWPC?DWPC->GetActionKeyLabel(EDWInputAction::Harvest):FText::FromString(TEXT("--"));
                Prompt->SetPromptText(Title, DWText(this, TEXT("长按采集"), TEXT("Hold to gather")), HarvestKey);
                Prompt->SetProgress(Player->GetHarvestProgress(), true);
            }
        }
        else
        {
            bShouldShow = bWithinHeight && Delta.SizeSquared2D() <= FMath::Square(FMath::Max(1.f, GenericDetectionRadius));
            if (bShouldShow)
            {
                const bool bEnglish = UDWLocalizationLibrary::GetLanguage(this) == EDWGameLanguage::English;
                const FText Title = bEnglish && !GenericEnglishPromptTitle.IsEmpty() ? GenericEnglishPromptTitle : GenericPromptTitle;
                const FText Action = bEnglish && !GenericEnglishAction.IsEmpty() ? GenericEnglishAction : GenericPromptAction;
                Prompt->SetPromptText(Title, Action, GenericPromptKey);
                Prompt->SetProgress(0.f, false);
            }
        }
    }
    // Menus, death and explicit disabling hide immediately even if the world is paused.
    // Range/focus changes retain the reversible exit animation.
    ChangePromptVisibility(bShouldShow, bBlocked);
    if (!IsValid(this) || !IsValid(Prompt)) return;
    Prompt->AdvancePresentation(FMath::Max(0.f, DeltaTime));
    RefreshDebugVisibility();
}

void UDWInteractionPromptComponent::ChangePromptVisibility(bool bNewVisible, bool bImmediate)
{
    if (bChangingVisibility) return;
    UDWInteractionPromptWidget* Prompt = GetPromptWidget();
    if (!Prompt) return;
    const bool bChanged = bPromptDesiredVisible != bNewVisible;
    if (!bChanged && !bImmediate) return;
    TGuardValue<bool> VisibilityGuard(bChangingVisibility, true);
    bPromptDesiredVisible = bNewVisible;
    Prompt->SetPromptVisible(bNewVisible, bImmediate);
    RefreshDebugVisibility();
    if (!bChanged) return;
    if (bNewVisible)
    {
        ++PromptShownCount;
        TryPlayAppearSound();
        OnPromptShown();
    }
    else
    {
        ++PromptHiddenCount;
        OnPromptHidden();
    }
}

void UDWInteractionPromptComponent::TryPlayAppearSound()
{
    if (!bPlayAppearSound || !Style || !Style->AppearSound || Style->SoundVolume <= 0.f || !GetWorld()) return;
    for (auto It = NextWorldPromptSoundTime.CreateIterator(); It; ++It)
        if (!It.Key().IsValid()) It.RemoveCurrent();
    const double Now = FPlatformTime::Seconds();
    double& NextSharedTime = NextWorldPromptSoundTime.FindOrAdd(TWeakObjectPtr<UWorld>(GetWorld()));
    if (Now < NextLocalSoundTime || Now < NextSharedTime) return;
    const float Cooldown = FMath::Max(.05f, bOverrideSoundCooldown ? SoundCooldownOverride : Style->SoundCooldown);
    NextLocalSoundTime = Now + Cooldown;
    NextSharedTime = Now + Cooldown;
    UDWUserSettings::PlayFeedback(this, Style->AppearSound, FMath::Clamp(Style->SoundVolume, 0.f, 2.f), FMath::Clamp(Style->SoundPitch, .5f, 2.f));
    ++SoundPlayCount;
}

void UDWInteractionPromptComponent::RefreshDebugVisibility()
{
    const UDWInteractionPromptWidget* Prompt = GetPromptWidget();
    PresentationOpacity = Prompt ? Prompt->GetPresentationOpacity() : 0.f;
    bPromptVisible = PresentationOpacity > KINDA_SMALL_NUMBER;
}

void UDWInteractionPromptComponent::SetPromptEnabled(bool bEnabled)
{
    bPromptEnabled = bEnabled;
    if (!bEnabled) ChangePromptVisibility(false, true);
}

void UDWInteractionPromptComponent::Deactivate()
{
    ChangePromptVisibility(false, true);
    Super::Deactivate();
}

void UDWInteractionPromptComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    ChangePromptVisibility(false, true);
    InitializedWidget.Reset();
    AppliedStyle.Reset();
    Super::EndPlay(EndPlayReason);
}

#if WITH_EDITOR
void UDWInteractionPromptComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);
    RefreshWorldPlacement();
}
#endif
