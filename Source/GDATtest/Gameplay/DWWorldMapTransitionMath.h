#pragma once
#include <algorithm>
#include <cmath>

// Plain math, independent of Unreal: same snapshot is used for open and close.
namespace DWWorldMapTransitionMath
{
    struct Rect { double X=0, Y=0, Width=0, Height=0, ViewWidth=0, ViewHeight=0; };
    struct Transform { double X=0, Y=0, ScaleX=1, ScaleY=1; };
    inline bool Resolve(const Rect& R,double Width,double Height,double Progress,Transform& Out)
    {
        const double Values[]={R.X,R.Y,R.Width,R.Height,R.ViewWidth,R.ViewHeight,Width,Height,Progress};
        for(double V:Values)if(!std::isfinite(V))return false;
        if(R.Width<=0||R.Height<=0||R.ViewWidth<=0||R.ViewHeight<=0||Width<=0||Height<=0)return false;
        const double SizeX=std::min(R.Width,Width),SizeY=std::min(R.Height,Height);
        const double X=std::clamp(R.X+Width-R.ViewWidth,0.,Width-SizeX);
        const double Y=std::clamp(R.Y+Height-R.ViewHeight,0.,Height-SizeY);
        const double P=std::clamp(Progress,0.,1.);
        Out.X=X*(1.-P);Out.Y=Y*(1.-P);
        Out.ScaleX=SizeX/Width+(1.-SizeX/Width)*P;
        Out.ScaleY=SizeY/Height+(1.-SizeY/Height)*P;
        return true;
    }
}
