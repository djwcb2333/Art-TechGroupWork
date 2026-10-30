#pragma once
#include "CoreMinimal.h"
class UDWSaveGame;

// Operates only on the caller's duplicate in memory. Does not read or write a disk save.
namespace DWRetiredSaveProjection
{
bool ApplyToDuplicate(UDWSaveGame& Copy,FString& Error,int32 MaxInventorySlots);
}
