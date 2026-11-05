#include "DWInteractionPromptComponent.h"
#include "DWUserSettings.h"
#include "DWInteractionPromptStyle.h"
#include "DWInteractionPromptWidget.h"
#include "DWGameplayConfig.h"
#include "DWGameInstance.h"
#include "DWGameplayHUD.h"
#include "DWLocalizationLibrary.h"
#include "DWPlayerCharacter.h"
#include "DWPlayerController.h"
#include "DWLanguageCarrier.h"
#include "DWLanguageUIBridgeComponent.h"
#include "DWResourceNode.h"
#include "DWGatheringWorld.h"
#include "DWWorldPickup.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/SoftObjectPath.h"

namespace
{
    DWDemolitionPromptLease::Id DemolitionId(FGuid Id){return {Id.A,Id.B,Id.C,Id.D};}
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

bool UDWInteractionPromptComponent::UsesGatheringPromptBudget() const
{
    const UWorld* World=GetWorld();
    return World && World->IsGameWorld() &&
        (Cast<IDWWorldPickup>(GetOwner()) || (bBudgetGatheringResourcePrompt && Cast<ADWGatheringResource>(GetOwner())));
}

void UDWInteractionPromptComponent::InitWidget()
{
    // UWidgetComponent::BeginPlay calls this virtual function. Keep the authored
    // WidgetClass intact, but defer the instance for opted-in gathering resources.
    if(UsesGatheringPromptBudget() && !bAllowBudgetedWidgetInit && !GetUserWidgetObject())return;
    Super::InitWidget();
}

bool UDWInteractionPromptComponent::IsGatheringPromptNearLocalPawn() const
{
    const UWorld* World=GetWorld();const AActor* Owner=GetOwner();
    if(!World||World->bIsTearingDown||World->IsPaused()||!bPromptEnabled||!IsActive()||!IsVisible()
        ||!IsValid(Owner)||Owner->IsActorBeingDestroyed()||Owner->IsHidden())return false;
    const APlayerController* PC=UGameplayStatics::GetPlayerController(this,0);
    const APawn* Pawn=PC?PC->GetPawn():nullptr;
    if(!PC||!PC->IsLocalPlayerController()||!IsValid(Pawn))return false;
    if (const auto* Pickup = Cast<IDWWorldPickup>(Owner))
        return Pickup->IsPickupAvailable() && Pickup->IsPickupInReach(Pawn);
    const FVector Delta=Pawn->GetActorLocation()-Owner->GetActorLocation();
    return FMath::Abs(Delta.Z)<=FMath::Max(0.f,HeightTolerance)
        &&Delta.SizeSquared2D()<=FMath::Square(FMath::Max(1.f,GenericDetectionRadius));
}

void UDWInteractionPromptComponent::WakeGatheringPromptBudget()
{
    if(!bBudgetOwnsTickInterval)return;
    // A caller may have changed the tick interval after this component set it.
    if(FMath::IsNearlyEqual(GetComponentTickInterval(),BudgetTickInterval))
        SetComponentTickIntervalAndCooldown(0.f);
    bBudgetOwnsTickInterval=false;
}

void UDWInteractionPromptComponent::UpdateGatheringPromptBudget(bool bIdle)
{
    if(!UsesGatheringPromptBudget()){WakeGatheringPromptBudget();return;}
    const float Current=GetComponentTickInterval();
    if(bBudgetOwnsTickInterval&&!FMath::IsNearlyEqual(Current,BudgetTickInterval))
    {bBudgetOwnsTickInterval=false;return;} // Preserve externally configured intervals.
    const float Interval=FMath::IsFinite(GatheringHiddenPollSeconds)?FMath::Clamp(GatheringHiddenPollSeconds,0.f,.5f):0.f;
    if(!bIdle||Interval<=KINDA_SMALL_NUMBER){WakeGatheringPromptBudget();return;}
    if(!bBudgetOwnsTickInterval&&!FMath::IsNearlyZero(Current))return; // Honor an authored interval.
    if(!bBudgetOwnsTickInterval||!FMath::IsNearlyEqual(Current,Interval))
    {
        SetComponentTickInterval(Interval);BudgetTickInterval=Interval;bBudgetOwnsTickInterval=true;
    }
}

void UDWInteractionPromptComponent::BeginPlay()
{
    // These assets are created after the native class is compiled. Never load them on the CDO.
    if (!Style)
        Style = Cast<UDWInteractionPromptStyle>(FSoftObjectPath(TEXT("/Game/DoughWorld/UI/Interaction/DA_DWInteractionPromptStyle.DA_DWInteractionPromptStyle")).TryLoad());
    if (!Style) Style = NewObject<UDWInteractionPromptStyle>(this);
    if (!GetWidgetClass())
    {
        const FSoftClassPath DefaultWidget(TEXT("/Game/DoughWorld/UI/Interaction/WBP_DWInteractionPrompt.WBP_DWInteractionPrompt_C"));
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
    if(!UsesGatheringPromptBudget())PrepareWidget();
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
    if (!GetUserWidgetObject() && GetWidgetClass())
    {
        TGuardValue<bool> AllowInit(bAllowBudgetedWidgetInit,true);
        InitWidget();
    }
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

void UDWInteractionPromptComponent::NotifyHarvestCommitted(int32 ActualAmount)
{
    // No fraction-reset heuristic: only ResourceNode's accepted commit calls this path.
    // A blocked/full bag, interrupted hold, or restored stock never emits completion.
    UWorld* World=GetWorld();
    if(ActualAmount<=0||!World||!World->IsGameWorld()||World->bIsTearingDown||World->IsPaused()
        ||!bPromptEnabled||!IsActive()||!IsVisible()||!IsValid(GetOwner())||GetOwner()->IsHidden())return;
    APlayerController* PC=UGameplayStatics::GetPlayerController(this,0);
    ADWPlayerCharacter* Player=PC?Cast<ADWPlayerCharacter>(PC->GetPawn()):nullptr;
    const ADWPlayerController* DWPC=Cast<ADWPlayerController>(PC);
    const ADWGameplayHUD* HUD=PC?Cast<ADWGameplayHUD>(PC->GetHUD()):nullptr;
    const ADWResourceNode* Resource=Cast<ADWResourceNode>(GetOwner());
    if(!PC||!PC->IsLocalPlayerController()||!Player||Player->IsDead()||!Resource
        ||(DWPC&&DWPC->IsGameplayBlocked())||(HUD&&(!HUD->IsSessionStarted()||HUD->IsBlockingGameplay())))return;
    // GetFocusedResource becomes null immediately on depletion, so include the currently
    // visible target. This is what keeps the final successful batch visible for its pulse.
    if(!bPromptDesiredVisible&&Player->GetFocusedResource()!=Resource)return;
    PrepareWidget();
    if(UDWInteractionPromptWidget* Prompt=GetPromptWidget())
    {
        if(!Prompt->bUseC01Presentation)return;
        C04CompletionPawn=Player;
        C04CompletionDeadline=FPlatformTime::Seconds()+double(Prompt->GetCompletionDisplaySeconds());
        Prompt->NotifyHarvestCompleted();
    }
}

bool UDWInteractionPromptComponent::NotifyGatheringHitCommitted(ADWPlayerCharacter* User,float PreviousProgress,float CurrentProgress)
{
    UWorld* World=GetWorld();
    ADWGatheringResource* Resource=Cast<ADWGatheringResource>(GetOwner());
    if(!FMath::IsFinite(PreviousProgress)||!FMath::IsFinite(CurrentProgress)
        ||PreviousProgress<0.f||CurrentProgress>1.f||CurrentProgress<=PreviousProgress
        ||!World||!World->IsGameWorld()||World->bIsTearingDown||World->IsPaused()
        ||!bPromptEnabled||!IsActive()||!IsVisible()||!IsValid(Resource)||Resource->IsActorBeingDestroyed()
        ||Resource->IsHidden()||Resource->IsStaged()||!IsValid(User)||User->IsDead()||User->IsActorBeingDestroyed()
        ||!Resource->IsWithinReach(User))return false;
    APlayerController* PC=UGameplayStatics::GetPlayerController(this,0);
    const ADWPlayerController* DWPC=Cast<ADWPlayerController>(PC);
    const ADWGameplayHUD* HUD=PC?Cast<ADWGameplayHUD>(PC->GetHUD()):nullptr;
    if(!PC||!PC->IsLocalPlayerController()||PC->GetPawn()!=User
        ||(DWPC&&DWPC->IsGameplayBlocked())||(HUD&&(!HUD->IsSessionStarted()||HUD->IsBlockingGameplay())))return false;
    // Reject stale/fabricated progress and repeated calls for the same accepted step.
    // Explicit node restore calls ResetGatheringHitFeedback; Tick also notices a rewind.
    if(!FMath::IsNearlyEqual(Resource->GetGatheringProgress(),CurrentProgress,UE_SMALL_NUMBER)
        ||CurrentProgress<=C06LastNotifiedGatheringProgress)return false;
    PrepareWidget();
    UDWInteractionPromptWidget* Prompt=GetPromptWidget();
    if(!Prompt||!Prompt->bUseC01Presentation)return false;
    WakeGatheringPromptBudget();
    C06LastNotifiedGatheringProgress=CurrentProgress; // record before any UI event can re-enter
    C06GatheringFeedbackPawn=User;
    C06GatheringFeedbackDeadline=FPlatformTime::Seconds()+double(Prompt->GetCompletionDisplaySeconds());
    // Supply current title/action on a hit even if this is the first component tick.
    const bool bEnglish=UDWLocalizationLibrary::GetLanguage(this)==EDWGameLanguage::English;
    Prompt->SetPickupIcon(nullptr,false);
    Prompt->SetPromptText(bEnglish&&!GenericEnglishPromptTitle.IsEmpty()?GenericEnglishPromptTitle:GenericPromptTitle,
        bEnglish&&!GenericEnglishAction.IsEmpty()?GenericEnglishAction:GenericPromptAction,
        DWPC?DWPC->GetActionKeyLabel(EDWInputAction::Harvest):GenericPromptKey);
    Prompt->SetPromptInputKey(DWPC?DWPC->GetAppliedActionKey(EDWInputAction::Harvest):FKey());
    Prompt->NotifyGatheringProgressCommitted(PreviousProgress,CurrentProgress);
    return true;
}
bool UDWInteractionPromptComponent::HasPendingGatheringHitFeedback() const
{
    const UWorld* World=GetWorld();
    const ADWPlayerCharacter* Player=Cast<ADWPlayerCharacter>(C06GatheringFeedbackPawn.Get());
    const ADWGatheringResource* Resource=Cast<ADWGatheringResource>(GetOwner());
    return C06GatheringFeedbackDeadline>FPlatformTime::Seconds()&&World&&!World->bIsTearingDown&&!World->IsPaused()
        &&IsValid(Player)&&!Player->IsDead()&&IsValid(Resource)&&!Resource->IsActorBeingDestroyed()
        &&!Resource->IsStaged()&&Resource->IsWithinReach(Player);
}
void UDWInteractionPromptComponent::ResetGatheringHitFeedback()
{
    C06GatheringFeedbackDeadline=0.;C06LastNotifiedGatheringProgress=-1.f;C06GatheringFeedbackPawn.Reset();
    if(auto* Prompt=GetPromptWidget())
    {
        const auto* Resource=Cast<ADWGatheringResource>(GetOwner());
        Prompt->ResetHarvestFeedback(IsValid(Resource)?Resource->GetGatheringProgress():0.f);
    }
}

bool UDWInteractionPromptComponent::BeginDemolitionSession(APawn* User,FGuid Session,FKey AppliedKey)
{
    UWorld* World=GetWorld();const double Now=FPlatformTime::Seconds();
    if(!Session.IsValid()||!AppliedKey.IsValid()||!IsValid(User)||User->GetWorld()!=World||
       !World||!World->IsGameWorld()||World->bIsTearingDown||World->IsPaused()||!IsActive()||
       !IsValid(GetOwner())||GetOwner()->IsActorBeingDestroyed()||!Cast<ADWGatheringStation>(GetOwner()))return false;
    APlayerController* PC=UGameplayStatics::GetPlayerController(this,0);
    if(!IsValid(PC)||!PC->IsLocalPlayerController()||PC->GetPawn()!=User)return false;
    const float Timeout=FMath::IsFinite(DemolitionStaleSeconds)?FMath::Clamp(DemolitionStaleSeconds,.1f,2.f):.5f;
    if(!DemolitionLease.Current(Now,Timeout))ResetDemolitionPresentation();
    if(!DemolitionLease.Begin(DemolitionId(Session),Now))return false;
    DemolitionPawn=User;DemolitionKey=AppliedKey;WakeGatheringPromptBudget();return true;
}
bool UDWInteractionPromptComponent::UpdateDemolitionSession(FGuid Session,float AuthoritativeProgress)
{
    const auto Id=DemolitionId(Session);
    if(!DemolitionLease.Matches(Id))return false;
    UWorld* World=GetWorld();
    if(!World||World->bIsTearingDown||World->IsPaused()||!IsValid(DemolitionPawn.Get())||
       !IsValid(GetOwner())||GetOwner()->IsActorBeingDestroyed()){ResetDemolitionPresentation();return false;}
    const float Timeout=FMath::IsFinite(DemolitionStaleSeconds)?FMath::Clamp(DemolitionStaleSeconds,.1f,2.f):.5f;
    const double Now=FPlatformTime::Seconds();
    if(!DemolitionLease.Current(Now,Timeout)){ResetDemolitionPresentation();return false;}
    return DemolitionLease.Update(Id,AuthoritativeProgress,Now,Timeout);
}
void UDWInteractionPromptComponent::EndDemolitionSession(FGuid Session)
{
    if(DemolitionLease.End(DemolitionId(Session)))ResetDemolitionPresentation();
}
void UDWInteractionPromptComponent::ResetDemolitionPresentation()
{
    DemolitionLease.Reset();DemolitionPawn.Reset();DemolitionKey=FKey();
    if(auto* Prompt=GetPromptWidget())Prompt->SetDemolitionPresentation(false,0.f);
}

void UDWInteractionPromptComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    const bool bWasBudgetIdle=bBudgetOwnsTickInterval;
    UWorld* World = GetWorld();
    if (!World || !World->IsGameWorld() || World->bIsTearingDown)
    {Super::TickComponent(DeltaTime, TickType, ThisTickFunction);return;}
    // A fully hidden, distant opted-in resource has no visual work to advance.
    // Skip the WidgetComponent/Slate path even after a previous visit created its widget.
    // Visible fades, near prompts and committed-hit feedback retain the original full update.
    if(UsesGatheringPromptBudget()&&!IsGatheringPromptNearLocalPawn()
        &&!bPromptDesiredVisible&&!bPromptVisible&&!HasPendingGatheringHitFeedback()
        &&C06GatheringFeedbackDeadline<=FPlatformTime::Seconds())
    {
        UpdateGatheringPromptBudget(true);
        return;
    }
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    RefreshWorldPlacement();
    PrepareWidget();
    UDWInteractionPromptWidget* Prompt = GetPromptWidget();
    if (!Prompt) return;

    AActor* Owner = GetOwner();
    if (!Cast<IDWWorldPickup>(Owner)) Prompt->SetPickupIcon(nullptr, false);
    if(const auto* Node=Cast<ADWGatheringResource>(Owner))
        if(Node->GetGatheringProgress()<C06LastNotifiedGatheringProgress)ResetGatheringHitFeedback();
    APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
    APawn* Pawn = PC ? PC->GetPawn() : nullptr;
    const ADWPlayerCharacter* Player = Cast<ADWPlayerCharacter>(Pawn);
    const ADWPlayerController* DWPC = Cast<ADWPlayerController>(PC);
    const ADWGameplayHUD* HUD = PC ? Cast<ADWGameplayHUD>(PC->GetHUD()) : nullptr;
    const float DemolitionTimeout=FMath::IsFinite(DemolitionStaleSeconds)?FMath::Clamp(DemolitionStaleSeconds,.1f,2.f):.5f;
    const bool bDemolition=DemolitionLease.Current(FPlatformTime::Seconds(),DemolitionTimeout)&&DemolitionPawn.Get()==Pawn&&IsValid(Pawn)&&IsValid(Owner)
        &&FMath::Abs(Pawn->GetActorLocation().Z-Owner->GetActorLocation().Z)<=FMath::Max(0.f,HeightTolerance)
        &&FVector::DistSquared2D(Pawn->GetActorLocation(),Owner->GetActorLocation())<=FMath::Square(FMath::Max(1.f,GenericDetectionRadius));
    const bool bBlocked = (!bPromptEnabled&&!bDemolition) || !IsActive() || !IsVisible() || !IsValid(Owner) || Owner->IsHidden()
        || !IsValid(PC) || !IsValid(Pawn) || !PC->IsLocalPlayerController() || World->IsPaused()
        || (Player && Player->IsDead()) || (DWPC && DWPC->IsGameplayBlocked())
        || (HUD && (!HUD->IsSessionStarted() || HUD->IsBlockingGameplay()));
    if(bBlocked||!bDemolition)ResetDemolitionPresentation();
    else Prompt->SetDemolitionPresentation(true,float(DemolitionLease.DisplayProgress()));
    if(!IsValid(this)||!IsValid(Prompt))return;
    if(bDemolition&&(!DemolitionLease.Current(FPlatformTime::Seconds(),DemolitionTimeout)||DemolitionPawn.Get()!=Pawn||!IsValid(Pawn)||!IsValid(Owner)))return;
    if(bBlocked||C04CompletionPawn.Get()!=Pawn)
    {
        C04CompletionDeadline=0.;
        C04CompletionPawn.Reset();
    }

    if(bBlocked||C06GatheringFeedbackPawn.Get()!=Pawn)
    {C06GatheringFeedbackDeadline=0.;C06GatheringFeedbackPawn.Reset();}
    bool bShouldShow = false;
    if (!bBlocked)
    {
        const FVector Delta = Pawn->GetActorLocation() - Owner->GetActorLocation();
        const bool bWithinHeight = FMath::Abs(Delta.Z) <= FMath::Max(0.f, HeightTolerance);
        if(bDemolition)
        {
            bShouldShow=true;
            const bool bEnglish=UDWLocalizationLibrary::GetLanguage(this)==EDWGameLanguage::English;
            const FText Title=bEnglish&&!GenericEnglishPromptTitle.IsEmpty()?GenericEnglishPromptTitle:GenericPromptTitle;
            Prompt->SetPromptText(Title,DWText(this,TEXT("长按拆除"),TEXT("Hold to dismantle")),FText::GetEmpty());
        }
        else if (const ADWResourceNode* Resource = Cast<ADWResourceNode>(Owner))
        {
            const bool bFocused=bWithinHeight&&Player&&Player->GetFocusedResource()==Resource;
            const bool bRetainCompletion=bWithinHeight&&C04CompletionDeadline>FPlatformTime::Seconds()
                &&Delta.SizeSquared2D()<=FMath::Square(FMath::Max(1.f,Resource->InteractionRadius));
            bShouldShow=bFocused||bRetainCompletion;
            if (bFocused)
            {
                const UDWGameplayConfig* Config = Player->GetGameplayConfig();
                const FDWItemDefinition* Item = Config ? Config->GetItemDefinition(Resource->ItemId) : nullptr;
                const FText Title = Item ? UDWLocalizationLibrary::GetItemDisplayName(this, *Item) : FText::FromName(Resource->ItemId);
                const FText HarvestKey=DWPC?DWPC->GetActionKeyLabel(EDWInputAction::Harvest):FText::FromString(TEXT("--"));
                Prompt->SetPromptText(Title, DWText(this, TEXT("长按采集"), TEXT("Hold to gather")), HarvestKey);
                Prompt->SetProgress(Player->GetHarvestProgress(), true);
            }
            else if(bRetainCompletion)
            {
                // Retain the final title and a completely filled panel while the depleted
                // resource's completion spring settles. No interaction remains enabled here.
                Prompt->SetProgress(1.f,true);
            }
        }
        else if(const ADWGatheringResource* GatheringResource=Cast<ADWGatheringResource>(Owner))
        {
            const bool bInRange=bWithinHeight&&Delta.SizeSquared2D()<=FMath::Square(FMath::Max(1.f,GenericDetectionRadius));
            const bool bRetainHit=bInRange&&HasPendingGatheringHitFeedback();
            bShouldShow=bInRange&&(GatheringResource->IsInteractionAvailable()||bRetainHit);
            if(bShouldShow)
            {
                const bool bEnglish=UDWLocalizationLibrary::GetLanguage(this)==EDWGameLanguage::English;
                const FText Title=bEnglish&&!GenericEnglishPromptTitle.IsEmpty()?GenericEnglishPromptTitle:GenericPromptTitle;
                const FText Action=bEnglish&&!GenericEnglishAction.IsEmpty()?GenericEnglishAction:GenericPromptAction;
                const FText Key=DWPC?DWPC->GetActionKeyLabel(EDWInputAction::Harvest):GenericPromptKey;
                Prompt->SetPromptText(Title,Action,Key);
                // Read node health, not a per-player hold timer. Walking away never resets progress.
                Prompt->SetProgress(GatheringResource->GetGatheringProgress(),true);
            }
        }
        else if (const auto* Pickup = Cast<IDWWorldPickup>(Owner))
        {
            bShouldShow = Pickup->IsPickupAvailable() && Pickup->IsPickupInReach(Pawn);
            if(bShouldShow)
            {
                const UDWGameplayConfig* Config=Player?Player->GetGameplayConfig():nullptr;
                if(!Config)if(auto* GI=World->GetGameInstance<UDWGameInstance>())Config=GI->GetConfig();
                const FName ItemId = Pickup->GetPickupItemId();
                const FDWItemDefinition* Item = Config ? Config->GetItemDefinition(ItemId) : nullptr;
                const bool bEnglish = UDWLocalizationLibrary::GetLanguage(this) == EDWGameLanguage::English;
                const FText Title = Item ? UDWLocalizationLibrary::GetItemDisplayName(this, *Item) :
                    (ItemId.IsNone() ? (bEnglish ? GenericEnglishPromptTitle : GenericPromptTitle) : FText::FromName(ItemId));
                Prompt->SetPickupIcon(Item ? Item->Icon.Get() : nullptr, !ItemId.IsNone());
                Prompt->SetPromptText(Title, bEnglish && !GenericEnglishAction.IsEmpty() ? GenericEnglishAction : GenericPromptAction, GenericPromptKey);
                Prompt->SetProgress(0.f,false);
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
    // Presentation only. Read the applied keys each tick, not the editable pending fields.
    // Resource/pickup use the real Harvest action; unrelated authored generic actors keep text.
    bool bApplied=Cast<ADWResourceNode>(Owner)||Cast<ADWGatheringResource>(Owner)||Cast<IDWWorldPickup>(Owner);
    FKey Primary=DWPC?DWPC->GetAppliedActionKey(EDWInputAction::Harvest):FKey(),ReadKey;
    if(bDemolition&&!bBlocked){bApplied=true;Primary=DemolitionKey;}
    bool bSecondary=false;
    if(const auto* Carrier=Cast<ADWLanguageCarrier>(Owner))
    {
        const auto* Bridge=UDWLanguageUIBridgeComponent::Find(PC);
        bApplied=true;Primary=FKey();
        const bool bBridgeReady=Bridge&&Bridge->GetWorldPromptKeys(Primary,ReadKey);
        bSecondary=bBridgeReady&&Carrier->Kind==EDWLanguageCarrierKind::WaterSource;
    }
    Prompt->SetPromptInputKey(Primary,bApplied);
    Prompt->SetSecondaryPrompt(bSecondary?DWText(this,TEXT("查看文字"),TEXT("Read inscription")):FText::GetEmpty(),ReadKey,bSecondary);
    // Menus, death and explicit disabling hide immediately even if the world is paused.
    // Range/focus changes retain the reversible exit animation.
    const bool bBeginningAppear=bShouldShow&&!bPromptDesiredVisible;
    ChangePromptVisibility(bShouldShow, bBlocked);
    if (!IsValid(this) || !IsValid(Prompt)) return;
    // The accumulated idle polling interval occurred before entering range, not during the animation.
    Prompt->AdvancePresentation(bWasBudgetIdle&&bBeginningAppear?0.f:FMath::Max(0.f, DeltaTime));
    const FVector2D RequiredDrawSize=Prompt->GetMinimumDrawSize(),PromptDrawSize=GetDrawSize();
    const FVector2D SafeDrawSize(FMath::Max(PromptDrawSize.X,RequiredDrawSize.X),FMath::Max(PromptDrawSize.Y,RequiredDrawSize.Y));
    if(!SafeDrawSize.Equals(PromptDrawSize,.1))SetDrawSize(SafeDrawSize);
    RefreshDebugVisibility();
    UpdateGatheringPromptBudget(!bPromptDesiredVisible&&!bPromptVisible&&!HasPendingGatheringHitFeedback());
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
    if(bEnabled&&!bPromptEnabled)WakeGatheringPromptBudget();
    bPromptEnabled = bEnabled;
    if (!bEnabled){ResetDemolitionPresentation();C04CompletionDeadline=0.;C04CompletionPawn.Reset();C06GatheringFeedbackDeadline=0.;C06GatheringFeedbackPawn.Reset();ChangePromptVisibility(false, true);}
}

void UDWInteractionPromptComponent::Deactivate()
{
    ResetDemolitionPresentation();
    ResetGatheringHitFeedback();
    C04CompletionDeadline=0.;C04CompletionPawn.Reset();
    ChangePromptVisibility(false, true);
    Super::Deactivate();
}

void UDWInteractionPromptComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    ResetDemolitionPresentation();
    ResetGatheringHitFeedback();
    C04CompletionDeadline=0.;C04CompletionPawn.Reset();
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
