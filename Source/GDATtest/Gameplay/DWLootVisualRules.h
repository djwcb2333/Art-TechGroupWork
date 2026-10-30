#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

// Pure presentation decisions. These helpers never mutate gameplay or save data.
namespace DWLootVisualRules
{
enum class ExistingAppearance { Authored, KnownPlaceholder, Missing };
inline bool CanReplace(ExistingAppearance Existing,bool HasAuthoredMaterialOverride,bool HasCatalogMesh)
{
    return Existing==ExistingAppearance::KnownPlaceholder && !HasAuthoredMaterialOverride && HasCatalogMesh;
}
inline bool CanShow(bool Published,bool Hidden,bool Exhausted,bool Collecting,bool Destroying,bool HasAssets)
{
    return Published && !Hidden && !Exhausted && !Collecting && !Destroying && HasAssets;
}
struct FViewCandidate { double DistanceSquared; std::size_t StableOrder; };
enum class LookupStatus { Found, Missing, Ambiguous };
template<typename Container,typename Id,typename GetId>
LookupStatus FindUnique(const Container& Rows,const Id& Wanted,GetId Get,std::size_t& OutIndex)
{
    bool Found=false;std::size_t Index=0;
    for(const auto& Row:Rows)
    {
        if(Get(Row)==Wanted){if(Found)return LookupStatus::Ambiguous;Found=true;OutIndex=Index;}
        ++Index;
    }
    return Found?LookupStatus::Found:LookupStatus::Missing;
}
inline std::vector<std::size_t> Nearest(const std::vector<FViewCandidate>& Candidates,double MaxDistance,std::size_t Cap)
{
    if(!std::isfinite(MaxDistance)||MaxDistance<=0.||Cap==0)return {};
    const double Limit=MaxDistance*MaxDistance;
    if(!std::isfinite(Limit))return {};
    std::vector<FViewCandidate> Eligible;
    for(const auto& C:Candidates)if(std::isfinite(C.DistanceSquared)&&C.DistanceSquared>=0.&&C.DistanceSquared<=Limit)Eligible.push_back(C);
    std::stable_sort(Eligible.begin(),Eligible.end(),[](const FViewCandidate& A,const FViewCandidate& B){return A.DistanceSquared<B.DistanceSquared;});
    std::vector<std::size_t> Out;Out.reserve(std::min(Cap,Eligible.size()));
    for(const auto& C:Eligible){if(Out.size()==Cap)break;Out.push_back(C.StableOrder);}
    return Out;
}
}
