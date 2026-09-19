#include "DWUIEntryWidgets.h"
#include "DWGameplayWidget.h"
#include "DWGameplayConfig.h"
#include "DWInventoryComponent.h"
#include "DWPlayerCharacter.h"
#include "DWLocalizationLibrary.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"

namespace
{
    FString DWItemName(UDWGameplayWidget* UI,FName Id)
    {const auto* Config=UI?UI->GetGameplayConfig():nullptr;const auto* D=Config?Config->GetItemDefinition(Id):nullptr;return D?UDWLocalizationLibrary::GetItemDisplayName(UI,*D).ToString():Id.ToString();}
    FString DWStackSummary(UDWGameplayWidget* UI,const TArray<FDWItemStack>& Stacks,bool bOwned)
    {
        TArray<FString> Lines;
        for(const auto& S:Stacks)
        {
            FString Line=FText::Format(DWText(UI,TEXT("{0} × {1}"),TEXT("{0} x {1}")),FText::FromString(DWItemName(UI,S.ItemId)),S.Quantity).ToString();
            if(bOwned)Line+=FText::Format(DWText(UI,TEXT("（拥有 {0}）"),TEXT(" (Owned: {0})")),UI&&UI->GetInventory()?UI->GetInventory()->CountItem(S.ItemId):0).ToString();
            Lines.Add(Line);
        }
        return FString::Join(Lines,TEXT(" + "));
    }
}

void UDWInventorySlotWidget::NativeConstruct()
{
    Super::NativeConstruct();
    if(SlotButton){SlotButton->OnClicked.RemoveDynamic(this,&UDWInventorySlotWidget::ClickSlot);SlotButton->OnClicked.AddDynamic(this,&UDWInventorySlotWidget::ClickSlot);}
}
void UDWInventorySlotWidget::ApplyItemData(UDWGameplayWidget* OwnerScreen,int32 Index,const FDWItemStack& InItem,bool bSelected)
{
    Screen=OwnerScreen;SlotIndex=Index;Item=InItem;
    if(Screen)Screen->ApplyWidgetPresentation(this);
    const auto* Config=Screen?Screen->GetGameplayConfig():nullptr;
    const auto* D=Config?Config->GetItemDefinition(Item.ItemId):nullptr;
    const bool bOccupied=!Item.ItemId.IsNone()&&Item.Quantity>0;
    if(ItemNameText)ItemNameText->SetText(bOccupied?(D?UDWLocalizationLibrary::GetItemDisplayName(this,*D):FText::FromName(Item.ItemId)):DWText(this,TEXT("空槽"),TEXT("Empty")));
    if(QuantityText)QuantityText->SetText(FText::FromString(bOccupied?FString::FromInt(Item.Quantity):TEXT("")));
    if(ItemIcon)
    {
        if(D&&D->Icon){ItemIcon->SetBrushFromTexture(D->Icon,false);ItemIcon->SetColorAndOpacity(FLinearColor::White);}
        else
        {
            FSlateBrush Brush;Brush.DrawAs=ESlateBrushDrawType::Box;Brush.TintColor=FSlateColor(FLinearColor::White);Brush.ImageSize=FVector2D(32,32);
            ItemIcon->SetBrush(Brush);ItemIcon->SetColorAndOpacity(bOccupied?(D?D->IconColor:FLinearColor(.85f,.6f,.3f)):FLinearColor(.17f,.14f,.12f));
        }
    }
    if(SlotButton)SlotButton->SetBackgroundColor(bSelected?FLinearColor(.9f,.63f,.25f):FLinearColor(.34f,.27f,.21f));
    OnItemPresentationUpdated(bSelected);
}
void UDWInventorySlotWidget::ClickSlot(){if(Screen)Screen->SelectInventorySlot(SlotIndex);}

void UDWRecipeEntryWidget::NativeConstruct()
{
    Super::NativeConstruct();
    if(CraftButton){CraftButton->OnClicked.RemoveDynamic(this,&UDWRecipeEntryWidget::ClickCraft);CraftButton->OnClicked.AddDynamic(this,&UDWRecipeEntryWidget::ClickCraft);}
}
void UDWRecipeEntryWidget::ApplyRecipeData(UDWGameplayWidget* OwnerScreen,const FDWRecipeDefinition& R)
{
    Screen=OwnerScreen;RecipeId=R.RecipeId;
    if(Screen)Screen->ApplyWidgetPresentation(this);
    const bool bCan=Screen&&Screen->GetInventory()&&Screen->GetPlayer()&&Screen->GetInventory()->CanCraft(RecipeId,Screen->GetPlayer()->IsYeastForm());
    if(RecipeNameText)RecipeNameText->SetText(UDWLocalizationLibrary::GetRecipeDisplayName(this,R));
    if(IngredientsText)IngredientsText->SetText(FText::FromString(DWText(this,TEXT("需要："),TEXT("Needs: ")).ToString()+DWStackSummary(Screen,R.Inputs,true)));
    if(OutputsText)OutputsText->SetText(FText::FromString(DWText(this,TEXT("获得："),TEXT("Makes: ")).ToString()+DWStackSummary(Screen,R.Outputs,false)));
    if(RequirementText)RequirementText->SetText(R.bRequiresYeast?DWText(this,TEXT("仅酵母形态可制作"),TEXT("Requires Yeast form")):DWText(this,TEXT("任意形态可制作"),TEXT("Any form can craft")));
    if(CraftButton)CraftButton->SetIsEnabled(bCan);
    if(CraftButtonText)CraftButtonText->SetText(bCan?DWText(this,TEXT("制作一份"),TEXT("Craft one")):DWText(this,TEXT("材料 / 形态 / 背包空间不足"),TEXT("Check materials, form and bag space")));
    OnRecipePresentationUpdated(bCan);
}
void UDWRecipeEntryWidget::ClickCraft(){if(Screen){Screen->PlayClick();Screen->CraftRecipe(RecipeId);}}

void UDWSaveSlotWidget::NativeConstruct()
{
    Super::NativeConstruct();
    if(LoadButton){LoadButton->OnClicked.RemoveDynamic(this,&UDWSaveSlotWidget::ClickLoad);LoadButton->OnClicked.AddDynamic(this,&UDWSaveSlotWidget::ClickLoad);}
    if(NewButton){NewButton->OnClicked.RemoveDynamic(this,&UDWSaveSlotWidget::ClickNew);NewButton->OnClicked.AddDynamic(this,&UDWSaveSlotWidget::ClickNew);}
    if(DeleteButton){DeleteButton->OnClicked.RemoveDynamic(this,&UDWSaveSlotWidget::ClickDelete);DeleteButton->OnClicked.AddDynamic(this,&UDWSaveSlotWidget::ClickDelete);}
    if(CancelDeleteButton){CancelDeleteButton->OnClicked.RemoveDynamic(this,&UDWSaveSlotWidget::ClickCancelDelete);CancelDeleteButton->OnClicked.AddDynamic(this,&UDWSaveSlotWidget::ClickCancelDelete);}
}
void UDWSaveSlotWidget::ApplySaveData(UDWGameplayWidget* OwnerScreen,const FDWSlotSummary& InSummary)
{
    const bool bSameSlot=Screen==OwnerScreen&&Summary.SlotIndex==InSummary.SlotIndex&&Summary.bExists==InSummary.bExists;
    Screen=OwnerScreen;Summary=InSummary;if(!bSameSlot)bConfirmDelete=false;
    if(Screen)Screen->ApplyWidgetPresentation(this);
    FText DisplayName=FText::FromString(Summary.DisplayName);
    // Only translate the exact automatically generated name; preserve user names verbatim.
    const FString DefaultChinese=FString::Printf(TEXT("存档 %d"),Summary.SlotIndex+1);
    const FString DefaultEnglish=FString::Printf(TEXT("Save %d"),Summary.SlotIndex+1);
    if(Summary.DisplayName==DefaultChinese||Summary.DisplayName==DefaultEnglish)
        DisplayName=FText::Format(DWText(this,TEXT("存档 {0}"),TEXT("Save {0}")),Summary.SlotIndex+1);
    if(SlotNameText)SlotNameText->SetText(Summary.bExists?DisplayName:FText::Format(DWText(this,TEXT("存档 {0} · 空槽位"),TEXT("Save {0} - Empty")),Summary.SlotIndex+1));
    if(SlotDateText)SlotDateText->SetText(Summary.bExists?FText::FromString(Summary.Timestamp.ToString(TEXT("%Y-%m-%d  %H:%M"))):DWText(this,TEXT("开始一段新的冒险"),TEXT("Begin a new adventure")));
    if(LoadButton)LoadButton->SetVisibility(Summary.bExists?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
    if(DeleteButton)DeleteButton->SetVisibility(Summary.bExists?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
    if(NewButton)NewButton->SetVisibility(Summary.bExists?ESlateVisibility::Collapsed:ESlateVisibility::Visible);
    for(UButton* B:{LoadButton.Get(),NewButton.Get(),DeleteButton.Get(),CancelDeleteButton.Get()})
        if(B)if(auto* T=Cast<UTextBlock>(B->GetContent()))T->SetAutoWrapText(false);
    RefreshDeleteState();
}
void UDWSaveSlotWidget::RefreshDeleteState()
{
    if(DeleteButtonText)DeleteButtonText->SetAutoWrapText(false);
    if(DeleteButtonText)DeleteButtonText->SetText(bConfirmDelete?DWText(this,TEXT("确认永久删除"),TEXT("Confirm delete")):DWText(this,TEXT("删除"),TEXT("Delete")));
    if(CancelDeleteButton)CancelDeleteButton->SetVisibility(bConfirmDelete?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
    OnSavePresentationUpdated(bConfirmDelete);
}
void UDWSaveSlotWidget::ClickLoad(){if(Screen){Screen->PlayClick();Screen->LoadSlot(Summary.SlotIndex);}}
void UDWSaveSlotWidget::ClickNew(){if(Screen){Screen->PlayClick();Screen->NewSlot(Summary.SlotIndex);}}
void UDWSaveSlotWidget::ClickDelete(){if(Screen)Screen->PlayClick();if(!bConfirmDelete){bConfirmDelete=true;RefreshDeleteState();return;}if(Screen)Screen->DeleteSlot(Summary.SlotIndex);}
void UDWSaveSlotWidget::ClickCancelDelete(){if(Screen)Screen->PlayClick();bConfirmDelete=false;RefreshDeleteState();}
