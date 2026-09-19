#include "DepthAbyssPortalProxy.h"
#include "TopDownActionCharacter.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/SceneComponent.h"
#include "Components/TextBlock.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"

void UDepthAbyssPromptWidget::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    if (!WidgetTree) WidgetTree = NewObject<UWidgetTree>(this, TEXT("AbyssPromptTree"));
    UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("PromptCanvas"));
    WidgetTree->RootWidget = Canvas;
    PromptText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("PromptText"));
    PromptText->SetColorAndOpacity(FSlateColor(FLinearColor(1.f, 0.93f, 0.75f, 1.f)));
    PromptText->SetShadowColorAndOpacity(FLinearColor::Black);
    PromptText->SetShadowOffset(FVector2D(2.f, 2.f));
    PromptText->SetFontSize(22.f);
    PromptText->SetJustification(ETextJustify::Center);
    UCanvasPanelSlot* PromptSlot = Canvas->AddChildToCanvas(PromptText);
    PromptSlot->SetAnchors(FAnchors(0.5f, 0.84f));
    PromptSlot->SetAlignment(FVector2D(0.5f, 0.5f));
    PromptSlot->SetPosition(FVector2D::ZeroVector);
    PromptSlot->SetAutoSize(true);
    SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UDepthAbyssPromptWidget::SetPrompt(const FText& Text)
{
    if (PromptText) PromptText->SetText(Text);
    SetVisibility(Text.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
}

ADepthAbyssPortalProxy::ADepthAbyssPortalProxy()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickGroup = TG_PostUpdateWork;
    SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
    SetRootComponent(SceneRoot);
    Tags.Add(TEXT("SourceId:hole_abyss"));
    Tags.Add(TEXT("InteractionProxy"));
}

APawn* ADepthAbyssPortalProxy::GetLocalPawn(APlayerController*& OutController) const
{
    OutController = UGameplayStatics::GetPlayerController(this, LocalPlayerIndex);
    if (!IsValid(OutController) || !OutController->IsLocalController()) return nullptr;
    return OutController->GetPawn();
}

bool ADepthAbyssPortalProxy::CanRequestEntry() const
{
    if (!GetWorld() || GetWorld()->GetTimeSeconds() < NextEntryTime) return false;
    if (!bDestinationConfigured && !IsValid(UndergroundDestinationActor.Get())) return false;
    APlayerController* Controller = nullptr;
    APawn* Pawn = GetLocalPawn(Controller);
    if (!IsValid(Pawn) || FVector::DistSquared(Pawn->GetActorLocation(), GetActorLocation()) > FMath::Square(InteractionRadiusCm)) return false;
    if (const ATopDownActionCharacter* ActionCharacter = Cast<ATopDownActionCharacter>(Pawn))
        if (ActionCharacter->bIsRolling) return false;
    if (const ACharacter* Character = Cast<ACharacter>(Pawn))
        if (Character->GetCharacterMovement() && !Character->GetCharacterMovement()->IsMovingOnGround()) return false;
    return true;
}

bool ADepthAbyssPortalProxy::RequestEntryConfirmation()
{
    if (!CanRequestEntry()) return false;
    APlayerController* Controller = nullptr;
    ConfirmingPawn = GetLocalPawn(Controller);
    bPlayerInRange = true;
    bAwaitingConfirmation = true;
    LastEntryError = FText::GetEmpty();
    UpdatePrompt(Controller);
    return true;
}

bool ADepthAbyssPortalProxy::ConfirmEntry()
{
    APlayerController* Controller = nullptr;
    APawn* Pawn = GetLocalPawn(Controller);
    if (!bAwaitingConfirmation || !CanRequestEntry() || Pawn != ConfirmingPawn.Get())
    {
        CancelEntry();
        return false;
    }
    const FVector Destination = IsValid(UndergroundDestinationActor.Get())
        ? UndergroundDestinationActor->GetActorLocation() : UndergroundDestination;
    // Stop the existing click-move path. Keep the pawn's orientation and every camera setting.
    Controller->StopMovement();
    if (!Pawn->TeleportTo(Destination, Pawn->GetActorRotation(), false, false))
    {
        LastEntryError = NSLOCTEXT("DoughWorld", "AbyssOccupied", "落点被阻挡，暂时无法进入。按 Enter 重试，Esc 取消。");
        UpdatePrompt(Controller);
        return false;
    }
    if (ACharacter* Character = Cast<ACharacter>(Pawn))
        if (Character->GetCharacterMovement()) Character->GetCharacterMovement()->StopMovementImmediately();
    ++ConfirmedEntryCount;
    NextEntryTime = GetWorld()->GetTimeSeconds() + FMath::Max(0.f, EntryCooldownSeconds);
    CancelEntry();
    bPlayerInRange = false;
    if (PromptWidget) PromptWidget->SetPrompt(FText::GetEmpty());
    return true;
}

void ADepthAbyssPortalProxy::CancelEntry()
{
    bAwaitingConfirmation = false;
    ConfirmingPawn.Reset();
    LastEntryError = FText::GetEmpty();
}

void ADepthAbyssPortalProxy::UpdatePrompt(APlayerController* Controller)
{
    if (!IsValid(Controller) || !Controller->IsLocalController())
    {
        if (PromptWidget) PromptWidget->SetPrompt(FText::GetEmpty());
        return;
    }
    if (PromptOwner.Get() != Controller)
    {
        if (PromptWidget) PromptWidget->RemoveFromParent();
        PromptWidget = nullptr;
        PromptOwner = Controller;
    }
    if (!PromptWidget && bPlayerInRange)
    {
        PromptWidget = CreateWidget<UDepthAbyssPromptWidget>(Controller, UDepthAbyssPromptWidget::StaticClass());
        if (PromptWidget) PromptWidget->AddToPlayerScreen(30);
    }
    if (!PromptWidget) return;
    FText Text;
    if (bPlayerInRange)
    {
        if (!bDestinationConfigured && !IsValid(UndergroundDestinationActor.Get()))
            Text = NSLOCTEXT("DoughWorld", "AbyssUnavailable", "深渊之孔：入口暂未开放");
        else if (bAwaitingConfirmation)
            Text = LastEntryError.IsEmpty()
                ? NSLOCTEXT("DoughWorld", "AbyssConfirm", "跳下去，前往世界深层？\nEnter 确认进入  ·  Esc 取消\n此入口为单向通路")
                : LastEntryError;
        else
            Text = NSLOCTEXT("DoughWorld", "AbyssNear", "深渊之孔\n按 E 查看入口");
    }
    PromptWidget->SetPrompt(Text);
}

void ADepthAbyssPortalProxy::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (GetNetMode() == NM_DedicatedServer) return;
    APlayerController* Controller = nullptr;
    APawn* Pawn = GetLocalPawn(Controller);
    bPlayerInRange = IsValid(Pawn) &&
        FVector::DistSquared(Pawn->GetActorLocation(), GetActorLocation()) <= FMath::Square(InteractionRadiusCm);
    if (!bPlayerInRange || (bAwaitingConfirmation && ConfirmingPawn.Get() != Pawn)) CancelEntry();
    if (IsValid(Controller))
    {
        if (Controller->WasInputKeyJustPressed(EKeys::Escape)) CancelEntry();
        else if (bPlayerInRange && Controller->WasInputKeyJustPressed(EKeys::E)) RequestEntryConfirmation();
        else if (bAwaitingConfirmation && Controller->WasInputKeyJustPressed(EKeys::Enter)) ConfirmEntry();
    }
    UpdatePrompt(Controller);
}

void ADepthAbyssPortalProxy::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    CancelEntry();
    if (PromptWidget) PromptWidget->RemoveFromParent();
    PromptWidget = nullptr;
    PromptOwner.Reset();
    Super::EndPlay(EndPlayReason);
}
