#pragma once

// Engine-independent identity gate shared with the pure regression test.
namespace DWPlacementGuard
{
inline bool SameWritableLevel(const void* OldWorld,const void* OldLevel,const void* FreshWorld,const void* FreshLevel,bool OldWritable,bool FreshWritable)
{
    return OldWorld&&OldLevel&&FreshWorld&&FreshLevel&&OldWritable&&FreshWritable&&OldWorld==FreshWorld&&OldLevel==FreshLevel;
}
}
