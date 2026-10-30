#include "DWCombatWorldPersistence.h"
#include "DWGameplayConfig.h"
#include "DWRetiredSaveProjection.h"
#include "DWWorldMap.h"
#include "DWCombatOpeningDaggerProjection.h"
#include "DWGatheringWorldPersistence.h"
#include "DWLanguageSubsystem.h"
#include "DWFormalLanguagePersistence.h"
#include "DWCharacterA01SaveBridge.h"
#include "DWLanguageProgressPersistence.h"
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
#include "DWFormalSaveVersionPolicy.h"
#include "DWWorldEvent.h"
#include "Engine/World.h"
#include "UObject/StrongObjectPtr.h"
#include <algorithm>

namespace
{
bool Schema(const FDWCombatRules& Rules,int32& Out,FString& Error)
{return Rules.TryGetCount(TEXT("save.combat_schema_version"),Out,Error)&&Out==1;}
bool SamePersistedCombatWorldLife(DWCombatCommit::FEntityLife A,DWCombatCommit::FEntityLife B)
{return A.Entity==B.Entity&&A.Life==B.Life;}
bool DecodeAndCrossCheck(const FDWCombatRules& Rules,const UDWSaveGame& S,
    FDWCombatWorldObjectsSave& Objects,FDWCombatEffectsSave& Effects,FString& Error)
{
    if(!DWCombatWorldObjectsSave::Decode(S.CombatWorld.Objects,Objects,Error,S.FormalLanguage.Schema==2)||
        !DWCombatWorldObjectsSave::ValidateLegacy(S.WorldActors,Error)||
        !DWCombatEffectsSave::Decode(S.CombatWorld.Effects,Effects,Error)||
        !FDWGatheringWorldPersistence::ValidateAuthorityReferences(S.CombatWorld.Gathering,Effects.Authority,Error)||
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
        if(!Dead&&Enemy.Health>0.f&&Enemy.Dagger.bInitialized&&SamePersistedCombatWorldLife(Enemy.DaggerPlayerLife,Player->Life)&&Enemy.Dagger.bEquipped!=Equipped)
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
    To.FormalLanguage=From.FormalLanguage;To.AdventureFlow=From.AdventureFlow;To.WorldMapMarkers=From.WorldMapMarkers;
    To.LanguageTrialSchema=From.LanguageTrialSchema;To.LanguageProgressJson=From.LanguageProgressJson;To.A01StateJson=From.A01StateJson;
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
    int32 CombatRulesSchemaVersion=0;if(!Schema(Rules,CombatRulesSchemaVersion,Error)||S.CombatWorld.Schema!=CombatRulesSchemaVersion)
    {Error=TEXT("Missing or incompatible complete combat snapshot; original file preserved.");return false;}
    DWFormalSaveVersionPolicy::EFamily Family;if(!DWFormalSaveVersionPolicy::Classify(S,Family,Error))return false;
    if(Family==DWFormalSaveVersionPolicy::EFamily::Formal){FDWA01SavedProjection Projection;if(!DWCharacterA01SaveBridge::ValidateProjection(Rules,S,Projection,Error))return false;}
    if(!DWCombatPlayerSave::Validate(Rules,S,Error)||
        !FDWGatheringWorldPersistence::Validate(S.CombatWorld.Gathering,S.MapPackage,S.Inventory,S.CombatWorld.bNewAdventure,Error))return false;
    if(S.CombatWorld.bNewAdventure)
    {
        const FDWCombatPlayerExtendedSave EmptyExtended;
        // This marker represents only the never-started initial slot, not an escape hatch for partial saves.
        TStrongObjectPtr<UDWSaveGame> Initial(NewObject<UDWSaveGame>());
        if(!DWCombatPlayerSave::MakeNew(Rules,*Initial,Error))return false;
        bool bInitialResources=FDWCombatPlayerResourceSave::StaticStruct()->CompareScriptStruct(&S.CombatPlayerResources,&Initial->CombatPlayerResources,PPF_None);
        if(!bInitialResources&&Family==DWFormalSaveVersionPolicy::EFamily::Formal)
        {
            TStrongObjectPtr<UDWSaveGame> Full(NewObject<UDWSaveGame>());
            if(!Full.IsValid()||!DWCombatPlayerSave::MakeNewFullSurvival(Rules,*Full,Error))return false;
            bInitialResources=FDWCombatPlayerResourceSave::StaticStruct()->CompareScriptStruct(&S.CombatPlayerResources,&Full->CombatPlayerResources,PPF_None);
        }
        if(S.bHasPlayerTransform||!S.Inventory.IsEmpty()||!S.WorldActors.IsEmpty()||!S.CompletedWorldEvents.IsEmpty()||
            !S.VisitedMaps.IsEmpty()||!FDWCombatPlayerExtendedSave::StaticStruct()->CompareScriptStruct(&S.CombatWorld.Player,&EmptyExtended,PPF_None)||!S.CombatWorld.Objects.IsEmpty()||!S.CombatWorld.Effects.IsEmpty()||
            !bInitialResources)
        {Error=TEXT("New adventure marker cannot carry previously running state.");return false;}
        Error.Reset();return true;
    }
    FDWCombatWorldObjectsSave Objects;FDWCombatEffectsSave Effects;
    return DecodeAndCrossCheck(Rules,S,Objects,Effects,Error);
}
bool FDWCombatWorldPersistence::MarkNewAdventure(const FDWCombatRules& Rules,UDWSaveGame& S,FString& Error)
{return PrepareNewAdventureMarker(Rules,S,Error)&&Validate(Rules,S,Error);}
bool FDWCombatWorldPersistence::PrepareNewAdventureMarker(const FDWCombatRules& Rules,UDWSaveGame& S,FString& Error)
{
    int32 CombatRulesSchemaVersion=0;if(!Schema(Rules,CombatRulesSchemaVersion,Error))return false;
    S.CombatWorld={};S.CombatWorld.Schema=CombatRulesSchemaVersion;S.CombatWorld.bNewAdventure=true;
    if(!FDWGatheringWorldPersistence::MakeNewAdventure(S.MapPackage,S.CombatWorld.Gathering,Error))return false;
    Error.Reset();return true;
}
bool FDWCombatWorldPersistence::Capture(ADWPlayerCharacter& P,UDWSaveGame& Out,FString& Error)
{
    auto* W=P.GetWorld();auto* GI=P.GetGameInstance<UDWGameInstance>();auto* A=UDWCombatAuthoritySubsystem::Get(&P);
    const auto Rules=GI?GI->GetCombatRules():nullptr;
    if(!W||W->bIsTearingDown||!Rules||!A||P.IsActorBeingDestroyed()||P.IsCombatRestoreInProgress())
    {Error=TEXT("Complete capture requires a stable live world.");return false;}
    auto* DefinitionOwner=GI?GI->GetSubsystem<UDWLanguageSubsystem>():nullptr;
    DWCharacterA01SaveBridge::FScopedFormalDefinitions DefinitionScope(DefinitionOwner&&DefinitionOwner->GetNativeModel()?&DefinitionOwner->GetNativeModel()->GetCatalog():nullptr,DefinitionOwner?DefinitionOwner->GetTrialConfig():TSharedPtr<FJsonObject>());
    const double At=W->GetTimeSeconds();const uint64 Epoch=A->GetWorldEpoch();
    TStrongObjectPtr<UDWSaveGame> S(NewObject<UDWSaveGame>());
    // Capture does not invent a marker owner. Preserve the caller's validated candidate.
    if(!DWWorldMapData::ValidateSave(Out.WorldMapMarkers,Error))return false;
    S->WorldMapMarkers=Out.WorldMapMarkers;
    // Capture inherits its admitted owner format. It never upgrades an old slot.
    if(DefinitionOwner&&DefinitionOwner->IsTrialWorld())S->Version=DefinitionOwner->GetTrialContainerVersion();
    else if(GI->HasActiveSlot())
    {
        if(GI->ActiveSaveVersion<1||GI->ActiveSaveVersion>UDWSaveGame::CurrentVersion)
        {Error=TEXT("Capture has no accepted save-container owner.");return false;}
        S->Version=GI->ActiveSaveVersion;
        // This GI-only phase captures the real, still-legacy destination before
        // source personal/Formal2 facts are projected into the SAME candidate.
        // It cannot be written as v5 or adopted before full source-version validation.
        if(S->Version==DWFormalSaveVersionPolicy::FormalVersion&&GI->bPreparingFormalFreshDestination&&GI->bApplyingCombatPending&&
            GI->AdventureFlow.Schema==1&&GI->AdventureFlow.Phase==EDWAdventureOpeningPhase::CompletedPendingDestination&&
            UWorld::RemovePIEPrefix(W->GetOutermost()->GetName())==GI->AdventureFlow.DestinationMap&&
            (!P.CharacterA01||!P.CharacterA01->IsEnabled()))S->Version=DWFormalSaveVersionPolicy::LastLegacyVersion;
    }
    FDWCombatWorldObjectsSave Objects;FDWCombatEffectsSave Effects;TArray<FDWCombatObjectBinding> Bindings;
    if(!FDWCombatWorldObjectsPersistence::Capture(*W,P,Objects,S->WorldActors,Bindings,Error)||
        !FDWCombatPlayerPersistence::CaptureBase(P,*S,Error)||
        !FDWGatheringWorldPersistence::Capture(P,S->CombatWorld.Gathering,Error)||
        !FDWGatheringWorldPersistence::AppendCaptureAuthorityBindings(P,S->CombatWorld.Gathering,Bindings,Error)||
        !FDWCombatPlayerPersistence::Capture(P,Bindings,S->CombatWorld.Player,Error)||
        !FDWCombatMinePersistence::Capture(P,S->CombatWorld.Player,Error)||
        !A->CaptureShieldPackets(P,Bindings,S->CombatWorld.Player.ShieldPackets,Error)||
        !FDWCombatEffectsPersistence::Capture(*A,Bindings,Effects,Error))return false;
    if(!Schema(*Rules,S->CombatWorld.Schema,Error))return false;
    S->MapPackage=UWorld::RemovePIEPrefix(W->GetOutermost()->GetName());S->CombatWorld.bNewAdventure=false;
    if(P.CharacterA01&&P.CharacterA01->IsEnabled()&&S->LanguageTrialSchema==0&&!FDWFormalLanguagePersistence::Capture(P,*S,Error))return false;
    if(!DWCombatWorldObjectsSave::Encode(Objects,S->CombatWorld.Objects,Error,S->FormalLanguage.Schema==2)||
        !DWCombatEffectsSave::Encode(Effects,S->CombatWorld.Effects,Error)||!Validate(*Rules,*S,Error))return false;
    if(P.GetWorld()!=W||W->bIsTearingDown||W->GetTimeSeconds()!=At||A->GetWorldEpoch()!=Epoch||P.IsCombatRestoreInProgress())
    {Error=TEXT("World changed during capture; no partial snapshot was returned.");return false;}
    CopyCaptured(*S,Out);Error.Reset();return true;
}
bool FDWCombatWorldPersistence::Restore(ADWPlayerCharacter& P,const UDWSaveGame& Incoming,bool& OutCommitted,FString& Error)
{return RestoreImpl(P,Incoming,OutCommitted,Error,nullptr);}
bool FDWCombatWorldPersistence::RestoreImpl(ADWPlayerCharacter& P,const UDWSaveGame& Incoming,bool& OutCommitted,FString& Error,const FDWOpeningDaggerProjectionProof* OpeningProof)
{
    OutCommitted=false;auto* W=P.GetWorld();auto* GI=P.GetGameInstance<UDWGameInstance>();auto* A=UDWCombatAuthoritySubsystem::Get(&P);
    const auto Rules=GI?GI->GetCombatRules():nullptr;
    if(!W||W->bIsTearingDown||!Rules||!A||P.IsActorBeingDestroyed()||P.IsCombatRestoreInProgress())
    {Error=TEXT("World is not ready to prepare a complete restore.");return false;}
    auto* DefinitionOwner=GI?GI->GetSubsystem<UDWLanguageSubsystem>():nullptr;
    DWCharacterA01SaveBridge::FScopedFormalDefinitions DefinitionScope(DefinitionOwner&&DefinitionOwner->GetNativeModel()?&DefinitionOwner->GetNativeModel()->GetCatalog():nullptr,DefinitionOwner?DefinitionOwner->GetTrialConfig():TSharedPtr<FJsonObject>());
    TStrongObjectPtr<UDWSaveGame> S(DuplicateObject<UDWSaveGame>(&Incoming,GetTransientPackage()));
    if(!S||!P.GetGameplayConfig()||!DWRetiredSaveProjection::ApplyToDuplicate(*S,Error,P.GetGameplayConfig()->MaxInventorySlots)||!Validate(*Rules,*S,Error)||S->CombatWorld.bNewAdventure||
        UWorld::RemovePIEPrefix(W->GetOutermost()->GetName())!=S->MapPackage)return false;
    FDWCombatWorldObjectsSave Objects;FDWCombatEffectsSave Effects;
    if(!DecodeAndCrossCheck(*Rules,*S,Objects,Effects,Error))return false;
    auto* Events=W->GetSubsystem<UDWWorldEventSubsystem>();if(!Events||Events->IsCinematicActive())return false;
    FDWWorldEventRestorePlan EventPlan;
    if(!Events->PrepareCompletedFacts(S->CompletedWorldEvents,EventPlan,Error))return false;
    FDWCombatWorldObjectsRestorePlan WorldPlan;FDWCombatEffectsRestorePlan EffectsPlan;
    FDWCombatPlayerRestorePlan PlayerPlan;FDWCombatShieldRestorePlan ShieldPlan;FDWCombatMineRestorePlan MinePlan;
    FDWGatheringRestorePlan GatheringPlan;
    FDWLanguageProgressRestorePlan LanguagePlan;UDWLanguageSubsystem* Language=nullptr;
    UDWWorldMapSubsystem* MapState=nullptr;FDWWorldMapRestorePlan MapPlan;
    const int32 MapSlot=GI?GI->GetActiveSlot():INDEX_NONE;
    const uint64 MapGeneration=GI?GI->CombatSaveGeneration:0;
    UDWSaveGame* const MapPending=GI?GI->PendingSave.Get():nullptr;
    const bool bApplyMap=GI&&GI->HasActiveSlot()&&MapPending;
    if(bApplyMap)
    {
        MapState=W->GetSubsystem<UDWWorldMapSubsystem>();
        if(!MapState||!MapState->PrepareLoadedMarkers(MapSlot,S->WorldMapMarkers,MapPlan,Error))return false;
    }
    if(S->LanguageTrialSchema==1)
    {
        Language=GI->GetSubsystem<UDWLanguageSubsystem>();
        if(!Language||!Language->IsReady()||!FDWLanguageProgressPersistence::Prepare(*Language,S->LanguageProgressJson,LanguagePlan,Error))return false;
        LanguagePlan.TrialContainerVersion=S->Version;
    }
    if(!FDWCombatWorldObjectsPersistence::PrepareWorldObjects(*W,P,Objects,S->WorldActors,WorldPlan,Error,S->FormalLanguage.Schema==2)||
        !FDWGatheringWorldPersistence::Prepare(P,S->CombatWorld.Gathering,S->Inventory,GatheringPlan,Error))return false;
    auto LiveBindings=WorldPlan.LiveBindings();auto RestoredBindings=WorldPlan.RestoredBindings();
    if(!GatheringPlan.AppendAuthorityBindings(Effects.Authority,LiveBindings,RestoredBindings,Error))return false;
    TArray<uint64> Keep=WorldPlan.KeepActionIds();if(S->CombatWorld.Player.bRooted)Keep.AddUnique(S->CombatWorld.Player.RootAction);
    if(!FDWCombatEffectsPersistence::PrepareRestore(*A,Effects,LiveBindings,RestoredBindings,Keep,EffectsPlan,Error)||
        !GatheringPlan.ValidateRestoredAuthority(EffectsPlan.Authority(),Error)||
        !FDWCombatWorldObjectsPersistence::PrepareWorldFacts(WorldPlan,EffectsPlan.Authority(),Error)||
        !FDWCombatPlayerPersistence::Prepare(P,*S,Effects,EffectsPlan.Authority(),PlayerPlan,Error)||
        !A->PrepareShieldRestore(S->CombatWorld.Player.ShieldPackets,Effects,EffectsPlan.Authority(),ShieldPlan,Error)||
        !FDWCombatMinePersistence::Prepare(P,S->CombatWorld.Player,Effects.Authority,EffectsPlan.Authority(),PlayerPlan.RestoredRootIdentity(),MinePlan,Error)||
        !WorldPlan.CanCommit(Error)||!PlayerPlan.CanCommit(Error)||!MinePlan.CanCommit(Error)||!EffectsPlan.CanCommit(Error)||!GatheringPlan.CanCommit(Error)||
        !GatheringPlan.ValidateRestoredAuthority(EffectsPlan.Authority(),Error))return false;
    if(S->FormalLanguage.Schema==2)
    {
        Language=GI->GetSubsystem<UDWLanguageSubsystem>();
        if(!Language||!FDWFormalLanguagePersistence::Prepare(P,*S,EffectsPlan.Authority().NewEpoch(),EffectsPlan.Authority().FindLife(S->CombatWorld.Player.PlayerEntity),LanguagePlan,Error))return false;
    }
    if(!WorldPlan.CanCommit(Error)||!PlayerPlan.CanCommit(Error)||!MinePlan.CanCommit(Error)||!EffectsPlan.CanCommit(Error)||!GatheringPlan.CanCommit(Error)||
        !EventPlan.CanCommit(Error)||(Language&&!LanguagePlan.CanCommit()))return false;
    const auto Epoch=EffectsPlan.Authority().NewEpoch();const auto Life=EffectsPlan.Authority().FindLife(S->CombatWorld.Player.PlayerEntity);
    if(OpeningProof&&!OpeningProof->CanCommit(P,*S,Error))return false;
    if(bApplyMap&&(GI->CombatSaveGeneration!=MapGeneration||GI->GetActiveSlot()!=MapSlot||GI->PendingSave.Get()!=MapPending||
       GI->GetWorld()!=W||!MapState->CanAdoptLoadedMarkers(MapPlan,Error)))return false;
    if(!A->BeginWorldRestoreBarrier(EffectsPlan.Authority(),Error))return false;
    // From here onward every operation is fixed native adoption. No external callbacks, spawn, transform or fallible preparation.
    FDWCombatEffectsPersistence::CommitFactsUnchecked(EffectsPlan);
    FDWCombatPlayerPersistence::AdoptNativeFacts(PlayerPlan);
    FDWCombatWorldObjectsPersistence::AdoptNativeFacts(WorldPlan);
    FDWCombatMinePersistence::AdoptNativeFacts(MinePlan);
    FDWGatheringWorldPersistence::AdoptNativeFacts(GatheringPlan);
    if(Language)FDWLanguageProgressPersistence::AdoptNativeFacts(LanguagePlan);
    if(bApplyMap)MapState->AdoptLoadedMarkersUnchecked(MoveTemp(MapPlan));
    A->AdoptShieldFactsUnchecked(ShieldPlan);
    Events->AdoptCompletedFacts(EventPlan);OutCommitted=true;
    A->FinishWorldRestoreBarrier();
    const auto Current=[&](){return IsValid(&P)&&!P.IsActorBeingDestroyed()&&P.GetWorld()==W&&!W->bIsTearingDown&&
        UDWCombatAuthoritySubsystem::Get(&P)==A&&A->GetWorldEpoch()==Epoch&&SamePersistedCombatWorldLife(A->FindRegisteredActorLife(&P),Life)&&
        (!bApplyMap||(GI&&GI->GetWorld()==W&&GI->CombatSaveGeneration==MapGeneration&&GI->GetActiveSlot()==MapSlot&&GI->PendingSave.Get()==MapPending));};
    if(!Current()||!FDWCombatPlayerPersistence::Publish(PlayerPlan,Error)||
        !FDWCombatWorldObjectsPersistence::Publish(WorldPlan,Error)||!Current()||
        !FDWGatheringWorldPersistence::Publish(GatheringPlan,Error)||!Current()||
        !FDWCombatMinePersistence::Publish(MinePlan,Error)||!Current())return false;
    FDWCombatEffectsPersistence::PublishRestoredEffects(EffectsPlan);if(!Current())return false;
    if(!Events->PublishCompletedFacts(EventPlan,A,Epoch,&P,Life,Error)||!Current())return false;
    if(Language&&(!FDWLanguageProgressPersistence::Publish(LanguagePlan,Error)||!Current()))return false;
    if(!FDWCombatPlayerPersistence::FinishPublish(PlayerPlan,Error)||!Current())return false;
    if(bApplyMap&&(!MapState->PublishLoadedMarkers(MapSlot,Current,Error)||!Current()))return false;
    return true;
}
