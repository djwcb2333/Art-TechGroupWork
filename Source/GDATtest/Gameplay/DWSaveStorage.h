#pragma once
#include "CoreMinimal.h"
class USaveGame;

/** Serialized USaveGame files, with same-directory atomic replacement on Windows. */
namespace DWSaveStorage
{
    GDATTEST_API bool IsValidFolderName(const FString& Name);
    GDATTEST_API FString DocumentsDirectory(const FString& FolderName);
    GDATTEST_API bool WriteBytes(const FString& Path, const TArray<uint8>& Bytes);
    GDATTEST_API bool Write(const FString& Directory, const FString& Slot, USaveGame* Save);
    GDATTEST_API USaveGame* Read(const FString& Directory, const FString& Slot);
    GDATTEST_API bool Exists(const FString& Directory, const FString& Slot);
    GDATTEST_API bool Delete(const FString& Directory, const FString& Slot);
}
