#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DWBuildingEffectsComponent.generated.h"
class UStaticMesh; class UStaticMeshComponent; class UInstancedStaticMeshComponent;
class UMaterialInterface; class UMaterialInstanceDynamic; class UDWInteractionPromptComponent;

USTRUCT()
struct FDWBuildingVisualBinding
{
    GENERATED_BODY()
    UPROPERTY(Transient) TWeakObjectPtr<UStaticMeshComponent> Source;
    UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> Outline;
    UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> Bounce;
    UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> Flash;
    bool bOriginalHiddenInGame = false;
    bool bSourceHiddenBorrowed = false;
};

/** Cosmetic only. Never changes source transforms, collision, materials, inventory or saved facts. */
UCLASS(ClassGroup=(DoughWorld), BlueprintType, meta=(BlueprintSpawnableComponent))
class GDATTEST_API UDWBuildingEffectsComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UDWBuildingEffectsComponent();
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Building Effects|Assets") TSoftObjectPtr<UMaterialInterface> OutlineMaterial;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Building Effects|Assets") TSoftObjectPtr<UMaterialInterface> FlashMaterial;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Building Effects|Assets") TSoftObjectPtr<UMaterialInterface> CloudMaterial;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Building Effects|Assets") TSoftObjectPtr<UStaticMesh> CloudMesh;
    /** None includes owner's plain StaticMeshComponents; effect proxies and ISMs are excluded. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Building Effects|Sources") FName SourceComponentTag = NAME_None;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Building Effects|Outline") bool bAutoFollowPrompt = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Building Effects|Outline", meta=(ClampMin="0", Units="cm")) float OutlineWidth = 2.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Building Effects|Outline") FLinearColor OutlineColor = FLinearColor::White;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Building Effects|Construction", meta=(ClampMin="0.05", Units="s")) float BounceDuration = .65f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Building Effects|Construction", meta=(ClampMin="0", Units="cm")) float DropHeight = 28.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Building Effects|Construction", meta=(ClampMin="0", ClampMax="0.5")) float SquashAmount = .20f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Building Effects|Flash", meta=(ClampMin="0", Units="s")) float FlashStart = .13f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Building Effects|Flash", meta=(ClampMin="0.01", Units="s")) float FlashDuration = .26f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Building Effects|Flash", meta=(ClampMin="0")) float FlashStrength = 3.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Building Effects|Flash", meta=(ClampMin="0", Units="cm")) float FlashOffset = .05f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Building Effects|Flash") FLinearColor FlashColor = FLinearColor(1.f, .96f, .82f, 1.f);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Building Effects|Cloud", meta=(ClampMin="0", ClampMax="32")) int32 CloudPuffCount = 9;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Building Effects|Cloud", meta=(ClampMin="0", Units="s")) float CloudStart = .14f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Building Effects|Cloud", meta=(ClampMin="0.05", Units="s")) float CloudDuration = .72f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Building Effects|Cloud", meta=(ClampMin="1", Units="cm")) float CloudPuffRadius = 32.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Building Effects|Cloud", meta=(ClampMin="0", Units="cm")) float CloudTravelDistance = 46.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Building Effects|Cloud") FLinearColor CloudColor = FLinearColor(.94f, .90f, .80f, 1.f);

    /** Explicit post-commit call only. BeginPlay never plays construction. */
    UFUNCTION(BlueprintCallable, Category="Building Effects") bool PlayConstructionEffect();
    /** Explicit demo replay; never called automatically or on restore. */
    UFUNCTION(BlueprintCallable, Category="Building Effects|Preview") bool PreviewConstructionEffect();
    UFUNCTION(BlueprintCallable, Category="Building Effects|Preview") void ResetConstructionEffect(bool bAllowReplay = true);
    UFUNCTION(BlueprintCallable, Category="Building Effects|Preview") void SetHighlightAmount(float Amount, bool bDisableAutoFollow = true);
    UFUNCTION(BlueprintCallable, Category="Building Effects") void RefreshVisualSources();
    /** Only our live source hide may bypass the camera-fade visibility filter. */
    bool IsTemporarilyHiddenSource(const UStaticMeshComponent* Source) const;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Building Effects|Debug") bool bConstructionPlayed = false;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Building Effects|Debug") bool bConstructionActive = false;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Building Effects|Debug") float ActiveElapsed = 0.f;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Building Effects|Debug") float HighlightAlpha = 0.f;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Building Effects|Debug") float CurrentFlashAmount = 0.f;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Building Effects|Debug") int32 ActiveCloudCount = 0;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Building Effects|Debug") int32 SourceMeshCount = 0;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Building Effects|Debug") int32 ConstructionPlayCount = 0;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Building Effects|Debug") int32 MissingAssetCount = 0;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
    virtual void Activate(bool bReset = false) override;
    virtual void Deactivate() override;
protected:
    virtual void BeginPlay() override;
    virtual void OnUnregister() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
private:
    UPROPERTY(Transient) TArray<FDWBuildingVisualBinding> Bindings;
    UPROPERTY(Transient) TWeakObjectPtr<UDWInteractionPromptComponent> FollowPrompt;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> OutlineMID;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> FlashMID;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> CloudMID;
    UPROPERTY(Transient) TObjectPtr<UInstancedStaticMeshComponent> Clouds;
    UPROPERTY(Transient) TObjectPtr<UStaticMesh> LoadedCloudMesh;
    FVector BaseInOwnerSpace = FVector::ZeroVector;
    FVector2D CloudRingRadii = FVector2D(70.f, 55.f);
    float CloudMeshDiameter = 100.f;
    float ManualHighlightAmount = 0.f;
    bool bInitialized = false, bInitializing = false;
    void InitializeVisuals();
    UStaticMeshComponent* MakeProxy(UStaticMeshComponent* Source, UMaterialInterface* OverrideMaterial, bool bCopySurface);
    void CopySurface(UStaticMeshComponent* Source, UStaticMeshComponent* Proxy);
    void ApplyHighlight();
    void UpdateConstruction();
    void FinishConstruction();
    void DestroyAllProxies();
    void CreateClouds();
    bool IsSourceRenderable(const FDWBuildingVisualBinding& Binding) const;
    float GetEffectDuration() const;
};
