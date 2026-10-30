#pragma once
#include "Blueprint/UserWidget.h"
#include "DWComboOptionWidget.generated.h"

// Concrete GC-safe owner for an individual generated combo-box row.
// Its WidgetTree owns row content; SObjectWidget holds it only while in Slate.
UCLASS(Transient)
class GDATTEST_API UDWComboOptionWidget : public UUserWidget
{
    GENERATED_BODY()
};
