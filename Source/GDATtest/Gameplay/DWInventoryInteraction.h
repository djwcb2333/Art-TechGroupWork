#pragma once
#include "CoreMinimal.h"
#include "Blueprint/DragDropOperation.h"
#include "Framework/Commands/InputChord.h"
#include "DWInventoryComponent.h"
#include "DWInventoryInteraction.generated.h"

class UDWGameplayWidget;

/** Editor-owned combinations: these are intentionally not written to player key bindings. */
USTRUCT(BlueprintType)
struct FDWInventoryControls
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Inventory Controls") FKey DragButton=EKeys::LeftMouseButton;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Inventory Controls") FInputChord DiscardKey=FInputChord(EKeys::Delete);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Inventory Controls") FInputChord DiscardClick=FInputChord(EKeys::LeftMouseButton,false,true,false,false);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Inventory Controls") FInputChord QuickStackClick=FInputChord(EKeys::LeftMouseButton,true,false,false,false);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Inventory Controls") FInputChord SplitOneClick=FInputChord(EKeys::RightMouseButton);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Inventory Controls") FInputChord SplitHalfClick=FInputChord(EKeys::RightMouseButton,true,false,false,false);
};

/** The payload only reserves intent. Actual inventory contents stay untouched until a valid drop/confirmation. */
UCLASS()
class GDATTEST_API UDWInventoryDragOperation : public UDragDropOperation
{
    GENERATED_BODY()
public:
    UPROPERTY(Transient) TWeakObjectPtr<UDWGameplayWidget> SourceScreen;
    UPROPERTY(Transient) TWeakObjectPtr<UDWInventoryComponent> SourceInventory;
    UPROPERTY(Transient) FDWInventorySlotSnapshot Snapshot;
};
