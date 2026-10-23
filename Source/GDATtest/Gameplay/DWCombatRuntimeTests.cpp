// Opt-in real-world verification. Ordinary launches never install this ticker.
#include "CoreMinimal.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "DWPlayerCharacter.h"
#include "DWPlayerController.h"
#include "DWEnemyCharacter.h"
#include "DWGameInstance.h"
#include "DWGameplayHUD.h"
#include "DWInventoryComponent.h"
#include "DWCombatAnimInstance.h"
#include "DWGameplayConfig.h"
#include "DWSaveSettings.h"
#include "DWSaveStorage.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimInstance.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/DamageEvents.h"
#include "EngineUtils.h"
#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/DelayedAutoRegister.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UnrealClient.h"

namespace DWCombatRuntimeQA
{
static FString Full(FString P){P=FPaths::ConvertRelativePathToFull(P);FPaths::NormalizeFilename(P);FPaths::CollapseRelativeDirectories(P);while(P.EndsWith(TEXT("/")))P.LeftChopInline(1);return P;}
static FString ActiveSettingsPath()
{
    if(GConfig)if(const FConfigFile* File=GConfig->FindConfigFile(GGameUserSettingsIni))
        if(File->Branch&&!File->Branch->IniPath.IsEmpty())return File->Branch->IniPath;
    return GGameUserSettingsIni;
}
static bool Outside(const FString& P)
{
    const FString Formal=DWSaveStorage::DocumentsDirectory(GetDefault<UDWSaveSettings>()->DocumentsFolderName);
    return !FPaths::IsUnderDirectory(Full(P),Full(FPaths::ProjectDir()))&&!FPaths::IsUnderDirectory(Full(P),Full(Formal));
}
struct FRun
{
    FString Report;
    int32 Stage=0;
    double Start=FPlatformTime::Seconds(),Until=0;
    bool Passed=true;
    TArray<TSharedPtr<FJsonValue>> Checks;
    TSet<FName> PlayerGraphStates,EnemyGraphStates;
    TWeakObjectPtr<UWorld> World;
    TWeakObjectPtr<ADWPlayerCharacter> Hero;
    TWeakObjectPtr<ADWEnemyCharacter> Enemy,Second;
    TWeakObjectPtr<AStaticMeshActor> Wall;
    float PriorHealth=0,PriorEnemy=0;
    FVector PriorLocation;
    FTransform WeaponRest;
    bool bWeaponVisibleBefore=false,bWeaponHiddenBefore=false;
    void Check(const FString& Name,bool Result,const FString& Detail=FString())
    {
        auto Entry=MakeShared<FJsonObject>();Entry->SetStringField(TEXT("name"),Name);Entry->SetBoolField(TEXT("passed"),Result);Entry->SetStringField(TEXT("detail"),Detail);
        Checks.Add(MakeShared<FJsonValueObject>(Entry));Passed&=Result;
        UE_LOG(LogTemp,Log,TEXT("DW_COMBAT_QA %s %s %s"),Result?TEXT("PASS"):TEXT("FAIL"),*Name,*Detail);
    }
    void Next(int32 NewStage,double Wait=0){Stage=NewStage;Until=(Stage>=2&&World.IsValid()?double(World->GetTimeSeconds()):FPlatformTime::Seconds())+Wait;}
    void Place(ACharacter* Actor,float X,float Y=0)
    {
        Actor->GetCharacterMovement()->StopMovementImmediately();
        Actor->SetActorLocation(FVector(X,Y,6000.f+Actor->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+2.f),false,nullptr,ETeleportType::TeleportPhysics);
        Actor->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    }
    ADWEnemyCharacter* SpawnEnemy(UWorld* W,float X,float Y=0)
    {
        UClass* Cls=LoadClass<ADWEnemyCharacter>(nullptr,TEXT("/Game/DoughWorld/Characters/Enemies/Blueprints/BP_DWEnemy.BP_DWEnemy_C"));
        if(!Cls)return nullptr;
        FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* E=W->SpawnActor<ADWEnemyCharacter>(Cls,FVector(X,Y,6200),FRotator(0,180,0),Params);
        if(E){E->PersistentId=MakeUniqueObjectName(W,Cls,TEXT("DW_QA_CombatEnemy"));E->MaxHealth=1000;E->SetHealthForLoad(1000);E->PatrolRadius=0;E->DetectionRadius=1;E->LoseTargetRadius=1;E->MoveSpeed=0;E->DeathDestroyDelay=10;Place(E,X,Y);}
        return E;
    }
    AStaticMeshActor* Box(UWorld* W,FVector Location,FVector Scale)
    {
        auto* A=W->SpawnActor<AStaticMeshActor>(Location,FRotator::ZeroRotator);
        if(!A){Check(TEXT("spawn_qa_geometry"),false);return nullptr;}
        auto* C=A->GetStaticMeshComponent();C->SetMobility(EComponentMobility::Movable);C->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
        C->SetCollisionProfileName(TEXT("BlockAll"));A->SetActorScale3D(Scale);return A;
    }
    void Shot(const FString& Suffix){FScreenshotRequest::RequestScreenshot(FPaths::ChangeExtension(Report,TEXT(""))+Suffix+TEXT(".png"),true,false);}
    bool Finish()
    {
        for(const TCHAR* Name : {TEXT("Idle"),TEXT("Move"),TEXT("Throw"),TEXT("Transform"),TEXT("Dash"),TEXT("Melee"),TEXT("Death")})
            Check(FString(TEXT("player_graph_evaluated_"))+Name,PlayerGraphStates.Contains(FName(Name)));
        for(const TCHAR* Name : {TEXT("Idle"),TEXT("Move"),TEXT("Alert"),TEXT("Attack"),TEXT("Death")})
            Check(FString(TEXT("enemy_graph_evaluated_"))+Name,EnemyGraphStates.Contains(FName(Name)));
        auto Root=MakeShared<FJsonObject>();Root->SetBoolField(TEXT("passed"),Passed);Root->SetNumberField(TEXT("check_count"),Checks.Num());Root->SetArrayField(TEXT("checks"),Checks);
        Root->SetStringField(TEXT("boundary"),TEXT("Opt-in isolated Development runtime. Real BP actors and world ticks; synthetic API input. No editor asset writes, no hardware-input or other-machine proof."));
        FString Json;FJsonSerializer::Serialize(Root,TJsonWriterFactory<>::Create(&Json));FFileHelper::SaveStringToFile(Json,*Report,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
        if(GEngine)GEngine->Exec(World.Get(),TEXT("QUIT"));return false;
    }
    bool Tick(float)
    {
        if(Hero.IsValid())
        {
            if(auto* Anim=Cast<UDWCombatAnimInstance>(Hero->GetMesh()->GetAnimInstance()))PlayerGraphStates.Add(Anim->GetCombatGraphStateName());
            if(Stage==33)Hero->AddMovementInput(FVector::RightVector,1.f);
        }
        for(auto Candidate : {Enemy,Second})if(Candidate.IsValid())
            if(auto* Anim=Cast<UDWCombatAnimInstance>(Candidate->GetMesh()->GetAnimInstance()))EnemyGraphStates.Add(Anim->GetCombatGraphStateName());
        if(FPlatformTime::Seconds()-Start>160){Check(TEXT("timeout"),false,FString::FromInt(Stage));return Finish();}
        if((Stage>=2&&World.IsValid()?double(World->GetTimeSeconds()):FPlatformTime::Seconds())<Until)return true;
        UWorld* W=nullptr;for(const FWorldContext& C:GEngine->GetWorldContexts())if(C.World()&&C.WorldType==EWorldType::Game){W=C.World();break;}
        if(!W)return true;World=W;
        auto* GI=W->GetGameInstance<UDWGameInstance>();if(!GI)return true;
        auto* PC=Cast<ADWPlayerController>(W->GetFirstPlayerController());
        auto* P=PC?Cast<ADWPlayerCharacter>(PC->GetPawn()):nullptr;
        if(Stage==0)
        {
            FString SaveDir,Prefix,Ini;
            const bool Flags=FParse::Value(FCommandLine::Get(),TEXT("DWSaveTestDirectory="),SaveDir)&&FParse::Value(FCommandLine::Get(),TEXT("DWVerificationSavePrefix="),Prefix)&&FParse::Value(FCommandLine::Get(),TEXT("GameUserSettingsINI="),Ini);
            const bool Safe=Flags&&!FPaths::IsRelative(SaveDir)&&!FPaths::IsRelative(Ini)&&Prefix.StartsWith(TEXT("DW_QA_"))&&Prefix.Len()<80&&!Prefix.Contains(TEXT("/"))&&!Prefix.Contains(TEXT("\\"))&&Outside(SaveDir)&&Outside(Ini)&&Outside(Report)&&Full(GI->GetSaveDirectory()).Equals(Full(SaveDir),ESearchCase::IgnoreCase)&&Full(ActiveSettingsPath()).Equals(Full(Ini),ESearchCase::IgnoreCase)&&FPaths::GetCleanFilename(Ini).StartsWith(TEXT("DW_QA_"))&&!IFileManager::Get().FileExists(*Report);
            if(!Safe){UE_LOG(LogTemp,Error,TEXT("DW_COMBAT_QA refused unsafe isolation arguments: save=[%s] actual=[%s] ini=[%s] actual=[%s]"),*SaveDir,*GI->GetSaveDirectory(),*Ini,*ActiveSettingsPath());if(GEngine)GEngine->Exec(W,TEXT("QUIT"));return false;}
            Check(TEXT("isolated_new_game"),GI->NewGame(0,TEXT("Combat verification only")));Next(1,2);return true;
        }
        if(Stage==1)
        {
            if(!P||!P->IsCombatActionAllowed()||GI->IsPlayerRestorePending())return true;
            Hero=P;
            WeaponRest=P->SwordPlaceholder->GetRelativeTransform();bWeaponVisibleBefore=P->SwordPlaceholder->IsVisible();bWeaponHiddenBefore=P->SwordPlaceholder->bHiddenInGame;
            for(TActorIterator<ADWEnemyCharacter> It(W);It;++It){It->SetActorTickEnabled(false);It->SetActorEnableCollision(false);}
            if(!Box(W,FVector(0,0,5980),FVector(40,40,.4)))return Finish();Place(P,0);P->SetActorRotation(FRotator::ZeroRotator);P->bDrawCombatDebug=true;
            P->MeleeKnockbackDistance=0;P->MeleeDamage=25;P->MeleeWindupSeconds=.35f;P->MeleeActiveSeconds=.18f;P->MeleeRecoverySeconds=.25f;P->MeleeCooldown=.1f;
            Enemy=SpawnEnemy(W,140);Second=SpawnEnemy(W,1000,500);
            Check(TEXT("actual_enemy_blueprint_spawned"),Enemy.IsValid()&&Second.IsValid());
            if(!Enemy.IsValid()||!Second.IsValid())return Finish();
            Check(TEXT("player_anim_blueprint_active"),P->bUseCombatAnimBlueprint&&P->GetMesh()->GetAnimationMode()==EAnimationMode::AnimationBlueprint&&Cast<UDWCombatAnimInstance>(P->GetMesh()->GetAnimInstance())!=nullptr);
            Check(TEXT("enemy_anim_blueprint_active"),Enemy->bUseCombatAnimBlueprint&&Enemy->GetMesh()->GetAnimationMode()==EAnimationMode::AnimationBlueprint&&Cast<UDWCombatAnimInstance>(Enemy->GetMesh()->GetAnimInstance())!=nullptr);
            Check(TEXT("input_right_melee_left_throw"),PC->GetAppliedActionKey(EDWInputAction::Melee)==EKeys::RightMouseButton&&PC->GetAppliedActionKey(EDWInputAction::Throw)==EKeys::LeftMouseButton);
            Next(2,.3);return true;
        }
        if(!P||P!=Hero.Get()||!Enemy.IsValid()||!Second.IsValid()){Check(TEXT("test_actors_retained"),false);return Finish();}
        auto* E=Enemy.Get();auto* E2=Second.Get();
        switch(Stage)
        {
        case 2:
            PriorEnemy=E->Health;Check(TEXT("melee_start"),P->PerformMeleeAttack(E->GetActorLocation()));Check(TEXT("melee_windup_phase"),P->AttackPhase==EDWCombatPhase::Windup);Check(TEXT("melee_no_immediate_damage"),E->Health==PriorEnemy);Next(3,.12);break;
        case 3:
            Check(TEXT("melee_no_damage_during_windup"),E->Health==PriorEnemy);Check(TEXT("missing_clip_or_socket_placeholder_visible"),P->bMeleeWeaponPlaceholderActive&&P->SwordPlaceholder->IsVisible()&&!P->SwordPlaceholder->bHiddenInGame&&P->SwordPlaceholder->GetAttachParent()==P->GetCapsuleComponent()&&P->SwordPlaceholder->GetRelativeScale3D().Equals(FVector(1.f,.12f,.12f)));Place(E,900);Next(4,.85);break;
        case 4:
            Check(TEXT("melee_target_escaped_misses"),E->Health==PriorEnemy);Place(E,140,-40);Place(E2,140,40);Check(TEXT("melee_multi_start"),P->PerformMeleeAttack(FVector(500,0,6100)));Next(5,.48);break;
        case 5:
            Check(TEXT("melee_first_target_once"),FMath::IsNearlyEqual(E->Health,975.f));Check(TEXT("melee_second_target_once"),FMath::IsNearlyEqual(E2->Health,975.f));Shot(TEXT("_Melee"));Next(6,.4);break;
        case 6:
            Check(TEXT("melee_no_repeat_after_window"),FMath::IsNearlyEqual(E->Health,975.f));Check(TEXT("melee_placeholder_restores_original_visual"),!P->bMeleeWeaponPlaceholderActive&&P->SwordPlaceholder->GetRelativeTransform().Equals(WeaponRest)&&P->SwordPlaceholder->IsVisible()==bWeaponVisibleBefore&&P->SwordPlaceholder->bHiddenInGame==bWeaponHiddenBefore);Place(E,-130);Place(E2,1000,500);PriorEnemy=E->Health;Check(TEXT("melee_forward_start"),P->PerformMeleeAttack(FVector(500,0,6100)));Next(7,.9);break;
        case 7:
            Check(TEXT("melee_behind_excluded"),E->Health==PriorEnemy);Place(E,155);Wall=Box(W,FVector(82,0,6100),FVector(.15,3,2));Check(TEXT("melee_wall_start"),P->PerformMeleeAttack(E->GetActorLocation()));Next(8,.9);break;
        case 8:
            Check(TEXT("melee_wall_blocks_damage"),E->Health==PriorEnemy);Check(TEXT("melee_wall_exists"),Wall.IsValid());if(Wall.IsValid())Wall->Destroy();Wall.Reset();Place(E,135);E->AttackWindup=.4f;E->AttackActiveDuration=.15f;E->AttackRecovery=.2f;E->AttackInterval=.8f;E->AttackDamage=8;
            PriorHealth=P->Health;Check(TEXT("enemy_attack_starts"),E->TryStartMeleeAttack(P));Check(TEXT("enemy_no_immediate_damage"),P->Health==PriorHealth);Next(9,.15);break;
        case 9:
            Check(TEXT("enemy_windup_no_damage"),P->Health==PriorHealth);Place(P,-650);Next(10,.85);break;
        case 10:
            Check(TEXT("enemy_escape_misses"),P->Health==PriorHealth);Place(P,0);Place(E,135);Check(TEXT("enemy_hit_start"),E->TryStartMeleeAttack(P));Next(11,.6);break;
        case 11:
            Check(TEXT("enemy_hit_once"),FMath::IsNearlyEqual(P->Health,PriorHealth-8));Shot(TEXT("_EnemyAttack"));Next(12,.4);break;
        case 12:
            Check(TEXT("enemy_no_repeat"),FMath::IsNearlyEqual(P->Health,PriorHealth-8));PriorHealth=P->Health;Check(TEXT("enemy_windup_before_death"),E->TryStartMeleeAttack(P));{FDamageEvent Damage;E->TakeDamage(2000,Damage,PC,P);}Next(13,.7);break;
        case 13:
            Check(TEXT("enemy_death_cancels_attack"),P->Health==PriorHealth&&E->AttackPhase==EDWCombatPhase::None&&!E->IsAlive());
            Check(TEXT("enemy_dead_anim_state"),E->GetCombatAnimationState()==EDWCombatAnimationState::Death);E->SetHealthForLoad(1000);
            Check(TEXT("enemy_restore_alive_collision_tick"),E->IsAlive()&&E->GetActorEnableCollision()&&E->IsActorTickEnabled()&&!E->IsHidden());Place(E,400);PriorLocation=E->GetActorLocation();E->ApplyCombatKnockback(FVector::ForwardVector,250,.25f);Next(14,.55);break;
        case 14:
            Check(TEXT("knockback_250cm"),FMath::Abs((E->GetActorLocation()-PriorLocation).Size2D()-250)<8,FString::SanitizeFloat((E->GetActorLocation()-PriorLocation).Size2D()));
            Place(E,300);PriorLocation=E->GetActorLocation();Wall=Box(W,FVector(450,0,6100),FVector(.2,4,2));E->ApplyCombatKnockback(FVector::ForwardVector,250,.25f);Next(15,.55);break;
        case 15:
            Check(TEXT("knockback_stops_at_wall"),Wall.IsValid()&&E->GetActorLocation().X<440&&E->GetActorLocation().X>PriorLocation.X);if(Wall.IsValid())Wall->Destroy();Wall.Reset();
            Place(E,190);Place(P,0);P->SetActorRotation(FRotator::ZeroRotator);P->DashDamage=35;P->DashKnockbackDistance=250;PriorEnemy=E->Health;PriorLocation=E->GetActorLocation();P->PerformDash();Check(TEXT("dash_started"),P->bIsRolling);Next(16,.85);break;
        case 16:
            Check(TEXT("dash_damage_once"),FMath::IsNearlyEqual(E->Health,PriorEnemy-35),FString::SanitizeFloat(PriorEnemy-E->Health));Check(TEXT("dash_pushes_enemy"),E->GetActorLocation().X>PriorLocation.X+150);Shot(TEXT("_Dash"));Next(17,.3);break;
        case 17:
            Place(P,0);Place(E,900);P->Inventory->TryAddItem(TEXT("Alcohol"),5);Check(TEXT("throw_still_works"),P->ThrowAlcoholAt(FVector(700,700,6000)));Check(TEXT("throw_anim_state"),P->GetCombatAnimationState()==EDWCombatAnimationState::Throw);Next(18,.15);break;
        case 18:
            Check(TEXT("throw_keeps_anim_blueprint"),P->GetMesh()->GetAnimationMode()==EAnimationMode::AnimationBlueprint);Next(19,2);break;
        case 19:
            {auto* HUD=PC->GetHUD<ADWGameplayHUD>();if(HUD)HUD->ShowMenu(EDWMenuPage::Inventory);Check(TEXT("inventory_blocks_direct_melee"),!P->PerformMeleeAttack(E->GetActorLocation()));if(HUD)HUD->ClosePanels();}
            Next(20,.2);break;
        case 20:
            Place(E,140);PriorEnemy=E->Health;Check(TEXT("melee_before_cancel"),P->PerformMeleeAttack(E->GetActorLocation()));P->CancelCombatActions();Next(21,.95);break;
        case 21:
            Check(TEXT("cancel_prevents_delayed_damage"),E->Health==PriorEnemy);P->MeleeAnimation=nullptr;Check(TEXT("empty_melee_slot_starts"),P->PerformMeleeAttack(E->GetActorLocation()));Next(22,.95);break;
        case 22:
            Check(TEXT("empty_slot_finishes_and_hits"),P->AttackPhase==EDWCombatPhase::None&&FMath::IsNearlyEqual(E->Health,PriorEnemy-25));
            Check(TEXT("isolated_save_after_combat"),GI->SaveCurrentGame());P->Transformation=P->GetGameplayConfig()->MaxTransformation;P->TryTransform();Check(TEXT("transformation_still_works"),P->IsYeastForm());Next(30,.2);break;
        case 30:
            Check(TEXT("transformation_keeps_anim_blueprint"),P->GetMesh()->GetAnimationMode()==EAnimationMode::AnimationBlueprint);Next(31,2);break;
        case 31:
            Place(P,0);Place(E,1400,600);Place(E2,700);E2->DetectionRadius=1000;E2->LoseTargetRadius=1500;E2->AlertDuration=.7f;E2->MoveSpeed=250;Next(32,.3);break;
        case 32:
            Check(TEXT("enemy_detection_enters_alert"),E2->GetCombatAnimationState()==EDWCombatAnimationState::Alert);Next(33,.65);break;
        case 33:
            Check(TEXT("player_movement_still_works"),P->GetActorLocation().Y>30);Next(34,.3);break;
        case 34:
            Check(TEXT("enemy_chase_still_moves"),E2->GetActorLocation().X<690);E2->DetectionRadius=1;E2->LoseTargetRadius=1;E2->MoveSpeed=0;Place(E2,1400,-600);
            {FDamageEvent Damage;P->TakeDamage(10000,Damage,nullptr,E);}Next(23,.2);break;
        case 23:
            Check(TEXT("player_death_animation_state"),P->GetCombatAnimationState()==EDWCombatAnimationState::Death);Check(TEXT("dead_player_cannot_attack"),!P->PerformMeleeAttack(E->GetActorLocation()));Shot(TEXT("_Death"));Next(24,.4);break;
        default:return Finish();
        }
        return true;
    }
};
static TSharedPtr<FRun> Active;
static FDelayedAutoRegisterHelper Startup(EDelayedRegisterRunPhase::EndOfEngineInit,[]
{
    if(GIsEditor||!FParse::Param(FCommandLine::Get(),TEXT("DWCombatQA")))return;
    FString Report;if(!FParse::Value(FCommandLine::Get(),TEXT("DWCombatQAReport="),Report)||FPaths::IsRelative(Report)||!Outside(Report)||!FPaths::GetExtension(Report).Equals(TEXT("json"),ESearchCase::IgnoreCase)||IFileManager::Get().FileExists(*Report))return;
    Active=MakeShared<FRun>();Active->Report=Report;
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float Delta){return Active&&Active->Tick(Delta);}));
});
}
#endif


