#pragma once
#include <vector>
#include <cmath>
#include <algorithm>
#include <cstdint>
namespace DWMapPondGeometry
{
struct Point {double X,Y;};
inline bool Inside(const std::vector<Point>& P,double X,double Y)
{
 if(P.size()<3||P.size()>512||!std::isfinite(X)||!std::isfinite(Y))return false;
 bool Result=false;
 for(size_t I=0,J=P.size()-1;I<P.size();J=I++)
 {const auto A=P[I],B=P[J];if(!std::isfinite(A.X)||!std::isfinite(A.Y))return false;
  if((A.Y>Y)!=(B.Y>Y)&&X<(B.X-A.X)*(Y-A.Y)/(B.Y-A.Y)+A.X)Result=!Result;}
 return Result;
}
inline void AddCoverage(const std::vector<Point>& P,int N,std::vector<uint8_t>& Out)
{
 if(N<32||N>1024||Out.size()!=size_t(N)*N||P.size()<3||P.size()>512)return;
 double X0=1,Y0=1,X1=0,Y1=0;
 for(const auto V:P){if(!std::isfinite(V.X)||!std::isfinite(V.Y))return;X0=std::min(X0,V.X);Y0=std::min(Y0,V.Y);X1=std::max(X1,V.X);Y1=std::max(Y1,V.Y);}
 if(X1<0||Y1<0||X0>1||Y0>1)return;
 int A=std::clamp(int(std::floor(std::clamp(X0,0.,1.)*N)),0,N-1),B=std::clamp(int(std::ceil(std::clamp(X1,0.,1.)*N)),0,N-1);
 int C=std::clamp(int(std::floor(std::clamp(Y0,0.,1.)*N)),0,N-1),D=std::clamp(int(std::ceil(std::clamp(Y1,0.,1.)*N)),0,N-1);
 for(int Y=C;Y<=D;++Y)for(int X=A;X<=B;++X)if(Inside(P,(X+.5)/N,(Y+.5)/N))Out[Y*N+X]=255;
}
inline void FillRow(const std::vector<Point>& P,int N,int Row,uint8_t* Bytes)
{
 if(!Bytes||N<32||N>1024||Row<0||Row>=N||P.size()<3||P.size()>512)return;
 const double Y=(Row+.5)/N;std::vector<double> Hits;Hits.reserve(P.size());
 for(size_t A=0,B=P.size()-1;A<P.size();B=A++)
 {const auto X=P[A],Z=P[B];if(!std::isfinite(X.X)||!std::isfinite(X.Y))return;if((X.Y>Y)!=(Z.Y>Y))Hits.push_back(X.X+(Z.X-X.X)*(Y-X.Y)/(Z.Y-X.Y));}
 std::sort(Hits.begin(),Hits.end());
 for(size_t K=0;K+1<Hits.size();K+=2)
 {
  if(Hits[K+1]<0.||Hits[K]>1.)continue;
  int L=std::clamp(int(std::ceil(std::clamp(Hits[K],0.,1.)*N-.5)),0,N);
  int R=std::clamp(int(std::ceil(std::clamp(Hits[K+1],0.,1.)*N-.5))-1,-1,N-1);
  if(R>=L)std::fill(Bytes+Row*N+L,Bytes+Row*N+R+1,uint8_t(255));
 }
}
inline double VisibleWater(double Coverage,double Explore,double Opacity)
{return std::isfinite(Coverage)&&std::isfinite(Explore)&&std::isfinite(Opacity)&&Explore>=.9999?std::clamp(Coverage,0.,1.)*std::clamp(Opacity,0.,1.):0.;}
}
