#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DWLanguageTypes.h"
#include "DWLanguageCarrier.generated.h"
class UStaticMeshComponent;class UDWInteractionPromptComponent;class UTexture2D;class UMaterialInterface;class ADWPlayerCharacter;
class UStaticMesh;
USTRUCT(BlueprintType) struct FDWLanguageVisualPart
{
 GENERATED_BODY()
 UPROPERTY(EditAnywhere,BlueprintReadWrite) FName PartId;
 UPROPERTY(EditAnywhere,BlueprintReadWrite) TObjectPtr<UStaticMesh> Mesh;
 UPROPERTY(EditAnywhere,BlueprintReadWrite) TArray<TObjectPtr<UMaterialInterface>> Materials;
 UPROPERTY(EditAnywhere,BlueprintReadWrite) FTransform RelativeTransform=FTransform::Identity;
 UPROPERTY(EditAnywhere,BlueprintReadWrite) bool bAttachToMovingRoot=false;
 UPROPERTY(EditAnywhere,BlueprintReadWrite) bool bEnableCollision=false;
};

/** Stable world identity. Looking at this actor never discovers a glyph or awards anything. */
UCLASS(Blueprintable) class GDATTEST_API ADWLanguageCarrier:public AActor
{
 GENERATED_BODY()
public:
 ADWLanguageCarrier();
 virtual void Tick(float DeltaSeconds)override;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TObjectPtr<USceneComponent> SceneRoot;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TObjectPtr<UStaticMeshComponent> CarrierMesh;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TObjectPtr<UDWInteractionPromptComponent> Prompt;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TObjectPtr<USceneComponent> MovingRoot;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Language|Art") TArray<FDWLanguageVisualPart> VisualParts;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Language|Art") FTransform MovingRootLocked=FTransform::Identity;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Language|Art") FTransform MovingRootDecoded=FTransform(FVector(0,0,50));
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Language|Art") FName GlyphGlowParameter=TEXT("GlyphGlow");
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Language|Art",meta=(ClampMin="0")) float GlyphGlow=4.f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Language|Art",meta=(ClampMin="0.01",Units="s")) float DecodeRiseSeconds=1.f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Language|Art",meta=(ClampMin="0",Units="cm")) float DecodedBobAmplitude=4.f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Language|Identity") FName StableId;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Language|Identity") FName DefinitionId;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Language|Identity") FName EntryId;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Language|Identity") FName ClueId;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Language|Identity") EDWLanguageCarrierKind Kind=EDWLanguageCarrierKind::Inscription;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Language|Interaction",meta=(ClampMin="1",Units="cm")) float InteractionRadius=220.f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Language|Interaction",meta=(ClampMin="1",Units="cm")) float HeightTolerance=180.f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Language|Art") TArray<TObjectPtr<UTexture2D>> GlyphTextures;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Language|Art") TObjectPtr<UMaterialInterface> GlyphMaterial;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Language|Art") FVector GlyphOffset=FVector(0,0,62);
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Language|Art") FRotator GlyphRotation=FRotator::ZeroRotator;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Language|Art") bool bGlyphVertical=false;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Language|Art") bool bGlyphAttachToMovingRoot=false;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Language|Art") float GlyphSize=65.f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Language|Art") TObjectPtr<AActor> LinkedGateActor;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Language|Art",meta=(Units="cm")) FVector GateOpenOffset=FVector(0,0,114);
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Language|Text") FText ChineseTitle;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Language|Text") FText EnglishTitle;
 UFUNCTION(BlueprintPure) FName GetContextId()const{return DefinitionId.IsNone()?StableId:DefinitionId;}
 UFUNCTION(BlueprintPure) bool IsInReach(const ADWPlayerCharacter* Player)const;
 UFUNCTION(BlueprintCallable,CallInEditor) void RebuildGlyphDisplay();
 UFUNCTION(BlueprintCallable,CallInEditor) void RebuildVisualParts();
 UFUNCTION(BlueprintCallable) void RefreshCommittedPresentation();
protected:
 virtual void BeginPlay()override;
 virtual void OnConstruction(const FTransform& Transform)override;
 virtual void EndPlay(const EEndPlayReason::Type Reason)override;
 UFUNCTION() void HandleProgressChanged();
private:
 UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> GlyphPlanes;
 UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> BuiltVisualParts;
 FTransform ClosedGateTransform;
 bool bGateCaptured=false;
 bool bDecoded=false;
 float DecodeAlpha=0.f;
};
