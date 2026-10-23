#include "DWCombatGroundAreaSubsystem.h"
#include "DWCombatAuthoritySubsystem.h"
#include "DWPlayerCharacter.h"
#include "DWEnemyCharacter.h"
#include "DWDamageVolumeLibrary.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "UObject/ConstructorHelpers.h"

ADWCombatGroundAreaMarker::ADWCombatGroundAreaMarker()
{
    PrimaryActorTick.bCanEverTick=false;Disc=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("AreaPlaceholder"));SetRootComponent(Disc);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Mesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    if(Mesh.Succeeded())Disc->SetStaticMesh(Mesh.Object);
    Disc->SetCollisionEnabled(ECollisionEnabled::NoCollision);Disc->SetCanEverAffectNavigation(false);
    Disc->SetGenerateOverlapEvents(false);SetActorEnableCollision(false);
}
void ADWCombatGroundAreaMarker::ConfigureRadius(double RadiusCm)
{Disc->SetWorldScale3D(FVector(RadiusCm/50.,RadiusCm/50.,.02));}
UDWCombatGroundAreaSubsystem* UDWCombatGroundAreaSubsystem::Get(const UObject* Context)
{auto* W=Context?Context->GetWorld():nullptr;return W&&!W->bIsTearingDown?W->GetSubsystem<UDWCombatGroundAreaSubsystem>():nullptr;}
bool UDWCombatGroundAreaSubsystem::DoesSupportWorldType(EWorldType::Type Type)const
{return Type==EWorldType::Game||Type==EWorldType::PIE;}
TStatId UDWCombatGroundAreaSubsystem::GetStatId()const
{RETURN_QUICK_DECLARE_CYCLE_STAT(UDWCombatGroundAreaSubsystem,STATGROUP_Tickables);}
bool UDWCombatGroundAreaSubsystem::Operational()const
{auto* A=UDWCombatAuthoritySubsystem::Get(this);return !bStopping&&IsInGameThread()&&GetWorld()&&!GetWorld()->bIsTearingDown&&A&&(!Epoch||Epoch==A->GetWorldEpoch());}
bool UDWCombatGroundAreaSubsystem::FindLegalGround(UWorld* World,FVector Impact,AActor* Ignore,FVector& Out)
{
    if(!World||World->bIsTearingDown||Impact.ContainsNaN())return false;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(DWReleasedAreaGround),false,Ignore);
    // Projection limits match the existing alcohol ground search; actors never count as a floor.
    FHitResult Ground;if(!World->LineTraceSingleByObjectType(Ground,Impact+FVector(0,0,100),Impact-FVector(0,0,3000),
        FCollisionObjectQueryParams(ECC_WorldStatic),Query)||Ground.ImpactPoint.ContainsNaN())return false;
    const auto* P=Cast<ADWPlayerCharacter>(Ignore);const auto* Movement=P?P->GetCharacterMovement():GetDefault<UCharacterMovementComponent>();
    if(!Movement||!Movement->IsWalkable(Ground))return false;
    Out=Ground.ImpactPoint+FVector(0,0,2.);return true;
}
EDWAreaAdoption UDWCombatGroundAreaSubsystem::AdoptReleasedAlcohol(const FDWAlcoholAreaSnapshot& S,TWeakObjectPtr<ADWPlayerCharacter> Source,
    TWeakObjectPtr<AController> Instigator,DWCombatCommit::FEntityLife Life,DWCombatCommit::FEmitterId Emitter,FVector Ground,double At)
{
    if(!S.IsValid())return EDWAreaAdoption::Invalid;
    FArea A;A.Source=Source;A.Instigator=Instigator;A.SourceLife=Life;A.Emitter=Emitter;A.Ground=Ground;
    A.Started=At;A.Ends=At+S.DurationSeconds;A.RadiusCm=S.RadiusCm;A.SlowFraction=S.SlowFraction;A.Damage=S.Damage;A.Period=S.TickIntervalSeconds;
    return Adopt(A);
}
EDWAreaAdoption UDWCombatGroundAreaSubsystem::AdoptReleasedBowSlow(const FDWBowSlowAreaSnapshot& S,TWeakObjectPtr<ADWPlayerCharacter> Source,
    TWeakObjectPtr<AController> Instigator,DWCombatCommit::FEntityLife Life,DWCombatCommit::FEmitterId Emitter,FVector Ground,double At)
{
    if(!S.IsValid())return EDWAreaAdoption::Invalid;
    FArea A;A.Source=Source;A.Instigator=Instigator;A.SourceLife=Life;A.Emitter=Emitter;A.Ground=Ground;
    A.Started=At;A.Ends=At+S.DurationSeconds;A.RadiusCm=S.RadiusCm;A.SlowFraction=S.SlowFraction;return Adopt(A);
}
EDWAreaAdoption UDWCombatGroundAreaSubsystem::Adopt(const FArea& A)
{
    auto* Authority=UDWCombatAuthoritySubsystem::Get(this);
    if(!Operational()||!Authority||!A.Emitter.Id||A.Emitter.Epoch!=Authority->GetWorldEpoch()||!A.SourceLife.Entity||!A.SourceLife.Life||
        A.Ground.ContainsNaN()||!FMath::IsFinite(A.Started)||A.Started<0.||A.Started>GetWorld()->GetTimeSeconds()||
        !FMath::IsFinite(A.Ends)||A.Ends<=A.Started)return EDWAreaAdoption::Invalid;
    if(Epoch&&Epoch!=A.Emitter.Epoch)return EDWAreaAdoption::Invalid;
    if(Areas.Contains(A.Emitter.Id))return EDWAreaAdoption::Existing; // Tombstone: never renew or spawn a second area.
    auto* Ledger=Authority->GetLedgerForResourceTransaction();
    if(!Ledger||!Ledger->IsEmitterActive(A.Emitter))return EDWAreaAdoption::Invalid;
    Epoch=A.Emitter.Epoch;Areas.Add(A.Emitter.Id,A);DiscoverTargets(A.Started);
    for(TActorIterator<ADWEnemyCharacter> It(GetWorld());It;++It)if(It->IsAlive())It->RefreshCombatGroundAreaSlow();
    // Native facts only. The carrier must mark ownership transferred before any scene callback can destroy it.
    return EDWAreaAdoption::Created;
}
bool UDWCombatGroundAreaSubsystem::Covers(const FArea& A,const ADWEnemyCharacter& E,double At,bool IncludeEnd)const
{
    if(!A.bActive||At<A.Started||(IncludeEnd?At>A.Ends:At>=A.Ends)||!E.IsAlive()||E.IsActorBeingDestroyed()||
        !UDWDamageVolumeLibrary::IsTargetInDamageSphere(const_cast<ADWEnemyCharacter*>(&E),A.Ground,static_cast<float>(A.RadiusCm)))return false;
    FCollisionQueryParams Q(SCENE_QUERY_STAT(DWGroundAreaSight),false,A.Source.Get());
    // Environment occludes effects; other characters are not walls around an explicitly radial effect.
    for(TActorIterator<APawn> It(GetWorld());It;++It)Q.AddIgnoredActor(*It);
    TInlineComponentArray<UPrimitiveComponent*> Components(&E);
    for(auto* C:Components)if(UDWDamageVolumeLibrary::IsDamageHitComponent(C)&&
        C->OverlapComponent(A.Ground,FQuat::Identity,FCollisionShape::MakeSphere(static_cast<float>(A.RadiusCm))))
    {
        FVector Point;if(C->GetClosestPointOnCollision(A.Ground,Point)<0.f)continue;
        FHitResult Block;if(!GetWorld()->LineTraceSingleByChannel(Block,A.Ground,Point,ECC_Visibility,Q))return true;
    }
    return false;
}
double UDWCombatGroundAreaSubsystem::GetStrongestSlowAt(const ADWEnemyCharacter& E,double At)const
{
    if(!Operational()||!FMath::IsFinite(At))return 0.;double Slow=0.;
    for(const auto& Pair:Areas)if(Covers(Pair.Value,E,At,false))Slow=FMath::Max(Slow,Pair.Value.SlowFraction);return Slow;
}
bool UDWCombatGroundAreaSubsystem::FindStrongestAlcohol(const ADWEnemyCharacter& E,double At,FArea& Out)const
{
    bool Found=false;
    for(const auto& Pair:Areas){const auto& A=Pair.Value;if(A.Damage<=0.||!Covers(A,E,At,true))continue;
        if(!Found||A.Damage/A.Period>Out.Damage/Out.Period||(A.Damage/A.Period==Out.Damage/Out.Period&&A.Emitter.Id<Out.Emitter.Id)){Out=A;Found=true;}}
    return Found;
}
void UDWCombatGroundAreaSubsystem::DiscoverTargets(double At)
{
    auto* Authority=UDWCombatAuthoritySubsystem::Get(this);if(!Operational()||!Authority)return;
    for(TActorIterator<ADWEnemyCharacter> It(GetWorld());It;++It)
    {
        auto* E=*It;if(!E->IsAlive()||E->IsActorBeingDestroyed())continue;const auto Life=Authority->CaptureActorLife(E);if(!Life.Entity)continue;
        if(const auto* Current=Targets.Find(Life.Entity);Current&&Current->Life.Life==Life.Life)continue;
        FArea Strongest;if(!FindStrongestAlcohol(*E,At,Strongest))continue;
        FTarget T;T.Actor=E;T.Life=Life;if(T.Clock.Start(At,Strongest.Period))Targets.Add(Life.Entity,T);
    }
}
void UDWCombatGroundAreaSubsystem::Materialize(uint64 Id)
{
    auto* Record=Areas.Find(Id);if(!Operational()||GetWorld()->IsPaused()||!Record||!Record->bActive||Record->Marker.IsValid())return;
    const uint64 MaterializeEpoch=Epoch;const auto Copy=*Record;FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* Marker=GetWorld()->SpawnActor<ADWCombatGroundAreaMarker>(ADWCombatGroundAreaMarker::StaticClass(),Copy.Ground,FRotator::ZeroRotator,Params);
    Record=Areas.Find(Id);if(!Operational()||Epoch!=MaterializeEpoch||!Record||!Record->bActive||!IsValid(Marker)||Marker->IsActorBeingDestroyed())
    {if(IsValid(Marker)&&Marker->GetWorld()&&!Marker->GetWorld()->bIsTearingDown)Marker->Destroy();return;}
    Record->Marker=Marker;Marker->ConfigureRadius(Copy.RadiusCm);
}
void UDWCombatGroundAreaSubsystem::RetireExpired(double Through)
{
    auto* A=UDWCombatAuthoritySubsystem::Get(this);if(!A)return;const uint64 CleanupEpoch=Epoch;TArray<TWeakObjectPtr<ADWCombatGroundAreaMarker>> Destroy;
    for(auto& Pair:Areas)if(Pair.Value.bActive&&Pair.Value.Ends<=Through)
    {Pair.Value.bActive=false;A->GetLedgerForResourceTransaction()->RetireEmitter(Pair.Value.Emitter);}
    // An EndPlay listener can pause midway. Keep retired marker references in the records so the next tick resumes cleanup.
    for(const auto& Pair:Areas)if(!Pair.Value.bActive&&Pair.Value.Marker.IsValid())Destroy.Add(Pair.Value.Marker);
    for(auto Weak:Destroy){if(!Operational()||Epoch!=CleanupEpoch||GetWorld()->IsPaused())return;if(auto* M=Weak.Get();M&&!M->IsActorBeingDestroyed())M->Destroy();}
}
void UDWCombatGroundAreaSubsystem::Tick(float Dt)
{
    if(!Operational()||bAdvancing||GetWorld()->IsPaused()||!FMath::IsFinite(Dt)||Dt<0.)return;
    TGuardValue<bool> Guard(bAdvancing,true);auto* Authority=UDWCombatAuthoritySubsystem::Get(this);if(!Authority)return;
    const uint64 TickEpoch=Authority->GetWorldEpoch();const double Now=GetWorld()->GetTimeSeconds();DiscoverTargets(Now);int32 Budget=2048;
    while(Budget-->0&&Operational()&&Authority->GetWorldEpoch()==TickEpoch&&!GetWorld()->IsPaused())
    {
        uint64 Earliest=0;double Due=MAX_dbl;
        for(auto& Pair:Targets)
        {
            auto* E=Pair.Value.Actor.Get();if(!E||!E->IsAlive()||E->IsActorBeingDestroyed())continue;
            const auto Life=Authority->CaptureActorLife(E);if(Life.Life!=Pair.Value.Life.Life)continue;
            const double T=Pair.Value.Clock.NextSeconds();if(T<=Now&&(T<Due||(T==Due&&Pair.Key<Earliest))){Earliest=Pair.Key;Due=T;}
        }
        if(!Earliest)break;
        TArray<FDWGroundAreaTick> Batch;
        for(auto& Pair:Targets)
        {
            auto& T=Pair.Value;auto* E=T.Actor.Get();if(!E||!E->IsAlive()||E->IsActorBeingDestroyed()||T.Clock.NextSeconds()!=Due||
                Authority->CaptureActorLife(E).Life!=T.Life.Life)continue;
            uint64 Ordinal=0;double Consumed=0.;if(!T.Clock.ConsumeNext(Now,Consumed,Ordinal))continue;
            FArea Strongest;if(FindStrongestAlcohol(*E,Due,Strongest))
                Batch.Add({Strongest.Source,Strongest.Instigator,T.Actor,Strongest.SourceLife,T.Life,Strongest.Emitter,Ordinal,static_cast<float>(Strongest.Damage)});
        }
        if(!Batch.IsEmpty())Authority->SubmitGroundAreaTickBatch(Batch);
        // All clocks and same-instant victim facts precede listeners; reentry cannot repeat a tick.
    }
    if(!Operational()||Authority->GetWorldEpoch()!=TickEpoch||GetWorld()->IsPaused())return;
    if(Budget<0){UE_LOG(LogTemp,Warning,TEXT("Ground-area catch-up budget reached; remaining ticks are retained."));return;}
    RetireExpired(Now);if(!Operational()||Authority->GetWorldEpoch()!=TickEpoch||GetWorld()->IsPaused())return;
    TArray<uint64> ToMaterialize;for(const auto& Pair:Areas)if(Pair.Value.bActive&&!Pair.Value.Marker.IsValid())ToMaterialize.Add(Pair.Key);
    for(const uint64 Id:ToMaterialize){if(!Operational()||Authority->GetWorldEpoch()!=TickEpoch||GetWorld()->IsPaused())return;Materialize(Id);}
    if(!Operational()||Authority->GetWorldEpoch()!=TickEpoch||GetWorld()->IsPaused())return;
    // Immediate departure/strong-to-weak updates use only the current overlap; no duration tails are added.
    for(TActorIterator<ADWEnemyCharacter> It(GetWorld());It;++It)if(It->IsAlive())It->RefreshCombatGroundAreaSlow();
    for(auto It=Targets.CreateIterator();It;++It)if(!It.Value().Actor.IsValid()||!It.Value().Actor->IsAlive()||It.Value().Actor->IsActorBeingDestroyed()||
        Authority->CaptureActorLife(It.Value().Actor.Get()).Life!=It.Value().Life.Life)It.RemoveCurrent();
}
int32 UDWCombatGroundAreaSubsystem::GetLiveAreaCount()const
{int32 N=0;for(const auto& Pair:Areas)if(Pair.Value.bActive)++N;return N;}
void UDWCombatGroundAreaSubsystem::Deinitialize()
{bStopping=true;Areas.Reset();Targets.Reset();Super::Deinitialize();}
