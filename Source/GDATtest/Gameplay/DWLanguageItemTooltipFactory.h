#pragma once
#include "CoreMinimal.h"
#include "Components/Widget.h"
#include "DWItemTooltip.h"
#include "DWLanguageItemTooltipFactory.generated.h"
class UDWGameplayWidget;
class SBox;
struct FDWItemStack;
namespace DWLanguageItemTooltip
{
 /** Pure item/recipe presentation eligibility. Never authorizes language, talents or persistence. */
 bool IsTooltipEnabled(const UDWGameplayWidget* Screen);
 /** Current language owner only: the subsystem retains formal-v5/trial and session identity guards. */
 bool IsLanguageSessionReady(const UDWGameplayWidget* Screen);
 /** Compatibility for existing gameplay callers; deliberately retains language-session semantics. */
 bool IsEnabled(const UDWGameplayWidget* Screen);
 TSharedPtr<FDWLanguageUIContext> MakeContext(UDWGameplayWidget* Screen);
 FDWItemTooltipView Item(UDWGameplayWidget* Screen,const FDWItemStack& Stack);
 FDWItemTooltipView Recipe(UDWGameplayWidget* Screen,FName RecipeId);
 TArray<FDWCraftingConditionView> Conditions(UDWGameplayWidget* Screen,FName RecipeId);
 void AttachItem(UWidget* Target,UDWGameplayWidget* Screen,const FDWItemStack& Stack,const FText& EmptyText=FText::GetEmpty());
 void AttachRecipe(UWidget* Target,UDWGameplayWidget* Screen,FName RecipeId);
}
/** Small native wrapper for the existing Needs Slate projection, not an overlay blocking slot input. */
UCLASS()
class GDATTEST_API UDWLanguageRecipeNeedsWidget : public UWidget
{
 GENERATED_BODY()
public:
 void SetRecipeContext(UDWGameplayWidget* Screen,FName RecipeId);
 virtual void ReleaseSlateResources(bool bReleaseChildren)override;
protected:
 virtual TSharedRef<SWidget> RebuildWidget()override;
private:
 void RefreshNeeds();
 TWeakObjectPtr<UDWGameplayWidget> OwnerScreen;
 FName CurrentRecipe;
 TSharedPtr<SBox> Host;
 FString LastNeedsSignature;
 bool bNeedsSignatureValid=false;
};
