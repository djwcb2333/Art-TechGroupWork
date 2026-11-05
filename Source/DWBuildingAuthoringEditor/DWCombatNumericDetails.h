#pragma once
#include "CoreMinimal.h"
#include "IDetailCustomization.h"
namespace DWCombatDetails {struct FNumericModel;}
class FDWCombatNumericDetails final : public IDetailCustomization
{
public:
 static TSharedRef<IDetailCustomization> MakeInstance();
 virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
private:
 TSharedPtr<DWCombatDetails::FNumericModel> Model;
};
void RegisterDWCombatNumericDetails();
void UnregisterDWCombatNumericDetails();
