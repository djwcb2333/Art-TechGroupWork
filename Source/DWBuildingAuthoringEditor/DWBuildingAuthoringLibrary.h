#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "DWBuildingDefinition.h"
#include "DWGameplayTypes.h"
#include "DWBuildingAuthoringLibrary.generated.h"

class UDWGameplayConfig;
class ACharacter;

/** Transient form; never a second item database or gameplay asset. */
UCLASS(BlueprintType, Transient)
class DWBUILDINGAUTHORINGEDITOR_API UDWBuildingAuthoringDraft : public UObject
{
    GENERATED_BODY()
public:
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="01 游戏配置", meta=(DisplayName="当前游戏配置（自动读取，只读）")) TObjectPtr<UDWGameplayConfig> Config;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="02 物品与身份", meta=(DisplayName="资源名称（仅英文、数字、下划线）")) FString AssetStem;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="02 物品与身份", meta=(DisplayName="物品 ID")) FName ItemId;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="02 物品与身份", meta=(DisplayName="中文名称")) FText DisplayName;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="02 物品与身份", meta=(DisplayName="英文名称")) FText EnglishDisplayName;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="02 物品与身份", meta=(DisplayName="物品图标（可空，沿用原UI回退）")) TObjectPtr<UTexture2D> Icon;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="02 物品与身份", meta=(DisplayName="明确允许更新已存在的同 ID 物品定义")) bool bUpdateExistingItem = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="03 模型与占地", meta=(DisplayName="模型分件", TitleProperty="PartName")) TArray<FDWBuildingMeshPart> Parts;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="03 模型与占地", meta=(DisplayName="占地半尺寸 XYZ（Z 为半高度）", Units="cm")) FVector FootprintHalfExtent = FVector(80,60,50);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="04 建筑玩法", meta=(DisplayName="建筑提供的工作台身份（可空）")) FName StationId;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="04 建筑玩法", meta=(DisplayName="交互距离", ClampMin="1", Units="cm")) float InteractionRadius = 250.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="04 建筑玩法", meta=(DisplayName="背包堆叠上限", ClampMin="1", ClampMax="999")) int32 StackLimit = 1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="05 放置规则", meta=(DisplayName="最远放置距离", ClampMin="20", Units="cm")) float MaxPlacementDistance = 600.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="05 放置规则", meta=(DisplayName="最大地面坡度", ClampMin="0", ClampMax="60", Units="Degrees")) float MaxSlopeDegrees = 15.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="05 放置规则", meta=(DisplayName="底部支撑允许高差", ClampMin="0", Units="cm")) float MaxSupportHeightDifference = 20.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="06 合成配方", meta=(DisplayName="同时发布制作配方")) bool bCreateRecipe = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="06 合成配方", meta=(DisplayName="配方 ID", EditCondition="bCreateRecipe")) FName RecipeId;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="06 合成配方", meta=(DisplayName="消耗原料（用窗口下拉添加）", EditCondition="bCreateRecipe", TitleProperty="ItemId")) TArray<FDWItemStack> Ingredients;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="06 合成配方", meta=(DisplayName="所需工作台身份（None 表示徒手）", EditCondition="bCreateRecipe", GetOptions="GetRequiredStationOptions")) FName RequiredCraftingStation;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="06 合成配方", meta=(DisplayName="该配方需要酵母形态", EditCondition="bCreateRecipe")) bool bRequiresYeast = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="06 合成配方", meta=(DisplayName="明确允许更新已存在的同 ID 配方", EditCondition="bCreateRecipe")) bool bUpdateExistingRecipe = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="07 尺度预览", meta=(DisplayName="实际玩家 Blueprint 类")) TSoftClassPtr<ACharacter> PlayerReferenceClass;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="08 导入记录", meta=(DisplayName="本草稿导入的资产（只保存这些新包）")) TArray<FString> ImportedAssets;
    UFUNCTION() TArray<FString> GetRequiredStationOptions() const;
};

USTRUCT(BlueprintType)
struct DWBUILDINGAUTHORINGEDITOR_API FDWBuildingAuthoringResult
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) bool bSuccess = false;
    UPROPERTY(BlueprintReadOnly) FString Message;
    UPROPERTY(BlueprintReadOnly) TArray<FString> ChangedPackages;
    UPROPERTY(BlueprintReadOnly) FString BlueprintPath;
    UPROPERTY(BlueprintReadOnly) FString DefinitionPath;
};

UCLASS()
class DWBUILDINGAUTHORINGEDITOR_API UDWBuildingAuthoringLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintPure, Category="DoughWorld|Building Authoring") static UDWGameplayConfig* GetAuthoritativeConfig();
    /** Read-only editor-world facts; never starts play, spawns or changes a world. */
    UFUNCTION(BlueprintCallable, Category="DoughWorld|Diagnostics") static FString InspectCurrentEditorWorldStructure();
    UFUNCTION(BlueprintCallable, Category="DoughWorld|Building Authoring") static UDWBuildingAuthoringDraft* CreateDraft(UDWGameplayConfig* Config);
    UFUNCTION(BlueprintCallable, Category="DoughWorld|Building Authoring") static bool ReadEffectiveDefinitions(UDWGameplayConfig* Config, TArray<FDWItemDefinition>& Items, TArray<FDWRecipeDefinition>& Recipes, FString& Error);
    UFUNCTION(BlueprintCallable, Category="DoughWorld|Building Authoring") static TArray<FString> GetItemOptions(UDWGameplayConfig* Config);
    UFUNCTION(BlueprintCallable, Category="DoughWorld|Building Authoring") static bool SelectExistingItem(UDWBuildingAuthoringDraft* Draft, FName ItemId);
    /** Loads reusable visuals/rules; caller must provide a new unique ItemId/AssetStem before publishing. */
    UFUNCTION(BlueprintCallable, Category="DoughWorld|Building Authoring") static bool LoadDefinitionAsNewDraft(UDWBuildingAuthoringDraft* Draft, UDWBuildingDefinition* Definition);
    UFUNCTION(BlueprintCallable, Category="DoughWorld|Building Authoring") static bool ValidateDraft(UDWBuildingAuthoringDraft* Draft, FString& Report);
    UFUNCTION(BlueprintCallable, Category="DoughWorld|Building Authoring") static FDWBuildingAuthoringResult PublishDraft(UDWBuildingAuthoringDraft* Draft, bool bConfirmed);
    UFUNCTION(BlueprintCallable, Category="DoughWorld|Building Authoring") static FDWBuildingAuthoringResult ImportModels(UDWBuildingAuthoringDraft* Draft, const TArray<FString>& Filenames, bool bShowImportOptions = true);
    UFUNCTION(BlueprintCallable, Category="DoughWorld|Building Authoring") static bool OpenBuildingAuthoringTool();
};
