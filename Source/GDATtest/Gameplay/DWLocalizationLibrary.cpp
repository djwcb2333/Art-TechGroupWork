#include "DWLocalizationLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/GameInstance.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

namespace
{
    FString PreferencesPath(){return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("Config/DoughWorldPreferences.ini"));}
    UDWLocalizationSubsystem* Resolve(const UObject* Context)
    {
        if(!Context||!Context->GetWorld())return nullptr;
        UGameInstance* GI=UGameplayStatics::GetGameInstance(Context);return GI?GI->GetSubsystem<UDWLocalizationSubsystem>():nullptr;
    }
}
void UDWLocalizationSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);FString Code;
    FConfigFile Preferences;Preferences.Read(PreferencesPath());Preferences.GetString(TEXT("Language"),TEXT("Code"),Code);
    Language=Code==TEXT("zh-Hans")?EDWGameLanguage::SimplifiedChinese:EDWGameLanguage::English;
}
void UDWLocalizationSubsystem::SetLanguage(EDWGameLanguage NewLanguage)
{
    if(NewLanguage!=EDWGameLanguage::SimplifiedChinese)NewLanguage=EDWGameLanguage::English;
    if(Language==NewLanguage)return;
    Language=NewLanguage;++Revision;
    const FString File=PreferencesPath();IFileManager::Get().MakeDirectory(*FPaths::GetPath(File),true);
    // Standalone preferences are not a layered engine config branch. Write this
    // owned file directly so UE's branch save restrictions cannot drop it.
    FConfigFile Preferences;Preferences.Read(File);Preferences.bCanSaveAllSections=true;
    Preferences.SetString(TEXT("Language"),TEXT("Code"),Language==EDWGameLanguage::English?TEXT("en"):TEXT("zh-Hans"));
    if(!Preferences.Write(File))UE_LOG(LogTemp,Warning,TEXT("Could not save Dough World language preferences: %s"),*File);
    OnLanguageChanged.Broadcast(Language);
}
EDWGameLanguage UDWLocalizationLibrary::GetLanguage(const UObject* Context){const auto* S=Resolve(Context);return S?S->Language:EDWGameLanguage::English;}
void UDWLocalizationLibrary::SetLanguage(const UObject* Context,EDWGameLanguage Language){if(auto* S=Resolve(Context))S->SetLanguage(Language);}
int32 UDWLocalizationLibrary::GetRevision(const UObject* Context){const auto* S=Resolve(Context);return S?S->Revision:0;}
FText UDWLocalizationLibrary::TranslateLabel(const UObject* Context,const FText& Authored)
{
    if(GetLanguage(Context)!=EDWGameLanguage::English)return Authored;
    static const TMap<FString,FString> EnglishLabels={
#include "DWLocalizationCatalog.inl"
    };
    if(const FString* Value=EnglishLabels.Find(Authored.ToString()))return FText::FromString(*Value);
    return Authored;
}
FText UDWLocalizationLibrary::GetItemDisplayName(const UObject* Context,const FDWItemDefinition& Item)
{
    if(GetLanguage(Context)!=EDWGameLanguage::English)return Item.DisplayName;
    if(!Item.EnglishDisplayName.IsEmpty())return Item.EnglishDisplayName;
    return FText::FromName(Item.ItemId);
}
FText UDWLocalizationLibrary::GetRecipeDisplayName(const UObject* Context,const FDWRecipeDefinition& Recipe)
{
    if(GetLanguage(Context)!=EDWGameLanguage::English)return Recipe.DisplayName;
    if(!Recipe.EnglishDisplayName.IsEmpty())return Recipe.EnglishDisplayName;
    if(Recipe.RecipeId==TEXT("CraftDough"))return FText::FromString(TEXT("Dough"));
    if(Recipe.RecipeId==TEXT("CraftAlcohol"))return FText::FromString(TEXT("Alcohol"));
    return FText::FromName(Recipe.RecipeId);
}
