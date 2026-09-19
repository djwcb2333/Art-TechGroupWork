#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "DWInteractionPromptStyle.generated.h"
class UFont; class USoundBase;

/** Shared presentation only: editing this asset changes every default interaction prompt. */
UCLASS(BlueprintType)
class GDATTEST_API UDWInteractionPromptStyle : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Typography") TObjectPtr<UFont> TitleFont;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Typography") TObjectPtr<UFont> KeyFont;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Typography",meta=(ClampMin="8",ClampMax="96")) int32 TitleFontSize=32;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Typography",meta=(ClampMin="8",ClampMax="64")) int32 ActionFontSize=20;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Typography",meta=(ClampMin="8",ClampMax="64")) int32 KeyFontSize=23;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Typography",meta=(ClampMin="0",ClampMax="8")) int32 OutlineSize=2;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Colors") FLinearColor TextColor=FLinearColor(1.f,.94f,.77f,1.f);
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Colors") FLinearColor OutlineColor=FLinearColor(.07f,.035f,.018f,1.f);
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Colors") FLinearColor KeycapColor=FLinearColor(.92f,.83f,.64f,1.f);
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Colors") FLinearColor ProgressColor=FLinearColor(.62f,.83f,.42f,1.f);
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Motion",meta=(ClampMin="0.05",Units="s")) float AppearDuration=.42f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Motion",meta=(ClampMin="0.05",Units="s")) float DisappearDuration=.16f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Motion",meta=(ClampMin="0",DisplayName="Float Distance (Pixels)")) float FloatDistance=24.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Motion",meta=(ClampMin="0.1",ClampMax="1")) float InitialScale=.55f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Motion",meta=(ClampMin="0",ClampMax="0.5")) float OvershootAmount=.16f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Motion",meta=(ClampMin="0",ClampMax="15")) float WobbleDegrees=4.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Motion") bool bUseNativeMotion=true;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Audio") TObjectPtr<USoundBase> AppearSound;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Audio",meta=(ClampMin="0",ClampMax="2")) float SoundVolume=.65f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Audio",meta=(ClampMin="0.5",ClampMax="2")) float SoundPitch=1.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Audio",meta=(ClampMin="0.05",Units="s")) float SoundCooldown=.65f;
};
