#pragma once
#include "DWResourcePlacementMath.h"
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace DWPlacementGrid
{
enum class EResult { NoHit, OtherGround, Invalid, Steep, TooClose, Accepted };
struct FCounts
{
    int NoHit=0,Other=0,Invalid=0,Steep=0,Near=0,Accepted=0;
    void Record(EResult R)
    {
        switch(R){case EResult::NoHit:++NoHit;break;case EResult::OtherGround:++Other;break;case EResult::Invalid:++Invalid;break;
        case EResult::Steep:++Steep;break;case EResult::TooClose:++Near;break;case EResult::Accepted:++Accepted;break;}
    }
    int Total()const{return NoHit+Other+Invalid+Steep+Near+Accepted;}
};
struct FPoint{double X,Y;};
struct FKey{std::int64_t X,Y;bool operator==(const FKey& Other)const{return X==Other.X&&Y==Other.Y;}};
struct FHash{std::size_t operator()(const FKey& K)const{return std::hash<std::int64_t>{}(K.X)^(std::hash<std::int64_t>{}(K.Y)<<1);}};
class FSpacing
{
public:
    explicit FSpacing(double InDistance):Distance(InDistance){}
    void Add(double X,double Y)
    {
        const FPoint P{X,Y};All.push_back(P);if(CanGrid(P)){Cells[Key(P)].push_back(P);++Indexed;}
    }
    bool IsTooClose(double X,double Y)const
    {
        const FPoint P{X,Y};const auto Near=[this,&P](const FPoint& Q){return !DWResourcePlacementMath::SeparatedXY(P.X,P.Y,Q.X,Q.Y,Distance);};
        if(!CanGrid(P)||Indexed!=All.size()){for(const auto& Q:All)if(Near(Q))return true;return false;}
        const auto K=Key(P);
        for(int XOffset=-1;XOffset<=1;++XOffset)for(int YOffset=-1;YOffset<=1;++YOffset)
        {
            const auto It=Cells.find({K.X+XOffset,K.Y+YOffset});
            if(It!=Cells.end())for(const auto& Q:It->second)if(Near(Q))return true;
        }
        return false;
    }
private:
    bool CanGrid(const FPoint& P)const{return std::isfinite(P.X)&&std::isfinite(P.Y)&&std::isfinite(Distance)&&Distance>=1.&&std::abs(P.X/Distance)<2000000000.&&std::abs(P.Y/Distance)<2000000000.;}
    FKey Key(const FPoint& P)const{return {static_cast<std::int64_t>(std::floor(P.X/Distance)),static_cast<std::int64_t>(std::floor(P.Y/Distance))};}
    double Distance;std::size_t Indexed=0;std::vector<FPoint> All;std::unordered_map<FKey,std::vector<FPoint>,FHash> Cells;
};
}
