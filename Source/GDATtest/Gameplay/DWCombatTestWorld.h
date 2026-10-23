#pragma once
#if WITH_DEV_AUTOMATION_TESTS
#include "DWGameInstance.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "Misc/Guid.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "UObject/StrongObjectPtr.h"

// Real project initialization in a transient world. Never uses a user's slot or imports old saves.
// Callers own actor EndPlay, GI Shutdown, and world/context teardown, in that order.
namespace DWCombatTestWorld
{
inline UWorld* Create(TStrongObjectPtr<UDWGameInstance>& GI, const TCHAR* Name)
{
    if (!GEngine) return nullptr;
    const FString Previous = FCommandLine::Get();
    FString Directory;
    if (!FParse::Value(*Previous, TEXT("DWSaveTestDirectory="), Directory))
        Directory = FPaths::ProjectSavedDir() / TEXT("Automation/CombatFixtures");
    Directory = FPaths::ConvertRelativePathToFull(Directory);
    const FString Prefix = TEXT("DW_QA_Fixture_") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT("_");
    const FString Isolated = FString::Printf(TEXT("-DWVerificationSavePrefix=%s -DWSaveTestDirectory=\"%s\" %s"), *Prefix, *Directory, *Previous);
    ON_SCOPE_EXIT { FCommandLine::Set(*Previous); };
    FCommandLine::Set(*Isolated);
    GI.Reset(NewObject<UDWGameInstance>(GEngine));
    GI->InitializeStandalone(MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), Name), GetTransientPackage());
    UWorld* World = GI->GetWorld();
    if (!World || !GI->GetCombatRules() || GI->HasActiveSlot() || GI->GetSaveDirectory() != Directory) return nullptr;
    FURL URL; URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
    World->SetGameMode(URL); World->InitializeActorsForPlay(URL);
    return World;
}
}
#endif
