#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Camera/CameraShakeBase.h"
#include "DWCinematicDemo.h"
#include "Blueprint/UserWidget.h"
#include "Components/CanvasPanelSlot.h"
#include "DWWeaponAnimationSet.h"
#include "DWDialogueSequence.h"
#include "DWOpeningStepCore.h"
#include "DWOpeningPrototype.generated.h"

class ADWOpeningExitVolume;
class UTextBlock;
class UImage;
class UCanvasPanelSlot;
class ADWPlayerController;
class ADWPlayerCharacter;
class ADWEnemyCharacter;
class ACameraActor;
class UCameraComponent;
class UDWCinematicComponent;
class UDWMinimapComponent;
class UAudioComponent;
class USoundBase;
class USoundAttenuation;
class USkeletalMeshComponent;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMesh;
class ADWOpeningPrototype;
class ADWGameplayHUD;
class UDWGameplayWidget;
class UDWWorldEventSubsystem;
class APlayerCameraManager;
class APlayerController;
class UHorizontalBox;
class UFont;
class UDWInputGlyphWidget;
class UDWCinematicSubtitleWidget;
class UDWDialogueSequenceComponent;
class UInputComponent;

UENUM(BlueprintType)
enum class EDWOpeningStage : uint8
{ Waiting, Wake, StretchPrompt, Stretch, Escape, Reveal, Approach, Encounter, Combat, Exit, Title };

/** Text that cross-fades whenever its wanted value changes. Widgets are owned by the overlay's WidgetTree. */
struct FDWOpeningFadeText
{
 UTextBlock* Text=nullptr;
 FString Shown,Wanted;
 FLinearColor WantedColor=FLinearColor::White;
 float Alpha=0,Speed=5;
 void Tick(float Dt);
};

/** Presentation only. The director sets targets every frame; everything eases in real time (hit-stop safe). */
UCLASS()
class GDATTEST_API UDWOpeningOverlay : public UUserWidget
{
 GENERATED_BODY()
public:
 void Advance(float RealDt);
 void SetLetterbox(bool bOn,float Speed=1.6f){LetterboxTarget=bOn?1.f:0.f;LetterboxSpeed=Speed;}
 void SetBlack(float Value);
 void Flash(float Value){FlashAlpha=FMath::Max(FlashAlpha,Value);}
 void Dip(){DipTime=0;}
 void SetSubtitle(const FString& Value,bool bSpoken);
 void SetCenterLine(const FString& Value){Center.Wanted=Value;}
 void SetObjective(const FString& Value){Objective.Wanted=Value;}
 void SetPrompt(const FString& Value){Prompt.Wanted=Value;bGlyphPrompt=false;}
 void ShowMovementPrompt(ADWPlayerController* Controller,bool bStretch);
 void ShowCombatPrompt(ADWPlayerController* Controller,bool bBlock,bool bDash);
 void ShowTitle(const FString& Small,const FString& Big,float Seconds);
 void HideTitle(){if(TitleTime<TitleSeconds)TitleSeconds=FMath::Min(TitleSeconds,TitleTime+1.4f);}
 void SetDialogueHint(bool Active){SkipVisible=Active?1.f:0.f;if(!Active)SkipAlpha=0.f;}
 const FString& GetObjective()const{return Objective.Wanted;}
 /** Game title that burns in: a fire front sweeps across, leaving the letters behind it, embers rising off the edge. */
 void BurnTitle(const FString& Big,const FString& Small,float Seconds,float ChapterGap);
 bool IsBurnTitleDone()const{return BurnSeconds<=0||BurnTime>=BurnSeconds;}
protected:
 virtual void NativeOnInitialized() override;
private:
 UPROPERTY() TObjectPtr<UFont> PromptInputFont;
 UPROPERTY() TObjectPtr<UHorizontalBox> PromptKeyRow;
 UPROPERTY() TArray<TObjectPtr<UDWInputGlyphWidget>> PromptKeyGlyphs;
 UPROPERTY() TArray<TObjectPtr<UTextBlock>> PromptKeyLabels;
 void SetPromptKey(int32 Index,ADWPlayerController* Controller,int32 Action,const FText& Label);
 void HidePromptKeys();
 bool bGlyphPrompt=false;
 UPROPERTY() TObjectPtr<UImage> TopBar;
 UPROPERTY() TObjectPtr<UImage> BottomBar;
 UPROPERTY() TObjectPtr<UImage> BlackImage;
 UPROPERTY() TObjectPtr<UImage> FlashImage;
 UPROPERTY() TObjectPtr<UTextBlock> ObjectiveHeader;
 UPROPERTY() TObjectPtr<UImage> ObjectiveMark;
 UPROPERTY() TObjectPtr<UTextBlock> TitleSmall;
 UPROPERTY() TObjectPtr<UTextBlock> TitleBig;
 UPROPERTY() TObjectPtr<UImage> TitleRule;
 UPROPERTY() TObjectPtr<UTextBlock> SkipText;
 UPROPERTY() TObjectPtr<UDWInputGlyphWidget> NextLineGlyph;
 UPROPERTY() TObjectPtr<UImage> SkipTrack;
 UPROPERTY() TObjectPtr<UImage> SkipFill;
 UPROPERTY() TObjectPtr<UTextBlock> BurnBig;
 UPROPERTY() TObjectPtr<UTextBlock> BurnSmall;
 UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> BurnMaterial;
 UPROPERTY() TArray<TObjectPtr<UImage>> Embers;
 struct FEmber { FVector2D Pos=FVector2D::ZeroVector,Vel=FVector2D::ZeroVector; float Age=0,Life=0,Size=4; };
 TArray<FEmber> EmberState;
 float BurnTime=0,BurnSeconds=0,EmberClock=0;
 void AdvanceBurn(float Dt);
 UCanvasPanelSlot* TopSlot=nullptr;
 UCanvasPanelSlot* BottomSlot=nullptr;
 UCanvasPanelSlot* SkipFillSlot=nullptr;
 FDWOpeningFadeText Subtitle,Center,Objective,Prompt;
 float LetterboxTarget=0,LetterboxSpeed=1.6f,Letterbox=0,BlackAlpha=0,FlashAlpha=0,DipTime=10;
 float ObjectiveAlpha=0,TitleTime=0,TitleSeconds=0,SkipVisible=0,SkipAlpha=0,SkipProgress=0;
};

/** One camera move. Positions are absolute world values resolved when the beat starts. */
struct FDWOpeningShot
{
 enum class EMode:uint8 { Move, Path, Hold, ToGameplay };
 // A pose held relative to the hero: view yaw/pitch, ln(distance), where the hero sits on screen (as view-space tangents), ln(focus).
 // Interpolating these instead of world positions keeps the hero exactly where each key puts it all the way between keys.
 struct FAnchor { FVector View=FVector::ZeroVector,Screen=FVector::ZeroVector; };
 struct FKey { float Time=0; FVector Pos=FVector::ZeroVector,Look=FVector::ZeroVector; float Fov=45; FAnchor Anchor; };
 EMode Mode=EMode::Move;
 bool bAnchored=false;
 float Duration=3,BlendIn=0,Ease=2,FovA=45,FovB=45,Handheld=.12f,Fstop=0;
 // Move: camera A->B, looking LookA->LookB. Path: one continuous take through timed keys (Catmull-Rom).
 FVector A=FVector::ZeroVector,B=FVector::ZeroVector,LookA=FVector::ZeroVector,LookB=FVector::ZeroVector;
 TArray<FKey> Keys;
 TWeakObjectPtr<AActor> Track;
 FDWOpeningShot& Blend(float Seconds){BlendIn=Seconds;return *this;}
 FDWOpeningShot& Dof(float Value){Fstop=Value;return *this;}
 FDWOpeningShot& Shake(float Degrees){Handheld=Degrees;return *this;}
 FDWOpeningShot& Curve(float Exponent){Ease=Exponent;return *this;}
 FDWOpeningShot& Anchored(){bAnchored=true;return *this;}
};

/** Infinite additive shake that reads the director's trauma; works on the cinematic and gameplay view alike. */
UCLASS(Transient,NotBlueprintable)
class GDATTEST_API UDWOpeningTraumaPattern:public UCameraShakePattern
{
 GENERATED_BODY()
public:
 TWeakObjectPtr<ADWOpeningPrototype> Director;
private:
 double Clock=0.;
 virtual void GetShakePatternInfoImpl(FCameraShakeInfo& OutInfo)const override;
 virtual void UpdateShakePatternImpl(const FCameraShakePatternUpdateParams& Params,FCameraShakePatternUpdateResult& OutResult)override;
 virtual bool IsFinishedImpl()const override{return !Director.IsValid();}
};

UCLASS(Transient,NotBlueprintable)
class GDATTEST_API UDWOpeningTraumaShake:public UCameraShakeBase
{
 GENERATED_BODY()
public:
 UDWOpeningTraumaShake(const FObjectInitializer& Init=FObjectInitializer::Get());
};

/** Concrete presentation cues. Reusing the same source asset does not link the editable event slots. */
UENUM(BlueprintType)
enum class EDWOpeningAudioEvent : uint8
{
 WakeRumble UMETA(DisplayName="01 苏醒低鸣"),
 StretchStart UMETA(DisplayName="02 起身拉伸"),
 AmbienceLoop UMETA(DisplayName="03 洞穴环境声"),
 SplashFirst UMETA(DisplayName="04 初次水花"),
 SplashSlip UMETA(DisplayName="05 滑落水花"),
 SplashPush UMETA(DisplayName="06 撑起水花"),
 StretchPush UMETA(DisplayName="07 撑起拉伸"),
 GroundCollapse UMETA(DisplayName="08 地面塌陷"),
 EscapeObjective UMETA(DisplayName="09 逃离提示"),
 GroundTremor UMETA(DisplayName="10 逃离地震"),
 CreatureBite UMETA(DisplayName="11 首次啃食落点"),
 ApproachObjective UMETA(DisplayName="12 靠近提示"),
 LeapWhoosh UMETA(DisplayName="13 敌人跃起"),
 LeapImpact UMETA(DisplayName="14 敌人落地"),
 ExitObjective UMETA(DisplayName="15 离开提示"),
 TitleBurn UMETA(DisplayName="16 标题燃烧"),
 FeedingChew UMETA(DisplayName="17 持续啃食"),
};

USTRUCT(BlueprintType)
struct GDATTEST_API FDWOpeningAudioCue
{
 GENERATED_BODY()
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Opening|Audio",meta=(DisplayName="音效资源 / Sound",ToolTip="替换这一个演出事件的声音。None静音，不改变其他使用相同原声音的事件。"))
 TObjectPtr<USoundBase> Sound;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Opening|Audio",meta=(DisplayName="音量倍率 / Volume Multiplier",ToolTip="乘在该事件原音量上；1保留原演出音量，0静音。",ClampMin="0"))
 float VolumeMultiplier=1.f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Opening|Audio",meta=(DisplayName="音高倍率 / Pitch Multiplier",ToolTip="乘在原音高或原随机音高上；1保留原效果。",ClampMin="0.01"))
 float PitchMultiplier=1.f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Opening|Audio",meta=(DisplayName="启用 / Enabled",ToolTip="关闭只停用此演出事件，不改变演出时长和玩法。"))
 bool bEnabled=true;
};

/** SC0101 presentation director. Formal startup, checkpoint and completion are admitted by the GameInstance. */
UCLASS(Blueprintable)
class GDATTEST_API ADWOpeningPrototype : public AActor
{
 GENERATED_BODY()
public:
 ADWOpeningPrototype();
 virtual void Tick(float DeltaSeconds) override;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Opening") FVector EnemyCenter=FVector(1900,0,100);
 /** Default preserves existing directors; only the approved SC0101 instance selects the Sour BP. */
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Opening|Enemies") TSubclassOf<ADWEnemyCharacter> OpeningEnemyClass;
 /** Underside of the crystal overhang above the pit; the first shot looks up at it. */
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Opening") FVector CrystalFocus=FVector(-60,260,860);
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Opening") FVector PitCenter=FVector::ZeroVector;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Opening",meta=(Units="cm")) float EscapeRadius=580.f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Opening",meta=(Units="cm")) float EncounterDistance=760.f;
 /** Already-owned weapon selected when the player gets control; starter acquisition belongs to the GI transaction. */
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Opening") EDWAnimatedWeapon StartingWeapon=EDWAnimatedWeapon::Spear;
 /** Legacy values retained for assets only; ExitVolume is the sole spatial gate. */
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Opening") float ExitLineX=3330.f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Opening",meta=(ToolTip="旧数据兼容字段，不再控制出口；请编辑 Exit Volume。")) FVector ExitPoint=FVector(4450,0,0);
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Opening|Exit",meta=(ClampMin="1",Units="cm",ToolTip="旧数据兼容字段，不再控制出口；仅使用 Exit Volume 的真实三维盒范围。")) float ExitTriggerRadiusCm=140.f;
 /** Explicit binding: no automatic search or fallback to legacy point/radius. */
 UPROPERTY(EditInstanceOnly,BlueprintReadWrite,Category="Opening|Exit",meta=(DisplayName="出口体积 / Exit Volume",ToolTip="明确指定本关的 DWOpeningExitVolume；缺失或禁用时不触发，不自动选择其他体积。")) TObjectPtr<ADWOpeningExitVolume> ExitVolume;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Opening|Exit",meta=(DisplayName="允许出口 / Enable Exit Trigger")) bool bEnableExitTrigger=true;
 UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="Opening|Exit",meta=(DisplayName="出口状态 / Exit Trigger Status")) FString ExitTriggerStatus=TEXT("Not checked / 尚未检查；可点击 Refresh Exit Trigger Status");
 UFUNCTION(CallInEditor,BlueprintCallable,Category="Opening|Exit") void RefreshExitTriggerStatus();
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Opening|Exit",meta=(ClampMin="0.05",Units="s",ToolTip="到达光区后从透明线性变为全黑。")) float ExitFadeToBlackSeconds=1.2f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Opening|Exit",meta=(ClampMin="0.1",Units="s",ToolTip="全黑且离场对白完成后才显示居中标题；标题沿用原燃烧材质。")) float ExitTitleSeconds=10.f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Opening|Exit",meta=(DisplayName="章节与标题间距 / Chapter Title Gap",ClampMin="0",ToolTip="主标题文字框下缘到章节文字框顶部的距离，单位为界面像素。")) float ChapterTitleGap=40.f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Opening") FString GameTitle=TEXT("BREAD OF THE WILD");
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Opening") EDWOpeningStage Stage=EDWOpeningStage::Waiting;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Opening|Flow",meta=(ClampMin="0",Units="s")) float DeathRetryDelaySeconds=1.5f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Opening|Flow",meta=(ClampMin="0.1",Units="s")) float HandoffRetrySeconds=1.f;
 /** Defaults are authored in the constructor only; placed-instance edits are never overwritten by Waiting. */
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Opening|Audio|Events",meta=(DisplayName="演出音效清单 / Opening Audio Events",ToolTip="按具体演出事件分别替换声音；展开条目调整Sound、音量、音高和启用。默认保留原10个声音及各自时点/空间化。"))
 TMap<EDWOpeningAudioEvent,FDWOpeningAudioCue> OpeningAudioEvents;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Opening|Audio|Music",meta=(DisplayName="开场背景音乐 / Opening BGM",ToolTip="战斗教程开始时播放的独立音乐。开场前段静音，沿用HUD唯一Music播放器及音乐音量设置；None表示整段静音，不回退GameplayBGM。离图后新HUD恢复原选曲。"))
 TObjectPtr<USoundBase> OpeningBGM;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Opening|Audio|Music",meta=(DisplayName="开场音乐音量倍率 / Opening BGM Volume",ToolTip="乘在GameplayConfig的MusicVolume上，并继续受设置界面的Music与Master控制。",ClampMin="0"))
 float OpeningBGMVolumeMultiplier=1.f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Opening|Audio|Music",meta=(DisplayName="开场音乐音高倍率 / Opening BGM Pitch",ToolTip="只影响开场独立背景音乐；1保留资源原音高。",ClampMin="0.01"))
 float OpeningBGMPitchMultiplier=1.f;
 /** Multiplies each line's own post-reveal hold; does not retime camera/action cues. */
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Opening|Dialogue",meta=(DisplayName="逐句停留倍率 / Hold Multiplier",ClampMin="0")) float DialogueHoldMultiplier=1.f;
 UPROPERTY(BlueprintReadWrite,Category="Opening|Legacy Timing") float WakeSequenceSeconds=20.3f;
 UPROPERTY(BlueprintReadWrite,Category="Opening|Legacy Timing") float WakePostDialogueHold=0.f;
 UPROPERTY(BlueprintReadWrite,Category="Opening|Legacy Timing") float RevealShotSeconds=16.f;
 UPROPERTY(BlueprintReadWrite,Category="Opening|Legacy Timing") float RevealPostDialogueHold=0.f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Opening|Timing|Reveal",meta=(DisplayName="发现镜头回位 / Reveal Return Seconds",ClampMin="0",ClampMax="30",Units="s")) float RevealReturnSeconds=2.f;
 UPROPERTY(BlueprintReadWrite,Category="Opening|Legacy Timing") float EncounterSequenceSeconds=10.5f;
 UPROPERTY(BlueprintReadWrite,Category="Opening|Legacy Timing") float EncounterPostDialogueHold=0.f;
 UPROPERTY(BlueprintReadWrite,Category="Opening|Legacy Timing") bool bQuickEncounterAfterDialogue=false;
 UPROPERTY(BlueprintReadWrite,Category="Opening|Legacy Timing") float EncounterQuickTransitionSeconds=.2f;
 // Camera movement lengths are per shot, not a required total director duration.
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Opening|Shot Steps",meta=(DisplayName="苏醒第一镜头 / Wake Intro Shot",ClampMin="0.1",ClampMax="120",Units="s")) float WakeIntroShotSeconds=14.4f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Opening|Shot Steps",meta=(DisplayName="苏醒第二镜头 / Wake Close Shot",ClampMin="0.1",ClampMax="120",Units="s")) float WakeCloseShotSeconds=5.1f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Opening|Shot Steps",meta=(DisplayName="观察镜头 / Reveal Look Shot",ClampMin="0.1",ClampMax="120",Units="s")) float RevealLookShotSeconds=3.4f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Opening|Shot Steps",meta=(DisplayName="沟通起始镜头 / Encounter Intro Shot",ClampMin="0.1",ClampMax="120",Units="s")) float EncounterIntroShotSeconds=1.1f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Opening|Shot Steps",meta=(DisplayName="转身镜头 / Encounter Turn Shot",ClampMin="1.7",ClampMax="120",Units="s")) float EncounterTurnShotSeconds=1.7f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Opening|Shot Steps",meta=(DisplayName="扑击镜头 / Encounter Face Shot",ClampMin="2.9",ClampMax="120",Units="s")) float EncounterFaceShotSeconds=2.9f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Opening|Dialogue",meta=(DisplayName="01 黑屏苏醒 / Wake Dark",ToolTip="逐句编辑Chinese Text、English Text和Hold Seconds；英文留空沿中文回退。空列表跳过这段文字，不跳镜头。")) TArray<FDWDialogueLine> WakeDarkLines;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Opening|Dialogue",meta=(DisplayName="02 苏醒心声 / Wake Thought")) TArray<FDWDialogueLine> WakeThoughtLines;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Opening|Dialogue",meta=(DisplayName="03 地面下沉 / Ground Sinking")) TArray<FDWDialogueLine> SinkThoughtLines;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Opening|Dialogue",meta=(DisplayName="04 观察馊团 / Creature Reveal")) TArray<FDWDialogueLine> RevealThoughtLines;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Opening|Dialogue",meta=(DisplayName="05 尝试交谈 / Encounter Spoken")) TArray<FDWDialogueLine> EncounterSpokenLines;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Opening|Dialogue",meta=(DisplayName="06 离开独白 / Exit Thought")) TArray<FDWDialogueLine> ExitThoughtLines;
 /** Retained as a no-op for old Blueprint references; only subtitles may be skipped. */
 UFUNCTION(BlueprintCallable,Category="Opening",meta=(DeprecatedFunction,DeprecationMessage="Whole-shot skipping is disabled; left click advances subtitles only.")) void SkipCurrentShot();
 float GetShakeAmount()const{return Trauma*Trauma;}
 /** Presentation only, called under the loader's opaque cover after exact pending-opening admission. */
 bool PrepareArrivalCover(APlayerController* InController);
 /** Called under loader input lock only after the pending safe checkpoint is restored. */
 bool PrepareArrivalControl(APlayerController* InController);
 void FlushCinematicHandoff();
 void CancelArrivalCover();
protected:
 virtual void BeginPlay() override;
 virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
 struct FEnemyPose
 {
  FTransform BodyBase;
  FVector Home=FVector::ZeroVector;
  float Phase=0,Yaw=0,TurnFrom=0,TurnTo=0,Sink=0;
  // bFree: the director no longer pins the actor (it has leapt).
  bool bFeeding=true,bFree=false,bDefeatCommitted=false;
 };
 UPROPERTY() TObjectPtr<UDWCinematicComponent> InputGate;
 UPROPERTY() TObjectPtr<UDWOpeningOverlay> Overlay;
 UPROPERTY() TObjectPtr<ACameraActor> ShotCamera;
 UPROPERTY(Transient) TObjectPtr<UDWCinematicSubtitleWidget> OpeningSubtitle;
 UPROPERTY(Transient) TObjectPtr<UDWDialogueSequenceComponent> OpeningDialogue;
 UPROPERTY(Transient) TObjectPtr<UInputComponent> OpeningSubtitleInput;
 TWeakObjectPtr<ADWPlayerController> DialogueLeaseController;
 TSet<FName> StartedDialogueBeats;
 TUniquePtr<DWOpeningSteps::Sequence> OpeningSequence;
 DWOpeningSteps::Step PreparedSequenceStep=DWOpeningSteps::Step::Done;
 DWOpeningSteps::Token ActiveSequenceDialogueToken{DWOpeningSteps::Step::Done,0};
 float ActiveSequenceShotSeconds=0.f;
 bool bSequencePreparedThisFrame=false;
 bool PrepareSequenceStep();
 void CompleteSequenceDialogue(bool bSuccess);
 bool ValidSequenceParameters()const;
 FName TimingActiveBeat;
 TArray<FName> PendingDialogueBeats;
 FAnchorData OriginalSubtitleLayout;
 bool bSubtitleCanvasLayout=false,bSubtitleInputPushed=false,bPresentationKeyOwned=false,bSubtitlePressHeld=false;
 bool bStoppingOpeningDialogue=false,bDialogueFailure=false;
 float InitialOpeningAspect=1.777778f;
 bool QueueDialogueBeat(FName Beat);
 bool StartNextDialogueBeat();
 bool IsOpeningDialogueBusy()const;
 bool UpdateDialogueInputLease();
 void OpeningSubtitlePressed();
 void OpeningSubtitleReleased();
 UFUNCTION() void OpeningDialogueFinished(bool bCompleted);
 UFUNCTION() void CancelOpeningFromCinematic();
 void StopOpeningDialogue();
 void FailOpeningDialogue(const FString& Error);
 void TickOpeningDialogue();
 void RequestOpeningAspect(bool bMovie);
 void UpdateOpeningAspect(float Dt);
 /** Cinematic stand-in for the hero while it floats and gets up: same mesh and materials, but animated only by us. */
 UPROPERTY() TObjectPtr<USkeletalMeshComponent> Double;
 UPROPERTY() TArray<TObjectPtr<ADWEnemyCharacter>> Enemies;
 UPROPERTY() TArray<TObjectPtr<AActor>> Crumbs;
 UPROPERTY() TArray<TObjectPtr<AActor>> FeedingGround;
 UPROPERTY() TArray<TObjectPtr<AActor>> PitFloor;
 UPROPERTY() TObjectPtr<UStaticMesh> RippleMesh;
 UPROPERTY() TObjectPtr<UMaterialInterface> RippleMaterial;
 UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> RippleDiscs;
 UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> RippleMids;
 struct FRipple { float Age=0,Life=1.6f,Radius=120,Strength=1; };
 TArray<FRipple> Ripples;
 UPROPERTY() TObjectPtr<UAudioComponent> Ambience;
 UPROPERTY() TObjectPtr<USoundAttenuation> ChewAttenuation;
 TArray<TWeakObjectPtr<UAudioComponent>> OpeningOneShots;
 TWeakObjectPtr<ADWGameplayHUD> OpeningMusicHUD;
 FGuid OpeningMusicLease;
 TWeakObjectPtr<ADWPlayerController> PC;
 TWeakObjectPtr<ADWPlayerCharacter> Player;
 TWeakObjectPtr<UCameraComponent> GameCamera;
 TWeakObjectPtr<UDWMinimapComponent> HiddenMinimap;
 TWeakObjectPtr<UDWGameplayWidget> HiddenGameplayHUD;
 TWeakObjectPtr<USkeletalMeshComponent> SourceHeroMesh;
 TWeakObjectPtr<AActor> PreviousViewTarget;

 TWeakObjectPtr<APlayerCameraManager> TraumaCameraManager;
 UPROPERTY(Transient) TObjectPtr<UCameraShakeBase> ActiveTraumaShake;
 ESlateVisibility SavedGameplayHUDVisibility=ESlateVisibility::Collapsed;
 TMap<TWeakObjectPtr<USceneComponent>,bool> DoubleHiddenVisibility;
 float SavedTimeDilation=1.f;
 bool bPresentationInitialized=false,bPresentationCleaned=false,bArrivalCoverOwned=false,bDirectorTickInProgress=false;
 bool bExitTitleStarted=false;float ExitTitleElapsed=0.f;
 bool bViewTargetOverridden=false;
 bool bGameplayHUDVisibilityOwned=false,bMinimapOverrideOwned=false,bMinimapWasActive=false;
 bool bDamageOverrideOwned=false,bTimeDilationOwned=false,bHeroPoseOwned=false;
 TMap<TWeakObjectPtr<UUserWidget>,ESlateVisibility> TitleHidden;
 TArray<FEnemyPose> EnemyPoses;
 TArray<FVector> CrumbHome;
 TArray<FDWOpeningShot> Shots;
 int32 ShotIndex=INDEX_NONE,CaptureShot=INDEX_NONE;
 float ShotAge=0,LastFstop=0;
 FVector BlendLoc=FVector::ZeroVector,LeapStart=FVector::ZeroVector,LeapEnd=FVector::ZeroVector,EncounterHero=FVector::ZeroVector;
 FQuat BlendRot=FQuat::Identity;
 float BlendFov=60;
 FVector MeshScale=FVector::OneVector,MeshLoc=FVector::ZeroVector;
 FQuat MeshRot=FQuat::Identity;
 // The hero's form system rewrites the mesh scale when its bones finalize; we re-apply ours right after it.
 FDelegateHandle BoneFinalizedHandle;
 FVector PoseScale=FVector::OneVector;
 bool bPoseScale=false;
 void ReapplyPoseScale();
 // The stand-in floats on the water and gets up by playing the hero's fall backwards. Base: where the real mesh stands.
 // LieAxis/LieAngle/LiePivot (component space) roll the fallen pose onto its back; the roll unwinds as it gets up.
 FTransform DoubleBase=FTransform::Identity;
 TWeakObjectPtr<AActor> Water;
 FVector LieAxis=FVector::UpVector,LiePivot=FVector::ZeroVector,StandPivot=FVector::ZeroVector,DoubleCenter=FVector::ZeroVector,BodyCenter=FVector::ZeroVector;
 float LieAngle=0,WaterZ=0,RippleTimer=0,ClearedAt=-1;
 bool bDoubleActive=false,bWaterFound=false,bWeaponGiven=false;
 void ProbeLyingPose();
 void TickDouble(float Dt);
 void AddRipple(const FVector& At,float Radius,float Strength,float Life=1.6f);
 void TickRipples(float Dt);
 void EquipStartingWeapon();
 void UpdateCombatPrompt();
 bool TickFormalFlow(float RealDt);
 bool bFormalFlowAdmitted=false,bAwaitingHandoff=false,bAwaitingRestart=false,bFlowErrorShown=false;
 bool bStarterSelectionWarningShown=false;
 float FlowRetryElapsed=0,DeathRetryElapsed=0;
 FString DiagnosticsRoot;
 bool AllEnemiesDown()const;
 void RefreshEnemyDefeatFacts();
 UFUNCTION() void OnOpeningEnemyDestroyed(AActor* Actor);
 bool bUnexpectedEnemyLoss=false;
 /** Hero half-width / 90cm. Shot offsets are authored at 1 and scale with the real body. */
 float Unit=1;
 float Age=0,PrevAge=0,TotalAge=0,InitialHealth=0,Trauma=0,HitStop=0,SkipHold=0,SkipHintTime=0,PendingSkip=-1,ChewTimer=0,TremorTimer=0,StretchStart=-1,Recoil=10;
 EDWOpeningStage SkipStage=EDWOpeningStage::Waiting;
 bool bLocked=false,bOriginalDamage=true,bAutoTest=false,bSkipTest=false,bPoseTest=false,bShotTaken=false,bMoved=false;
 TSet<EDWOpeningStage> Visited;
 void Enter(EDWOpeningStage Next);
 bool SetLocked(bool Locked);
 void HideMinimapForOpening();
 void RestoreMinimapState();
 void RestoreGameplayHUD();
 bool CanEnterExit(FString& Reason) const;
 void RestoreTitleWidgets();
 void RestorePlayerDamage();
 void RestoreTimeDilation();
 void RestoreDoubleVisibility();
 void RestoreHeroPose();
 void CleanupPresentation(bool bKeepBlackCover=false);
 bool Cue(float Time)const{return PrevAge<Time&&Age>=Time;}
 bool Between(float From,float To)const{return Age>=From&&Age<To;}
 // Shots
 FDWOpeningShot& AddMove(float Duration,const FVector& A,const FVector& B,const FVector& LookA,const FVector& LookB,float FovA,float FovB);
 FDWOpeningShot& AddPath();
 void Key(FDWOpeningShot& Path,float Time,const FVector& Pos,const FVector& Look,float Fov);
 FVector Clear(const FVector& Look,const FVector& Camera)const;
 // Camera position that puts Subject at screen (Sx,Sy in -1..1, y up) from Dist away, looking along View / toward Target.
 FVector Place(const FVector& Subject,const FRotator& View,float Dist,float Sx,float Sy,float Fov)const;
 FVector PlaceToward(const FVector& Subject,const FVector& Target,float Dist,float Sx,float Sy,float Fov)const;
 FDWOpeningShot& AddHold(float Duration);
 FDWOpeningShot& AddToGameplay(float Duration);
 void PlayShots();
 void CutToGameplay();
 void CapturePose();
 void TickShots(float Dt);
 float ShotsTotal()const;
 bool ShotsDone()const{return ShotIndex==INDEX_NONE;}
 // Beats
 void BuildWake(bool bOnlyCloseUp);
 void BuildReveal();
 void BuildEncounter(int32 Segment=0);
 void TickEnemies(float Dt);
 void TickHero(float Dt);
 void CreateDouble();
 void ShowDouble(bool bShow);

 void FaceHero(const FVector& Target,float Dt);
 const FDWOpeningAudioCue* ResolveOpeningAudio(EDWOpeningAudioEvent Event)const;
 void TrackOpeningOneShot(UAudioComponent* Audio);
 void Play(EDWOpeningAudioEvent Event,float Volume=1,float Pitch=1);
 void PlayAt(EDWOpeningAudioEvent Event,const FVector& Location,float Volume=1,float Pitch=1);
 void StartOpeningAmbience(bool bFadeIn);
 void AdjustOpeningAmbience(float FadeSeconds,float TargetVolume);
 void AddTrauma(float Value){Trauma=FMath::Clamp(Trauma+Value,0.f,1.f);}
 void StartHitStop(float RealSeconds);
 FVector Feet(const AActor* Actor)const;
 FVector HeroCenter()const;
 void Thought(const FString& Line){if(Overlay)Overlay->SetSubtitle(Line,false);}
 void Say(const FString& Line){if(Overlay)Overlay->SetSubtitle(Line,true);}
 void Capture(const FString& Name);
 void FinishTest(bool Passed,const FString& Detail);
};

UCLASS()
class GDATTEST_API ADWOpeningPrototypeGameMode : public ADWCinematicDemoGameMode
{
 GENERATED_BODY()
public:
 ADWOpeningPrototypeGameMode();
};
