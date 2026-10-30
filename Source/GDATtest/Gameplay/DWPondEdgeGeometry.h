#pragma once
#include <vector>
#include <array>
#include <cmath>
#include <algorithm>
#include <limits>

// Pure display geometry. Input water triangles must cover the validated simple polygon.
// Band is first clipped by signed distance, then by the exact water triangles.
namespace DWPondEdgeGeometry
{
struct P { double X=0,Y=0; };
struct V { P Position; double Distance=0; };
using Tri=std::array<P,3>;
using Face=std::array<V,3>;
inline P Sub(P a,P b){return {a.X-b.X,a.Y-b.Y};}
inline double Cross(P a,P b){return a.X*b.Y-a.Y*b.X;}
inline double Dot(P a,P b){return a.X*b.X+a.Y*b.Y;}
inline V Lerp(V a,V b,double t){return {{a.Position.X+(b.Position.X-a.Position.X)*t,a.Position.Y+(b.Position.Y-a.Position.Y)*t},a.Distance+(b.Distance-a.Distance)*t};}
inline bool Finite(P p){return std::isfinite(p.X)&&std::isfinite(p.Y);}
inline double Distance(const std::vector<P>& polygon,P p)
{
 double best=std::numeric_limits<double>::max();
 for(size_t i=0;i<polygon.size();++i){const P a=polygon[i],b=polygon[(i+1)%polygon.size()],ab=Sub(b,a);const double n=Dot(ab,ab);const double t=n>0?std::clamp(Dot(Sub(p,a),ab)/n,0.,1.):0.;const P q=Sub(p,{a.X+t*ab.X,a.Y+t*ab.Y});best=std::min(best,Dot(q,q));}
 return std::sqrt(best);
}
inline bool Inside(const std::vector<P>& polygon,P p)
{
 bool yes=false;for(size_t i=0,j=polygon.size()-1;i<polygon.size();j=i++)
 {const auto a=polygon[i],b=polygon[j];if((a.Y>p.Y)!=(b.Y>p.Y)&&p.X<(b.X-a.X)*(p.Y-a.Y)/(b.Y-a.Y)+a.X)yes=!yes;}return yes;
}
template<class F> inline std::vector<V> Clip(const std::vector<V>& in,F value)
{
 std::vector<V> out;if(in.empty())return out;V a=in.back();double da=value(a);
 for(V b:in){const double db=value(b);const bool ia=da>=-1e-8,ib=db>=-1e-8;if(ia!=ib){const double d=da-db;if(std::abs(d)>1e-14)out.push_back(Lerp(a,b,std::clamp(da/d,0.,1.)));}if(ib)out.push_back(b);a=b;da=db;}return out;
}
inline bool Build(const std::vector<P>& polygon,const std::vector<Tri>& water,double width,double spacing,int maxCells,size_t maxFaces,std::vector<Face>& out)
{
 out.clear();if(polygon.size()<3||polygon.size()>512||water.empty()||water.size()>512||!std::isfinite(width)||width<=0||!std::isfinite(spacing)||spacing<=0||maxCells<1||maxCells>192||maxFaces<1)return false;
 P lo=polygon[0],hi=lo;for(P p:polygon){if(!Finite(p))return false;lo.X=std::min(lo.X,p.X);lo.Y=std::min(lo.Y,p.Y);hi.X=std::max(hi.X,p.X);hi.Y=std::max(hi.Y,p.Y);}
 for(const Tri& t:water)for(P p:t)if(!Finite(p))return false;
 const double sx=hi.X-lo.X,sy=hi.Y-lo.Y;if(!std::isfinite(sx)||!std::isfinite(sy)||sx<=0||sy<=0)return false;
 const double step=std::max({spacing,sx/maxCells,sy/maxCells});const int nx=std::clamp(static_cast<int>(std::ceil(sx/step)),1,maxCells),ny=std::clamp(static_cast<int>(std::ceil(sy/step)),1,maxCells);
 std::vector<V> grid;grid.reserve((nx+1)*(ny+1));
 for(int y=0;y<=ny;++y)for(int x=0;x<=nx;++x){P p{lo.X+sx*x/nx,lo.Y+sy*y/ny};double d=Distance(polygon,p);grid.push_back({p,Inside(polygon,p)?-d:d});}
 auto Add=[&](V a,V b,V c)->bool
 {
  // Outer clipping is ONLY against exact water triangles. An interpolated SDF
  // zero contour would shrink concave shoreline corners and leave visible gaps.
  std::vector<V> band=Clip(std::vector<V>{a,b,c},[&](V v){return v.Distance+width;});if(band.size()<3)return true;
  for(const Tri& t:water)
  {
   const double tx0=std::min({t[0].X,t[1].X,t[2].X}),tx1=std::max({t[0].X,t[1].X,t[2].X}),ty0=std::min({t[0].Y,t[1].Y,t[2].Y}),ty1=std::max({t[0].Y,t[1].Y,t[2].Y});
   if(std::max({a.Position.X,b.Position.X,c.Position.X})<tx0||std::min({a.Position.X,b.Position.X,c.Position.X})>tx1||std::max({a.Position.Y,b.Position.Y,c.Position.Y})<ty0||std::min({a.Position.Y,b.Position.Y,c.Position.Y})>ty1)continue;
   const double area=Cross(Sub(t[1],t[0]),Sub(t[2],t[0]));if(std::abs(area)<1e-10)continue;const double sign=area>0?1.:-1.;auto clipped=band;
   for(int k=0;k<3&&clipped.size()>=3;++k){const P p=t[k],edge=Sub(t[(k+1)%3],p);clipped=Clip(clipped,[&](V v){return sign*Cross(edge,Sub(v.Position,p));});}
   for(size_t k=1;k+1<clipped.size();++k){Face f{clipped[0],clipped[k],clipped[k+1]};const double ar=Cross(Sub(f[1].Position,f[0].Position),Sub(f[2].Position,f[0].Position));if(std::abs(ar)<1e-8)continue;if(ar<0)std::swap(f[1],f[2]);for(V& v:f)v.Distance=-std::min(width,Distance(polygon,v.Position));if(out.size()>=maxFaces){out.clear();return false;}out.push_back(f);}
  }return true;
 };
 for(int y=0;y<ny;++y)for(int x=0;x<nx;++x){const size_t a=y*(nx+1)+x,b=a+1,c=a+nx+1,d=c+1;if(!Add(grid[a],grid[b],grid[d])||!Add(grid[a],grid[d],grid[c]))return false;}
 return !out.empty();
}
}
