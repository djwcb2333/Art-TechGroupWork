#include "DWOpeningMapCompatibility.h"
#include "DWGameplayConfig.h"
#include "DWSaveGame.h"
#include "DWCharacterA01SaveBridge.h"
#include "DWCombatWorldPersistence.h"
#include "DWCombatWorldObjectsSave.h"
#include "DWCombatEffectsSave.h"
#include "Misc/PackageName.h"
#include "UObject/StrongObjectPtr.h"

namespace DWOpeningMapCompatibility
{
const TCHAR* LegacyPackage(){return TEXT("/Game/DoughWorld/OpeningPrototype/L_SC0101_Awakening");}
const TCHAR* CurrentPackage(){return TEXT("/Game/DoughWorld/Maps/Gameplay/Levels/L_SC0101_Awakening");}
FString CanonicalPackage(const FString& Package)
{return Package.Equals(LegacyPackage(),ESearchCase::CaseSensitive)?FString(CurrentPackage()):Package;}
bool NeedsReadProjection(const UDWSaveGame& Source)
{
 return Source.AdventureFlow.Schema==1&&Source.AdventureFlow.ScenarioId==TEXT("SC0101")&&
  (Source.AdventureFlow.Phase==EDWAdventureOpeningPhase::Pending||Source.AdventureFlow.Phase==EDWAdventureOpeningPhase::CompletedPendingDestination)&&
  Source.AdventureFlow.OpeningMap.Equals(LegacyPackage(),ESearchCase::CaseSensitive);
}
bool FindProfileOverride(const UDWGameplayConfig& Config,const FString& CanonicalMap,
 const TSoftObjectPtr<UDWGameplayConfig>*& Out,FString& Error)
{
 Out=nullptr;
 if(CanonicalMap.Equals(CurrentPackage(),ESearchCase::CaseSensitive))
 {
  const auto* Old=Config.MapConfigOverrides.Find(FName(LegacyPackage()));
  const auto* New=Config.MapConfigOverrides.Find(FName(CurrentPackage()));
  if(Old&&New){Error=TEXT("Opening map profile has both old and new alias keys; refusing an ambiguous profile.");return false;}
  Out=New?New:Old;
 }
 else Out=Config.MapConfigOverrides.Find(FName(*CanonicalMap));
 Error.Reset();return true;
}

namespace
{
struct FProjection
{
 int32 Changes=0;
 bool bSawNewAlias=false;
 bool Fail(FString& Error,const TCHAR* Message){Error=Message;return false;}
 bool Package(FString& Value,FString& Error,bool bAllowEmpty=false)
 {
  if(Value.IsEmpty())return bAllowEmpty||Fail(Error,TEXT("Empty map in opening alias candidate."));
  if(!FPackageName::IsValidLongPackageName(Value)||Value.Len()!=FCString::Strlen(*Value))return Fail(Error,TEXT("Invalid map in opening alias candidate."));
  if(Value.Equals(CurrentPackage(),ESearchCase::CaseSensitive))bSawNewAlias=true;
  if(Value.Equals(LegacyPackage(),ESearchCase::CaseSensitive)){Value=CurrentPackage();++Changes;}
  return true;
 }
 static bool ActorName(const FString& Value)
 {
  if(Value.IsEmpty())return false;
  for(TCHAR Ch:Value)if(!FChar::IsAlnum(Ch)&&Ch!=TEXT('_'))return false;
  return true;
 }
 bool ActorPath(FString& Value,FString& Error,bool bAllowEmpty=false)
 {
  if(Value.IsEmpty())return bAllowEmpty||Fail(Error,TEXT("Empty actor path in opening alias candidate."));
  const FSoftObjectPath Path(Value);
  const FString PackageName=Path.GetLongPackageName();
  const FString SubPath=Path.GetSubPathString();
  const FString Prefix=TEXT("PersistentLevel.");
  if(Value.Len()!=FCString::Strlen(*Value)||!FPackageName::IsValidLongPackageName(PackageName)||
   !SubPath.StartsWith(Prefix,ESearchCase::CaseSensitive)||!ActorName(SubPath.Mid(Prefix.Len())))
   return Fail(Error,TEXT("Unknown authored actor-path grammar in opening alias candidate."));
  if(PackageName.Equals(LegacyPackage(),ESearchCase::CaseSensitive)||PackageName.Equals(CurrentPackage(),ESearchCase::CaseSensitive))
  {
   if(Path.GetAssetName()!=TEXT("L_SC0101_Awakening"))return Fail(Error,TEXT("Opening alias actor has the wrong world object name."));
   if(PackageName.Equals(CurrentPackage(),ESearchCase::CaseSensitive))bSawNewAlias=true;
   else{Value=FString(CurrentPackage())+TEXT(".L_SC0101_Awakening:")+SubPath;++Changes;}
  }
  return true;
 }
 static bool Guid(const FString& Value)
 {FGuid Parsed;return FGuid::ParseExact(Value,EGuidFormats::Digits,Parsed)&&Parsed.IsValid();}
 static bool PositiveDecimal(const FString& Value)
 {
  uint64 Number=0;if(Value.IsEmpty()||!LexTryParseString(Number,*Value)||!Number)return false;
  return Value==FString::Printf(TEXT("%llu"),static_cast<unsigned long long>(Number));
 }
 bool StableId(FString& Value,FString& Error,bool bAllowEmpty=false,bool bAllowController=true)
 {
  if(Value.IsEmpty())return bAllowEmpty||Fail(Error,TEXT("Empty stable identity in opening alias candidate."));
  if(Value.Len()!=FCString::Strlen(*Value))return Fail(Error,TEXT("Embedded zero in opening alias identity."));
  if(Value==TEXT("player:0")||Value==TEXT("controller:0"))return true;
  if(Value.StartsWith(TEXT("controller:"),ESearchCase::CaseSensitive))
  {
   if(!bAllowController)return Fail(Error,TEXT("Nested controller identity is not supported."));
   FString Tail=Value.Mid(11);
   if(!Tail.StartsWith(TEXT("enemy:"),ESearchCase::CaseSensitive))return Fail(Error,TEXT("Unknown controller stable-identity grammar."));
   if(!StableId(Tail,Error,false,false))return false;
   Value=TEXT("controller:")+Tail;return true;
  }
  for(const TCHAR* Head:{TEXT("enemy:scene:"),TEXT("nest:"),TEXT("resource:")})
   if(Value.StartsWith(Head,ESearchCase::CaseSensitive))
   {FString Path=Value.Mid(FCString::Strlen(Head));if(!ActorPath(Path,Error))return false;Value=FString(Head)+Path;return true;}
  if(Value.StartsWith(TEXT("enemy:runtime:"),ESearchCase::CaseSensitive))
   return Guid(Value.Mid(14))||Fail(Error,TEXT("Invalid runtime enemy identity in opening alias candidate."));
  if(Value.StartsWith(TEXT("enemy:"),ESearchCase::CaseSensitive))
   return Guid(Value.Mid(6))||Fail(Error,TEXT("Invalid formal enemy identity in opening alias candidate."));
  if(Value.StartsWith(TEXT("tombstone:"),ESearchCase::CaseSensitive))
  {
   FString Epoch,Entity;
   return (Value.Mid(10).Split(TEXT(":"),&Epoch,&Entity)&&PositiveDecimal(Epoch)&&PositiveDecimal(Entity))||Fail(Error,TEXT("Invalid historical tombstone identity in opening alias candidate."));
  }
  if(Value.StartsWith(TEXT("gathering:resource:"),ESearchCase::CaseSensitive))
  {
   FString Map,Name;
   if(!Value.Mid(19).Split(TEXT(":"),&Map,&Name)||!ActorName(Name)||!Package(Map,Error))return Fail(Error,TEXT("Unknown gathering authority identity in opening alias candidate."));
   Value=TEXT("gathering:resource:")+Map+TEXT(":")+Name;return true;
  }
  return Fail(Error,TEXT("Unknown stable identity grammar; opening alias projection refused."));
 }
 bool LegacyActor(FDWPersistedActorState& Row,FString& Error)
 {
  return StableId(Row.ActorId,Error)&&ActorPath(Row.ActorPath,Error)&&
   StableId(Row.OwnerId,Error,true)&&ActorPath(Row.OwnerPath,Error,true);
 }
 bool LegacyActors(TArray<FDWPersistedActorState>& Rows,FString& Error)
 {
  TSet<FString> Before,After;
  for(const auto& Row:Rows)
  {
   if(Row.ActorId.IsEmpty()||Before.Contains(Row.ActorId))return Fail(Error,TEXT("Empty or duplicate source actor identity in opening alias table."));
   Before.Add(Row.ActorId);
  }
  for(auto& Row:Rows)
  {
   if(!LegacyActor(Row,Error))return false;
   if(Row.ActorId.IsEmpty()||After.Contains(Row.ActorId))return Fail(Error,TEXT("Opening alias would create an empty or duplicate actor identity."));
   After.Add(Row.ActorId);
  }
  return true;
 }
 bool Objects(FDWCombatWorldObjectsSave& S,FString& Error)
 {
  if(!Package(S.MapPackage,Error)||!Package(S.Recovery.SafePoint.MapPackage,Error,true))return false;
  for(auto& Bag:S.Recovery.Bags)if(!Package(Bag.MapPackage,Error))return false;
  for(auto& Enemy:S.Enemies)
   if(!StableId(Enemy.StableId,Error)||!ActorPath(Enemy.ActorPath,Error)||!StableId(Enemy.SourceNestId,Error,true)||
    !StableId(Enemy.TargetId,Error,true)||!StableId(Enemy.MeleeTargetId,Error,true))return false;
  for(auto& Clock:S.NestClocks)if(!StableId(Clock.StableId,Error))return false;
  return true;
 }
 bool Effects(FDWCombatEffectsSave& S,FString& Error)
 {
  for(auto& Actor:S.Authority.Actors)if(!StableId(Actor.StableId,Error))return false;
  for(auto& Flight:S.Spores)if(!StableId(Flight.Owner.InstigatorId,Error,true))return false;
  for(auto& Flight:S.Arrows)if(!StableId(Flight.Owner.InstigatorId,Error,true))return false;
  for(auto& Flight:S.Bottles)if(!StableId(Flight.Owner.InstigatorId,Error,true))return false;
  for(auto& Area:S.Areas)if(!StableId(Area.Owner.InstigatorId,Error,true))return false;
  return true;
 }
};
}

EReadProjection BuildReadCopy(const UDWSaveGame& Source,const FDWCombatRules& Rules,const DWLanguage::FCatalog* Catalog,const TSharedPtr<FJsonObject>& Trial,UDWSaveGame*& Out,FString& Error)
{
 Out=nullptr;
 if(!NeedsReadProjection(Source)){Error.Reset();return EReadProjection::Unchanged;}
 DWCharacterA01SaveBridge::FScopedFormalDefinitions DefinitionScope(Catalog,Trial);
 if(Source.LanguageTrialSchema!=0||!Source.LanguageProgressJson.IsEmpty()||!Source.A01StateJson.IsEmpty())
 {Error=TEXT("Opening alias cannot promote isolated trial fields into a formal opening.");return EReadProjection::Rejected;}
 // These are source-format checks, not current filesystem/config admission. Do not
 // repair an originally inconsistent object map, ledger or payload by renaming it.
 if(!FDWCombatWorldPersistence::Validate(Rules,Source,Error))return EReadProjection::Rejected;
 TStrongObjectPtr<UDWSaveGame> Copy(DuplicateObject<UDWSaveGame>(&Source,GetTransientPackage()));
 if(!Copy.Get()){Error=TEXT("Cannot allocate opening alias read copy.");return EReadProjection::Rejected;}
 FProjection Project;
 if(!Project.Package(Copy->MapPackage,Error)||!Project.Package(Copy->AdventureFlow.OpeningMap,Error)||
  !Project.Package(Copy->AdventureFlow.DestinationMap,Error)||!Project.LegacyActors(Copy->WorldActors,Error))return EReadProjection::Rejected;
 // Native D22 currently permits only an empty gathering block in this opening.
 // Never reinterpret an unexpected trial block or rewrite JSON to make it fit.
 if(Copy->CombatWorld.Gathering.Schema||!Copy->CombatWorld.Gathering.MapPackage.IsEmpty()||!Copy->CombatWorld.Gathering.Actors.IsEmpty()||!Copy->CombatWorld.Gathering.Ponds.IsEmpty())
 {Error=TEXT("Opening alias cannot adopt a gathering/trial block.");return EReadProjection::Rejected;}
 TSet<FString> Visited;
 for(auto& Map:Copy->VisitedMaps)
 {
  if(!Project.Package(Map.MapPackage,Error)||!Project.LegacyActors(Map.Actors,Error))return EReadProjection::Rejected;
  if(Visited.Contains(Map.MapPackage)){Error=TEXT("Opening alias would merge duplicate visited-map snapshots.");return EReadProjection::Rejected;}Visited.Add(Map.MapPackage);
 }
 for(auto& Packet:Copy->CombatWorld.Player.ShieldPackets)if(!Project.StableId(Packet.InstigatorId,Error,true))return EReadProjection::Rejected;
 if(!Copy->CombatWorld.bNewAdventure)
 {
  FDWCombatWorldObjectsSave Objects;FDWCombatEffectsSave Effects;
  if(!DWCombatWorldObjectsSave::Decode(Source.CombatWorld.Objects,Objects,Error,Source.FormalLanguage.Schema==2)||!DWCombatEffectsSave::Decode(Source.CombatWorld.Effects,Effects,Error))return EReadProjection::Rejected;
  const int32 BeforeObjects=Project.Changes;
  if(!Project.Objects(Objects,Error))return EReadProjection::Rejected;
  if(Project.Changes!=BeforeObjects&&!DWCombatWorldObjectsSave::Encode(Objects,Copy->CombatWorld.Objects,Error,Copy->FormalLanguage.Schema==2))return EReadProjection::Rejected;
  const int32 BeforeEffects=Project.Changes;
  if(!Project.Effects(Effects,Error))return EReadProjection::Rejected;
  if(Project.Changes!=BeforeEffects&&!DWCombatEffectsSave::Encode(Effects,Copy->CombatWorld.Effects,Error))return EReadProjection::Rejected;
 }
 // A partially hand-edited alias mixture is not evidence of one coherent source
 // world. Reject instead of merging old/new actor records or correcting a mismatch.
 if(Project.bSawNewAlias){Error=TEXT("Old opening metadata is mixed with new-alias typed facts; no automatic merge is allowed.");return EReadProjection::Rejected;}
 if(!Project.Changes||!FDWCombatWorldPersistence::Validate(Rules,*Copy,Error))return EReadProjection::Rejected;
 // GI immediately applies its unchanged ValidateAdventureFlow/ValidateSave,
 // then its original complete D22 Prepare/CanCommit/adoption barrier.
 Out=Copy.Get();Error.Reset();return EReadProjection::Projected;
}
}
