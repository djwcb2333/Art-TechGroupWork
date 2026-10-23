#include "DWCombatEffectsSave.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include <set>
namespace DWCombatEffectsSave { namespace
{
constexpr uint32 Magic=0x44574546,Version=1,CountLimit=262144;
constexpr int32 ByteLimit=64*1024*1024;
bool Stable(const FString& S){return !S.IsEmpty()&&S.Len()<=4096&&S.Len()==FCString::Strlen(*S);}
bool Finite(double V){return FMath::IsFinite(V);}
bool Vector(const FVector& V){return !V.ContainsNaN();}
bool Unit(const FVector& V){return Vector(V)&&FMath::IsNearlyEqual(V.SizeSquared(),1.,1.e-6);}
bool Same(DWCombatCommit::FEntityLife A,DWCombatCommit::FEntityLife B){return A.Entity==B.Entity&&A.Life==B.Life;}
void Life(FArchive& A,DWCombatCommit::FEntityLife& V){A<<V.Entity<<V.Life;}
void Action(FArchive& A,DWCombatCommit::FActionId& V){A<<V.Epoch<<V.Id;}
void Flag(FArchive& A,bool& V){uint8 B=V?1:0;A<<B;if(A.IsLoading()){if(B>1)A.SetError();V=B!=0;}}
template<class E>void Enum(FArchive& A,E& V){uint8 N=static_cast<uint8>(V);A<<N;if(A.IsLoading())V=static_cast<E>(N);}
void String(FArchive& A,FString& V)
{
    if(A.IsSaving())
    {FTCHARToUTF8 U(*V,V.Len());int32 N=U.Length();A<<N;if(N<0||N>16384){A.SetError();return;}A.Serialize(const_cast<ANSICHAR*>(U.Get()),N);}
    else
    {int32 N=0;A<<N;if(A.IsError()||N<0||N>16384||N>A.TotalSize()-A.Tell()){A.SetError();return;}
        TArray<ANSICHAR> B;B.SetNumUninitialized(N+1);A.Serialize(B.GetData(),N);B[N]=0;
        FUTF8ToTCHAR T(B.GetData(),N);V=FString(T.Length(),T.Get());FTCHARToUTF8 Back(*V,V.Len());
        if(Back.Length()!=N||(N&&FMemory::Memcmp(Back.Get(),B.GetData(),N)!=0)||V.Len()!=FCString::Strlen(*V))A.SetError();}
}
void Vec(FArchive& A,FVector& V){A<<V.X<<V.Y<<V.Z;}
template<class T,class F>void Items(FArchive& A,TArray<T>& V,F Serialize)
{uint32 N=static_cast<uint32>(V.Num());A<<N;if(A.IsError()||N>CountLimit){A.SetError();return;}if(A.IsLoading())V.SetNum(N);for(auto& X:V){Serialize(A,X);if(A.IsError())return;}}
template<class T,class F>void Items(FArchive& A,std::vector<T>& V,F Serialize)
{uint32 N=static_cast<uint32>(V.size());A<<N;if(A.IsError()||N>CountLimit){A.SetError();return;}if(A.IsLoading())V.resize(N);for(auto& X:V){Serialize(A,X);if(A.IsError())return;}}
void Owner(FArchive& A,FDWReleasedEffectOwnerSave& V){Life(A,V.SourceLife);String(A,V.InstigatorId);A<<V.Emitter;}
void Spore(FArchive& A,FDWSporeShotSnapshot& V){A<<V.Damage<<V.RangeCm<<V.SpeedCmPerSecond<<V.TurnDegreesPerSecond<<V.RadiusCm<<V.KnockbackCm<<V.KnockbackSeconds;}
void Bow(FArchive& A,FDWBowShotSnapshot& V){A<<V.Damage<<V.RangeCm<<V.SpeedCmPerSecond<<V.RadiusCm<<V.KnockbackCm<<V.KnockbackSeconds<<V.Area.RadiusCm<<V.Area.DurationSeconds<<V.Area.SlowFraction;}
void Bottle(FArchive& A,FDWBottleSnapshot& V){A<<V.RangeCm<<V.FlightSeconds<<V.ArcHeightCm<<V.CollisionRadiusCm<<V.AreaRadiusCm<<V.AreaSeconds<<V.TickSeconds<<V.TickDamage<<V.SlowFraction;}
void BowVisualExtension(FArchive& A,FDWCombatEffectsSave& S)
{
    // Optional tagged tail leaves every old Schema1 layout byte intact. Unknown tails still reject.
    if(A.IsLoading()&&A.Tell()==A.TotalSize())return;
    if(A.IsSaving())
    {
        bool Any=false;for(const auto& X:S.Arrows)Any|=X.bHasVisualTransform;if(!Any)return;
    }
    constexpr uint32 Tag=0x44574256;uint32 MagicTag=Tag,ExtensionVersion=1,Count=static_cast<uint32>(S.Arrows.Num());
    A<<MagicTag<<ExtensionVersion<<Count;
    if(A.IsError()||MagicTag!=Tag||ExtensionVersion!=1||Count!=static_cast<uint32>(S.Arrows.Num())){A.SetError();return;}
    for(auto& X:S.Arrows)
    {
        Flag(A,X.bHasVisualTransform);if(A.IsError())return;
        if(!X.bHasVisualTransform)continue;
        FVector T=X.VisualTransform.GetTranslation(),Scale=X.VisualTransform.GetScale3D();FQuat Q=X.VisualTransform.GetRotation();
        Vec(A,T);A<<Q.X<<Q.Y<<Q.Z<<Q.W;Vec(A,Scale);
        if(A.IsLoading())
        {
            if(T.ContainsNaN()||Scale.ContainsNaN()||Q.ContainsNaN()||!Q.IsNormalized()){A.SetError();return;}
            X.VisualTransform=FTransform(Q,T,Scale);
        }
        if(A.IsError())return;
    }
}
void Serialize(FArchive& A,FDWCombatEffectsSave& S)
{
    uint32 M=Magic,V=Version;A<<M<<V<<S.Schema;if(M!=Magic||V!=Version||S.Schema!=1){A.SetError();return;}
    auto& L=S.Authority.Ledger;A<<S.Authority.LastEntity<<L.Schema<<L.Epoch<<L.LastAction<<L.LastEmitter<<L.LastReceipt;
    Items(A,L.Lives,[](FArchive& R,DWCombatCommit::FLifeSave& X){R<<X.Entity<<X.Life;});
    Items(A,L.Actions,[](FArchive& R,DWCombatCommit::FActionSave& X){R<<X.Id;Life(R,X.Owner);Flag(R,X.Active);});
    Items(A,L.Emitters,[](FArchive& R,DWCombatCommit::FEmitterSave& X){R<<X.Id;Life(R,X.Owner);Action(R,X.RootEpisode);Flag(R,X.Active);});
    Items(A,L.Receipts,[](FArchive& R,DWCombatCommit::FReceiptSave& X){R<<X.Receipt.Handle.Epoch<<X.Receipt.Handle.Id;Enum(R,X.Receipt.State);Flag(R,X.Resource);Life(R,X.Source);Life(R,X.Target);Action(R,X.Action);Enum(R,X.ResourceKind);});
    Items(A,L.Keys,[](FArchive& R,DWCombatCommit::FKeySave& X){for(auto& K:X.Key)R<<K;R<<X.Receipt;});
    Items(A,L.Emissions,[](FArchive& R,DWCombatCommit::FEmissionSave& X){R<<X.Parent<<X.Ordinal<<X.Emitter;});
    Items(A,S.Authority.Actors,[](FArchive& R,FDWCombatActorIdentitySave& X){String(R,X.StableId);Life(R,X.Life);R<<X.RestoreRevision;Enum(R,X.Kind);Flag(R,X.bDead);Flag(R,X.bEnded);});
    Items(A,S.Spores,[](FArchive& R,FDWSporeFlightSave& X){Owner(R,X.Owner);Spore(R,X.Shot);Vec(R,X.Position);Vec(R,X.Direction);Life(R,X.LockedTargetLife);R<<X.TravelledCm<<X.LastWorldOffset<<X.PendingSeconds;Flag(R,X.bLostLock);});
    Items(A,S.Arrows,[](FArchive& R,FDWBowFlightSave& X){Owner(R,X.Owner);Bow(R,X.Shot);Vec(R,X.Position);Vec(R,X.Direction);R<<X.TravelledCm<<X.LastWorldOffset<<X.ReleasedOffset;});
    Items(A,S.Bottles,[](FArchive& R,FDWBottleFlightSave& X){Owner(R,X.Owner);Bottle(R,X.Shot);Vec(R,X.Position);Vec(R,X.FlightStart);Vec(R,X.GroundTarget);R<<X.FlightAge<<X.LastWorldOffset<<X.ReleasedOffset<<X.PendingSeconds<<X.WalkableFloorZ;});
    Items(A,S.Areas,[](FArchive& R,FDWGroundAreaSave& X){Owner(R,X.Owner);Vec(R,X.Ground);R<<X.StartedOffset<<X.EndsOffset<<X.RadiusCm<<X.SlowFraction<<X.Damage<<X.Period;Flag(R,X.bActive);});
    Items(A,S.TargetPhases,[](FArchive& R,FDWAreaTargetPhaseSave& X){Life(R,X.TargetLife);R<<X.AnchorOffset<<X.PeriodSeconds<<X.NextOrdinal;});
    if(!A.IsError())BowVisualExtension(A,S);
}
}}
bool DWCombatEffectsSave::ValidateAuthority(const FDWCombatAuthoritySave& S,FString& Error)
{
    std::string NativeError;if(!DWCombatCommit::FLedger::ValidateSnapshot(S.Ledger,NativeError)){Error=UTF8_TO_TCHAR(NativeError.c_str());return false;}
    TSet<uint64> Entities;TSet<FString> Names;std::map<uint64,uint64> Lives;for(const auto& V:S.Ledger.Lives)Lives.emplace(V.Entity,V.Life);
    if(!S.LastEntity||S.Actors.Num()>static_cast<int32>(CountLimit)||Lives.find(1)==Lives.end()||Lives.at(1)!=1){Error=TEXT("Invalid authority world identity");return false;}
    for(const auto& V:S.Actors)
    {
        const auto I=Lives.find(V.Life.Entity);
        if(!Stable(V.StableId)||Names.Contains(V.StableId)||Entities.Contains(V.Life.Entity)||V.Life.Entity<=1||V.Life.Entity>S.LastEntity||!V.RestoreRevision||I==Lives.end()||I->second!=V.Life.Life||static_cast<uint8>(V.Kind)>static_cast<uint8>(EDWCombatSavedActorKind::Controller)||(V.bEnded&&!V.bDead))
        {Error=TEXT("Invalid persistent actor identity");return false;}Names.Add(V.StableId);Entities.Add(V.Life.Entity);
    }
    if(Entities.Num()+1!=static_cast<int32>(Lives.size())){Error=TEXT("Unbound ledger identity");return false;}Error.Reset();return true;
}
bool DWCombatEffectsSave::Validate(const FDWCombatEffectsSave& S,FString& Error)
{
    const auto Bad=[&](const TCHAR* M){Error=M;return false;};std::string LedgerError;
    if(S.Schema!=1||!DWCombatCommit::FLedger::ValidateSnapshot(S.Authority.Ledger,LedgerError)){Error=TEXT("Effects ledger: ")+FString(UTF8_TO_TCHAR(LedgerError.c_str()));return false;}
    const auto& L=S.Authority.Ledger;
    if(S.Authority.Actors.Num()>static_cast<int32>(CountLimit)||S.Spores.Num()>static_cast<int32>(CountLimit)||S.Arrows.Num()>static_cast<int32>(CountLimit)||S.Bottles.Num()>static_cast<int32>(CountLimit)||S.Areas.Num()>static_cast<int32>(CountLimit)||S.TargetPhases.Num()>static_cast<int32>(CountLimit))return Bad(TEXT("Effects count limit"));
    TMap<uint64,const FDWCombatActorIdentitySave*> Actors;TSet<FString> Names;std::map<uint64,uint64> Lives;
    for(const auto& V:L.Lives)Lives.emplace(V.Entity,V.Life);
    if(!S.Authority.LastEntity||Lives.find(1)==Lives.end()||Lives.at(1)!=1)return Bad(TEXT("Missing world legacy origin"));
    for(const auto& V:S.Authority.Actors)
    {
        const auto I=Lives.find(V.Life.Entity);
        if(!Stable(V.StableId)||Names.Contains(V.StableId)||V.Life.Entity<=1||V.Life.Entity>S.Authority.LastEntity||!V.RestoreRevision||I==Lives.end()||I->second!=V.Life.Life||Actors.Contains(V.Life.Entity)||static_cast<uint8>(V.Kind)>static_cast<uint8>(EDWCombatSavedActorKind::Controller)||(V.bEnded&&!V.bDead))return Bad(TEXT("Invalid persistent actor table"));
        Names.Add(V.StableId);Actors.Add(V.Life.Entity,&V);
    }
    if(Actors.Num()+1!=static_cast<int32>(Lives.size()))return Bad(TEXT("Every ledger entity requires an explicit actor or tombstone"));
    TMap<uint64,const DWCombatCommit::FEmitterSave*> Emitters;for(const auto& V:L.Emitters)Emitters.Add(V.Id,&V);
    const auto Historic=[&](DWCombatCommit::FEntityLife V,EDWCombatSavedActorKind Kind)
    {const auto* const* I=Actors.Find(V.Entity);return I&&V.Life&&V.Life<=(*I)->Life.Life&&(*I)->Kind==Kind;};
    const auto ValidOwner=[&](const FDWReleasedEffectOwnerSave& O,bool Active)
    {
        const auto* const* E=Emitters.Find(O.Emitter);if(!E||(*E)->Active!=Active||(*E)->RootEpisode.Id||!Same((*E)->Owner,O.SourceLife)||!Historic(O.SourceLife,EDWCombatSavedActorKind::Player))return false;
        if(!O.InstigatorId.IsEmpty())
        {if(!Stable(O.InstigatorId))return false;bool Found=false;for(const auto& V:S.Authority.Actors)if(V.StableId.Equals(O.InstigatorId,ESearchCase::CaseSensitive)&&V.Kind==EDWCombatSavedActorKind::Controller){Found=true;break;}if(!Found)return false;}
        return true;
    };
    TSet<uint64> Carriers;
    for(const auto& V:S.Spores)
    {
        if(!ValidOwner(V.Owner,true)||Carriers.Contains(V.Owner.Emitter)||!V.Shot.IsValid()||!Vector(V.Position)||!Unit(V.Direction)||!Finite(V.TravelledCm)||V.TravelledCm<0.||V.TravelledCm>=V.Shot.RangeCm||!Finite(V.LastWorldOffset)||V.LastWorldOffset>0.||!Finite(V.PendingSeconds)||V.PendingSeconds<0.)return Bad(TEXT("Invalid spore flight"));
        if(V.LockedTargetLife.Entity){if(!Historic(V.LockedTargetLife,EDWCombatSavedActorKind::Enemy))return Bad(TEXT("Invalid locked target life"));}
        else if(V.LockedTargetLife.Life||!V.bLostLock)return Bad(TEXT("Partial spore lock"));Carriers.Add(V.Owner.Emitter);
    }
    for(const auto& V:S.Arrows)
    {
        if(!ValidOwner(V.Owner,true)||Carriers.Contains(V.Owner.Emitter)||!V.Shot.IsValid()||!Vector(V.Position)||!Unit(V.Direction)||!Finite(V.TravelledCm)||V.TravelledCm<0.||V.TravelledCm>=V.Shot.RangeCm||!Finite(V.LastWorldOffset)||V.LastWorldOffset>0.||!Finite(V.ReleasedOffset)||V.ReleasedOffset>V.LastWorldOffset||
            (V.bHasVisualTransform&&(!V.VisualTransform.IsValid()||!V.VisualTransform.GetRotation().IsNormalized())))return Bad(TEXT("Invalid arrow flight"));Carriers.Add(V.Owner.Emitter);
    }
    for(const auto& V:S.Bottles)
    {
        if(!ValidOwner(V.Owner,true)||Carriers.Contains(V.Owner.Emitter)||!V.Shot.IsValid()||!Vector(V.Position)||!Vector(V.FlightStart)||!Vector(V.GroundTarget)||!Finite(V.FlightAge)||V.FlightAge<0.||V.FlightAge>=V.Shot.FlightSeconds||!Finite(V.LastWorldOffset)||V.LastWorldOffset>0.||!Finite(V.ReleasedOffset)||V.ReleasedOffset>V.LastWorldOffset||!Finite(V.PendingSeconds)||V.PendingSeconds<0.||!Finite(V.WalkableFloorZ)||V.WalkableFloorZ<0.||V.WalkableFloorZ>1.)return Bad(TEXT("Invalid bottle flight"));Carriers.Add(V.Owner.Emitter);
    }
    for(const auto& V:S.Areas)
    {
        if(!ValidOwner(V.Owner,V.bActive)||Carriers.Contains(V.Owner.Emitter)||!Vector(V.Ground)||!Finite(V.StartedOffset)||V.StartedOffset>0.||!Finite(V.EndsOffset)||V.EndsOffset<=V.StartedOffset||!Finite(V.RadiusCm)||V.RadiusCm<=0.||!Finite(V.SlowFraction)||V.SlowFraction<0.||V.SlowFraction>1.||!Finite(V.Damage)||V.Damage<0.||!Finite(V.Period)||(V.Damage>0.?V.Period<=0.:V.Period!=0.))return Bad(TEXT("Invalid area facts"));Carriers.Add(V.Owner.Emitter);
    }
    // Active mine emitters belong to the root-ring aggregate; every other active emitter needs exactly one physical carrier or area.
    for(const auto& V:L.Emitters)if(V.Active&&!V.RootEpisode.Id&&!Carriers.Contains(V.Id))return Bad(TEXT("Released emitter has no saved carrier"));
    TSet<uint64> Targets;
    for(const auto& V:S.TargetPhases)
    {
        if(!Historic(V.TargetLife,EDWCombatSavedActorKind::Enemy)||Targets.Contains(V.TargetLife.Entity)||!Finite(V.AnchorOffset)||V.AnchorOffset>0.||!Finite(V.PeriodSeconds)||V.PeriodSeconds<=0.||!V.NextOrdinal||V.NextOrdinal==MAX_uint64)return Bad(TEXT("Invalid target cadence"));
        const double Due=V.AnchorOffset+static_cast<double>(V.NextOrdinal)*V.PeriodSeconds;
        if(!Finite(Due)||V.AnchorOffset+static_cast<double>(V.NextOrdinal+1)*V.PeriodSeconds<=Due)return Bad(TEXT("Unrepresentable target cadence"));Targets.Add(V.TargetLife.Entity);
    }
    Error.Reset();return true;
}
bool DWCombatEffectsSave::Encode(const FDWCombatEffectsSave& S,TArray<uint8>& Out,FString& Error)
{
    if(!Validate(S,Error))return false;FDWCombatEffectsSave Copy=S;TArray<uint8> Bytes;FMemoryWriter Writer(Bytes,true);Serialize(Writer,Copy);
    if(Writer.IsError()||Bytes.Num()>ByteLimit){Error=TEXT("Effects encode size limit");return false;}Out=MoveTemp(Bytes);return true;
}
bool DWCombatEffectsSave::Decode(const TArray<uint8>& Bytes,FDWCombatEffectsSave& Out,FString& Error)
{
    if(Bytes.IsEmpty()||Bytes.Num()>ByteLimit){Error=TEXT("Effects blob size");return false;}FMemoryReader Reader(Bytes,true);FDWCombatEffectsSave S;Serialize(Reader,S);
    if(Reader.IsError()||Reader.Tell()!=Reader.TotalSize()){Error=TEXT("Effects corrupt/truncated/trailing data");return false;}if(!Validate(S,Error))return false;Out=MoveTemp(S);return true;
}
