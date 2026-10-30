#include "DWLanguageCombatArena.h"
#include "DWEnemyCharacter.h"
#include "DWEnemyCombatProfile.h"
#include "DWPlayerCharacter.h"
#include "DWCombatWorldPersistence.h"
#include "DWCombatWorldObjectsSave.h"
#include "DWCombatAuthoritySubsystem.h"
#include "DWPlayerController.h"
#include "DWGatheringComponent.h"
#include "DWSaveGame.h"
#include "DWInteractionPromptComponent.h"
#include "DWLocalizationLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"

ADWLanguageCombatArena::ADWLanguageCombatArena(){StableId=TEXT("LANG01_ArenaSession");InteractionRadius=300.f;EnemyClass=ADWEnemyCharacter::StaticClass();PrimaryActorTick.bTickEvenWhenPaused=true;}
void ADWLanguageCombatArena::BeginPlay()
{
 Super::BeginPlay();OutsideConsole=GetActorTransform();SetEntranceClosed(false);
 if(Prompt){Prompt->GenericPromptTitle=FText::FromString(TEXT("独立战斗训练"));Prompt->GenericEnglishPromptTitle=FText::FromString(TEXT("Isolated combat training"));Prompt->GenericPromptAction=FText::FromString(TEXT("开始训练 · 退出将恢复入场前状态"));Prompt->GenericEnglishAction=FText::FromString(TEXT("Train · exit restores pre-round state"));}
}
bool ADWLanguageCombatArena::InteractTraining(ADWPlayerCharacter* P,FText& Message)
{
 if(bChanging||!IsInReach(P)){Message=DWText(this,TEXT("请靠近训练控制台"),TEXT("Approach the training console."));return false;}
 TGuardValue<bool> Guard(bChanging,true);FString Error;
 if(bActive)
 {
  return RestoreRound(P,Message);
 }
 auto* Save=NewObject<UDWSaveGame>();TArray<uint8> Snapshot;
 if(!FDWCombatWorldPersistence::Capture(*P,*Save,Error)||!UGameplayStatics::SaveGameToMemory(Save,Snapshot)||Snapshot.IsEmpty())
 {Message=FText::FromString(TEXT("暂不能开始训练，先结束进行中的动作/掉落：")+Error);return false;}
 static const EDWEnemyCombatProfile Profiles[]={EDWEnemyCombatProfile::DoughZombie,EDWEnemyCombatProfile::MoldGrunt,EDWEnemyCombatProfile::YeastGrunt,EDWEnemyCombatProfile::DoughElite};
 const FTransform SpawnTransform(FRotator::ZeroRotator,EnemySpawn);
 auto* Enemy=GetWorld()->SpawnActorDeferred<ADWEnemyCharacter>(EnemyClass?EnemyClass.Get():ADWEnemyCharacter::StaticClass(),SpawnTransform,this,nullptr,ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
 if(!Enemy){Message=DWText(this,TEXT("训练敌人无法生成，未开始"),TEXT("Could not create the training opponent."));return false;}
 const FGuid NewRoundId=FGuid::NewGuid();
 Enemy->CombatProfile=Profiles[NextProfile%UE_ARRAY_COUNT(Profiles)];Enemy->PersistentId=FName(*(TEXT("LANG01_ArenaEnemy_")+NewRoundId.ToString(EGuidFormats::Digits)));
 // This disposable round instance opts into the formal no-authored-attack contract.
 // Keep its CDO/old assets and all non-attack visuals unchanged; the formal guard still runs.
 Enemy->AttackAnimation=nullptr;Enemy->AttackVariants.Reset();
 UGameplayStatics::FinishSpawningActor(Enemy,SpawnTransform);
 if(!IsValid(Enemy)||!Enemy->IsFormalCombatReady()){if(IsValid(Enemy))Enemy->Destroy();Message=DWText(this,TEXT("敌人正式配置未就绪，未开始"),TEXT("The opponent's combat profile is not ready."));return false;}
 BeforeRound=MoveTemp(Snapshot);EntryWorldObjects=Save->CombatWorld.Objects;
 RoundEnemy=Enemy;RoundPlayer=P;ActiveRoundId=NewRoundId;bActive=true;RoundEpoch=UDWCombatAuthoritySubsystem::Get(P)->GetWorldEpoch();
 SetEntranceClosed(true);
 P->SetActorLocation(PlayerInside,false,nullptr,ETeleportType::TeleportPhysics);SetActorLocation(InsideConsole,false,nullptr,ETeleportType::TeleportPhysics);
 if(Prompt){Prompt->GenericPromptAction=FText::FromString(TEXT("退出并恢复入场前状态（弹药/资源/敌人全部重置）"));Prompt->GenericEnglishAction=FText::FromString(TEXT("Exit and restore entry state, including ammo/resources/opponent"));}
 Message=DWText(this,TEXT("训练开始；此控制台可安全退出并重置本场"),TEXT("Training started. Use this console to exit safely and reset this round."));return true;
}
bool ADWLanguageCombatArena::RestoreRound(ADWPlayerCharacter* P,FText& Message)
{
 if(!IsValid(P)||RoundPlayer.Get()!=P||!bActive||!ActiveRoundId.IsValid()||BeforeRound.IsEmpty())return false;
 TGuardValue<bool> RestoreScope(bRestoringRound,true);
 auto* Save=Cast<UDWSaveGame>(UGameplayStatics::LoadGameFromMemory(BeforeRound));bool Committed=false;FString Error;
 if(!Save||!FDWCombatWorldPersistence::Restore(*P,*Save,Committed,Error))
 {Message=FText::FromString(TEXT("训练恢复尚未完成：")+Error);return false;}
 bActive=false;BeforeRound.Reset();EntryWorldObjects.Reset();RoundEnemy.Reset();RoundPlayer.Reset();ActiveRoundId.Invalidate();SetEntranceClosed(false);SetActorTransform(OutsideConsole,false,nullptr,ETeleportType::TeleportPhysics);++NextProfile;
 if(Prompt){Prompt->GenericPromptAction=FText::FromString(TEXT("开始下一类训练 · 退出恢复入场状态"));Prompt->GenericEnglishAction=FText::FromString(TEXT("Train next type · exit restores entry state"));}
 if(auto* PC=Cast<ADWPlayerController>(P->GetController()))PC->SetPause(false);
 Message=DWText(this,TEXT("已退出并恢复本场开始前的完整试玩状态"),TEXT("Exited and restored the complete pre-round trial state."));return true;
}
void ADWLanguageCombatArena::Tick(float Dt)
{
 Super::Tick(Dt);if(!bActive||bChanging||!GetWorld())return;
 auto* PC=GetWorld()->GetFirstPlayerController();auto* P=PC?Cast<ADWPlayerCharacter>(PC->GetPawn()):nullptr;
 auto* Authority=UDWCombatAuthoritySubsystem::Get(P);if(!P||!Authority)return;
 if(Authority->GetWorldEpoch()!=RoundEpoch)
 {bActive=false;BeforeRound.Reset();EntryWorldObjects.Reset();RoundEnemy.Reset();RoundPlayer.Reset();ActiveRoundId.Invalidate();SetEntranceClosed(false);SetActorTransform(OutsideConsole,false,nullptr,ETeleportType::TeleportPhysics);return;}
 if(P->IsDead()&&GetWorld()->GetRealTimeSeconds()>=NextRecoveryAttempt)
 {
  TGuardValue<bool> Guard(bChanging,true);FText Message;
  if(RestoreRound(P,Message))UDWGatheringComponent::Notify(P,TEXT("训练结束：已自动恢复入场前状态（仅此隔离训练规则）。"),TEXT("Training ended: restored the entry snapshot automatically (isolated training only)."));
  else{NextRecoveryAttempt=GetWorld()->GetRealTimeSeconds()+1.;UE_LOG(LogTemp,Error,TEXT("LANG01 training recovery failed, entry snapshot retained for retry: %s"),*Message.ToString());}
 }
}
bool ADWLanguageCombatArena::CanRetireRoundEnemy(const ADWEnemyCharacter& Enemy,const ADWPlayerCharacter& Player,const FDWCombatWorldObjectsSave& Target)const
{
 const auto* W=GetWorld();const auto* Authority=UDWCombatAuthoritySubsystem::Get(this);
 if(!IsInGameThread()||!IsValid(this)||IsActorBeingDestroyed()||!W||W->bIsTearingDown||
  UWorld::RemovePIEPrefix(W->GetOutermost()->GetName())!=TEXT("/Game/DoughWorld/Maps/Gameplay/Levels/L_DoughWorld_LanguagePlaytest")||
  !bActive||!bChanging||!bRestoringRound||!ActiveRoundId.IsValid()||RoundEnemy.Get()!=&Enemy||RoundPlayer.Get()!=&Player||
  !IsValid(&Enemy)||Enemy.IsActorBeingDestroyed()||Enemy.GetWorld()!=W||Enemy.SourceNest||
  Enemy.PersistentId!=FName(*(TEXT("LANG01_ArenaEnemy_")+ActiveRoundId.ToString(EGuidFormats::Digits)))||
  !IsValid(&Player)||Player.IsActorBeingDestroyed()||Player.GetWorld()!=W||
  !Authority||Authority->GetWorldEpoch()!=RoundEpoch||EntryWorldObjects.IsEmpty())return false;
 // Use the production codec for the entire target object payload; no broad enemy-ID exception.
 TArray<uint8> TargetBytes;FString Error;
 return DWCombatWorldObjectsSave::Encode(Target,TargetBytes,Error)&&TargetBytes==EntryWorldObjects;
}
void ADWLanguageCombatArena::SetEntranceClosed(bool Closed)
{if(IsValid(EntranceGateActor)){EntranceGateActor->SetActorHiddenInGame(!Closed);EntranceGateActor->SetActorEnableCollision(Closed);}}
