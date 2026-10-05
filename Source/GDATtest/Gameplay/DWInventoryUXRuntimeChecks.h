#pragma once
#if WITH_DEV_AUTOMATION_TESTS
#include "DWGameplayWidget.h"
#include "DWGameplayHUD.h"
#include "DWPlayerCharacter.h"
#include "DWInventoryComponent.h"
#include "DWGameplayConfig.h"
#include "DWUIEntryWidgets.h"
#include "DWInventoryInteraction.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/UniformGridPanel.h"
#include "Components/TextBlock.h"
#include "Input/DragAndDrop.h"
#include "Widgets/SWidget.h"

// Isolated QA only. These are synthesized Slate widget events, not operating-system input or a full hit-test-route test.
// Caller must have opened the actual Inventory page at least one rendered frame earlier.
namespace DWInventoryRuntimeQA
{
    inline FPointerEvent Pointer(const FKey& Key,const FVector2D& Position,bool bShift=false,bool bCtrl=false)
    {
        TSet<FKey> Pressed;Pressed.Add(Key);
        return FPointerEvent(0,Position,Position,Pressed,Key,0,FModifierKeysState(bShift,false,bCtrl,false,false,false,false,false,false));
    }
    inline UDWInventorySlotWidget* Slot(UDWGameplayWidget* Widget,int32 Index)
    {
        UUniformGridPanel* Grid=Widget?Cast<UUniformGridPanel>(Widget->GetWidgetFromName(TEXT("InventoryGrid"))):nullptr;
        return Grid?Cast<UDWInventorySlotWidget>(Grid->GetChildAt(Index)):nullptr;
    }
    inline bool Click(UDWGameplayWidget* Widget,int32 Index,const FKey& Key,bool bShift=false,bool bCtrl=false)
    {
        UDWInventorySlotWidget* Entry=Slot(Widget,Index);if(!Entry)return false;
        const FGeometry& G=Entry->GetTickSpaceGeometry();const FVector2D Position=G.LocalToAbsolute(G.GetLocalSize()*.5f);
        return Entry->TakeWidget()->OnPreviewMouseButtonDown(G,Pointer(Key,Position,bShift,bCtrl)).IsEventHandled();
    }
    inline TSharedPtr<FDragDropOperation> BeginDrag(UDWGameplayWidget* Widget,int32 Index)
    {
        UDWInventorySlotWidget* Entry=Slot(Widget,Index);if(!Entry)return nullptr;
        const FGeometry& G=Entry->GetTickSpaceGeometry();const FVector2D Position=G.LocalToAbsolute(G.GetLocalSize()*.5f);
        const FPointerEvent Event=Pointer(Widget->InventoryControls.DragButton,Position);
        Entry->TakeWidget()->OnPreviewMouseButtonDown(G,Event);
        return Entry->TakeWidget()->OnDragDetected(G,Event).GetDragDropContent();
    }
    inline void Seed(UDWGameplayWidget* Widget,UDWInventoryComponent* Bag)
    {
        Widget->CancelInventoryDiscard();
        Bag->SetSlots({FDWItemStack(TEXT("Water"),16),FDWItemStack(TEXT("Flour"),3),FDWItemStack(TEXT("Water"),4)});
        Widget->RefreshFromHUD(true);
    }
    inline bool PopupLayoutMatches(UDWGameplayWidget* Widget,const FVector2D& ScreenPoint,FString& Detail)
    {
        UWidget* Popup=Widget?Widget->GetWidgetFromName(TEXT("InventoryDiscardModal")):nullptr;
        UCanvasPanelSlot* Slot=Popup?Cast<UCanvasPanelSlot>(Popup->Slot):nullptr;
        UCanvasPanel* Root=Slot?Cast<UCanvasPanel>(Popup->GetParent()):nullptr;
        if(!Slot||!Root){Detail=TEXT("Popup or direct root Canvas slot missing");return false;}
        const FGeometry& G=Root->GetTickSpaceGeometry();const FVector2D Bounds=G.GetLocalSize();
        const FVector2D Size=Slot->GetSize(),Position=Slot->GetPosition(),Desired=G.AbsoluteToLocal(ScreenPoint)+FVector2D(8,8);
        const FVector2D Expected(FMath::Clamp(Desired.X,8.,FMath::Max(8.,Bounds.X-Size.X-8.)),FMath::Clamp(Desired.Y,8.,FMath::Max(8.,Bounds.Y-Size.Y-8.)));
        const FAnchors Anchors=Slot->GetAnchors();
        Detail=FString::Printf(TEXT("release=%s local=%s position=%s expected=%s size=%s canvas=%s"),*ScreenPoint.ToString(),*G.AbsoluteToLocal(ScreenPoint).ToString(),*Position.ToString(),*Expected.ToString(),*Size.ToString(),*Bounds.ToString());
        return Position.Equals(Expected,.5)&&Size.Equals(FVector2D(FMath::Min(260.,Bounds.X-16.),FMath::Min(128.,Bounds.Y-16.)),.5)&&
            Anchors.Minimum.IsNearlyZero()&&Anchors.Maximum.IsNearlyZero()&&Slot->GetAlignment().IsNearlyZero()&&
            Position.X>=7.5&&Position.Y>=7.5&&Position.X+Size.X<=Bounds.X-7.5&&Position.Y+Size.Y<=Bounds.Y-7.5;
    }
}

static void DWRunInventoryUXRuntimeChecks(ADWPlayerCharacter* Player,UDWGameplayWidget* Widget,TFunctionRef<void(const FString& Name,bool bPassed,const FString& Detail)> Check)
{
    using namespace DWInventoryRuntimeQA;
    UDWInventoryComponent* Bag=Player?Player->GetInventory():nullptr;
    Check(TEXT("inventory.live_widget_and_component"),Widget&&Bag,TEXT("Actual gameplay WBP and player inventory; isolated QA slot only."));
    if(!Widget||!Bag)return;
    UDWGameplayConfig* Config=Widget->GetGameplayConfig();if(!Config)return;
    const auto Total=[Bag](){return FString::Printf(TEXT("Water=%d Flour=%d Slots=%d Revision=%lld"),Bag->CountItem(TEXT("Water")),Bag->CountItem(TEXT("Flour")),Bag->GetSlots().Num(),Bag->GetInventoryRevision());};
    const auto Report=[&](const FString& Name,bool bPassed){Check(Name,bPassed,Total());};
    Seed(Widget,Bag);
    Check(TEXT("inventory.authored_footer_exists"),Widget->GetWidgetFromName(TEXT("InventoryControlsHelpText"))!=nullptr,TEXT("Existing panel footer, content generated from editor-owned combinations."));
    Check(TEXT("inventory.authored_confirmation_exists"),Widget->GetWidgetFromName(TEXT("InventoryDiscardModal"))&&Widget->GetWidgetFromName(TEXT("InventoryDiscardConfirmButton"))&&Widget->GetWidgetFromName(TEXT("InventoryDiscardCancelButton")),TEXT("Existing WBP or compatible runtime fallback; modal begins collapsed."));
    Report(TEXT("inventory.right_click_split_one_event"),Click(Widget,0,EKeys::RightMouseButton)&&Bag->GetSlots()[0].Quantity==15&&Bag->GetSlots().Last().Quantity==1&&Bag->CountItem(TEXT("Water"))==20);
    Report(TEXT("inventory.shift_right_split_half_event"),Click(Widget,0,EKeys::RightMouseButton,true)&&Bag->GetSlots()[0].Quantity==8&&Bag->GetSlots().Last().Quantity==7&&Bag->CountItem(TEXT("Water"))==20);
    Report(TEXT("inventory.shift_left_quick_stack_event"),Click(Widget,0,EKeys::LeftMouseButton,true)&&Bag->GetSlots().Num()==2&&Bag->CountItem(TEXT("Water"))==20&&Bag->CountItem(TEXT("Flour"))==3);
    const FDWInventoryControls PreviousControls=Widget->InventoryControls;
    Widget->InventoryControls.SplitOneClick=FInputChord(EKeys::MiddleMouseButton,false,true,false,false);
    Report(TEXT("inventory.editor_combo_change_controls_behavior"),Click(Widget,0,EKeys::MiddleMouseButton,false,true)&&Bag->GetSlots().Num()==3&&Bag->CountItem(TEXT("Water"))==20);
    Check(TEXT("inventory.help_tracks_editor_combo"),Widget->GetInventoryControlHelp().ToString().Contains(TEXT("Ctrl+")),Widget->GetInventoryControlHelp().ToString());
    Widget->InventoryControls=PreviousControls;

    Seed(Widget,Bag);Click(Widget,0,EKeys::LeftMouseButton,false,true);
    Report(TEXT("inventory.ctrl_click_opens_confirm_without_removal"),Widget->IsInventoryDiscardPending()&&Bag->CountItem(TEXT("Water"))==20);
    if(UDWInventorySlotWidget* Entry=Slot(Widget,0))
    {
        const FGeometry& G=Entry->GetTickSpaceGeometry();FString Detail;
        const bool bAnchored=PopupLayoutMatches(Widget,G.LocalToAbsolute(G.GetLocalSize()*.5f),Detail);
        Check(TEXT("inventory.ctrl_click_popup_uses_pointer_position"),bAnchored,Detail);
    }
    UTextBlock* Message=Cast<UTextBlock>(Widget->GetWidgetFromName(TEXT("InventoryDiscardMessage")));
    Check(TEXT("inventory.popup_has_no_item_message"),!Message||(Message->GetVisibility()==ESlateVisibility::Collapsed&&Message->GetText().IsEmpty()),TEXT("Only title and Yes/No buttons are presented."));
    UBorder* Blocker=Cast<UBorder>(Widget->GetWidgetFromName(TEXT("InventoryDiscardHitBlocker")));
    Check(TEXT("inventory.popup_does_not_dim_background"),Blocker&&Blocker->Background.DrawAs==ESlateBrushDrawType::NoDrawType,TEXT("The full-screen input blocker has no painted brush."));
    if(UButton* Cancel=Cast<UButton>(Widget->GetWidgetFromName(TEXT("InventoryDiscardCancelButton"))))Cancel->OnClicked.Broadcast();
    Report(TEXT("inventory.cancel_button_preserves_items"),!Widget->IsInventoryDiscardPending()&&Bag->CountItem(TEXT("Water"))==20);
    Click(Widget,0,EKeys::LeftMouseButton,false,true);
    if(UButton* Confirm=Cast<UButton>(Widget->GetWidgetFromName(TEXT("InventoryDiscardConfirmButton"))))Confirm->OnClicked.Broadcast();
    Report(TEXT("inventory.confirm_button_discards_exact_stack"),!Widget->IsInventoryDiscardPending()&&Bag->CountItem(TEXT("Water"))==4&&Bag->CountItem(TEXT("Flour"))==3);
    Seed(Widget,Bag);Click(Widget,0,EKeys::LeftMouseButton);
    const FKeyEvent DeleteEvent(EKeys::Delete,FModifierKeysState(),0,false,0,0);
    const bool bDeleteHandled=Widget->TakeWidget()->OnPreviewKeyDown(Widget->GetTickSpaceGeometry(),DeleteEvent).IsEventHandled();
    Report(TEXT("inventory.delete_key_opens_confirmation"),bDeleteHandled&&Widget->IsInventoryDiscardPending()&&Bag->CountItem(TEXT("Water"))==20);
    Widget->TakeWidget()->OnPreviewKeyDown(Widget->GetTickSpaceGeometry(),FKeyEvent(EKeys::Escape,FModifierKeysState(),0,false,0,0));
    Report(TEXT("inventory.escape_cancels_confirmation_only"),!Widget->IsInventoryDiscardPending()&&Widget->GetDWHUD()->GetMenuPage()==EDWMenuPage::Inventory&&Bag->CountItem(TEXT("Water"))==20);
    Widget->SelectInventorySlot(0);Widget->RequestDiscardSelectedInventory();Bag->TryAddItem(TEXT("Flour"),1);Widget->ConfirmInventoryDiscard();
    Report(TEXT("inventory.changed_contents_reject_confirmation"),!Widget->IsInventoryDiscardPending()&&Bag->CountItem(TEXT("Water"))==20&&Bag->CountItem(TEXT("Flour"))==4);

    TArray<FDWItemStack> Full;for(int32 I=0;I<Config->MaxInventorySlots;++I)Full.Emplace(TEXT("Water"),I==0?2:1);
    Bag->SetSlots(Full);Widget->RefreshFromHUD(true);const int32 BeforeFull=Bag->CountItem(TEXT("Water"));const int64 BeforeRevision=Bag->GetInventoryRevision();Click(Widget,0,EKeys::RightMouseButton);
    Report(TEXT("inventory.full_bag_split_has_no_partial_change"),Bag->CountItem(TEXT("Water"))==BeforeFull&&Bag->GetSlots().Num()==Config->MaxInventorySlots&&Bag->GetInventoryRevision()==BeforeRevision);

    Seed(Widget,Bag);
    UBorder* Card=Cast<UBorder>(Widget->GetWidgetFromName(TEXT("InventoryCard")));
    const FGeometry& RootG=Widget->GetTickSpaceGeometry();
    const bool bGeometry=Card&&RootG.GetLocalSize().GetMin()>1&&Card->GetTickSpaceGeometry().GetLocalSize().GetMin()>1;
    Check(TEXT("inventory.rendered_panel_geometry"),bGeometry,FString::Printf(TEXT("Root=%s Card=%s"),*RootG.GetLocalSize().ToString(),Card?*Card->GetTickSpaceGeometry().GetLocalSize().ToString():TEXT("missing")));
    if(bGeometry)
    {
        const FGeometry& CardG=Card->GetTickSpaceGeometry();
        FVector2D Outside=RootG.LocalToAbsolute(FVector2D(8,RootG.GetLocalSize().Y*.5));
        if(CardG.IsUnderLocation(Outside))Outside=RootG.LocalToAbsolute(FVector2D(RootG.GetLocalSize().X-8,RootG.GetLocalSize().Y*.5));
        const bool bOutsideValid=RootG.IsUnderLocation(Outside)&&!CardG.IsUnderLocation(Outside);
        Check(TEXT("inventory.backdrop_drop_area_exists"),bOutsideValid,FString::Printf(TEXT("Screen point %s"),*Outside.ToString()));
        TSharedPtr<FDragDropOperation> Drag=BeginDrag(Widget,0);
        Report(TEXT("inventory.slate_drag_payload_created"),Drag.IsValid()&&Bag->CountItem(TEXT("Water"))==20);
        if(Drag.IsValid())
        {
            const FVector2D Inside=CardG.LocalToAbsolute(CardG.GetLocalSize()*.5);
            Widget->TakeWidget()->OnDrop(RootG,FDragDropEvent(Pointer(EKeys::LeftMouseButton,Inside),Drag));
            Report(TEXT("inventory.panel_interior_drop_does_not_discard"),!Widget->IsInventoryDiscardPending()&&Bag->CountItem(TEXT("Water"))==20);
            const FVector2D OffWindow=RootG.LocalToAbsolute(FVector2D(-100,-100));
            Widget->TakeWidget()->OnDrop(RootG,FDragDropEvent(Pointer(EKeys::LeftMouseButton,OffWindow),Drag));
            Report(TEXT("inventory.outside_game_window_drop_does_not_discard"),!Widget->IsInventoryDiscardPending()&&Bag->CountItem(TEXT("Water"))==20);
            if(bOutsideValid)
            {
                Widget->TakeWidget()->OnDrop(RootG,FDragDropEvent(Pointer(EKeys::LeftMouseButton,Outside),Drag));
                Report(TEXT("inventory.backdrop_drop_requests_confirm"),Widget->IsInventoryDiscardPending()&&Bag->CountItem(TEXT("Water"))==20);
                FString PositionDetail;const bool bPositioned=PopupLayoutMatches(Widget,Outside,PositionDetail);
                Check(TEXT("inventory.drop_popup_anchored_at_release"),bPositioned,PositionDetail);
                Widget->CancelInventoryDiscard();
                Report(TEXT("inventory.drag_confirmation_cancel_preserves_items"),!Widget->IsInventoryDiscardPending()&&Bag->CountItem(TEXT("Water"))==20);
                const FVector2D Edge=RootG.LocalToAbsolute(RootG.GetLocalSize()-FVector2D(4,4));
                Widget->TakeWidget()->OnDrop(RootG,FDragDropEvent(Pointer(EKeys::LeftMouseButton,Edge),Drag));
                FString EdgeDetail;const bool bEdgePositioned=PopupLayoutMatches(Widget,Edge,EdgeDetail);
                Check(TEXT("inventory.drop_popup_clamped_at_bottom_right"),Widget->IsInventoryDiscardPending()&&bEdgePositioned,EdgeDetail);
                Widget->CancelInventoryDiscard();
            }
        }
    }

    FDWInventorySlotSnapshot Snapshot;Bag->CaptureSlotSnapshot(0,Snapshot);
    UDragDropOperation* SwapDrag=Widget->CreateInventoryDrag(Snapshot,Slot(Widget,0));
    Report(TEXT("inventory.slot_drop_swaps_different_items"),SwapDrag&&Widget->HandleInventorySlotDrop(1,SwapDrag)&&Bag->GetSlots()[0].ItemId==FName(TEXT("Flour"))&&Bag->CountItem(TEXT("Water"))==20);

    Check(TEXT("crafting.actual_config_gate_defaults_off"),!Config->bRequireTransformationForCrafting,TEXT("Actual gameplay Data Asset switch, not the native test default."));
    const bool PreviousGate=Config->bRequireTransformationForCrafting;
    const FDWRecipeDefinition* FormRecipe=nullptr;for(const FDWRecipeDefinition& Recipe:Config->Recipes)if(Recipe.bRequiresYeast){FormRecipe=&Recipe;break;}
    Check(TEXT("crafting.existing_form_recipe_available_for_toggle_test"),FormRecipe!=nullptr,TEXT("Uses the existing authored recipe and item definitions."));
    if(FormRecipe)
    {
        Bag->SetSlots({});bool bSeed=true;for(const FDWItemStack& Input:FormRecipe->Inputs)bSeed&=Bag->TryAddItem(Input.ItemId,Input.Quantity);
        Config->bRequireTransformationForCrafting=false;
        Check(TEXT("crafting.gate_off_allows_untransformed_preview"),bSeed&&Bag->CanCraft(FormRecipe->RecipeId,false),FormRecipe->RecipeId.ToString());
        Config->bRequireTransformationForCrafting=true;
        Check(TEXT("crafting.gate_on_blocks_untransformed_preview"),!Bag->CanCraft(FormRecipe->RecipeId,false),FormRecipe->RecipeId.ToString());
        Check(TEXT("crafting.gate_on_allows_transformed_preview"),Bag->CanCraft(FormRecipe->RecipeId,true),FormRecipe->RecipeId.ToString());
        Config->bRequireTransformationForCrafting=false;
        Check(TEXT("crafting.gate_off_real_craft_succeeds"),Bag->TryCraft(FormRecipe->RecipeId,false),FormRecipe->RecipeId.ToString());
    }
    Config->bRequireTransformationForCrafting=PreviousGate;Seed(Widget,Bag);
    Check(TEXT("inventory.qa_finished_no_pending_discard"),!Widget->IsInventoryDiscardPending(),TEXT("QA inventory stays visible for screenshot. No save call made by helper."));
}

static bool DWSetupInventoryUXDiscardScreenshotState(UDWGameplayWidget* Widget)
{
    if(!Widget||!Widget->GetInventory())return false;
    DWInventoryRuntimeQA::Seed(Widget,Widget->GetInventory());Widget->SelectInventorySlot(0);return Widget->RequestDiscardSelectedInventory();
}
#endif
