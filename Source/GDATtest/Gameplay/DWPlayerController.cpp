#include "DWPlayerController.h"
#include "DWWorldEvent.h"
#include "Camera/PlayerCameraManager.h"
#include "DWUserSettings.h"
#include "Misc/ConfigCacheIni.h"
#include "DWPlayerCharacter.h"
#include "DWGameplayHUD.h"
#include "DWGameInstance.h"
#include "DWGameplayConfig.h"
#include "DWLocalizationLibrary.h"
#include "Components/InputComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
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
            EKeys::F,EKeys::E,EKeys::LeftMouseButton,EKeys::RightMouseButton,EKeys::B,EKeys::Tab,EKeys::Escape,EKeys::P};
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
        KeyHarvest,KeyTransform,KeyThrow,KeyCameraDrag,KeyInventory,KeyCrafting,KeyPauseMenu,KeyPauseAlternate};
}

bool ADWPlayerController::ValidateInputBindings(FText& OutError) const
{
    OutError = FText::GetEmpty();
    const TArray<FKey> Keys = GetConfiguredKeys();
    const TCHAR* Names[] = {TEXT("MoveForward"),TEXT("MoveBackward"),TEXT("MoveLeft"),TEXT("MoveRight"),
        TEXT("Dash"),TEXT("Sprint"),TEXT("SprintAlternate"),TEXT("Harvest"),TEXT("Transform"),TEXT("Throw"),
        TEXT("CameraDrag"),TEXT("Inventory"),TEXT("Crafting"),TEXT("PauseMenu"),TEXT("PauseAlternate")};
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
        if (const int32* Previous=Seen.Find(Key))
        {
            OutError=FText::Format(DWText(this,TEXT("{0} 与 {1} 重复使用 {2}；未应用更改。"),
                TEXT("{0} and {1} both use {2}. Changes were not applied.")),
                FText::FromString(Names[*Previous]),FText::FromString(Names[Index]),DWKeyLabel(this,Key));
            return false;
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
    const TArray<FKey> Keys=DefaultDWKeys();
    KeyMoveForward=Keys[0];KeyMoveBackward=Keys[1];KeyMoveLeft=Keys[2];KeyMoveRight=Keys[3];
    KeyDash=Keys[4];KeySprint=Keys[5];KeySprintAlternate=Keys[6];KeyHarvest=Keys[7];KeyTransform=Keys[8];
    KeyThrow=Keys[9];KeyCameraDrag=Keys[10];KeyInventory=Keys[11];KeyCrafting=Keys[12];KeyPauseMenu=Keys[13];KeyPauseAlternate=Keys[14];
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
    if (ADWPlayerCharacter* P=Player()) P->ClearHeldActions();
    RemoveOwnedKeyBindings();
    AppliedKeys=MoveTemp(PendingKeys);
    OwnedInputComponent=InputComponent;
    AddOwnedBinding(GetAppliedActionKey(EDWInputAction::Dash),IE_Pressed,&ADWPlayerController::Dash);
    for (EDWInputAction Action : {EDWInputAction::Sprint,EDWInputAction::SprintAlternate})
    {
        AddOwnedBinding(GetAppliedActionKey(Action),IE_Pressed,&ADWPlayerController::SprintDown);
        AddOwnedBinding(GetAppliedActionKey(Action),IE_Released,&ADWPlayerController::SprintUp);
    }
    AddOwnedBinding(GetAppliedActionKey(EDWInputAction::Harvest),IE_Pressed,&ADWPlayerController::HarvestDown);
    AddOwnedBinding(GetAppliedActionKey(EDWInputAction::Harvest),IE_Released,&ADWPlayerController::HarvestUp);
    AddOwnedBinding(GetAppliedActionKey(EDWInputAction::Transform),IE_Pressed,&ADWPlayerController::Transform);
    AddOwnedBinding(GetAppliedActionKey(EDWInputAction::Throw),IE_Pressed,&ADWPlayerController::Throw);
    AddOwnedBinding(GetAppliedActionKey(EDWInputAction::Inventory),IE_Pressed,&ADWPlayerController::Inventory,true);
    AddOwnedBinding(GetAppliedActionKey(EDWInputAction::Crafting),IE_Pressed,&ADWPlayerController::Crafting,true);
    AddOwnedBinding(GetAppliedActionKey(EDWInputAction::PauseMenu),IE_Pressed,&ADWPlayerController::PauseMenu,true);
    AddOwnedBinding(GetAppliedActionKey(EDWInputAction::PauseAlternate),IE_Pressed,&ADWPlayerController::PauseMenu,true);
    bInputBindingsPending=false;
    ++InputBindingsRevision;
}

void ADWPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    // Input components are owned by this controller and will be torn down by the engine.
    // Do not mutate their binding arrays here: EndPlay can itself run inside an input callback.
    PendingKeys.Reset();bInputBindingsPending=false;
    OwnedKeyRelays.Reset();OwnedInputComponent.Reset();
    Super::EndPlay(EndPlayReason);
}
bool ADWPlayerController::IsGameplayBlocked()const
{const auto* H=Cast<ADWGameplayHUD>(GetHUD());return UDWWorldEventSubsystem::IsPlaying(this)||IsPaused()||(H&&H->IsBlockingGameplay())||(Player()&&Player()->IsDead());}
void ADWPlayerController::PlayerTick(float DeltaSeconds)
{
    // A UI/input event may queue rebinding while Super::PlayerTick processes delegates.
    // Commit only before the next input pass so active KeyBindings iteration remains valid.
    CommitQueuedBindings();
    Super::PlayerTick(DeltaSeconds);auto* P=Player();if(!P)return;
    if(IsPaused()&&IsLocalPlayerController())
    {
        // CameraManager starts with the controller's eye-level cache, before possession.
        // Refresh the pawn POV while the title is paused without advancing any camera time.
        if(GetViewTarget()==this)SetViewTarget(P);
        UpdateCameraManager(0.f);
    }
    if(IsGameplayBlocked()){P->ClearHeldActions();return;}
    const float Forward=float(IsActionDown(EDWInputAction::MoveForward))-float(IsActionDown(EDWInputAction::MoveBackward));
    const float Right=float(IsActionDown(EDWInputAction::MoveRight))-float(IsActionDown(EDWInputAction::MoveLeft));
    P->MoveCameraRelative(Forward,Right);
    P->SetSprintHeld(IsActionDown(EDWInputAction::Sprint)||IsActionDown(EDWInputAction::SprintAlternate));
    P->SetHarvestHeld(IsActionDown(EDWInputAction::Harvest));
    if(IsActionDown(EDWInputAction::CameraDrag)){float X=0,Y=0;GetInputMouseDelta(X,Y);P->DragCamera(X,Y);}
}
void ADWPlayerController::Dash(){if(Player()&&!IsGameplayBlocked())Player()->PerformDash();}
void ADWPlayerController::SprintDown(){if(Player()&&!IsGameplayBlocked())Player()->SetSprintHeld(true);}
void ADWPlayerController::SprintUp(){if(Player())Player()->SetSprintHeld(IsActionDown(EDWInputAction::Sprint)||IsActionDown(EDWInputAction::SprintAlternate));}
void ADWPlayerController::HarvestDown(){if(Player()&&!IsGameplayBlocked())Player()->SetHarvestHeld(true);}
void ADWPlayerController::HarvestUp(){if(Player())Player()->SetHarvestHeld(false);}
void ADWPlayerController::Transform(){if(Player()&&!IsGameplayBlocked())Player()->TryTransform();}
void ADWPlayerController::Throw()
{
    auto* P=Player();if(!P||IsGameplayBlocked())return;
    FHitResult Hit;FVector Target=P->GetActorLocation()+P->GetActorForwardVector()*600.f;
    if(GetHitResultUnderCursor(ECC_Visibility,false,Hit))Target=Hit.ImpactPoint;
    else{FVector Origin,Direction;if(DeprojectMousePositionToWorld(Origin,Direction)&&FMath::Abs(Direction.Z)>KINDA_SMALL_NUMBER){const float T=(P->GetActorLocation().Z-70.f-Origin.Z)/Direction.Z;if(T>0)Target=Origin+Direction*T;}}
    P->ThrowAlcoholAt(Target);
}
void ADWPlayerController::Inventory(){if(UDWWorldEventSubsystem::IsPlaying(this))return;if(auto* H=Cast<ADWGameplayHUD>(GetHUD())){if(Player())Player()->ClearHeldActions();H->ToggleInventory();}}
void ADWPlayerController::Crafting(){if(UDWWorldEventSubsystem::IsPlaying(this))return;if(auto* H=Cast<ADWGameplayHUD>(GetHUD())){if(Player())Player()->ClearHeldActions();H->ToggleCrafting();}}
void ADWPlayerController::PauseMenu(){if(UDWWorldEventSubsystem::IsPlaying(this))return;if(auto* H=Cast<ADWGameplayHUD>(GetHUD())){if(Player())Player()->ClearHeldActions();H->TogglePause();}}

void ADWPlayerController::AssignKeys(const TArray<FKey>& K)
{
 FKey* Fields[]={&KeyMoveForward,&KeyMoveBackward,&KeyMoveLeft,&KeyMoveRight,&KeyDash,&KeySprint,&KeySprintAlternate,&KeyHarvest,&KeyTransform,&KeyThrow,&KeyCameraDrag,&KeyInventory,&KeyCrafting,&KeyPauseMenu,&KeyPauseAlternate};
 if(K.Num()!=UE_ARRAY_COUNT(Fields))return;for(int32 I=0;I<K.Num();++I)*Fields[I]=K[I];
}
void ADWPlayerController::LoadSavedBindings()
{
 const TArray<FKey> Before=GetConfiguredKeys();TArray<FKey> K=Before;
 for(int32 I=0;I<K.Num();++I){FString Value;const FString Name=StaticEnum<EDWInputAction>()->GetNameStringByValue(I);if(GConfig->GetString(TEXT("DoughWorld.Input"),*Name,Value,GGameUserSettingsIni))K[I]=FKey(FName(*Value));}
 AssignKeys(K);FText Error;if(!ValidateInputBindings(Error)){AssignKeys(Before);UE_LOG(LogTemp,Warning,TEXT("Ignoring invalid saved bindings: %s"),*Error.ToString());}
}
void ADWPlayerController::SaveBindings()const
{
 const auto K=GetConfiguredKeys();for(int32 I=0;I<K.Num();++I)GConfig->SetString(TEXT("DoughWorld.Input"),*StaticEnum<EDWInputAction>()->GetNameStringByValue(I),*K[I].GetFName().ToString(),GGameUserSettingsIni);GConfig->Flush(false,GGameUserSettingsIni);
}
bool ADWPlayerController::SetActionBinding(EDWInputAction Action,FKey Key,FText& Error,bool Persist)
{
 const auto Before=GetConfiguredKeys();auto K=Before;const int32 I=int32(Action);if(!K.IsValidIndex(I))return false;K[I]=Key;AssignKeys(K);
 if(!ApplyInputBindings(Error)){AssignKeys(Before);return false;}if(Persist)SaveBindings();return true;
}
bool ADWPlayerController::RestoreAuthoredBindings(FText& Error)
{
 const auto Before=GetConfiguredKeys();AssignKeys(GetClass()->GetDefaultObject<ADWPlayerController>()->GetConfiguredKeys());
 if(!ApplyInputBindings(Error)){AssignKeys(Before);return false;}GConfig->EmptySection(TEXT("DoughWorld.Input"),GGameUserSettingsIni);GConfig->Flush(false,GGameUserSettingsIni);return true;
}
FText ADWPlayerController::GetActionDisplayName(EDWInputAction A)const
{
 const TCHAR* CN[]={TEXT("向前移动"),TEXT("向后移动"),TEXT("向左移动"),TEXT("向右移动"),TEXT("短距离冲刺"),TEXT("持续疾跑"),TEXT("疾跑备用键"),TEXT("长按采集"),TEXT("变身"),TEXT("投掷酒精"),TEXT("拖拽视角"),TEXT("背包"),TEXT("制作"),TEXT("暂停 / 返回"),TEXT("暂停备用键")};
 const TCHAR* EN[]={TEXT("Move forward"),TEXT("Move backward"),TEXT("Move left"),TEXT("Move right"),TEXT("Dash"),TEXT("Sprint"),TEXT("Sprint alternate"),TEXT("Hold to gather"),TEXT("Transform"),TEXT("Throw alcohol"),TEXT("Drag camera"),TEXT("Inventory"),TEXT("Crafting"),TEXT("Pause / Back"),TEXT("Pause alternate")};
 const int32 I=int32(A);return I>=0&&I<UE_ARRAY_COUNT(CN)?DWText(this,CN[I],EN[I]):FText::GetEmpty();
}
