#include "DWPlayerController.h"
#include "DWGatheringComponent.h"
#include "DWCombatHUDModel.h"
#include "DWCombatInputAdapter.h"
#include "Misc/ScopeExit.h"
#include "DWWorldEvent.h"
#include "Camera/PlayerCameraManager.h"
#include "DWUserSettings.h"
#include "Misc/ConfigCacheIni.h"
#include "DWPlayerCharacter.h"
#include "DWWeaponAnimationComponent.h"
#include "DWGameplayHUD.h"
#include "DWGameplayWidget.h"
#include "DWGameInstance.h"
#include "DWLoadingTransition.h"
#include "DWGameplayConfig.h"
#include "DWLocalizationLibrary.h"
#include "Components/InputComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerInput.h"
#include "Engine/World.h"
#include "InputCoreTypes.h"

/** One separately identifiable delegate owner per binding, independent of the controller's Blueprint delegates. */
struct FDWInputBindingRelay
{
    TFunction<void()> Callback;
    void Execute() { if (Callback) Callback(); }
};

namespace
{
    TArray<FKey> DefaultDWKeys()
    {
        return {EKeys::W,EKeys::S,EKeys::A,EKeys::D,EKeys::SpaceBar,EKeys::LeftShift,EKeys::RightShift,
            EKeys::F,EKeys::E,EKeys::LeftMouseButton,EKeys::MiddleMouseButton,EKeys::B,EKeys::Tab,EKeys::Escape,EKeys::P,EKeys::RightMouseButton,EKeys::C,EKeys::H,EKeys::I,EKeys::Q,EKeys::E,EKeys::R,EKeys::Zero,EKeys::One,EKeys::Two,EKeys::Three,FKey(),FKey(),FKey(),FKey(),FKey()};
    }

    FText DWKeyLabel(const UObject* Context, const FKey Key)
    {
        if (!Key.IsValid()) return FText::FromString(TEXT("--"));
        struct FLabel { FKey Key; const TCHAR* Chinese; const TCHAR* English; };
        const FLabel Labels[] = {
            {EKeys::SpaceBar,TEXT("空格"),TEXT("Space")}, {EKeys::Escape,TEXT("Esc"),TEXT("Esc")},
            {EKeys::LeftMouseButton,TEXT("左键"),TEXT("LMB")}, {EKeys::RightMouseButton,TEXT("右键"),TEXT("RMB")},
            {EKeys::MiddleMouseButton,TEXT("中键"),TEXT("MMB")}, {EKeys::ThumbMouseButton,TEXT("鼠标4"),TEXT("Mouse4")},
            {EKeys::ThumbMouseButton2,TEXT("鼠标5"),TEXT("Mouse5")},
            {EKeys::LeftShift,TEXT("Shift"),TEXT("Shift")}, {EKeys::RightShift,TEXT("右Shift"),TEXT("RShift")},
            {EKeys::LeftControl,TEXT("Ctrl"),TEXT("Ctrl")}, {EKeys::RightControl,TEXT("右Ctrl"),TEXT("RCtrl")},
            {EKeys::LeftAlt,TEXT("Alt"),TEXT("Alt")}, {EKeys::RightAlt,TEXT("右Alt"),TEXT("RAlt")},
            {EKeys::Enter,TEXT("回车"),TEXT("Enter")}, {EKeys::BackSpace,TEXT("退格"),TEXT("Backspace")},
            {EKeys::Tab,TEXT("Tab"),TEXT("Tab")}, {EKeys::Delete,TEXT("Delete"),TEXT("Delete")},
            {EKeys::Insert,TEXT("Insert"),TEXT("Insert")}, {EKeys::Home,TEXT("Home"),TEXT("Home")},
            {EKeys::End,TEXT("End"),TEXT("End")}, {EKeys::PageUp,TEXT("PageUp"),TEXT("PgUp")},
            {EKeys::PageDown,TEXT("PageDown"),TEXT("PgDn")},
            {EKeys::Up,TEXT("上箭头"),TEXT("Up")}, {EKeys::Down,TEXT("下箭头"),TEXT("Down")},
            {EKeys::Left,TEXT("左箭头"),TEXT("Left")}, {EKeys::Right,TEXT("右箭头"),TEXT("Right")},
            {EKeys::Zero,TEXT("0"),TEXT("0")}, {EKeys::One,TEXT("1"),TEXT("1")},
            {EKeys::Two,TEXT("2"),TEXT("2")}, {EKeys::Three,TEXT("3"),TEXT("3")},
            {EKeys::Four,TEXT("4"),TEXT("4")}, {EKeys::Five,TEXT("5"),TEXT("5")},
            {EKeys::Six,TEXT("6"),TEXT("6")}, {EKeys::Seven,TEXT("7"),TEXT("7")},
            {EKeys::Eight,TEXT("8"),TEXT("8")}, {EKeys::Nine,TEXT("9"),TEXT("9")}
        };
        for (const FLabel& Label : Labels)
            if (Key == Label.Key) return DWText(Context, Label.Chinese, Label.English);
        // Canonical key names keep fallback labels independent of the editor's language.
        return FText::FromName(Key.GetFName());
    }
}

ADWPlayerController::ADWPlayerController(){bShowMouseCursor=true;DefaultMouseCursor=EMouseCursor::Crosshairs;bShouldPerformFullTickWhenPaused=true;}
ADWPlayerCharacter* ADWPlayerController::Player()const{return Cast<ADWPlayerCharacter>(GetPawn());}
void ADWPlayerController::GetPlayerViewPoint(FVector& Location,FRotator& Rotation)const
{
 // At destination time 0, the stock controller rejects even an initialized camera
 // cache because its timestamp is 0, and renders the pawn's eye-level transform.
 // Loading deliberately pauses that clock. Use the primed POV for our pawn.
 if(PlayerCameraManager)if(const auto* P=Cast<ADWPlayerCharacter>(GetViewTarget());P&&P->HasActorBegunPlay())
 {PlayerCameraManager->GetCameraViewPoint(Location,Rotation);return;}
 Super::GetPlayerViewPoint(Location,Rotation);
}
void ADWPlayerController::BeginPlay()
{
    Super::BeginPlay();
    if(auto* Settings=UDWUserSettings::Resolve(this))Settings->ApplyAudio();
    // Existing Blueprint defaults can override constructor values. Paused camera updates
    // are gated by this runtime flag in UWorld::Tick, separately from SpringArm updates.
    bShouldPerformFullTickWhenPaused=true;
    SetTickableWhenPaused(true);
    // SceneViewFamily otherwise freezes temporal view history when the title pauses
    // before the first correct pawn view, even though CameraManager's POV is current.
    if(UWorld* World=GetWorld())World->bIsCameraMoveableWhenPaused=true;
    FInputModeGameAndUI Mode;Mode.SetHideCursorDuringCapture(false);Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);SetInputMode(Mode);
    CombatInputAdapter=FDWCombatInputAdapter::Create(this);
    ImportLegacyCombatShortcutDefaults();
    RefreshCombatInputKeys();
    if(IsLocalPlayerController()&&!CombatInputAdapter.IsValid())
        UE_LOG(LogTemp,Warning,TEXT("DoughWorld physical combat input observer is unavailable; controller combat presses remain disabled."));
}
void ADWPlayerController::SetupInputComponent()
{
    Super::SetupInputComponent();
    LoadSavedBindings();
    FText Error;
    if (!ApplyInputBindings(Error))
    {
        // Invalid Blueprint defaults must not strand the user without a pause/menu key.
        UE_LOG(LogTemp, Warning, TEXT("DoughWorld input: %s Falling back to the original key layout."), *Error.ToString());
        PendingKeys = DefaultDWKeys();
        bInputBindingsPending = true;
    }
    // Setup is outside input processing; initial bindings are ready for the first frame.
    CommitQueuedBindings();
}

TArray<FKey> ADWPlayerController::GetConfiguredKeys() const
{
    return {KeyMoveForward,KeyMoveBackward,KeyMoveLeft,KeyMoveRight,KeyDash,KeySprint,KeySprintAlternate,
        KeyHarvest,KeyTransform,KeyThrow,KeyCameraDrag,KeyInventory,KeyCrafting,KeyPauseMenu,KeyPauseAlternate,KeyMelee,KeyCharacter,KeyBuilding,KeyLanguageJournal,KeyPreviousBookTab,KeyNextBookTab,KeyReload,KeySelectUnarmed,KeySelectWeapon1,KeySelectWeapon2,KeySelectWeapon3,FKey(),FKey(),FKey(),FKey(),FKey()};
}

bool ADWPlayerController::DoBindingContextsOverlap(int32 A,int32 B)
{
 auto IsTab=[](int32 I){return I==int32(EDWInputAction::PreviousBookTab)||I==int32(EDWInputAction::NextBookTab);};
 auto IsMenu=[](int32 I){return I==int32(EDWInputAction::Inventory)||I==int32(EDWInputAction::Crafting)||I==int32(EDWInputAction::PauseMenu)||I==int32(EDWInputAction::PauseAlternate)||(I>=int32(EDWInputAction::Character)&&I<=int32(EDWInputAction::NextBookTab));};
 return (!IsTab(A)&&!IsTab(B))||(IsTab(A)&&IsMenu(B))||(IsTab(B)&&IsMenu(A));
}

bool ADWPlayerController::ValidateInputBindings(FText& OutError) const
{
    OutError = FText::GetEmpty();
    const TArray<FKey> Keys = GetConfiguredKeys();
    const TCHAR* Names[] = {TEXT("MoveForward"),TEXT("MoveBackward"),TEXT("MoveLeft"),TEXT("MoveRight"),
        TEXT("Dash"),TEXT("Sprint"),TEXT("SprintAlternate"),TEXT("Harvest"),TEXT("Transform"),TEXT("Throw"),
        TEXT("CameraDrag"),TEXT("Inventory"),TEXT("Crafting"),TEXT("PauseMenu"),TEXT("PauseAlternate"),TEXT("Melee"),TEXT("Character"),TEXT("Building"),TEXT("LanguageJournal"),TEXT("PreviousBookTab"),TEXT("NextBookTab"),TEXT("Reload"),TEXT("SelectUnarmed"),TEXT("SelectWeapon1"),TEXT("SelectWeapon2"),TEXT("SelectWeapon3"),TEXT("SelectWeapon4"),TEXT("SelectWeapon5"),TEXT("SelectWeapon6"),TEXT("SelectWeapon7"),TEXT("SelectWeapon8")};
    static_assert(UE_ARRAY_COUNT(Names)==int32(EDWInputAction::Count),"Every input action requires a validation name");
    TMap<FKey, int32> Seen;
    for (int32 Index=0; Index<Keys.Num(); ++Index)
    {
        const FKey Key=Keys[Index];
        if (Key.GetFName().IsNone())
        {
            if (Index != static_cast<int32>(EDWInputAction::PauseMenu)) continue;
            OutError=DWText(this,TEXT("暂停/返回菜单必须保留一个按键。"),TEXT("Pause / return to menu must keep an assigned key."));
            return false;
        }
        if (!Key.IsValid() || Key==EKeys::AnyKey || Key.IsAxis1D() || Key.IsAxis2D() || Key.IsAxis3D() ||
            Key.IsGamepadKey() || Key.IsTouch() || Key==EKeys::MouseScrollUp || Key==EKeys::MouseScrollDown)
        {
            OutError=FText::Format(DWText(this,TEXT("{0} 的按键不受支持。请使用键盘键或鼠标按钮。"),
                TEXT("{0} uses an unsupported key. Choose a keyboard key or mouse button.")),FText::FromString(Names[Index]));
            return false;
        }
        for(int32 PreviousIndex=0;PreviousIndex<Index;++PreviousIndex)if(Keys[PreviousIndex]==Key&&DoBindingContextsOverlap(Index,PreviousIndex))
        {const int32* Previous=&PreviousIndex;
        {
            OutError=FText::Format(DWText(this,TEXT("{0} 与 {1} 重复使用 {2}；未应用更改。"),
                TEXT("{0} and {1} both use {2}. Changes were not applied.")),
                FText::FromString(Names[*Previous]),FText::FromString(Names[Index]),DWKeyLabel(this,Key));
            return false;
        }
        }
        Seen.Add(Key,Index);
    }
    return true;
}

bool ADWPlayerController::ApplyInputBindings(FText& OutError)
{
    if (!ValidateInputBindings(OutError)) return false;
    PendingKeys=GetConfiguredKeys();
    bInputBindingsPending=true;
    return true;
}

bool ADWPlayerController::ResetInputBindingsToDefaults(FText& OutError)
{
    AssignKeys(DefaultDWKeys());
    return ApplyInputBindings(OutError);
}

FKey ADWPlayerController::GetAppliedActionKey(EDWInputAction Action) const
{
    const int32 Index=static_cast<int32>(Action);
    return AppliedKeys.IsValidIndex(Index)?AppliedKeys[Index]:FKey();
}

FText ADWPlayerController::GetActionKeyLabel(EDWInputAction Action) const
{
    return DWKeyLabel(this,GetAppliedActionKey(Action));
}

bool ADWPlayerController::IsActionDown(EDWInputAction Action) const
{
    const FKey Key=GetAppliedActionKey(Action);
    return Key.IsValid() && IsInputKeyDown(Key);
}

void ADWPlayerController::RemoveOwnedKeyBindings()
{
    if (UInputComponent* Component=OwnedInputComponent.Get())
    {
        // FInputKeyBinding has no public handle. Our shared relays are unique delegate owners;
        // never remove Blueprint delegates or bindings merely because they use the same key/controller.
        Component->KeyBindings.RemoveAll([this](const FInputKeyBinding& Binding)
        {
            const void* DelegateObject=Binding.KeyDelegate.GetObject();
            return OwnedKeyRelays.ContainsByPredicate([DelegateObject](const TSharedPtr<FDWInputBindingRelay>& Relay)
            {
                return Relay.IsValid() && Relay.Get()==DelegateObject;
            });
        });
    }
    OwnedKeyRelays.Reset();
    OwnedInputComponent.Reset();
}

void ADWPlayerController::AddOwnedBinding(FKey Key,EInputEvent Event,void(ADWPlayerController::*Method)(),bool bWhenPaused)
{
    if (!Key.IsValid() || !InputComponent) return;
    TSharedPtr<FDWInputBindingRelay> Relay=MakeShared<FDWInputBindingRelay>();
    const TWeakObjectPtr<ADWPlayerController> WeakController(this);
    Relay->Callback=[WeakController,Method]()
    {
        if (ADWPlayerController* Controller=WeakController.Get()) (Controller->*Method)();
    };
    FInputKeyBinding Binding(FInputChord(Key),Event);
    Binding.KeyDelegate=FInputActionUnifiedDelegate(FInputActionHandlerSignature::CreateSP(Relay.ToSharedRef(),&FDWInputBindingRelay::Execute));
    Binding.bExecuteWhenPaused=bWhenPaused;
    InputComponent->KeyBindings.Add(MoveTemp(Binding));
    OwnedKeyRelays.Add(MoveTemp(Relay));
}

void ADWPlayerController::CommitQueuedBindings()
{
    if (!bInputBindingsPending || !InputComponent || PendingKeys.Num()!=static_cast<int32>(EDWInputAction::Count)) return;
    // Rebinding invalidates input identity; never masquerade as a physical release.
    RemoveOwnedKeyBindings();
    AppliedKeys=MoveTemp(PendingKeys);
    OwnedInputComponent=InputComponent;
    AddOwnedBinding(GetAppliedActionKey(EDWInputAction::Dash),IE_Pressed,&ADWPlayerController::Dash);
    AddOwnedBinding(GetAppliedActionKey(EDWInputAction::Dash),IE_Released,&ADWPlayerController::WeaponPrimaryReleased,true);
    for (EDWInputAction Action : {EDWInputAction::Sprint,EDWInputAction::SprintAlternate})
    {
        AddOwnedBinding(GetAppliedActionKey(Action),IE_Pressed,&ADWPlayerController::SprintDown);
        AddOwnedBinding(GetAppliedActionKey(Action),IE_Released,&ADWPlayerController::SprintUp);
    }
    AddOwnedBinding(GetAppliedActionKey(EDWInputAction::Harvest),IE_Pressed,&ADWPlayerController::HarvestDown);
    AddOwnedBinding(GetAppliedActionKey(EDWInputAction::Harvest),IE_Released,&ADWPlayerController::HarvestUp);
    AddOwnedBinding(GetAppliedActionKey(EDWInputAction::Transform),IE_Pressed,&ADWPlayerController::Transform);
    AddOwnedBinding(GetAppliedActionKey(EDWInputAction::Throw),IE_Pressed,&ADWPlayerController::Throw);
    AddOwnedBinding(GetAppliedActionKey(EDWInputAction::Melee),IE_Pressed,&ADWPlayerController::Melee);
    AddOwnedBinding(GetAppliedActionKey(EDWInputAction::Throw),IE_DoubleClick,&ADWPlayerController::Throw);
    AddOwnedBinding(GetAppliedActionKey(EDWInputAction::Melee),IE_DoubleClick,&ADWPlayerController::Melee);
    AddOwnedBinding(GetAppliedActionKey(EDWInputAction::Throw),IE_Released,&ADWPlayerController::WeaponPrimaryReleased,true);
    AddOwnedBinding(GetAppliedActionKey(EDWInputAction::Melee),IE_Released,&ADWPlayerController::WeaponSecondaryReleased,true);
    AddOwnedBinding(GetAppliedActionKey(EDWInputAction::Inventory),IE_Pressed,&ADWPlayerController::Inventory,true);
    AddOwnedBinding(GetAppliedActionKey(EDWInputAction::Crafting),IE_Pressed,&ADWPlayerController::Crafting,true);
    AddOwnedBinding(GetAppliedActionKey(EDWInputAction::PauseMenu),IE_Pressed,&ADWPlayerController::PauseMenu,true);
    AddOwnedBinding(GetAppliedActionKey(EDWInputAction::PauseAlternate),IE_Pressed,&ADWPlayerController::PauseMenu,true);
    AddOwnedBinding(GetAppliedActionKey(EDWInputAction::Character),IE_Pressed,&ADWPlayerController::Character,true);
    AddOwnedBinding(GetAppliedActionKey(EDWInputAction::Building),IE_Pressed,&ADWPlayerController::Building,true);
    AddOwnedBinding(GetAppliedActionKey(EDWInputAction::LanguageJournal),IE_Pressed,&ADWPlayerController::LanguageJournal,true);
    // Tab navigation is handled by the focused UI; do not bind E twice and consume Transform in gameplay.
    bInputBindingsPending=false;
    ++InputBindingsRevision;
    RefreshCombatInputKeys();
}

void ADWPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    // Input components are owned by this controller and will be torn down by the engine.
    // Do not mutate their binding arrays here: EndPlay can itself run inside an input callback.
    if(CombatInputAdapter){CombatInputAdapter->Shutdown();CombatInputAdapter.Reset();}
    PendingCombatReleases.Reset();
    PendingKeys.Reset();bInputBindingsPending=false;
    OwnedKeyRelays.Reset();OwnedInputComponent.Reset();
    Super::EndPlay(EndPlayReason);
}
bool ADWPlayerController::IsGameplayBlocked()const
{const auto* H=Cast<ADWGameplayHUD>(GetHUD());return UDWWorldEventSubsystem::IsPlaying(this)||IsPaused()||(H&&H->IsBlockingGameplay())||(Player()&&Player()->IsDead());}
bool ADWPlayerController::IsCameraInputAllowed()const
{
    const auto* P=Player();const auto* H=Cast<ADWGameplayHUD>(GetHUD());
    if(!IsLocalPlayerController()||!P||GetViewTarget()!=P||IsGameplayBlocked()||!H||!H->IsSessionStarted())return false;
    const auto* GI=GetWorld()?GetWorld()->GetGameInstance<UDWGameInstance>():nullptr;
    const auto* Loading=GI?GI->GetSubsystem<UDWLoadingTransitionSubsystem>():nullptr;
    return !GI||(!GI->IsPlayerRestorePending()&&(!Loading||!Loading->IsTransitionActive()));
}
void ADWPlayerController::PlayerTick(float DeltaSeconds)
{
    // A UI/input event may queue rebinding while Super::PlayerTick processes delegates.
    // Commit only before the next input pass so active KeyBindings iteration remains valid.
    CommitQueuedBindings();
    PrepareCombatInputForSimulation();
    ON_SCOPE_EXIT { if(CombatInputAdapter)CombatInputAdapter->FinishInputBatch(); };
    Super::PlayerTick(DeltaSeconds);
    PrepareCombatInputForSimulation();
    auto* P=Player();if(!P)return;
    if(IsPaused()&&IsLocalPlayerController())
    {
        // CameraManager starts with the controller's eye-level cache, before possession.
        // Refresh the pawn POV while the title is paused without advancing any camera time.
        if(GetViewTarget()==this)SetViewTarget(P);
        UpdateCameraManager(0.f);
    }
    if(IsGameplayBlocked())
    {
        // Normal menus freeze the existing action. Death/cutscene retain explicit hard cancellation.
        if(!IsPaused()&&!IsCombatInputSuspended())P->CancelCombatActions();
        return;
    }
    if(auto* Weapons=P->WeaponAnimationComponent.Get();Weapons&&P->HasCombatVitalsReady())
    {
        auto ShortcutAvailable=[this](EDWInputAction Action)
        {
            const FKey Key=GetAppliedActionKey(Action);if(!Key.IsValid())return false;
            for(int32 A=0;A<int32(EDWInputAction::Count);++A)
                if(A!=int32(Action)&&DoBindingContextsOverlap(A,int32(Action))&&GetAppliedActionKey(static_cast<EDWInputAction>(A))==Key)return false;
            return true;
        };
        const FKey ReloadKey=GetAppliedActionKey(EDWInputAction::Reload),UnarmedKey=GetAppliedActionKey(EDWInputAction::SelectUnarmed);
        FDWCombatInputPress ShortcutPress;
        if(ShortcutAvailable(EDWInputAction::Reload)&&WasInputKeyJustPressed(ReloadKey))
        {if(ConsumeCombatPress(ReloadKey,TEXT("Reload"),ShortcutPress)&&!Weapons->RequestReload())DWCombatHUD::NotifyRejected(P,EDWCombatHUDRequest::Reload);}
        else if(Weapons->bEnableQuickSelection&&ShortcutAvailable(EDWInputAction::SelectUnarmed)&&WasInputKeyJustPressed(UnarmedKey))
        {if(ConsumeCombatPress(UnarmedKey,TEXT("SelectWeapon"),ShortcutPress))Weapons->SelectWeapon(EDWAnimatedWeapon::Unarmed);}
        else if(Weapons->bEnableQuickSelection)for(int32 I=0;I<3;++I)
        {
            const auto Action=static_cast<EDWInputAction>(int32(EDWInputAction::SelectWeapon1)+I);const FKey Key=GetAppliedActionKey(Action);
            if(ShortcutAvailable(Action)&&WasInputKeyJustPressed(Key))
            {if(ConsumeCombatPress(Key,TEXT("InventoryQuickSlot"),ShortcutPress))
             {if(auto* H=Cast<ADWGameplayHUD>(GetHUD()))if(auto* Screen=H->GetGameplayWidget())Screen->ActivateQuickInventorySlot(I);
              if(!IsValid(P)||Player()!=P||IsGameplayBlocked())return;}break;}
        }
    }
    const float Forward=float(IsActionDown(EDWInputAction::MoveForward))-float(IsActionDown(EDWInputAction::MoveBackward));
    const float Right=float(IsActionDown(EDWInputAction::MoveRight))-float(IsActionDown(EDWInputAction::MoveLeft));
    if(auto* Weapons=P->WeaponAnimationComponent.Get())
    {
        // Physical edges after the accepted charge, not a held axis sampled every frame.
        for(const EDWInputAction MoveAction:{EDWInputAction::MoveForward,EDWInputAction::MoveBackward,EDWInputAction::MoveLeft,EDWInputAction::MoveRight})
        {
            FDWCombatInputPress MovePress;
            if(ConsumeCombatPress(GetAppliedActionKey(MoveAction),TEXT("SwordMoveCancel"),MovePress))Weapons->ApplyFormalSwordMovementPress(MovePress);
            if(!IsValid(P)||Player()!=P||IsGameplayBlocked())return;
        }
        if(Weapons->GetFormalSwordPhase()==EDWSwordPhase::Charge&&Weapons->IsBusy())
        {
            FHitResult Aim;if(GetHitResultUnderCursor(ECC_Visibility,false,Aim))Weapons->UpdateFormalSwordChargeAim(Aim.ImpactPoint);
            if(!IsValid(P)||Player()!=P||IsGameplayBlocked())return;
        }
        else if(Weapons->IsFormalSwordGap())
        {
            if(Forward!=0.f||Right!=0.f)Weapons->ApplyFormalSwordGapMovement();
            else{FHitResult Aim;if(GetHitResultUnderCursor(ECC_Visibility,false,Aim))Weapons->UpdateFormalSwordGapAim(Aim.ImpactPoint);}
            if(!IsValid(P)||Player()!=P||IsGameplayBlocked())return;
        }
    }
    if(P->WeaponAnimationComponent&&P->WeaponAnimationComponent->IsFormalShieldGuarding())
    {
        FHitResult Aim;if(GetHitResultUnderCursor(ECC_Visibility,false,Aim))
        {const FVector Facing=(Aim.ImpactPoint-P->GetActorLocation()).GetSafeNormal2D();if(!Facing.IsNearlyZero())P->SetActorRotation(Facing.Rotation());}
    }
    if(auto* Weapons=P->WeaponAnimationComponent.Get();Weapons&&Weapons->SelectedWeapon==EDWAnimatedWeapon::SporeGun)
    {FHitResult Aim;if(GetHitResultUnderCursor(ECC_Visibility,false,Aim))Weapons->UpdateFormalSporeGapAim(Aim.ImpactPoint);}
    P->MoveCameraRelative(Forward,Right);
    P->SetSprintHeld(IsActionDown(EDWInputAction::Sprint)||IsActionDown(EDWInputAction::SprintAlternate));
    if(P->GatheringComponent&&P->GatheringComponent->IsGatheringEnabled())
    {
        // Existing Slate observation also sees releases consumed by a paused/focused widget.
        // Rebind, pawn/world replacement and focus invalidation retire the old press identity.
        if(bGatheringHarvestPressActive&&(!CombatInputAdapter||
            GatheringHarvestPress.Edge.Context!=CombatInputAdapter->GetContext()||
            GatheringHarvestPress.Edge.Key!=GetAppliedActionKey(EDWInputAction::Harvest).GetFName()||
            CombatInputAdapter->HasObservedReleaseAfter(GatheringHarvestPress)))
            bGatheringHarvestPressActive=false;
        P->SetHarvestHeld(bGatheringHarvestPressActive);
        // An immediate gathering edge may open a menu or run a presentation callback.
        if(!IsValid(P)||P!=Player()||IsGameplayBlocked())return;
    }
    else P->SetHarvestHeld(IsActionDown(EDWInputAction::Harvest));
    if(IsCameraInputAllowed()&&PlayerInput)
    {
        // Raw frame-accumulated mouse movement avoids the legacy .07 axis scaling and mouse smoothing.
        // Neither a movement delta nor a wheel amount is multiplied by DeltaSeconds.
        if(IsActionDown(EDWInputAction::CameraDrag))
            P->DragCamera(PlayerInput->GetRawKeyValue(EKeys::MouseX),PlayerInput->GetRawKeyValue(EKeys::MouseY));
        P->ZoomCamera(PlayerInput->GetRawKeyValue(EKeys::MouseWheelAxis));
    }
}
void ADWPlayerController::Dash()
{
    auto* P=Player();FDWCombatInputPress Press;
    if(!P||IsGameplayBlocked()||!ConsumeCombatPress(GetAppliedActionKey(EDWInputAction::Dash),TEXT("Dash"),Press))return;
    const double Forward=double(IsActionDown(EDWInputAction::MoveForward))-double(IsActionDown(EDWInputAction::MoveBackward));
    const double Right=double(IsActionDown(EDWInputAction::MoveRight))-double(IsActionDown(EDWInputAction::MoveLeft));
    const FRotationMatrix Camera(FRotator(0.,P->GetMovementCameraYaw(),0.));
    const FVector Move=Camera.GetUnitAxis(EAxis::X)*Forward+Camera.GetUnitAxis(EAxis::Y)*Right;
    FVector Aim=P->GetActorForwardVector();FHitResult Hit;
    if(GetHitResultUnderCursor(ECC_Visibility,false,Hit))Aim=(Hit.ImpactPoint-P->GetActorLocation()).GetSafeNormal2D();
    else{FVector O,D;if(DeprojectMousePositionToWorld(O,D)&&FMath::Abs(D.Z)>KINDA_SMALL_NUMBER)
    {const double T=(P->GetActorLocation().Z-O.Z)/D.Z;if(T>0.)Aim=(O+D*T-P->GetActorLocation()).GetSafeNormal2D();}}
    if(!P->TryStartFormalDash(Press,Move,Aim)){DWCombatHUD::NotifyRejected(P,EDWCombatHUDRequest::Dash);return;}
    if(!IsValid(P)||P!=Player()||!CombatInputAdapter)return;
    if(CombatInputAdapter->ActivateHold(Press)){P->PublishFormalDashStartEffects();return;}
    if(CombatInputAdapter->HasObservedReleaseAfter(Press)){P->ApplyFormalDashRelease(Press);P->PublishFormalDashStartEffects();}
    else P->CancelFormalDashInput(Press);
}
void ADWPlayerController::SprintDown(){if(Player()&&!IsGameplayBlocked())Player()->SetSprintHeld(true);}
void ADWPlayerController::SprintUp(){if(Player())Player()->SetSprintHeld(IsActionDown(EDWInputAction::Sprint)||IsActionDown(EDWInputAction::SprintAlternate));}
void ADWPlayerController::HarvestDown()
{
    auto* P=Player();if(!P||IsGameplayBlocked())return;
    if(!P->GatheringComponent||!P->GatheringComponent->IsGatheringEnabled())
    {P->SetHarvestHeld(true);return;}
    FDWCombatInputPress Press;
    if(!ConsumeCombatPress(GetAppliedActionKey(EDWInputAction::Harvest),TEXT("GatheringHarvest"),Press))return;
    GatheringHarvestPress=Press;bGatheringHarvestPressActive=true;
    const TWeakObjectPtr<ADWPlayerCharacter> WeakPlayer(P);
    P->SetHarvestHeld(true); // Existing edge fix dispatches one immediate interaction.
    // A whole tap may already have both physical edges in this batch; never leave it held.
    if(WeakPlayer.IsValid()&&Player()==WeakPlayer.Get()&&CombatInputAdapter&&CombatInputAdapter->HasObservedReleaseAfter(Press))
    {bGatheringHarvestPressActive=false;WeakPlayer->SetHarvestHeld(false);}
}
void ADWPlayerController::HarvestUp()
{
    bGatheringHarvestPressActive=false;
    if(Player())Player()->SetHarvestHeld(false);
}
void ADWPlayerController::Transform()
{FDWCombatInputPress Press;if(Player()&&!IsGameplayBlocked()&&ConsumeCombatPress(GetAppliedActionKey(EDWInputAction::Transform),TEXT("Transform"),Press))Player()->TryTransform();}
void ADWPlayerController::Throw()
{
    auto* P=Player();if(!P||IsGameplayBlocked())return;
    FDWCombatInputPress Press;
    if(!ConsumeCombatPress(GetAppliedActionKey(EDWInputAction::Throw),TEXT("Primary"),Press))return;
    if(P->GatheringComponent&&P->GatheringComponent->HandlePrimaryClick())return;
    FHitResult Hit;FVector Target=P->GetActorLocation()+P->GetActorForwardVector()*600.f;
    if(GetHitResultUnderCursor(ECC_Visibility,false,Hit))Target=Hit.ImpactPoint;
    else{FVector Origin,Direction;if(DeprojectMousePositionToWorld(Origin,Direction)&&FMath::Abs(Direction.Z)>KINDA_SMALL_NUMBER){const float T=(P->GetActorLocation().Z-70.f-Origin.Z)/Direction.Z;if(T>0)Target=Origin+Direction*T;}}
    if(P->HasCombatVitalsReady()&&P->WeaponAnimationComponent)
    {if(P->WeaponAnimationComponent->PrimaryPressed(Target))CompleteCombatHold(P->WeaponAnimationComponent,Press,false);else DWCombatHUD::NotifyRejected(P,EDWCombatHUDRequest::Primary);}
    else if(!P->HasCombatVitalsReady())P->ThrowAlcoholAt(Target);
}
void ADWPlayerController::Melee()
{
    auto* P=Player();if(!P||IsGameplayBlocked())return;
    FDWCombatInputPress Press;
    if(!ConsumeCombatPress(GetAppliedActionKey(EDWInputAction::Melee),TEXT("Secondary"),Press))return;
    FHitResult Hit;FVector Target=P->GetActorLocation()+P->GetActorForwardVector()*600.f;
    if(GetHitResultUnderCursor(ECC_Visibility,false,Hit))Target=Hit.ImpactPoint;
    else{FVector Origin,Direction;if(DeprojectMousePositionToWorld(Origin,Direction)&&FMath::Abs(Direction.Z)>KINDA_SMALL_NUMBER){const float T=(P->GetActorLocation().Z-Origin.Z)/Direction.Z;if(T>0.f)Target=Origin+Direction*T;}}
    if(P->HasCombatVitalsReady()&&P->WeaponAnimationComponent)
    {
        if(P->WeaponAnimationComponent->CanSelectCombatEquipment(EDWAnimatedWeapon::ShieldAxe)&&P->WeaponAnimationComponent->SelectedWeapon==EDWAnimatedWeapon::ShieldAxe)
        {const FVector Facing=(Target-P->GetActorLocation()).GetSafeNormal2D();if(!Facing.IsNearlyZero())P->SetActorRotation(Facing.Rotation());}
        if(P->WeaponAnimationComponent->SecondaryPressed())CompleteCombatHold(P->WeaponAnimationComponent,Press,true);
    }
    else if(!P->HasCombatVitalsReady())P->PerformMeleeAttack(Target);
}
void ADWPlayerController::WeaponPrimaryReleased(){PrepareCombatInputForSimulation();}
void ADWPlayerController::WeaponSecondaryReleased(){PrepareCombatInputForSimulation();}
void ADWPlayerController::Inventory(){if(UDWWorldEventSubsystem::IsPlaying(this))return;if(auto* H=Cast<ADWGameplayHUD>(GetHUD())){H->ToggleInventory();}}
void ADWPlayerController::Crafting(){if(UDWWorldEventSubsystem::IsPlaying(this))return;if(auto* H=Cast<ADWGameplayHUD>(GetHUD())){H->ToggleCrafting();}}
void ADWPlayerController::PauseMenu(){if(UDWWorldEventSubsystem::IsPlaying(this))return;if(auto* H=Cast<ADWGameplayHUD>(GetHUD())){H->TogglePause();}}

void ADWPlayerController::Character(){if(!UDWWorldEventSubsystem::IsPlaying(this))if(auto* H=Cast<ADWGameplayHUD>(GetHUD()))H->ToggleHandbook(EDWMenuPage::Character);}
void ADWPlayerController::Building(){if(!UDWWorldEventSubsystem::IsPlaying(this))if(auto* H=Cast<ADWGameplayHUD>(GetHUD()))H->ToggleHandbook(EDWMenuPage::Building);}
void ADWPlayerController::LanguageJournal(){if(!UDWWorldEventSubsystem::IsPlaying(this))if(auto* H=Cast<ADWGameplayHUD>(GetHUD()))H->ToggleHandbook(EDWMenuPage::LanguageJournal);}
void ADWPlayerController::PreviousBookTab(){if(auto* H=Cast<ADWGameplayHUD>(GetHUD()))H->CycleHandbook(-1);}
void ADWPlayerController::NextBookTab(){if(auto* H=Cast<ADWGameplayHUD>(GetHUD()))H->CycleHandbook(1);}

void ADWPlayerController::AssignKeys(const TArray<FKey>& K)
{
 FKey* Fields[]={&KeyMoveForward,&KeyMoveBackward,&KeyMoveLeft,&KeyMoveRight,&KeyDash,&KeySprint,&KeySprintAlternate,&KeyHarvest,&KeyTransform,&KeyThrow,&KeyCameraDrag,&KeyInventory,&KeyCrafting,&KeyPauseMenu,&KeyPauseAlternate,&KeyMelee,&KeyCharacter,&KeyBuilding,&KeyLanguageJournal,&KeyPreviousBookTab,&KeyNextBookTab,&KeyReload,&KeySelectUnarmed,&KeySelectWeapon1,&KeySelectWeapon2,&KeySelectWeapon3,&KeySelectWeapon4,&KeySelectWeapon5,&KeySelectWeapon6,&KeySelectWeapon7,&KeySelectWeapon8};
 static_assert(UE_ARRAY_COUNT(Fields)==int32(EDWInputAction::Count),"Every action requires one configured field");
 if(K.Num()!=UE_ARRAY_COUNT(Fields))return;for(int32 I=0;I<K.Num();++I)*Fields[I]=K[I];
}
void ADWPlayerController::LoadSavedBindings()
{
  TArray<FKey> Before=GetConfiguredKeys();
  const int32 MeleeIndex=int32(EDWInputAction::Melee);
  // Preserve authored legacy controls too: an old Blueprint may already assign RMB to the camera.
  for(int32 I=0;I<Before.Num();++I)if(I!=MeleeIndex&&Before[I].IsValid()&&Before[I]==Before[MeleeIndex]){Before[MeleeIndex]=FKey();break;}
  AssignKeys(Before);TArray<FKey> K=Before;
  bool bSavedMelee=false;TSet<int32> SavedActions;
  const int32 CameraIndex=int32(EDWInputAction::CameraDrag);bool bSavedCameraDrag=false;
  for(int32 I=0;I<K.Num();++I)
  {
      FString Value;const FString Name=StaticEnum<EDWInputAction>()->GetNameStringByValue(I);
      if(GConfig->GetString(TEXT("DoughWorld.Input"),*Name,Value,GGameUserSettingsIni))
      {SavedActions.Add(I);K[I]=FKey(FName(*Value));if(I==CameraIndex)bSavedCameraDrag=true;if(I==MeleeIndex)bSavedMelee=true;}
  }
  // New actions must never invalidate a user's older custom binding set.
  if(!bSavedMelee)
  {
      K[MeleeIndex]=KeyMelee;
      for(int32 I=0;I<K.Num();++I)if(I!=MeleeIndex&&K[I].IsValid()&&K[I]==K[MeleeIndex]){K[MeleeIndex]=FKey();break;}
  }
  // A newly introduced default must not displace any existing saved action.
  for(int32 I=int32(EDWInputAction::Character);I<=int32(EDWInputAction::NextBookTab);++I)if(!SavedActions.Contains(I))
    for(int32 J=0;J<K.Num();++J)if(I!=J&&K[I].IsValid()&&K[I]==K[J]&&DoBindingContextsOverlap(I,J)){K[I]=FKey();break;}
  AssignKeys(K);FText Error;
  if(!ValidateInputBindings(Error)){AssignKeys(Before);UE_LOG(LogTemp,Warning,TEXT("Ignoring invalid saved bindings: %s"),*Error.ToString());return;}
  int32 DefaultVersion=0;
  GConfig->GetInt(TEXT("DoughWorld.Input"),TEXT("CameraDragDefaultVersion"),DefaultVersion,GGameUserSettingsIni);
  if(DefaultVersion<2)
  {
      // One-time upgrade of the former default only. All other saved bindings remain untouched.
      // Marking the version also preserves a deliberate RMB rebind made after this upgrade.
      if(bSavedCameraDrag&&K[CameraIndex]==EKeys::RightMouseButton)
      {
          bool bMiddleUsed=false;
          for(int32 I=0;I<K.Num();++I)if(I!=CameraIndex&&K[I]==EKeys::MiddleMouseButton)bMiddleUsed=true;
          if(!bMiddleUsed)
          {
              K[CameraIndex]=EKeys::MiddleMouseButton;AssignKeys(K);
              GConfig->SetString(TEXT("DoughWorld.Input"),TEXT("CameraDrag"),TEXT("MiddleMouseButton"),GGameUserSettingsIni);
              UE_LOG(LogTemp,Log,TEXT("DoughWorld input: migrated legacy CameraDrag RMB to MMB; other saved keys preserved."));
          }
          else UE_LOG(LogTemp,Warning,TEXT("DoughWorld input: legacy CameraDrag RMB preserved because another custom action owns MMB; rebind in Controls."));
      }
      GConfig->SetInt(TEXT("DoughWorld.Input"),TEXT("CameraDragDefaultVersion"),2,GGameUserSettingsIni);
      GConfig->Flush(false,GGameUserSettingsIni);
  }
}
void ADWPlayerController::SaveBindings()const
{
  const auto K=GetConfiguredKeys();for(int32 I=0;I<K.Num();++I)GConfig->SetString(TEXT("DoughWorld.Input"),*StaticEnum<EDWInputAction>()->GetNameStringByValue(I),*K[I].GetFName().ToString(),GGameUserSettingsIni);
  GConfig->SetInt(TEXT("DoughWorld.Input"),TEXT("CameraDragDefaultVersion"),2,GGameUserSettingsIni);GConfig->Flush(false,GGameUserSettingsIni);
}
bool ADWPlayerController::SetActionBinding(EDWInputAction Action,FKey Key,FText& Error,bool Persist)
{
 const auto Before=GetConfiguredKeys();auto K=Before;const int32 I=int32(Action);if(!K.IsValidIndex(I)||!IsBindableInputAction(Action))return false;K[I]=Key;AssignKeys(K);
 if(!ApplyInputBindings(Error)){AssignKeys(Before);return false;}if(Persist)SaveBindings();return true;
}
bool ADWPlayerController::RestoreAuthoredBindings(FText& Error)
{
 const auto Before=GetConfiguredKeys();AssignKeys(GetClass()->GetDefaultObject<ADWPlayerController>()->GetConfiguredKeys());
 if(!ApplyInputBindings(Error)){AssignKeys(Before);return false;}GConfig->EmptySection(TEXT("DoughWorld.Input"),GGameUserSettingsIni);GConfig->Flush(false,GGameUserSettingsIni);return true;
}
FText ADWPlayerController::GetActionDisplayName(EDWInputAction A)const
{
 if(const auto* P=Player();P&&P->WeaponAnimationComponent&&P->WeaponAnimationComponent->IsEnabled())
 {
     if(A==EDWInputAction::Throw)return DWText(this,TEXT("武器攻击 / 蓄力"),TEXT("Weapon attack / charge"));
     if(A==EDWInputAction::Melee)return DWText(this,TEXT("盾斧格挡"),TEXT("Shield-axe block"));
 }
 const TCHAR* CN[]={TEXT("向前移动"),TEXT("向后移动"),TEXT("向左移动"),TEXT("向右移动"),TEXT("酵母冲撞"),TEXT("疾跑（旧兼容）"),TEXT("疾跑备用（旧兼容）"),TEXT("场景交互"),TEXT("霉菌扎根 / 解除"),TEXT("投掷酒精"),TEXT("拖拽视角"),TEXT("背包"),TEXT("制作"),TEXT("暂停 / 返回"),TEXT("暂停备用键"),TEXT("近战攻击"),TEXT("角色"),TEXT("建造 / 放置页"),TEXT("语言图鉴"),TEXT("上一标签（手册内）"),TEXT("下一标签（手册内）"),TEXT("装填"),TEXT("选择空手"),TEXT("快捷栏1 · 背包第1格"),TEXT("快捷栏2 · 背包第2格"),TEXT("快捷栏3 · 背包第3格"),TEXT("装备4 · 双匕首"),TEXT("装备5 · 孢子枪"),TEXT("装备6 · 霰弹枪"),TEXT("装备7 · 霉菌弓"),TEXT("装备8 · 酒瓶")};
 const TCHAR* EN[]={TEXT("Move forward"),TEXT("Move backward"),TEXT("Move left"),TEXT("Move right"),TEXT("Yeast charge"),TEXT("Sprint (legacy)"),TEXT("Sprint alternate (legacy)"),TEXT("Interact"),TEXT("Mold root / release"),TEXT("Throw alcohol"),TEXT("Drag camera"),TEXT("Inventory"),TEXT("Crafting"),TEXT("Pause / Back"),TEXT("Pause alternate"),TEXT("Melee attack"),TEXT("Character"),TEXT("Building"),TEXT("Language journal"),TEXT("Previous handbook tab"),TEXT("Next handbook tab"),TEXT("Reload"),TEXT("Select unarmed"),TEXT("Quick slot 1 · Inventory slot 1"),TEXT("Quick slot 2 · Inventory slot 2"),TEXT("Quick slot 3 · Inventory slot 3"),TEXT("Equipment4 · Dual daggers"),TEXT("Equipment5 · Spore gun"),TEXT("Equipment6 · Shotgun"),TEXT("Equipment7 · Mold bow"),TEXT("Equipment8 · Bottle")};
 static_assert(UE_ARRAY_COUNT(CN)==int32(EDWInputAction::Count)&&UE_ARRAY_COUNT(EN)==int32(EDWInputAction::Count),"Every input action requires two labels");
 const int32 I=int32(A);return I>=0&&I<UE_ARRAY_COUNT(CN)?DWText(this,CN[I],EN[I]):FText::GetEmpty();
}

void ADWPlayerController::ImportLegacyCombatShortcutDefaults()
{
    const auto* P=Player();const auto* Weapons=P?P->WeaponAnimationComponent.Get():nullptr;if(!Weapons)return;
    const TArray<FKey> Before=GetConfiguredKeys(),Defaults=DefaultDWKeys();TArray<FKey> Candidate=Before;
    const auto Import=[&](EDWInputAction Action,FKey Legacy)
    {
        const int32 I=int32(Action);FString SavedValue;const FString Name=StaticEnum<EDWInputAction>()->GetNameStringByValue(I);
        // A saved new-action value and an explicitly authored controller key both outrank legacy pawn defaults.
        if(GConfig->GetString(TEXT("DoughWorld.Input"),*Name,SavedValue,GGameUserSettingsIni)||Candidate[I]!=Defaults[I]||Legacy==Defaults[I])return;
        Candidate[I]=Legacy;
    };
    Import(EDWInputAction::Reload,Weapons->ReloadKey);Import(EDWInputAction::SelectUnarmed,Weapons->UnarmedSelectionKey);
    for(int32 I=0;I<Weapons->WeaponSelectionKeys.Num()&&I<3;++I)Import(static_cast<EDWInputAction>(int32(EDWInputAction::SelectWeapon1)+I),Weapons->WeaponSelectionKeys[I]);
    if(Candidate==Before)return;AssignKeys(Candidate);FText Error;
    if(!ApplyInputBindings(Error)){AssignKeys(Before);UE_LOG(LogTemp,Warning,TEXT("DoughWorld: legacy weapon shortcut import rejected, current controller keys preserved: %s"),*Error.ToString());}
    // This is an in-memory missing-field compatibility import, never an automatic INI rewrite.
}

void ADWPlayerController::RefreshCombatInputKeys()
{
    if(!CombatInputAdapter)return;
    const TArray<FKey> Keys=AppliedKeys;
    CombatInputAdapter->RefreshMonitoredKeys(Keys);
}
bool ADWPlayerController::ConsumeCombatPress(FKey Key,FName Action,FDWCombatInputPress& OutPress)
{
    PrepareCombatInputForSimulation();
    return !IsGameplayBlocked()&&CombatInputAdapter&&CombatInputAdapter->TryConsumePress(Key,Action,OutPress);
}
void ADWPlayerController::CompleteCombatHold(UDWWeaponAnimationComponent* Weapons,const FDWCombatInputPress& Press,bool bSecondary)
{
    if(!IsValid(Weapons)||!CombatInputAdapter||!Weapons->BindObservedInputPress(Press,bSecondary))return;
    if(CombatInputAdapter->ActivateHold(Press)){Weapons->PublishAcceptedWeaponStartEffects();return;}
    // A completed rapid tap is valid for this batch, but it must never become a stuck hold.
    // Use the real Up already observed by Slate; lifecycle invalidation is a separate cancellation.
    if(CombatInputAdapter->HasObservedReleaseAfter(Press)&&!IsGameplayBlocked()){Weapons->ApplyObservedInputRelease(Press);Weapons->PublishAcceptedWeaponStartEffects();}
    else Weapons->CancelObservedInputAction(Press);
}
void ADWPlayerController::BeginCombatMenuSuspension()
{
    // Closing a menu with the same physical F still down must not open it again.
    // Only a new observed press can set this flag; ordinary gameplay profiles ignore it.
    bGatheringHarvestPressActive=false;
    if(CombatInputAdapter)CombatInputAdapter->BeginSuspension();
}
bool ADWPlayerController::IsCombatInputSuspended() const
{return CombatInputAdapter&&CombatInputAdapter->IsSuspended();}
void ADWPlayerController::InvalidateCombatInputLife()
{
    PendingCombatReleases.Reset();
    if(CombatInputAdapter)CombatInputAdapter->InvalidateLife();
}
FString ADWPlayerController::GetCombatInputDebugState() const
{
    return FString::Printf(TEXT("%s ControllerPendingReleases=%d AppliedReleaseCount=%llu LastReleaseSequence=%llu LastReleaseWorldTime=%.6f"),
        CombatInputAdapter?*CombatInputAdapter->DescribeState():TEXT("Capturing=0 AdapterAbsent=1"),PendingCombatReleases.Num(),
        static_cast<unsigned long long>(AppliedObservedReleaseCount),static_cast<unsigned long long>(LastObservedReleaseSequence),LastObservedReleaseWorldTime);
}
void ADWPlayerController::PrepareCombatInputForSimulation()
{
    if(bProcessingCombatInputs||!CombatInputAdapter)return;
    TGuardValue<bool> ProcessingGuard(bProcessingCombatInputs,true);
    CombatInputAdapter->SynchronizeOwnerLifecycle();
    if(IsPaused()||IsGameplayBlocked()||!Player())return;
    if(const auto* GI=GetGameInstance<UDWGameInstance>())
    {
        if(GI->IsPlayerRestorePending())return;
        if(const auto* Loading=GI->GetSubsystem<UDWLoadingTransitionSubsystem>();Loading&&Loading->IsTransitionActive())return;
    }
    auto Invalidations=CombatInputAdapter->DrainInvalidations();
    if(EnumHasAnyFlags(Invalidations,EDWCombatInputInvalidation::PawnChanged|EDWCombatInputInvalidation::WorldChanged))
    {RefreshCombatInputKeys();Invalidations|=CombatInputAdapter->DrainInvalidations();}
    if(Invalidations!=EDWCombatInputInvalidation::None)
    {
        PendingCombatReleases.Reset();
        // Explicit loss of identity is cancellation, not a guessed physical Up. Keep spent resources/cooldowns.
        if(auto* P=Player())P->CancelCombatActions();
    }
    PendingCombatReleases.Append(CombatInputAdapter->IsSuspended()
        ?CombatInputAdapter->ResumeAndDrainReleases():CombatInputAdapter->DrainReleasedHolds());
    while(!PendingCombatReleases.IsEmpty()&&!IsGameplayBlocked())
    {
        const FDWCombatInputRelease Released=PendingCombatReleases[0];
        PendingCombatReleases.RemoveAt(0,1,EAllowShrinking::No);
        if(Released.Press.Edge.Context!=CombatInputAdapter->GetContext()||Released.bActionStillHeldAfterRelease)continue;
        if(auto* P=Player())
        {
            bool Applied=false;
            if(Released.Press.Action==TEXT("Dash"))Applied=P->ApplyFormalDashRelease(Released.Press);
            else if(auto* Weapons=P->WeaponAnimationComponent.Get())Applied=Weapons->ApplyObservedInputRelease(Released.Press);
            if(Applied)
            {
                ++AppliedObservedReleaseCount;LastObservedReleaseSequence=Released.Edge.Sequence;
                LastObservedReleaseWorldTime=GetWorld()?GetWorld()->GetTimeSeconds():-1.f;
            }
        }
    }
}
