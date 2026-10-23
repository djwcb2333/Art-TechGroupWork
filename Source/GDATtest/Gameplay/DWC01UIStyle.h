#pragma once
#include "CoreMinimal.h"
#include "Styling/SlateBrush.h"
class UButton;
class UWidget;
namespace DWC01UI
{
 GDATTEST_API FLinearColor Paper();
 GDATTEST_API FLinearColor Ink();
 GDATTEST_API FLinearColor Muted();
 GDATTEST_API FLinearColor Gold();
 GDATTEST_API FLinearColor Danger();
 GDATTEST_API FSlateBrush Texture(const TCHAR* AssetName,FVector2D Size=FVector2D::ZeroVector);
 GDATTEST_API FSlateBrush Surface(FLinearColor Color,FVector2D Size=FVector2D(180,48));
 /** Execution buttons reuse the original Bounce component; item choices have no motion.
  * Existing Designer Bounce parameters are preserved. Press/Lift source classes stay available. */
 GDATTEST_API void AttachMotion(UButton* Button,bool bItemSlot=false,bool bDuringOwnerInitialize=false);
 /** Removes all automatic movement/shadow from item/recipe/building/quick slots only.
  * The existing page frame's explicit Pulse/entrance remains untouched. */
 GDATTEST_API void DisableSlotMotion(UButton* Button);
 /** Independent white outline; never multiplied by a button's selection tint. */
 GDATTEST_API FSlateBrush QuickSlotOutline(FVector2D Size,float Thickness=3.f);
 /** Same outer contour as QuickSlotOutline; white fill is tinted by the button. */
 GDATTEST_API FSlateBrush QuickSlotFill(FVector2D Size);
 GDATTEST_API void ReleaseMotionTree(UWidget* Widget);
 /** Center plain button labels without replacing their authored content or bindings.
  * Compound item slots retain their deliberately placed quantities and shortcut numbers. */
 GDATTEST_API void CenterButtonLabel(UButton* Button,float LatinOpticalOffsetEm=.15f);
 /** CSS ease, solving x(t) before evaluating y(t), not a linear Bézier parameter. */
 GDATTEST_API float Ease(float Alpha,FVector2D P1=FVector2D(.25,.1),FVector2D P2=FVector2D(.25,1));
}
struct FDWC01ButtonVisual
{
 FLinearColor Base=FLinearColor::White,Current=FLinearColor::White,Start=FLinearColor::White,Target=FLinearColor::White;
 double Started=0.;float Duration=0.f;
 bool bInitialized=false,bSelected=false,bTransparentRest=false,bItemSlot=false,bBrushFill=false;
};
