#include "DWGameInstance.h"
#include "DWWorldEvent.h"
#include "DWLoadingTransition.h"
#include "DWGameplayConfig.h"
#include "DWLocalizationLibrary.h"
#include "DWInventoryComponent.h"
#include "DWPlayerCharacter.h"
#include "DWSaveGame.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/PackageName.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

void UDWGameInstance::Init()
{
 Super::Init();GetSubsystem<UDWLoadingTransitionSubsystem>()->OnFailed.AddDynamic(this,&UDWGameInstance::HandleTravelFailure);
#if WITH_DEV_AUTOMATION_TESTS
 FString Prefix;if(FParse::Value(FCommandLine::Get(),TEXT("DWVerificationSavePrefix="),Prefix)&&Prefix.StartsWith(TEXT("DW_QA_"))&&Prefix.Len()<80&&!Prefix.Contains(TEXT("/"))&&!Prefix.Contains(TEXT("\\")))VerificationSavePrefix=Prefix;
#endif
 GetSubsystem<UDWLoadingTransitionSubsystem>()->OnFinished.AddDynamic(this,&UDWGameInstance::HandleTravelFinished);
}
void UDWGameInstance::HandleTravelFailure(FText Reason)
{
 if(bSaveTravelInFlight){PendingSave=nullptr;ActiveSlot=PreviousTravelSlot;bSaveTravelInFlight=false;}LastSaveError=Reason;
}

void UDWGameInstance::HandleTravelFinished(){bSaveTravelInFlight=false;}

UDWGameplayConfig* UDWGameInstance::GetConfig()
{
    if (!Config)
    {
        if (!SoftConfigPath.IsNull()) Config = Cast<UDWGameplayConfig>(SoftConfigPath.TryLoad());
        if (!Config) Config = NewObject<UDWGameplayConfig>(this);
        Config->RefreshDefinitionsFromTables();
    }
    return Config;
}

bool UDWGameInstance::IsValidSlot(int32 SlotIndex)
{
    return SlotIndex >= 0 && SlotIndex < SaveSlotCount;
}

FString UDWGameInstance::MakeSlotName(int32 SlotIndex)const
{
    if(!VerificationSavePrefix.IsEmpty())return VerificationSavePrefix+FString::FromInt(SlotIndex+1);
    return FString::Printf(TEXT("DoughWorld_Final_Slot_%d"), SlotIndex + 1);
}

bool UDWGameInstance::Fail(const FText& Reason)
{
    LastSaveError = Reason;
    UE_LOG(LogTemp, Warning, TEXT("DoughWorld save: %s"), *Reason.ToString());
    return false;
}

TArray<FDWSlotSummary> UDWGameInstance::GetSlotSummaries() const
{
    TArray<FDWSlotSummary> Result;
    Result.Reserve(SaveSlotCount);
    for (int32 Index = 0; Index < SaveSlotCount; ++Index)
    {
        FDWSlotSummary Summary;
        Summary.SlotIndex = Index;
        const FString SlotName = MakeSlotName(Index);
        Summary.bExists = UGameplayStatics::DoesSaveGameExist(SlotName, 0);
        if (Summary.bExists)
        {
            const UDWSaveGame* Save = Cast<UDWSaveGame>(UGameplayStatics::LoadGameFromSlot(SlotName, 0));
            if (Save)
            {
                Summary.DisplayName = Save->DisplayName;
                Summary.Timestamp = Save->Timestamp;
                Summary.MapPackage = Save->MapPackage;
            }
            else
            {
                Summary.DisplayName = DWText(this, TEXT("无法读取的存档（可删除）"), TEXT("Unreadable save (can be deleted)")).ToString();
            }
        }
        Result.Add(Summary);
    }
    return Result;
}

bool UDWGameInstance::ValidateSave(const UDWSaveGame* Save)
{
    if (!Save || (Save->Version < 1 || Save->Version > UDWSaveGame::CurrentVersion))
        return Fail(DWText(this, TEXT("存档格式无法读取或版本不兼容。"), TEXT("The save format is unreadable or its version is incompatible.")));
    if (!Save->MapPackage.StartsWith(TEXT("/Game/")) ||
        !FPackageName::IsValidLongPackageName(Save->MapPackage) ||
        !FPackageName::DoesPackageExist(Save->MapPackage) ||
        !IsAllowedGameplayMap(Save->MapPackage))
        return Fail(DWText(this, TEXT("存档地图不存在或不是当前玩法地图。"), TEXT("The saved map is missing or is not the current gameplay map.")));
    if (!FMath::IsFinite(Save->Health) || Save->Health < 0.f ||
        !FMath::IsFinite(Save->Transformation) || Save->Transformation < 0.f ||
        !FMath::IsFinite(Save->SprintAlcoholElapsed) || Save->SprintAlcoholElapsed < 0.f ||
        !FMath::IsFinite(Save->TransformationDecayElapsed) || Save->TransformationDecayElapsed < 0.f ||
        (Save->bHasPlayerTransform && !Save->PlayerTransform.IsValid()))
        return Fail(DWText(this, TEXT("存档包含无效的玩家属性或位置，已停止加载。"), TEXT("Loading stopped: the save contains invalid player attributes or a position.")));

    UDWInventoryComponent* Validator = NewObject<UDWInventoryComponent>(this);
    Validator->InitializeInventory(GetConfig());
    if (!Validator->AreSlotsValid(Save->Inventory))
        return Fail(DWText(this, TEXT("存档背包与当前物品或容量配置不兼容；原存档已保留。"), TEXT("The saved inventory is incompatible with the current items or capacity. The original save has been preserved.")));

    TSet<FString> ActorIds;
    for (const FDWPersistedActorState& ActorState : Save->WorldActors)
    {
        if (ActorState.ActorId.IsEmpty() || ActorIds.Contains(ActorState.ActorId) ||
            !FMath::IsFinite(ActorState.Health) || ActorState.Health < 0.f ||
            (ActorState.bHasTransform&&!ActorState.Transform.IsValid()) || !FMath::IsFinite(ActorState.SpawnProgress) || ActorState.SpawnProgress<0)
            return Fail(DWText(this, TEXT("存档包含无效或重复的场景物件记录。"), TEXT("The save contains invalid or duplicate world object records.")));
        ActorIds.Add(ActorState.ActorId);
    }
    return true;
}

bool UDWGameInstance::NewGame(int32 SlotIndex, const FString& Name)
{
    if(UDWWorldEventSubsystem::IsPlaying(this))return Fail(DWText(this,TEXT("请等待场景演出结束。"),TEXT("Please wait for the cinematic.")));
    if(auto* T=GetSubsystem<UDWLoadingTransitionSubsystem>();T&&T->IsTransitionActive())return Fail(DWText(this,TEXT("请等待当前过渡完成。"),TEXT("Please wait for the current transition.")));
    LastSaveError = FText::GetEmpty();
    if (!IsValidSlot(SlotIndex)) return Fail(DWText(this, TEXT("只能使用存档槽 1 至 3。"), TEXT("Only save slots 1 through 3 are available.")));
    if (!GetWorld() || !GetSubsystem<UDWLoadingTransitionSubsystem>()) return Fail(DWText(this, TEXT("游戏世界尚未准备好。"), TEXT("The game world is not ready yet.")));
    if (UGameplayStatics::DoesSaveGameExist(MakeSlotName(SlotIndex), 0))
        return Fail(DWText(this, TEXT("该槽位已有存档，请先删除或选择空槽位。"), TEXT("This slot already contains a save. Delete it first or choose an empty slot.")));

    UDWSaveGame* Save = Cast<UDWSaveGame>(UGameplayStatics::CreateSaveGameObject(UDWSaveGame::StaticClass()));
    if (!Save) return Fail(DWText(this, TEXT("无法创建存档对象。"), TEXT("Unable to create the save object.")));
    Save->DisplayName = Name.TrimStartAndEnd();
    if (Save->DisplayName.IsEmpty()) Save->DisplayName = FText::Format(DWText(this, TEXT("冒险 {0}"), TEXT("Adventure {0}")), FText::AsNumber(SlotIndex + 1)).ToString();
    Save->Timestamp = FDateTime::UtcNow();
    Save->MapPackage = GetConfig()->GameplayMap;
    Save->Health = FMath::Max(1.f, GetConfig()->MaxHealth);
    Save->bHasPlayerTransform = false;
    if (!ValidateSave(Save)) return false;
    if (!UGameplayStatics::SaveGameToSlot(Save, MakeSlotName(SlotIndex), 0))
        return Fail(DWText(this, TEXT("写入新存档失败，请检查磁盘空间与写入权限。"), TEXT("Unable to write the new save. Check disk space and write permissions.")));

    bSaveTravelInFlight=true;PreviousTravelSlot=ActiveSlot;ActiveSlot = SlotIndex;
    PendingSave = Save;
    auto* Transition=GetSubsystem<UDWLoadingTransitionSubsystem>();
    if(!Transition->RequestMap(Save->MapPackage)){HandleTravelFailure(Transition->LastError);return false;}
    if(!Transition->IsTransitionActive())bSaveTravelInFlight=false;
    return true;
}

bool UDWGameInstance::LoadGameSlot(int32 SlotIndex)
{
    if(UDWWorldEventSubsystem::IsPlaying(this))return Fail(DWText(this,TEXT("请等待场景演出结束。"),TEXT("Please wait for the cinematic.")));
    if(auto* T=GetSubsystem<UDWLoadingTransitionSubsystem>();T&&T->IsTransitionActive())return Fail(DWText(this,TEXT("请等待当前过渡完成。"),TEXT("Please wait for the current transition.")));
    LastSaveError = FText::GetEmpty();
    if (!IsValidSlot(SlotIndex)) return Fail(DWText(this, TEXT("只能使用存档槽 1 至 3。"), TEXT("Only save slots 1 through 3 are available.")));
    if (!GetWorld() || !GetSubsystem<UDWLoadingTransitionSubsystem>()) return Fail(DWText(this, TEXT("游戏世界尚未准备好。"), TEXT("The game world is not ready yet.")));
    if (!UGameplayStatics::DoesSaveGameExist(MakeSlotName(SlotIndex), 0))
        return Fail(DWText(this, TEXT("该存档槽为空。"), TEXT("This save slot is empty.")));
    UDWSaveGame* Save = Cast<UDWSaveGame>(UGameplayStatics::LoadGameFromSlot(MakeSlotName(SlotIndex), 0));
    if (!ValidateSave(Save)) return false;

    bSaveTravelInFlight=true;PreviousTravelSlot=ActiveSlot;ActiveSlot = SlotIndex;
    PendingSave = Save;
    auto* Transition=GetSubsystem<UDWLoadingTransitionSubsystem>();
    if(!Transition->RequestMap(Save->MapPackage)){HandleTravelFailure(Transition->LastError);return false;}
    if(!Transition->IsTransitionActive())bSaveTravelInFlight=false;
    return true;
}

bool UDWGameInstance::DeleteGameSlot(int32 SlotIndex)
{
    if(auto* T=GetSubsystem<UDWLoadingTransitionSubsystem>();T&&T->IsTransitionActive())return Fail(DWText(this,TEXT("请等待当前过渡完成。"),TEXT("Please wait for the current transition.")));
    LastSaveError = FText::GetEmpty();
    if (!IsValidSlot(SlotIndex)) return Fail(DWText(this, TEXT("只能使用存档槽 1 至 3。"), TEXT("Only save slots 1 through 3 are available.")));
    const FString SlotName = MakeSlotName(SlotIndex);
    if (!UGameplayStatics::DoesSaveGameExist(SlotName, 0)) return true;
    if (!UGameplayStatics::DeleteGameInSlot(SlotName, 0))
        return Fail(DWText(this, TEXT("删除存档失败，文件可能被占用。"), TEXT("Unable to delete the save. The file may be in use.")));
    if (ActiveSlot == SlotIndex)
    {
        ActiveSlot = INDEX_NONE;
        PendingSave = nullptr;
    }
    return true;
}

bool UDWGameInstance::SaveCurrentGame()
{
    if(UDWWorldEventSubsystem::IsPlaying(this))return Fail(DWText(this,TEXT("请等待场景演出结束再保存。"),TEXT("Please wait for the cinematic before saving.")));
    LastSaveError = FText::GetEmpty();
    if (!HasActiveSlot()) return Fail(DWText(this, TEXT("尚未进入存档，无法保存标题界面。"), TEXT("No save slot is active. You cannot save from the title screen.")));
    UWorld* World = GetWorld();
    ADWPlayerCharacter* Player = Cast<ADWPlayerCharacter>(UGameplayStatics::GetPlayerPawn(this, 0));
    if (!World || !Player) return Fail(DWText(this, TEXT("当前玩家尚未准备好，未覆盖原存档。"), TEXT("The player is not ready yet. The original save has not been overwritten.")));
    if (Player->IsDead()) return Fail(DWText(this, TEXT("角色已死亡，保留上次存档供重新加载。"), TEXT("The character has died. The previous save has been kept for reloading.")));
    const FString MapPackage = UWorld::RemovePIEPrefix(World->GetOutermost()->GetName());
    if (!IsAllowedGameplayMap(MapPackage))
        return Fail(DWText(this, TEXT("当前关卡不是正式玩法测试地图，未覆盖原存档。"), TEXT("This level is not the designated gameplay test map. The original save has not been overwritten.")));

    UDWSaveGame* Save = Cast<UDWSaveGame>(UGameplayStatics::CreateSaveGameObject(UDWSaveGame::StaticClass()));
    if (!Save) return Fail(DWText(this, TEXT("无法创建存档对象。"), TEXT("Unable to create the save object.")));
    const UDWSaveGame* Existing = Cast<UDWSaveGame>(UGameplayStatics::LoadGameFromSlot(MakeSlotName(ActiveSlot), 0));
    Save->DisplayName = Existing ? Existing->DisplayName : FText::Format(DWText(this, TEXT("冒险 {0}"), TEXT("Adventure {0}")), FText::AsNumber(ActiveSlot + 1)).ToString();
    Player->CaptureSaveData(Save);
    if(Existing)Save->VisitedMaps=Existing->VisitedMaps;
    FDWMapSnapshot* Snapshot=Save->VisitedMaps.FindByPredicate([&](const FDWMapSnapshot& M){return M.MapPackage==MapPackage;});
    if(!Snapshot)Snapshot=&Save->VisitedMaps.AddDefaulted_GetRef();
    Snapshot->MapPackage=MapPackage;Snapshot->Actors=Save->WorldActors;Snapshot->CompletedWorldEvents=Save->CompletedWorldEvents;Snapshot->PlayerTransform=Save->PlayerTransform;
    Save->bHasPlayerTransform = true;
    Save->MapPackage = MapPackage;
    Save->Version = UDWSaveGame::CurrentVersion;
    Save->Timestamp = FDateTime::UtcNow();
    if (!ValidateSave(Save)) return false;
    if (!UGameplayStatics::SaveGameToSlot(Save, MakeSlotName(ActiveSlot), 0))
        return Fail(DWText(this, TEXT("保存失败，请检查磁盘空间与写入权限。"), TEXT("Saving failed. Check disk space and write permissions.")));
    return true;
}

void UDWGameInstance::ApplyPendingSave(ADWPlayerCharacter* Player)
{
    if (!Player || !PendingSave || !HasActiveSlot()) return;
    const UWorld* World = Player->GetWorld();
    if (!World || UWorld::RemovePIEPrefix(World->GetOutermost()->GetName()) != PendingSave->MapPackage) return;
    if (!ValidateSave(PendingSave)) return;
    Player->ApplySaveData(PendingSave);
    PendingSave = nullptr;
}

void UDWGameInstance::ReturnToTitle()
{
    if(UDWWorldEventSubsystem::IsPlaying(this))return;
    if(auto* T=GetSubsystem<UDWLoadingTransitionSubsystem>();T&&T->IsTransitionActive())return;
    const FString MenuMap = GetConfig()->MainMenuMap;
    if (!GetWorld() || !FPackageName::IsValidLongPackageName(MenuMap) || !FPackageName::DoesPackageExist(MenuMap))
    {
        Fail(DWText(this, TEXT("标题地图不存在，已留在当前关卡。"), TEXT("The title map is missing. You have remained in the current level.")));
        return;
    }
    bSaveTravelInFlight=true;PreviousTravelSlot=ActiveSlot;ActiveSlot = INDEX_NONE;
    PendingSave = nullptr;
    auto* Transition=GetSubsystem<UDWLoadingTransitionSubsystem>();
    if(!Transition->RequestMap(MenuMap))HandleTravelFailure(Transition->LastError);
    if(!Transition->IsTransitionActive())bSaveTravelInFlight=false;
}

bool UDWGameInstance::IsAllowedGameplayMap(const FString& Map)
{
 auto* C=GetConfig();if(Map==C->GameplayMap)return true;
 if(Map==C->MainMenuMap)return false;
 for(const auto& M:C->AdditionalGameplayMaps)if(M.ToSoftObjectPath().GetLongPackageName()==Map)return true;
 return false;
}
bool UDWGameInstance::TravelToGameplayMap(TSoftObjectPtr<UWorld> Destination)
{
 const FString Map=Destination.ToSoftObjectPath().GetLongPackageName();
 auto* T=GetSubsystem<UDWLoadingTransitionSubsystem>();
 if(T->IsTransitionActive())return false;
 if(!IsAllowedGameplayMap(Map)||!FPackageName::DoesPackageExist(Map))return Fail(DWText(this,TEXT("请在 Additional Gameplay Maps 中登记目标地图。"),TEXT("Register the destination in Additional Gameplay Maps first.")));
 if(!SaveCurrentGame())return false;
 auto* Save=Cast<UDWSaveGame>(UGameplayStatics::LoadGameFromSlot(MakeSlotName(ActiveSlot),0));if(!Save)return false;
 Save->MapPackage=Map;Save->WorldActors.Reset();Save->CompletedWorldEvents.Reset();Save->bHasPlayerTransform=false;
 for(const auto& M:Save->VisitedMaps)if(M.MapPackage==Map){Save->WorldActors=M.Actors;Save->CompletedWorldEvents=M.CompletedWorldEvents;Save->PlayerTransform=M.PlayerTransform;Save->bHasPlayerTransform=true;break;}
 if(!ValidateSave(Save))return false;
 PendingSave=Save;PreviousTravelSlot=ActiveSlot;bSaveTravelInFlight=true;
 if(!T->RequestMap(Map)){HandleTravelFailure(T->LastError);return false;}
 return true;
}
