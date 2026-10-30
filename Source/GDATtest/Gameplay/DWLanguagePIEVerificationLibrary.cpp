#include "DWLanguagePIEVerificationLibrary.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Editor.h"
#include "DWPlayerCharacter.h"
#include "DWPlayerController.h"
#include "DWLanguageUIBridgeComponent.h"
#include "DWCharacterA01Component.h"
#include "DWLanguageSubsystem.h"
#include "DWLanguageCarrier.h"
#include "DWGameInstance.h"
#include "DWInventoryComponent.h"
#include "DWWeaponAnimationComponent.h"
#include "DWCombatAuthoritySubsystem.h"
#include "DWCombatWorldPersistence.h"
#include "DWGatheringProfile.h"
#include "DWGatheringWorld.h"
#include "DWSaveGame.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "JsonObjectConverter.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UObject/StrongObjectPtr.h"
#endif
namespace DWLanguagePIEEvidence
{
using FObject=TSharedRef<FJsonObject>;
FString Encode(const FObject& O){FString S;FJsonSerializer::Serialize(O,TJsonWriterFactory<>::Create(&S));return S;}
FString Reject(const FString& E){auto O=MakeShared<FJsonObject>();O->SetBoolField(TEXT("ok"),false);O->SetStringField(TEXT("error"),E);return Encode(O);}
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
struct FContext{UWorld* World=nullptr;UDWGameInstance* GI=nullptr;UDWLanguageSubsystem* Language=nullptr;FString Namespace,Directory,TrialFile;};
FString Normalize(FString S){S=FPaths::ConvertRelativePathToFull(S);FPaths::NormalizeDirectoryName(S);FPaths::CollapseRelativeDirectories(S);return S;}
bool Gate(ADWPlayerCharacter* P,FContext& C,FString& Error)
{
 auto Fail=[&](const TCHAR* E){Error=E;return false;};
 if(!IsInGameThread()||!GIsEditor||!GEditor||!IsValid(P)||P->IsActorBeingDestroyed()||!P->HasActorBegunPlay())return Fail(TEXT("Live editor PIE player on game thread required."));
 C.World=P->GetWorld();
 if(!C.World||C.World->WorldType!=EWorldType::PIE||GEditor->PlayWorld!=C.World||C.World->bIsTearingDown||C.World->GetNetMode()!=NM_Standalone||UGameplayStatics::GetPlayerPawn(C.World,0)!=P)return Fail(TEXT("Current player zero in active standalone PIE required."));
 if(UWorld::RemovePIEPrefix(C.World->GetOutermost()->GetName())!=TEXT("/Game/DoughWorld/Maps/Gameplay/Levels/L_DoughWorld_LanguagePlaytest"))return Fail(TEXT("Exact LanguagePlaytest map required."));
 if(!FParse::Value(FCommandLine::Get(),TEXT("DWLanguageSaveNamespace="),C.Namespace)||!C.Namespace.StartsWith(TEXT("DW_QA_"))||C.Namespace.Len()>80)return Fail(TEXT("Explicit DW_QA_ namespace required. Trial_Current rejected."));
 for(TCHAR Ch:C.Namespace)if(!FChar::IsAlnum(Ch)&&Ch!=TEXT('_'))return Fail(TEXT("Malformed QA namespace."));
 C.GI=P->GetGameInstance<UDWGameInstance>();C.Language=C.GI?C.GI->GetSubsystem<UDWLanguageSubsystem>():nullptr;
 if(!C.GI||C.GI->GetWorld()!=C.World||!C.Language||!C.Language->IsReady()||!P->CharacterA01||!P->CharacterA01->IsEnabled()||!P->Inventory||!P->WeaponAnimationComponent||P->IsCombatRestoreInProgress()||C.GI->IsPlayerRestorePending())return Fail(TEXT("Ready Language/A01/inventory with settled restore required."));
 C.Directory=Normalize(FPaths::ProjectSavedDir()/TEXT("LanguagePIEQA")/C.Namespace);FString ArgDirectory;
 if(!FParse::Value(FCommandLine::Get(),TEXT("DWSaveTestDirectory="),ArgDirectory)||FPaths::IsRelative(ArgDirectory)||Normalize(ArgDirectory)!=C.Directory||Normalize(C.GI->GetSaveDirectory())!=C.Directory||C.GI->GetResourceVerificationSavePrefix()!=C.Namespace+TEXT("_"))return Fail(TEXT("Adventure storage must also equal Saved/LanguagePIEQA/<namespace>, DWVerificationSavePrefix=<namespace>_."));
 C.TrialFile=Normalize(FPaths::ProjectSavedDir()/TEXT("LanguagePlaytest"))/(C.Namespace+TEXT(".sav"));Error.Reset();return true;
}
TArray<TSharedPtr<FJsonValue>> Stacks(const TArray<FDWItemStack>& In)
{TArray<TSharedPtr<FJsonValue>> Out;int32 I=0;for(const auto& S:In){auto O=MakeShared<FJsonObject>();O->SetNumberField(TEXT("slot"),I++);O->SetStringField(TEXT("id"),S.ItemId.ToString());O->SetNumberField(TEXT("quantity"),S.Quantity);Out.Add(MakeShared<FJsonValueObject>(O));}return Out;}
bool ParseObject(const FString& S,TSharedPtr<FJsonObject>& O){return FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(S),O)&&O.IsValid();}
void Location(const FObject& O,const FVector& V){O->SetNumberField(TEXT("x"),V.X);O->SetNumberField(TEXT("y"),V.Y);O->SetNumberField(TEXT("z"),V.Z);}
#endif
}
FString UDWLanguagePIEVerificationLibrary::InspectTrial(ADWPlayerCharacter* P,bool bCaptureAggregate)
{
 using namespace DWLanguagePIEEvidence;
#if !(WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS)
 return Reject(TEXT("Editor development PIE evidence only."));
#else
 FContext C;FString Error;if(!Gate(P,C,Error))return Reject(Error);
 auto O=MakeShared<FJsonObject>();O->SetBoolField(TEXT("ok"),true);O->SetStringField(TEXT("schema"),TEXT("LANG01_REAL_PIE_v1"));
 O->SetStringField(TEXT("namespace"),C.Namespace);O->SetStringField(TEXT("trial_file"),C.TrialFile);O->SetStringField(TEXT("adventure_directory"),C.Directory);
 O->SetNumberField(TEXT("trial_file_bytes"),IFileManager::Get().FileSize(*C.TrialFile));O->SetNumberField(TEXT("world_time"),C.World->GetTimeSeconds());O->SetBoolField(TEXT("paused"),C.World->IsPaused());O->SetBoolField(TEXT("dead"),P->IsDead());
 O->SetStringField(TEXT("player_path"),P->GetPathName());O->SetStringField(TEXT("world_path"),C.World->GetPathName());auto Pos=MakeShared<FJsonObject>();Location(Pos,P->GetActorLocation());O->SetObjectField(TEXT("position"),Pos);
 const auto& R=P->CharacterA01->GetRuntime();const auto& S=R.GetState();const auto& K=R.GetConfig();const auto& B=P->CharacterA01->GetAbility().GetState();
 auto A=MakeShared<FJsonObject>();
 A->SetNumberField(TEXT("health"),P->GetHealth());A->SetNumberField(TEXT("health_mirror"),S.CurrentHealth);A->SetNumberField(TEXT("healthy_max"),K.HealthyMax);A->SetNumberField(TEXT("effective_max"),R.EffectiveMaxHealth());A->SetNumberField(TEXT("recorded_loss"),S.RecordedCapLoss);
 A->SetNumberField(TEXT("water"),S.Water);A->SetNumberField(TEXT("satiety"),S.Satiety);A->SetNumberField(TEXT("water_max"),K.WaterMax);A->SetNumberField(TEXT("satiety_max"),K.SatietyMax);
 A->SetNumberField(TEXT("gas"),S.Gas);A->SetNumberField(TEXT("corruption"),S.Corruption);A->SetNumberField(TEXT("gas_max"),K.GasMax);A->SetNumberField(TEXT("corruption_max"),K.CorruptionMax);
 A->SetNumberField(TEXT("form"),static_cast<int32>(S.Form));A->SetNumberField(TEXT("remaining"),S.FormRemainingSeconds);A->SetNumberField(TEXT("entry_duration"),S.EntryDurationSeconds);
 A->SetNumberField(TEXT("loss_clock"),S.CapLossElapsedSeconds);A->SetNumberField(TEXT("loss_interval"),K.CapLossIntervalSeconds);A->SetNumberField(TEXT("loss_amount"),K.CapLossAmount);A->SetNumberField(TEXT("recovery_clock"),S.CapRecoveryElapsedSeconds);A->SetNumberField(TEXT("regen_clock"),S.NormalRegenElapsedSeconds);A->SetNumberField(TEXT("maximum_loss_fraction"),K.MaximumLossFraction);
 A->SetBoolField(TEXT("duration_talent_contributing"),P->CharacterA01->IsDurationTalentContributing());A->SetStringField(TEXT("resource_revision"),FString::Printf(TEXT("%llu"),static_cast<unsigned long long>(P->CharacterA01->GetResourceRevision())));
 A->SetNumberField(TEXT("ability_phase"),static_cast<int32>(B.Phase));A->SetNumberField(TEXT("cooldown"),B.CooldownRemainingSeconds);A->SetNumberField(TEXT("dash_distance"),B.DashDistanceCm);A->SetBoolField(TEXT("physical_down"),B.bPhysicalDown);A->SetBoolField(TEXT("release_required"),B.bReleaseRequired);A->SetBoolField(TEXT("invulnerable"),P->CharacterA01->IsDashInvulnerable());A->SetBoolField(TEXT("can_open_menus"),P->CharacterA01->CanOpenMenus());A->SetBoolField(TEXT("injecting"),P->IsCoreInjecting());A->SetNumberField(TEXT("injection_remaining"),P->GetCoreInjectionRemainingSeconds());O->SetObjectField(TEXT("a01"),A);
 O->SetArrayField(TEXT("inventory"),Stacks(P->Inventory->GetSlots()));O->SetNumberField(TEXT("inventory_revision"),P->Inventory->GetInventoryRevision());
 auto Counts=MakeShared<FJsonObject>();for(const auto& Stack:P->Inventory->GetSlots()){double N=0.;Counts->TryGetNumberField(Stack.ItemId.ToString(),N);Counts->SetNumberField(Stack.ItemId.ToString(),N+Stack.Quantity);}O->SetObjectField(TEXT("counts"),Counts);
 auto* Weapon=P->WeaponAnimationComponent.Get();
 O->SetNumberField(TEXT("weapon_revision"),Weapon->GetCombatWeaponResourceRevision());O->SetStringField(TEXT("weapon_restore_epoch"),FString::Printf(TEXT("%llu"),static_cast<unsigned long long>(Weapon->GetCombatWeaponRestoreEpoch())));
 O->SetNumberField(TEXT("weapon_attack_lock"),Weapon->GetRemainingAttackLockSeconds());O->SetNumberField(TEXT("weapon_action_serial"),Weapon->ActionSerial);O->SetBoolField(TEXT("weapon_busy"),Weapon->IsBusy());O->SetNumberField(TEXT("loaded_shells"),Weapon->GetCombatWeaponStateSnapshot().LoadedShotgunShells);
 O->SetBoolField(TEXT("combat_action_allowed"),P->IsCombatActionAllowed());
 O->SetBoolField(TEXT("fourth_shortcut_allowed"),P->CanUseMainHandShortcut(3));
 O->SetStringField(TEXT("equipment"),P->WeaponAnimationComponent->GetCurrentEquipmentId().ToString());TArray<TSharedPtr<FJsonValue>> Owned;for(FName Id:P->WeaponAnimationComponent->GetCombatWeaponStateSnapshot().OwnedPermanentWeaponIds)Owned.Add(MakeShared<FJsonValueString>(Id.ToString()));O->SetArrayField(TEXT("owned_permanent"),Owned);
 FString Progress;if(!C.Language->CaptureProgressJson(Progress,Error))return Reject(Error);TSharedPtr<FJsonObject> Parsed;if(!ParseObject(Progress,Parsed))return Reject(TEXT("Invalid live progress JSON."));O->SetObjectField(TEXT("language"),Parsed);
 auto* Native=C.Language->GetNativeModel();auto Talents=MakeShared<FJsonObject>();TArray<TSharedPtr<FJsonValue>> Relics;
 for(const auto& Pair:Native->GetCatalog().Talents){auto T=MakeShared<FJsonObject>();T->SetBoolField(TEXT("enabled"),Pair.Value.bEnabled);T->SetNumberField(TEXT("cost"),Pair.Value.Cost);TArray<TSharedPtr<FJsonValue>> Prereq;for(FName Id:Pair.Value.Prerequisites)Prereq.Add(MakeShared<FJsonValueString>(Id.ToString()));T->SetArrayField(TEXT("prerequisites"),Prereq);Talents->SetObjectField(Pair.Key.ToString(),T);}O->SetObjectField(TEXT("catalog_talents"),Talents);
 for(const auto& Pair:Native->GetCatalog().Relics){auto T=MakeShared<FJsonObject>();T->SetStringField(TEXT("id"),Pair.Key.ToString());T->SetNumberField(TEXT("reward"),Pair.Value.RewardPoints);TArray<TSharedPtr<FJsonValue>> Answers;for(FName Id:Pair.Value.AnswerIds)Answers.Add(MakeShared<FJsonValueString>(Id.ToString()));T->SetArrayField(TEXT("answers"),Answers);T->SetBoolField(TEXT("completed"),Native->GetState().CompletedRelics.Contains(Pair.Key));Relics.Add(MakeShared<FJsonValueObject>(T));}O->SetArrayField(TEXT("catalog_relics"),Relics);
 TArray<TSharedPtr<FJsonValue>> Carriers;for(TActorIterator<ADWLanguageCarrier> It(C.World);It;++It){auto T=MakeShared<FJsonObject>();T->SetStringField(TEXT("stable_id"),It->StableId.ToString());T->SetStringField(TEXT("context_id"),It->GetContextId().ToString());T->SetStringField(TEXT("path"),It->GetPathName());T->SetNumberField(TEXT("kind"),static_cast<int32>(It->Kind));T->SetBoolField(TEXT("in_reach"),It->IsInReach(P));Location(T,It->GetActorLocation());Carriers.Add(MakeShared<FJsonValueObject>(T));}O->SetArrayField(TEXT("carriers"),Carriers);
 const auto* Profile=Cast<UDWGatheringProfile>(C.GI->GetConfig());const FName RecipeId(TEXT("GatherCraft_SugarSpear_CraftingTable"));auto Recipe=MakeShared<FJsonObject>();Recipe->SetStringField(TEXT("id"),RecipeId.ToString());const auto* D=Profile?Profile->GetRecipeDefinition(RecipeId):nullptr;Recipe->SetBoolField(TEXT("found"),D!=nullptr);
 if(D){Recipe->SetArrayField(TEXT("inputs"),Stacks(D->Inputs));Recipe->SetArrayField(TEXT("outputs"),Stacks(D->Outputs));Recipe->SetStringField(TEXT("required_station"),Profile->GetRequiredStation(RecipeId).ToString());Recipe->SetBoolField(TEXT("requires_yeast"),D->bRequiresYeast);Recipe->SetBoolField(TEXT("can_craft_now"),P->Inventory->CanCraft(RecipeId,P->IsYeastForm()));}O->SetObjectField(TEXT("spear_recipe"),Recipe);
 TArray<TSharedPtr<FJsonValue>> Stations;for(TActorIterator<ADWGatheringStation> It(C.World);It;++It){auto T=MakeShared<FJsonObject>();T->SetStringField(TEXT("path"),It->GetPathName());T->SetStringField(TEXT("station_id"),It->StationId.ToString());T->SetStringField(TEXT("persistent_id"),It->PersistentId.ToString());T->SetBoolField(TEXT("available"),It->IsInteractionAvailable());T->SetBoolField(TEXT("in_reach"),It->IsWithinReach(P));Location(T,It->GetActorLocation());Stations.Add(MakeShared<FJsonValueObject>(T));}O->SetArrayField(TEXT("stations"),Stations);
 if(auto* PC=Cast<ADWPlayerController>(P->GetController()))O->SetStringField(TEXT("input_debug"),PC->GetCombatInputDebugState());
 if(auto* Authority=UDWCombatAuthoritySubsystem::Get(P)){const auto Life=Authority->FindRegisteredActorLife(P);O->SetStringField(TEXT("epoch"),FString::Printf(TEXT("%llu"),static_cast<unsigned long long>(Authority->GetWorldEpoch())));O->SetStringField(TEXT("life"),FString::Printf(TEXT("%llu:%llu"),static_cast<unsigned long long>(Life.Entity),static_cast<unsigned long long>(Life.Life)));}
 O->SetBoolField(TEXT("aggregate_capture_requested"),bCaptureAggregate);
 if(bCaptureAggregate){TStrongObjectPtr<UDWSaveGame> Save(NewObject<UDWSaveGame>(GetTransientPackage(),NAME_None,RF_Transient));const bool Complete=FDWCombatWorldPersistence::Capture(*P,*Save,Error);O->SetBoolField(TEXT("aggregate_complete"),Complete);O->SetStringField(TEXT("aggregate_error"),Error);if(Complete){FString Json;TSharedPtr<FJsonObject> Projection;if(FJsonObjectConverter::UStructToJsonObjectString(UDWSaveGame::StaticClass(),Save.Get(),Json,0,0)&&ParseObject(Json,Projection))O->SetObjectField(TEXT("aggregate"),Projection);else return Reject(TEXT("Complete save JSON projection failed."));}}
 return Encode(O);
#endif
}
FString UDWLanguagePIEVerificationLibrary::InteractTrialCarrier(ADWPlayerCharacter* P,FName StableId,bool bLanguageOnly)
{
 using namespace DWLanguagePIEEvidence;
#if !(WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS)
 return Reject(TEXT("Editor development PIE evidence only."));
#else
 FContext C;FString Error;if(!Gate(P,C,Error))return Reject(Error);ADWLanguageCarrier* Carrier=nullptr;
 for(TActorIterator<ADWLanguageCarrier> It(C.World);It;++It)if(It->StableId==StableId){if(Carrier)return Reject(TEXT("Ambiguous stable carrier ID."));Carrier=*It;}
 if(!Carrier||!Carrier->IsInReach(P))return Reject(TEXT("Existing unique carrier must be in actual interaction reach."));
 FText Message;const bool Accepted=C.Language->Interact(Carrier,bLanguageOnly,Message);auto O=MakeShared<FJsonObject>();O->SetBoolField(TEXT("ok"),Accepted);O->SetStringField(TEXT("message"),Message.ToString());O->SetStringField(TEXT("stable_id"),StableId.ToString());return Encode(O);
#endif
}

FString UDWLanguagePIEVerificationLibrary::CloseTrialPanel(ADWPlayerCharacter* P)
{
 using namespace DWLanguagePIEEvidence;
#if !(WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS)
 return Reject(TEXT("Editor development PIE evidence only."));
#else
 FContext C;FString Error;if(!Gate(P,C,Error))return Reject(Error);
 auto* Bridge=UDWLanguageUIBridgeComponent::Find(Cast<ADWPlayerController>(P->GetController()));
 if(!Bridge)return Reject(TEXT("Existing language UI bridge required for fixture teardown."));
 const bool HadLease=Bridge->HasModalLease();Bridge->CloseForLifecycle();
 auto O=MakeShared<FJsonObject>();O->SetBoolField(TEXT("ok"),!Bridge->HasModalLease());O->SetBoolField(TEXT("had_modal_lease"),HadLease);
 O->SetStringField(TEXT("boundary"),TEXT("Automated verification fixture teardown via CloseForLifecycle; not a real-user Esc test."));return Encode(O);
#endif
}

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
FString DWCompareIdleCapturePayloads(UDWLanguageSubsystem& Language,const FString& BeforeNativeJson,const FString& AfterNativeJson);
#endif
FString UDWLanguagePIEVerificationLibrary::CompareIdleTrialCaptures(ADWPlayerCharacter* P,const FString& BeforeNativeJson,const FString& AfterNativeJson)
{
 using namespace DWLanguagePIEEvidence;
#if !(WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS)
 return Reject(TEXT("Editor development idle comparison only."));
#else
 FContext C;FString Error;if(!Gate(P,C,Error))return Reject(Error);
 return DWCompareIdleCapturePayloads(*C.Language,BeforeNativeJson,AfterNativeJson);
#endif
}
