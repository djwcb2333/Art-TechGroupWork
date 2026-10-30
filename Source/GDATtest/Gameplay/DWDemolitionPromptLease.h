#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

// Presentation lease only: does not own work, award items, or authorize destruction.
namespace DWDemolitionPromptLease
{
struct Id
{
    std::uint32_t A=0,B=0,C=0,D=0;
    bool Valid() const{return A||B||C||D;}
    bool operator==(const Id& O)const{return A==O.A&&B==O.B&&C==O.C&&D==O.D;}
};
class Lease
{
public:
    bool Begin(Id Session,double Now)
    {
        if(!Session.Valid()||!std::isfinite(Now)||Active)return false;
        Key=Session;Updated=Now;Progress=0.;Active=true;return true;
    }
    bool Matches(Id Session)const{return Active&&Key==Session;}
    bool Update(Id Session,double Value,double Now,double Timeout)
    {
        if(!Matches(Session)||!Current(Now,Timeout)||!std::isfinite(Value)||Value<0.||Value>1.||Value<Progress)return false;
        Progress=Value;Updated=Now;return true;
    }
    bool Current(double Now,double Timeout)const
    {
        return Active&&std::isfinite(Now)&&std::isfinite(Timeout)&&Timeout>0.&&Now>=Updated&&Now-Updated<=Timeout;
    }
    bool End(Id Session){if(!Active||!(Key==Session))return false;Reset();return true;}
    void Reset(){Active=false;Key={};Progress=0.;Updated=0.;}
    double DisplayProgress()const{return Active?std::min(Progress,.99):0.;}
private:
    Id Key;double Updated=0.,Progress=0.;bool Active=false;
};
}
