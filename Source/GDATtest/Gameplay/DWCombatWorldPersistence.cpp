#include "DWCombatWorldPersistence.h"
#include "DWCombatWorldSave.h"
#include "DWCombatPlayerPersistence.h"
#include "DWCombatWorldObjectsPersistence.h"
#include "DWCombatMinePersistence.h"
#include "DWCombatAuthoritySubsystem.h"
#include "DWCombatPlayerSave.h"
#include "DWCombatRules.h"
#include "DWPlayerCharacter.h"
#include "DWGameInstance.h"
#include "DWSaveGame.h"
#include "DWWorldEvent.h"
#include "Engine/World.h"
#include "UObject/StrongObjectPtr.h"
#include <algorithm>

namespace
{
bool Schema(const FDWCombatRules& Rules,int32& Out,FString& Error)
{return Rules.TryGetCount(TEXT("save.combat_schema_version"),Out,Error)&&Out==1;}
bool Same(DWCombatCommit::FEntityLife A,DWCombatCommit::FEntityLife B)
{return A.Entity==B.Entity&&A.Life==B.Life;}
bool DecodeAndCrossCheck(const FDWCombatRules& Rules,const UDWSaveGame& S,
    FDWCombatWorldObjectsSave& Objects,FDWCombatEffectsSave& Effects,FString& Error)
{
    if(!DWCombatWorldObjectsSave::Decode(S.CombatWorld.Objects,Objects,Error)||
        !DWCombatWorldObjectsSave::ValidateLegacy(S.WorldActors,Error)||
        !DWCombatEffectsSave::Decode(S.CombatWorld.Effects,Effects,Error)||
        !FDWCombatWorldPersistence::ValidateShieldPackets(S.CombatWorld.Player.ShieldPackets,Effects,Error)||
        !FDWCombatPlayerPersistence::Validate(Rules,S,Effects,Error)||
        !FDWCombatMinePersistence::ValidateSnapshot(S.CombatWorld.Player,Effects.Authority,Error)||
        !FDWCombatMinePersistence::ValidateTargetHistory(S.CombatWorld.Player,Objects,Error))return false;
    FDWCombatPlayerVitalsConfig V;if(!FDWCombatPlayerVitalsConfig::Build(Rules,V,Error))return false;
    const bool Dead=S.Health<=DWCombatPlayerSave::HealthFloor(V);const auto& P=S.CombatWorld.Player;
    const auto* Player=Effects.Authority.Actors.FindByPredicate([&](const auto& A){return A.StableId==TEXT("player:0");});
    if(Objects.MapPackage!=S.MapPackage||!Player||Player->Kind!=EDWCombatSavedActorKind::Player||Player->bEnded||
        Player->bDead!=Dead||Player->Life.Entity!=P.PlayerEntity||Player->Life.Life!=P.PlayerLife||
        Objects.Recovery.bAwaitingRespawn!=Dead||(Dead&&P.bRooted))
    {Error=TEXT("Complete combat player/world lifecycle records disagree.");return false;}
    const bool Equipped=S.CombatPlayerResources.CurrentEquipmentId==TEXT("DWWeapon_DualDaggers");
    for(const auto& Enemy:Objects.Enemies)
    {
        // Dead or old-life observations remain historical facts. Do not reset grace as a load side effect.
        if(!Dead&&Enemy.Health>0.f&&Enemy.Dagger.bInitialized&&Same(Enemy.DaggerPlayerLife,Player->Life)&&Enemy.Dagger.bEquipped!=Equipped)
        {Error=TEXT("Current-life dagger observation disagrees with saved equipment.");return false;}
        if(Enemy.ObservedPlayerLife.Entity==Player->Life.Entity&&Enemy.ObservedPlayerLife.Life==Player->Life.Life&&
            Enemy.ObservedPlayerDamageRevision>P.CombatDamageRevision)
        {Error=TEXT("Enemy observation is newer than saved player damage revision.");return false;}
    }
    TSet<FName> EventIds;for(FName Id:S.CompletedWorldEvents)
    {if(Id.IsNone()||EventIds.Contains(Id)){Error=TEXT("Invalid or duplicate completed world event.");return false;}EventIds.Add(Id);}
    return true;
}
void CopyCaptured(const UDWSaveGame& From,UDWSaveGame& To)
{
    To.Version=From.Version;To.MapPackage=From.MapPackage;To.PlayerTransform=From.PlayerTransform;To.bHasPlayerTransform=From.bHasPlayerTransform;
    To.Health=From.Health;To.Transformation=From.Transformation;To.bYeastForm=From.bYeastForm;
    To.SprintAlcoholElapsed=From.SprintAlcoholElapsed;To.TransformationDecayElapsed=From.TransformationDecayElapsed;
    To.Inventory=From.Inventory;To.CombatPlayerResources=From.CombatPlayerResources;To.CombatWorld=From.CombatWorld;
    To.WorldActors=From.WorldActors;To.CompletedWorldEvents=From.CompletedWorldEvents;
}
}

bool FDWCombatWorldPersistence::ValidateShieldPackets(const TArray<FDWShieldPacketSave>& Saved,const FDWCombatEffectsSave& Effects,FString& Error)
{
    using namespace DWCombatCommit;const auto& L=Effects.Authority.Ledger;TSet<uint64> Seen;
    if(Saved.Num()>65536){Error=TEXT("Pending shield payload count is invalid.");return false;}
    for(const auto& S:Saved)
    {
        const auto R=std::find_if(L.Receipts.begin(),L.Receipts.end(),[&](const auto& V){return V.Receipt.Handle.Id==S.Receipt;});
        const auto K=std::find_if(L.Keys.begin(),L.Keys.end(),[&](const auto& V){return V.Receipt==S.Receipt;});
        const auto* Target=Effects.Authority.Actors.FindByPredicate([&](const auto& V){return V.Life.Entity==S.TargetEntity;});
        const auto* Source=Effects.Authority.Actors.FindByPredicate([&](const auto& V){return V.Life.Entity==S.SourceEntity;});
        if(!S.Receipt||Seen.Contains(S.Receipt)||R==L.Receipts.end()||K==L.Keys.end()||R->Resource||R->Receipt.State!=EReceiptState::Pending||
            R->Target.Entity!=S.TargetEntity||R->Target.Life!=S.TargetLife||R->Source.Entity!=S.SourceEntity||R->Source.Life!=S.SourceLife||
            R->Action.Id!=S.Action||K->Key[0]!=2||K->Key[1]!=L.Epoch||K->Key[2]!=S.Action||K->Key[3]!=S.Phase||
            K->Key[4]!=S.ActionKind||K->Key[5]!=S.PacketOrdinal||K->Key[6]!=S.TargetEntity||K->Key[7]!=S.TargetLife||
            !Target||Target->Kind!=EDWCombatSavedActorKind::Player||Target->bEnded||Target->bDead||Target->Life.Life!=S.TargetLife||
            !Source||S.SourceLife>Source->Life.Life||S.bSourceWasPlayer!=(Source->Kind==EDWCombatSavedActorKind::Player)||
            (S.IncomingKind!=static_cast<uint8>(EDWShieldIncomingKind::Melee)&&S.IncomingKind!=static_cast<uint8>(EDWShieldIncomingKind::PhysicalProjectile)))
        {Error=TEXT("Pending shield payload and its unique damage key disagree.");return false;}
        for(double V:{double(S.Amount),double(S.PreparedAmount),double(S.NotificationAmount),double(S.KnockbackDistance),double(S.KnockbackSeconds),S.ImpactAge,S.Remaining})
            if(!FMath::IsFinite(V)||V<0.){Error=TEXT("Invalid pending shield numeric payload.");return false;}
        if(S.Amount<=0.f||S.KnockbackSeconds<=0.f||S.KnockbackDirection.ContainsNaN()||S.IncomingDirection.ContainsNaN()||S.DamageType.IsNull())
        {Error=TEXT("Invalid pending shield vector or damage type.");return false;}
        if(!S.InstigatorId.IsEmpty()&&!Effects.Authority.Actors.ContainsByPredicate([&](const auto& V){return V.StableId==S.InstigatorId&&!V.bEnded&&V.Kind==EDWCombatSavedActorKind::Controller;}))
        {Error=TEXT("Pending shield controller has no live saved identity.");return false;}
        Seen.Add(S.Receipt);
    }
    for(const auto& R:L.Receipts)
        if(!R.Resource&&R.Receipt.State==EReceiptState::Pending&&!Seen.Contains(R.Receipt.Handle.Id))
        {Error=TEXT("Pending damage lacks a shield payload.");return false;}
    Error.Reset();return true;
}

bool FDWCombatWorldPersistence::Validate(const FDWCombatRules& Rules,const UDWSaveGame& S,FString& Error)
{
    int32 Version=0;if(!Schema(Rules,Version,Error)||S.CombatWorld.Schema!=Version)
    {Error=TEXT("Missing or incompatible complete combat snapshot; original file preserved.");return false;}
    if(!DWCombatPlayerSave::Validate(Rules,S,Error))return false;
    if(S.CombatWorld.bNewAdventure)
    {
        const FDWCombatPlayerExtendedSave EmptyExtended;
        // This marker represents only the never-started initial slot, not an escape hatch for partial saves.
        TStrongObjectPtr<UDWSaveGame> Initial(NewObject<UDWSaveGame>());
        if(!DWCombatPlayerSave::MakeNew(Rules,*Initial,Error))return false;
        if(S.bHasPlayerTransform||!S.Inventory.IsEmpty()||!S.WorldActors.IsEmpty()||!S.CompletedWorldEvents.IsEmpty()||
            !S.VisitedMaps.IsEmpty()||!FDWCombatPlayerExtendedSave::StaticStruct()->CompareScriptStruct(&S.CombatWorld.Player,&EmptyExtended,PPF_None)||!S.CombatWorld.Objects.IsEmpty()||!S.CombatWorld.Effects.IsEmpty()||
            !FDWCombatPlayerResourceSave::StaticStruct()->CompareScriptStruct(&S.CombatPlayerResources,&Initial->CombatPlayerResources,PPF_None))
        {Error=TEXT("New adventure marker cannot carry previously running state.");return false;}
        Error.Reset();return true;
    }
    FDWCombatWorldObjectsSave Objects;FDWCombatEffectsSave Effects;
    return DecodeAndCrossCheck(Rules,S,Objects,Effects,Error);
}
bool FDWCombatWorldPersistence::MarkNewAdventure(const FDWCombatRules& Rules,UDWSaveGame& S,FString& Error)
{
    int32 Version=0;if(!Schema(Rules,Version,Error))return false;
    S.CombatWorld={};S.CombatWorld.Schema=Version;S.CombatWorld.bNewAdventure=true;
    return Validate(Rules,S,Error);
}
bool FDWCombatWorldPersistence::Capture(ADWPlayerCharacter& P,UDWSaveGame& Out,FString& Error)
{
    auto* W=P.GetWorld();auto* GI=P.GetGameInstance<UDWGameInstance>();auto* A=UDWCombatAuthoritySubsystem::Get(&P);
    const auto Rules=GI?GI->GetCombatRules():nullptr;
    if(!W||W->bIsTearingDown||!Rules||!A||P.IsActorBeingDestroyed()||P.IsCombatRestoreInProgress())
    {Error=TEXT("Complete capture requires a stable live world.");return false;}
    const double At=W->GetTimeSeconds();const uint64 Epoch=A->GetWorldEpoch();
    TStrongObjectPtr<UDWSaveGame> S(NewObject<UDWSaveGame>());
    FDWCombatWorldObjectsSave Objects;FDWCombatEffectsSave Effects;TArray<FDWCombatObjectBinding> Bindings;
    if(!FDWCombatWorldObjectsPersistence::Capture(*W,P,Objects,S->WorldActors,Bindings,Error)||
        !FDWCombatPlayerPersistence::CaptureBase(P,*S,Error)||
        !FDWCombatPlayerPersistence::Capture(P,Bindings,S->CombatWorld.Player,Error)||
        !FDWCombatMinePersistence::Capture(P,S->CombatWorld.Player,Error)||
        !A->CaptureShieldPackets(P,Bindings,S->CombatWorld.Player.ShieldPackets,Error)||
        !FDWCombatEffectsPersistence::Capture(*A,Bindings,Effects,Error))return false;
    if(!Schema(*Rules,S->CombatWorld.Schema,Error))return false;
    S->MapPackage=UWorld::RemovePIEPrefix(W->GetOutermost()->GetName());S->CombatWorld.bNewAdventure=false;
    if(!DWCombatWorldObjectsSave::Encode(Objects,S->CombatWorld.Objects,Error)||
        !DWCombatEffectsSave::Encode(Effects,S->CombatWorld.Effects,Error)||!Validate(*Rules,*S,Error))return false;
    if(P.GetWorld()!=W||W->bIsTearingDown||W->GetTimeSeconds()!=At||A->GetWorldEpoch()!=Epoch||P.IsCombatRestoreInProgress())
    {Error=TEXT("World changed during capture; no partial snapshot was returned.");return false;}
    CopyCaptured(*S,Out);Error.Reset();return true;
}
bool FDWCombatWorldPersistence::Restore(ADWPlayerCharacter& P,const UDWSaveGame& Incoming,bool& OutCommitted,FString& Error)
{
    OutCommitted=false;auto* W=P.GetWorld();auto* GI=P.GetGameInstance<UDWGameInstance>();auto* A=UDWCombatAuthoritySubsystem::Get(&P);
    const auto Rules=GI?GI->GetCombatRules():nullptr;
    if(!W||W->bIsTearingDown||!Rules||!A||P.IsActorBeingDestroyed()||P.IsCombatRestoreInProgress())
    {Error=TEXT("World is not ready to prepare a complete restore.");return false;}
    TStrongObjectPtr<UDWSaveGame> S(DuplicateObject<UDWSaveGame>(&Incoming,GetTransientPackage()));
    if(!S||!Validate(*Rules,*S,Error)||S->CombatWorld.bNewAdventure||
        UWorld::RemovePIEPrefix(W->GetOutermost()->GetName())!=S->MapPackage)return false;
    FDWCombatWorldObjectsSave Objects;FDWCombatEffectsSave Effects;
    if(!DecodeAndCrossCheck(*Rules,*S,Objects,Effects,Error))return false;
    auto* Events=W->GetSubsystem<UDWWorldEventSubsystem>();if(!Events||Events->IsCinematicActive())return false;
    FDWWorldEventRestorePlan EventPlan;
    if(!Events->PrepareCompletedFacts(S->CompletedWorldEvents,EventPlan,Error))return false;
    FDWCombatWorldObjectsRestorePlan WorldPlan;FDWCombatEffectsRestorePlan EffectsPlan;
    FDWCombatPlayerRestorePlan PlayerPlan;FDWCombatShieldRestorePlan ShieldPlan;FDWCombatMineRestorePlan MinePlan;
    if(!FDWCombatWorldObjectsPersistence::PrepareWorldObjects(*W,P,Objects,S->WorldActors,WorldPlan,Error))return false;
    TArray<uint64> Keep=WorldPlan.KeepActionIds();if(S->CombatWorld.Player.bRooted)Keep.AddUnique(S->CombatWorld.Player.RootAction);
    if(!FDWCombatEffectsPersistence::PrepareRestore(*A,Effects,WorldPlan.LiveBindings(),WorldPlan.RestoredBindings(),Keep,EffectsPlan,Error)||
        !FDWCombatWorldObjectsPersistence::PrepareWorldFacts(WorldPlan,EffectsPlan.Authority(),Error)||
        !FDWCombatPlayerPersistence::Prepare(P,*S,Effects,EffectsPlan.Authority(),PlayerPlan,Error)||
        !A->PrepareShieldRestore(S->CombatWorld.Player.ShieldPackets,Effects,EffectsPlan.Authority(),ShieldPlan,Error)||
        !FDWCombatMinePersistence::Prepare(P,S->CombatWorld.Player,Effects.Authority,EffectsPlan.Authority(),PlayerPlan.RestoredRootIdentity(),MinePlan,Error)||
        !WorldPlan.CanCommit(Error)||!PlayerPlan.CanCommit(Error)||!MinePlan.CanCommit(Error)||!EffectsPlan.CanCommit(Error))return false;
    if(!EventPlan.CanCommit(Error))return false;
    const auto Epoch=EffectsPlan.Authority().NewEpoch();const auto Life=EffectsPlan.Authority().FindLife(S->CombatWorld.Player.PlayerEntity);
    if(!A->BeginWorldRestoreBarrier(EffectsPlan.Authority(),Error))return false;
    // From here onward every operation is fixed native adoption. No external callbacks, spawn, transform or fallible preparation.
    FDWCombatEffectsPersistence::CommitFactsUnchecked(EffectsPlan);
    FDWCombatPlayerPersistence::AdoptNativeFacts(PlayerPlan);
    FDWCombatWorldObjectsPersistence::AdoptNativeFacts(WorldPlan);
    FDWCombatMinePersistence::AdoptNativeFacts(MinePlan);
    A->AdoptShieldFactsUnchecked(ShieldPlan);
    Events->AdoptCompletedFacts(EventPlan);OutCommitted=true;
    A->FinishWorldRestoreBarrier();
    const auto Current=[&](){return IsValid(&P)&&!P.IsActorBeingDestroyed()&&P.GetWorld()==W&&!W->bIsTearingDown&&
        UDWCombatAuthoritySubsystem::Get(&P)==A&&A->GetWorldEpoch()==Epoch&&Same(A->FindRegisteredActorLife(&P),Life);};
    if(!Current()||!FDWCombatPlayerPersistence::Publish(PlayerPlan,Error)||
        !FDWCombatWorldObjectsPersistence::Publish(WorldPlan,Error)||!Current()||
        !FDWCombatMinePersistence::Publish(MinePlan,Error)||!Current())return false;
    FDWCombatEffectsPersistence::PublishRestoredEffects(EffectsPlan);if(!Current())return false;
    if(!Events->PublishCompletedFacts(EventPlan,A,Epoch,&P,Life,Error)||!Current())return false;
    return FDWCombatPlayerPersistence::FinishPublish(PlayerPlan,Error);
}
