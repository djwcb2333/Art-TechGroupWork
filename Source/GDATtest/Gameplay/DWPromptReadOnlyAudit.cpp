#include "DWPromptReadOnlyAudit.h"
#if WITH_EDITOR
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Blueprint/WidgetTree.h"
#include "DWInteractionPromptWidget.h"
#include "DWInputGlyphWidget.h"
#include "Components/Widget.h"
#include "Components/PanelWidget.h"
#include "Components/PanelSlot.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/SizeBox.h"
#include "Components/Image.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/SlateRenderer.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
    FString Path(const UObject* O){return O?O->GetPathName():TEXT("");}
    TSharedPtr<FJsonObject> Inspect(const UWidget* Widget)
    {
        auto Out=MakeShared<FJsonObject>();if(!Widget)return Out;
        Out->SetStringField(TEXT("name"),Widget->GetName());Out->SetStringField(TEXT("class"),Path(Widget->GetClass()));
        Out->SetNumberField(TEXT("visibility"),int32(Widget->GetVisibility()));
        Out->SetNumberField(TEXT("clipping"),int32(Widget->GetClipping()));
        Out->SetNumberField(TEXT("render_opacity"),Widget->GetRenderOpacity());
        if(const auto* Box=Cast<USizeBox>(Widget))
        {
            Out->SetBoolField(TEXT("width_override_active"),Box->IsWidthOverride());Out->SetBoolField(TEXT("height_override_active"),Box->IsHeightOverride());
            Out->SetNumberField(TEXT("width_override"),Box->GetWidthOverride());Out->SetNumberField(TEXT("height_override"),Box->GetHeightOverride());
        }
        if(const auto* Image=Cast<UImage>(Widget))
        {
            const FSlateBrush& Brush=Image->GetBrush();
            Out->SetStringField(TEXT("brush_resource"),Path(Brush.GetResourceObject()));
            Out->SetNumberField(TEXT("brush_draw_as"),int32(Brush.DrawAs));
            Out->SetNumberField(TEXT("brush_width"),Brush.ImageSize.X);Out->SetNumberField(TEXT("brush_height"),Brush.ImageSize.Y);
        }
        if(const auto* Slot=Cast<UCanvasPanelSlot>(Widget->Slot))
        {
            const FAnchors A=Slot->GetAnchors();const FMargin O=Slot->GetOffsets();const FVector2D Align=Slot->GetAlignment();
            auto S=MakeShared<FJsonObject>();S->SetNumberField(TEXT("anchor_min_x"),A.Minimum.X);S->SetNumberField(TEXT("anchor_min_y"),A.Minimum.Y);
            S->SetNumberField(TEXT("anchor_max_x"),A.Maximum.X);S->SetNumberField(TEXT("anchor_max_y"),A.Maximum.Y);
            S->SetNumberField(TEXT("offset_left"),O.Left);S->SetNumberField(TEXT("offset_top"),O.Top);S->SetNumberField(TEXT("offset_right"),O.Right);S->SetNumberField(TEXT("offset_bottom"),O.Bottom);
            S->SetNumberField(TEXT("align_x"),Align.X);S->SetNumberField(TEXT("align_y"),Align.Y);Out->SetObjectField(TEXT("canvas_slot"),S);
        }
        TArray<TSharedPtr<FJsonValue>> Children;
        if(const auto* Panel=Cast<UPanelWidget>(Widget))for(int32 I=0;I<Panel->GetChildrenCount();++I)
            Children.Add(MakeShared<FJsonValueObject>(Inspect(Panel->GetChildAt(I))));
        Out->SetArrayField(TEXT("children"),Children);return Out;
    }
    FString Json(const TSharedRef<FJsonObject>& Root)
    {FString Result;TSharedRef<TJsonWriter<>> Writer=TJsonWriterFactory<>::Create(&Result);FJsonSerializer::Serialize(Root,Writer);return Result;}
    TSharedPtr<FJsonObject> SizeData(const UWidget* Widget)
    {
        auto Out=MakeShared<FJsonObject>();if(!Widget)return Out;
        const FVector2D Size=Widget->GetDesiredSize();
        Out->SetNumberField(TEXT("width"),Size.X);Out->SetNumberField(TEXT("height"),Size.Y);
        Out->SetNumberField(TEXT("visibility"),int32(Widget->GetVisibility()));
        Out->SetBoolField(TEXT("cached_slate"),Widget->GetCachedWidget().IsValid());
        return Out;
    }
}
#endif

FString UDWPromptReadOnlyAudit::InspectInteractionPrompt()
{
#if WITH_EDITOR
    auto* BP=LoadObject<UWidgetBlueprint>(nullptr,TEXT("/Game/DoughWorld/UI/Interaction/WBP_DWInteractionPrompt.WBP_DWInteractionPrompt"));
    if(!BP||!BP->WidgetTree)return TEXT("{\"status\":\"missing_tree\"}");
    auto Root=MakeShared<FJsonObject>();Root->SetStringField(TEXT("status"),TEXT("authored_tree_only_not_runtime"));
    Root->SetStringField(TEXT("blueprint"),BP->GetPathName());Root->SetStringField(TEXT("generated_class"),Path(BP->GeneratedClass));
    Root->SetObjectField(TEXT("tree"),Inspect(BP->WidgetTree->RootWidget));
    FString Result;TSharedRef<TJsonWriter<>> Writer=TJsonWriterFactory<>::Create(&Result);FJsonSerializer::Serialize(Root,Writer);return Result;
#else
    return TEXT("{\"status\":\"editor_only\"}");
#endif
}

FString UDWPromptReadOnlyAudit::InspectTransientPromptLayout(UTexture2D* Icon)
{
#if WITH_EDITOR
    auto Result=MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("scope"),TEXT("editor design-time transient subtree prepass; no gameplay world, actor, render or input"));
    auto Fail=[&](const FString& Reason){Result->SetStringField(TEXT("status"),TEXT("not_run"));Result->SetStringField(TEXT("reason"),Reason);return Json(Result);};
    if(!IsValid(Icon)||Icon->GetSizeX()<=0||Icon->GetSizeY()<=0)return Fail(TEXT("A loaded, nonempty texture is required."));
    if(!FSlateApplication::IsInitialized()||!FSlateApplication::Get().GetRenderer())return Fail(TEXT("Slate renderer/font services unavailable; do not start a game to satisfy this check."));
    auto* BP=LoadObject<UWidgetBlueprint>(nullptr,TEXT("/Game/DoughWorld/UI/Interaction/WBP_DWInteractionPrompt.WBP_DWInteractionPrompt"));
    auto* Class=BP?Cast<UWidgetBlueprintGeneratedClass>(BP->GeneratedClass):nullptr;
    if(!BP||!BP->WidgetTree||!Class||!Class->IsChildOf(UDWInteractionPromptWidget::StaticClass()))return Fail(TEXT("Expected authored prompt blueprint/class unavailable."));
    int32 ExtensionCount=0;Class->ForEachExtension([&](auto*){++ExtensionCount;});
    if(ExtensionCount)return Fail(TEXT("Blueprint contains generated extensions; inspect their editor initialization before attempting a transient probe."));
    for(UClass* C=Class;C;C=C->GetSuperClass())if(const auto* B=Cast<UWidgetBlueprintGeneratedClass>(C))
        if(B->Bindings.Num())return Fail(TEXT("Blueprint contains runtime bindings; side-effect-free prepass cannot be guaranteed."));
    bool bUnsafeChild=false;
    BP->WidgetTree->ForEachWidget([&](UWidget* W)
    {
        if(!W)return;
        // No nested user widget or custom Blueprint RebuildWidget is invoked by the probe.
        const FString Package=W->GetClass()->GetOutermost()->GetName();
        if(Package!=TEXT("/Script/UMG"))bUnsafeChild=true;
    });
    if(bUnsafeChild)return Fail(TEXT("Authored tree has a custom/nested widget; inspect it before permitting a Slate prepass."));
    UDWInteractionPromptWidget* Prompt=NewObject<UDWInteractionPromptWidget>(GetTransientPackage(),Class,NAME_None,RF_Transient);
    if(!Prompt)return Fail(TEXT("Could not allocate transient prompt."));
    Prompt->SetDesignerFlags(EWidgetDesignFlags::Designing); // avoids Blueprint Construct and runtime bounce wrappers
    if(!Prompt->Initialize()||Prompt->GetWorld())return Fail(TEXT("Transient prompt unexpectedly has a world or could not initialize."));
    Prompt->InitializePrompt(nullptr); // original C01 layout path; bounce mount intentionally skipped in design-time
    Prompt->SetPromptText(FText::FromString(TEXT("Flour")),FText::FromString(TEXT("Pick up")),FText::FromString(TEXT("F")));
    Prompt->SetPickupIcon(Icon,true);
    auto* Row=Cast<UWidget>(Prompt->GetWidgetFromName(TEXT("C06PromptContentRow")));
    auto* Slot=Cast<USizeBox>(Prompt->GetWidgetFromName(TEXT("C06PickupIconSlot")));
    auto* Image=Cast<UImage>(Prompt->GetWidgetFromName(TEXT("C06PickupIconImage")));
    auto* Placeholder=Cast<UWidget>(Prompt->GetWidgetFromName(TEXT("C06PickupIconPlaceholder")));
    if(!Row||!Slot||!Image||!Placeholder)return Fail(TEXT("Runtime C06 nodes were not mounted; inspect authored binding and bUseC01Presentation."));
    // Rebuild only the native row subtree, never Prompt->TakeWidget()/Blueprint Construct.
    TSharedRef<SWidget> KeepAlive=Row->TakeWidget();Row->ForceLayoutPrepass();Prompt->AdvancePresentation(0.f);Row->ForceLayoutPrepass();
    const FSlateBrush& Brush=Image->GetBrush();const FVector2D ImageSize=Image->GetDesiredSize(),SlotSize=Slot->GetDesiredSize();
    bool bParentsVisible=true;TArray<TSharedPtr<FJsonValue>> Parents;
    for(const UWidget* W=Image;W;W=W->GetParent())
    {
        if(W->GetVisibility()==ESlateVisibility::Collapsed||W->GetVisibility()==ESlateVisibility::Hidden)bParentsVisible=false;
        auto P=MakeShared<FJsonObject>();P->SetStringField(TEXT("name"),W->GetName());
        P->SetNumberField(TEXT("visibility"),int32(W->GetVisibility()));P->SetNumberField(TEXT("clipping"),int32(W->GetClipping()));
        Parents.Add(MakeShared<FJsonValueObject>(P));
    }
    const bool bPass=Brush.GetResourceObject()==Icon&&Brush.DrawAs==ESlateBrushDrawType::Image&&
        FMath::IsFinite(ImageSize.X)&&FMath::IsFinite(ImageSize.Y)&&FMath::IsFinite(SlotSize.X)&&FMath::IsFinite(SlotSize.Y)&&
        ImageSize.X>0.&&ImageSize.Y>0.&&SlotSize.X>0.&&SlotSize.Y>0.&&bParentsVisible&&Placeholder->GetVisibility()==ESlateVisibility::Collapsed&&
        Prompt->C04ResolvedPanelSize.X>0.&&Prompt->C04ResolvedPanelSize.Y>0.;
    Result->SetStringField(TEXT("status"),bPass?TEXT("layout_code_pass_not_rendered"):TEXT("layout_code_failed_not_rendered"));
    Result->SetStringField(TEXT("texture"),Path(Icon));Result->SetStringField(TEXT("brush_resource"),Path(Brush.GetResourceObject()));
    Result->SetNumberField(TEXT("brush_draw_as"),int32(Brush.DrawAs));Result->SetNumberField(TEXT("brush_width"),Brush.ImageSize.X);Result->SetNumberField(TEXT("brush_height"),Brush.ImageSize.Y);
    Result->SetObjectField(TEXT("image"),SizeData(Image));Result->SetObjectField(TEXT("slot"),SizeData(Slot));
    Result->SetObjectField(TEXT("row"),SizeData(Row));Result->SetObjectField(TEXT("placeholder"),SizeData(Placeholder));
    Result->SetArrayField(TEXT("parent_chain"),Parents);Result->SetNumberField(TEXT("slot_width_override"),Slot->GetWidthOverride());
    Result->SetNumberField(TEXT("slot_height_override"),Slot->GetHeightOverride());
    Result->SetBoolField(TEXT("parents_visible"),bParentsVisible);Result->SetNumberField(TEXT("panel_width"),Prompt->C04ResolvedPanelSize.X);
    Result->SetNumberField(TEXT("panel_height"),Prompt->C04ResolvedPanelSize.Y);
    Result->SetStringField(TEXT("limitation"),TEXT("Design-time prepass skips native initialization, runtime bounce/glow wrappers, player context, widget-component clipping and rendering."));
    return Json(Result);
#else
    return TEXT("{\"status\":\"editor_only\"}");
#endif
}
