#include "DWSaveStorage.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif

bool DWSaveStorage::IsValidFolderName(const FString& Name)
{
    if (Name.IsEmpty() || Name.Len() > 80 || Name != Name.TrimStartAndEnd() || Name.EndsWith(TEXT("."))) return false;
    for (TCHAR C : Name) if (C < 32 || FString(TEXT("<>:\"/\\|?*")).Contains(FString::Chr(C))) return false;
    const FString Base = FPaths::GetBaseFilename(Name).ToUpper();
    if (Base == TEXT("CON") || Base == TEXT("PRN") || Base == TEXT("AUX") || Base == TEXT("NUL")) return false;
    if (Base.Len() == 4 && (Base.StartsWith(TEXT("COM")) || Base.StartsWith(TEXT("LPT"))) && Base[3] >= '1' && Base[3] <= '9') return false;
    return Name != TEXT(".") && Name != TEXT("..");
}

FString DWSaveStorage::DocumentsDirectory(const FString& FolderName)
{
    const FString Documents = FPlatformProcess::UserDir(); // Windows uses FOLDERID_Documents, including redirected folders.
    if (Documents.IsEmpty()) return FString();
    const FString Name = IsValidFolderName(FolderName) ? FolderName : TEXT("DoughWorld");
    if (Name != FolderName) UE_LOG(LogTemp, Warning, TEXT("DoughWorld: invalid Documents Folder Name; using DoughWorld."));
    return FPaths::ConvertRelativePathToFull(FPaths::Combine(Documents, Name));
}

bool DWSaveStorage::WriteBytes(const FString& Path, const TArray<uint8>& Bytes)
{
    if (Bytes.IsEmpty() || !IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true)) return false;
    const FString Temp = Path + TEXT(".") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".tmp");
    if (!FFileHelper::SaveArrayToFile(Bytes, *Temp)) { IFileManager::Get().Delete(*Temp); return false; }
#if PLATFORM_WINDOWS
    // Never delete/truncate the last good save before the replacement is ready.
    const bool bOK = ::MoveFileExW(*Temp, *Path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    const bool bOK = IFileManager::Get().Move(*Path, *Temp, true, false) == COPY_OK;
#endif
    if (!bOK) IFileManager::Get().Delete(*Temp);
    return bOK;
}

bool DWSaveStorage::Write(const FString& Directory, const FString& Slot, USaveGame* Save)
{
    TArray<uint8> Bytes;
    return !Directory.IsEmpty() && UGameplayStatics::SaveGameToMemory(Save, Bytes) && WriteBytes(FPaths::Combine(Directory, Slot + TEXT(".sav")), Bytes);
}
USaveGame* DWSaveStorage::Read(const FString& Directory, const FString& Slot)
{
    TArray<uint8> Bytes;
    return !Directory.IsEmpty() && FFileHelper::LoadFileToArray(Bytes, *FPaths::Combine(Directory, Slot + TEXT(".sav"))) ? UGameplayStatics::LoadGameFromMemory(Bytes) : nullptr;
}
bool DWSaveStorage::Exists(const FString& Directory, const FString& Slot)
{
    return !Directory.IsEmpty() && IFileManager::Get().FileExists(*FPaths::Combine(Directory, Slot + TEXT(".sav")));
}
bool DWSaveStorage::Delete(const FString& Directory, const FString& Slot)
{
    return !Directory.IsEmpty() && IFileManager::Get().Delete(*FPaths::Combine(Directory, Slot + TEXT(".sav")), false, false);
}
