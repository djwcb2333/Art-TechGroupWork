#pragma once
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "DWCharacterPreview.generated.h"
class AActor;class UAnimSequenceBase;class UImage;class UMaterialInstanceDynamic;class USceneCaptureComponent2D;class USkeletalMeshComponent;class UTextureRenderTarget2D;

// External candidate only. Widget owns this object. Optional idle must use exactly the source skeleton.
UCLASS(Transient)
class GDATTEST_API UDWCharacterPreview : public UObject
{
 GENERATED_BODY()
public:
 bool Start(USkeletalMeshComponent* Source,UImage* Output,UAnimSequenceBase* OptionalIdle=nullptr);
 bool IsActiveFor(const USkeletalMeshComponent* Source) const;
 // The owner must call this while the character page is visible, including when gameplay is paused.
 void Refresh();
 // Call before owner destructs, changes page/world, or source mesh is replaced.
 void Stop();
private:
 UPROPERTY(Transient) TObjectPtr<AActor> Host;
 UPROPERTY(Transient) TObjectPtr<USkeletalMeshComponent> Mesh;
 UPROPERTY(Transient) TObjectPtr<USceneCaptureComponent2D> Capture;
 UPROPERTY(Transient) TObjectPtr<UTextureRenderTarget2D> Target;
 UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> PreviewMaterial;
 UPROPERTY(Transient) TObjectPtr<UAnimSequenceBase> PreviewIdle;
 TWeakObjectPtr<USkeletalMeshComponent> SourceMesh;
 TWeakObjectPtr<UImage> OutputImage;
 double LastCaptureSeconds=0.;
 double PreviewIdleStartSeconds=0.;
};
