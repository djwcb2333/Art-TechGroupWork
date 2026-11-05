#include "DWCombatNumericDetails.h"
#include "Gameplay/DWPlayerCombatTuningSubsystem.h"
#include "Gameplay/DWEnemyCharacter.h"
#include "Gameplay/DWCreatureDefinition.h"
#include "Gameplay/DWCombatRules.h"
#include "Gameplay/DWGameplayConfig.h"
#include "Gameplay/DWCombatDagger.h"
#include "Gameplay/DWWeaponAnimationComponent.h"
#include "Animation/AnimationAsset.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "DetailWidgetRow.h"
#include "PropertyEditorModule.h"
#include "ScopedTransaction.h"
#include "Editor.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"

#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Text/STextBlock.h"
#include "UObject/UnrealType.h"
#include "UObject/StrongObjectPtr.h"

namespace DWCombatDetails
{
struct FPlayerField {FName Id;bool bA01;FString Group;FText Label;FString Unit;FText Hint;bool bInteger;double Minimum;double Maximum;};
static const FPlayerField PlayerFields[]={
#include "DWCombatPlayerFields.inl"
};
using FSettings=UDWPlayerCombatTuningSettings;
static bool Editable(){return !GEditor||GEditor->PlayWorld==nullptr;}
static void Change(TWeakObjectPtr<FSettings> Weak,TFunctionRef<void(FSettings&)> Action)
{
 if(!Editable())return;TStrongObjectPtr<FSettings> AssetGuard(Weak.Get());if(auto* Asset=AssetGuard.Get())
 {const FScopedTransaction Transaction(FText::FromString(TEXT("修改战斗数值")));Asset->Modify();Asset->PreEditChange(nullptr);Action(*Asset);FPropertyChangedEvent Event(nullptr,EPropertyChangeType::ValueSet);Asset->PostEditChangeProperty(Event);Asset->MarkPackageDirty();}
}
struct FTree
{
 TSharedRef<SVerticalBox> Box=SNew(SVerticalBox);
 TMap<FString,TSharedPtr<FTree>> Children;
 TSharedRef<SVerticalBox> Get(const FString& Path)
 {
  FString First,Rest;if(!Path.Split(TEXT("|"),&First,&Rest)){First=Path;Rest.Reset();}
  if(First.IsEmpty())return Box;
  TSharedPtr<FTree>& Child=Children.FindOrAdd(First);
  if(!Child){Child=MakeShared<FTree>();Box->AddSlot().AutoHeight().Padding(4,3)[SNew(SExpandableArea).InitiallyCollapsed(true).HeaderContent()[SNew(STextBlock).Text(FText::FromString(First))].BodyContent()[Child->Box]];}
  return Rest.IsEmpty()?Child->Box:Child->Get(Rest);
 }
};
static void Note(TSharedRef<SVerticalBox> Box,const FString& Text)
{Box->AddSlot().AutoHeight().Padding(4)[SNew(STextBlock).Text(FText::FromString(Text)).AutoWrapText(true)];}
static void Number(TSharedRef<SVerticalBox> Box,const FText& Label,const FString& Unit,const FText& Hint,bool Integer,double Minimum,double Maximum,TFunction<TOptional<double>()> Read,TFunction<void(double)> Write)
{
 auto Line=SNew(SHorizontalBox);
 Line->AddSlot().FillWidth(1).VAlign(VAlign_Center).Padding(4)[SNew(STextBlock).Text(Label).ToolTipText(Hint)];
 if(Integer)
  Line->AddSlot().AutoWidth()[SNew(SBox).WidthOverride(140)[SNew(SNumericEntryBox<int32>).AllowSpin(false).MinValue(static_cast<int32>(Minimum)).MaxValue(static_cast<int32>(Maximum)).IsEnabled_Lambda([]{return Editable();}).Value_Lambda([Read]()->TOptional<int32>{const auto V=Read();return V.IsSet()?TOptional<int32>(static_cast<int32>(V.GetValue())):TOptional<int32>();}).OnValueCommitted_Lambda([Write](int32 V,ETextCommit::Type){Write(V);})]];
 else
  Line->AddSlot().AutoWidth()[SNew(SBox).WidthOverride(140)[SNew(SNumericEntryBox<double>).AllowSpin(false).MinValue(Minimum).MaxValue(Maximum).MinFractionalDigits(0).MaxFractionalDigits(6).IsEnabled_Lambda([]{return Editable();}).Value_Lambda([Read]{return Read();}).OnValueCommitted_Lambda([Write,Minimum,Maximum](double V,ETextCommit::Type){if(FMath::IsFinite(V)&&V>=Minimum&&V<=Maximum)Write(V);})]];
 Line->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(6)[SNew(STextBlock).Text(FText::FromString(Unit))];
 Box->AddSlot().AutoHeight().Padding(3,2)[Line];
}

struct FNumericModel
{
 TWeakObjectPtr<FSettings> Asset;
 TSharedPtr<const FDWCombatRules> Base,Cached;
 TArray<FDWCombatNumber> Last;
 FString Status=TEXT("直接填写当前有效数值。改周期按原比例调整阶段；改阶段必要时增加周期。保存后下一新会话生效。");
 bool bRead=false;
 explicit FNumericModel(TWeakObjectPtr<FSettings> A):Asset(A){FString Error;FDWCombatRules::LoadProjectPayload(Base,Error);}
 TSharedPtr<const FDWCombatRules> Rules()
 {
  const auto* A=Asset.Get();if(!A||!Base)return {};
  bool Same=bRead&&Last.Num()==A->PlayerNumbers.Num();
  if(Same)for(int32 I=0;I<Last.Num();++I)if(Last[I].ParameterId!=A->PlayerNumbers[I].ParameterId||FMemory::Memcmp(&Last[I].Value,&A->PlayerNumbers[I].Value,sizeof(double))!=0){Same=false;break;}
  if(Same)return Cached;Last=A->PlayerNumbers;bRead=true;Cached.Reset();TMap<FName,double> Numbers;
  for(const auto& R:Last){if(Numbers.Contains(R.ParameterId))return {};Numbers.Add(R.ParameterId,R.Value);}FString Error;FDWCombatRules::FromCompletePlayerValues(Base,Numbers,Cached,Error);return Cached;
 }
 void Commit(TFunctionRef<bool(FSettings&,FString&)> Edit)
 {
  TStrongObjectPtr<FSettings> AssetGuard(Asset.Get());auto* A=AssetGuard.Get();if(!A||!Editable())return;
  TStrongObjectPtr<FSettings> DraftGuard(NewObject<FSettings>());auto* Draft=DraftGuard.Get();Draft->NumericSchema=A->NumericSchema;Draft->PlayerNumbers=A->PlayerNumbers;Draft->AbilityNumbers=A->AbilityNumbers;Draft->EnemyNumbers=A->EnemyNumbers;Draft->SimpleSpearNumbers=A->SimpleSpearNumbers;
  FString Notice,Error;TSharedPtr<const FDWCombatNumericProvider> Provider;
  if(!Edit(*Draft,Notice)||!FDWCombatNumericProvider::Build(*Draft,Base,Provider,Error))
  {Status=TEXT("本次输入未采用：参数与现有阶段、判定或资源条件不匹配，请检查本次输入及关联项。旧值已保留。");UE_LOG(LogTemp,Warning,TEXT("DW_NUMERIC_EDIT_REJECTED: %s"),*Error);return;}
  Change(Asset,[&](auto& Target){Target.PlayerNumbers=Draft->PlayerNumbers;Target.AbilityNumbers=Draft->AbilityNumbers;Target.EnemyNumbers=Draft->EnemyNumbers;Target.SimpleSpearNumbers=Draft->SimpleSpearNumbers;});
  Status=Notice.IsEmpty()?TEXT("已采用并通过整表预检；Ctrl+S保存后下一新会话生效。"):Notice;bRead=false;
 }
};
static void Derived(TSharedRef<SVerticalBox> Box,TFunction<FText()> Read)
{Box->AddSlot().AutoHeight().Padding(4)[SNew(STextBlock).Text_Lambda([Read]{return Read();}).AutoWrapText(true)];}
static const UDWWeaponAnimationComponent* HeroWeapon()
{
 auto* Class=LoadClass<UObject>(nullptr,TEXT("/Game/DoughWorld/Characters/Hero/Blueprints/BP_DWTopDownCharacter.BP_DWTopDownCharacter_C"));auto* Property=Class?FindFProperty<FObjectPropertyBase>(Class,TEXT("WeaponAnimationComponent")):nullptr;
 return Property?Cast<UDWWeaponAnimationComponent>(Property->GetObjectPropertyValue_InContainer(Class->GetDefaultObject())):nullptr;
}
static void Players(FTree& Tree,TSharedRef<FNumericModel> Model)
{
 const auto Weak=Model->Asset;
 for(const auto& F:PlayerFields)
 {
  Number(Tree.Get(F.Group),F.Label,F.Unit,F.Hint,F.bInteger,F.Minimum,F.Maximum,
   [Weak,F]()->TOptional<double>{const auto* A=Weak.Get();if(!A)return {};const auto& Rows=F.bA01?A->AbilityNumbers:A->PlayerNumbers;const auto* R=Rows.FindByPredicate([&](const auto& V){return V.ParameterId==F.Id;});return R?TOptional<double>(R->Value):TOptional<double>();},
   [Model,F](double V){Model->Commit([&](auto& A,FString& Notice){auto& Rows=F.bA01?A.AbilityNumbers:A.PlayerNumbers;auto* R=Rows.FindByPredicate([&](const auto& Item){return Item.ParameterId==F.Id;});if(!R)return false;const double Previous=R->Value;R->Value=V;return F.bA01||DWCombatNumericPolicy::CoordinateTiming(A.PlayerNumbers,F.Id,Previous,Notice);});});
 }
 for(int32 I=0;I<4;++I)Derived(Tree.Get(FString::Printf(TEXT("主角|匕首|第%d段"),I+1)),[Model,I]
 {
  const auto Rules=Model->Rules();FDWDaggerTuning D;FDWSpearTuning T;FString Error;
  if(!Rules||!FDWDaggerTuning::Build(*Rules,D,Error)||!D.MakeSegmentTuning(I,T))return FText::FromString(TEXT("本段参数暂不可用；请检查关联数值。"));
  return FText::FromString(FString::Printf(TEXT("伤害 %.6g 点；前摇 %.6g 秒；有效期 %.6g 秒；后摇 %.6g 秒；完整周期 %.6g 秒；范围 %.6g 厘米；全角 %.6g°。伤害与判定共用，各段阶段时间按本段时长联动。"),T.Damage,T.WindupSeconds,T.ActiveSeconds,T.RecoverySeconds,T.IntervalSeconds,T.RangeCm,T.FullArcDegrees));
 });
 Note(Tree.Get(TEXT("主角|大剑")),TEXT("满蓄松键后自动两段。动画完整段随时长变速，视觉事件保持原段内相对进度；命中使用独立判定。"));
 const TCHAR* Labels[]={TEXT("伤害"),TEXT("完整攻击周期"),TEXT("判定距离"),TEXT("击退距离"),TEXT("击退时长")};
 for(int32 I=0;I<5;++I)
 {
  Number(Tree.Get(TEXT("主角|简易糖矛|工具刺击")),FText::FromString(Labels[I]),I==0?TEXT("点"):I==1||I==4?TEXT("秒"):TEXT("厘米"),FText::FromString(TEXT("直接编辑本攻击有效数值；原背包、工具资格、命中与资源提交保持。")),false,0.,MAX_flt,
   [Weak,I]()->TOptional<double>{const auto* A=Weak.Get();if(!A)return {};const auto& N=A->SimpleSpearNumbers;switch(I){case 0:return N.Damage;case 1:return N.IntervalSeconds;case 2:return N.RangeCm;case 3:return N.KnockbackCm;default:return N.KnockbackSeconds;}},
   [Model,I](double V){Model->Commit([&](auto& A,FString&){auto& N=A.SimpleSpearNumbers;switch(I){case 0:N.Damage=V;break;case 1:N.IntervalSeconds=V;break;case 2:N.RangeCm=V;break;case 3:N.KnockbackCm=V;break;default:N.KnockbackSeconds=V;break;}return true;});});
 }
 if(const auto* Weapon=HeroWeapon())if(const auto* Phase=Weapon->AttackSourcePhaseEnds.Find(EDWAnimatedWeapon::Spear))Derived(Tree.Get(TEXT("主角|简易糖矛|工具刺击")),[Model,Weak,Phase=*Phase]
 {const auto* A=Weak.Get();if(!A)return FText();const double P=A->SimpleSpearNumbers.IntervalSeconds;const auto Rules=Model->Rules();FDWSpearTuning T;FString E;if(!Rules||!FDWSpearTuning::Build(*Rules,T,E))return FText();return FText::FromString(FString::Printf(TEXT("前摇 %.6g 秒；有效期 %.6g 秒；后摇 %.6g 秒；全角 %.6g°。阶段联动周期与原动作比例，全角联动长枪；击退沿同一次命中的统一提交链。"),P*Phase.X,P*(Phase.Y-Phase.X),P*(1.-Phase.Y),T.FullArcDegrees));});
 for(const TCHAR* Prefix:{TEXT("weapon.spear."),TEXT("weapon.axe."),TEXT("weapon.gun."),TEXT("weapon.bow."),TEXT("weapon.shotgun."),TEXT("weapon.alcohol."),TEXT("unarmed.")})
 {
  FString Group;for(const auto& F:PlayerFields)if(F.Id==FName(*(FString(Prefix)+TEXT("interval_s")))){Group=F.Group;break;}
  Derived(Tree.Get(Group),[Weak,P=FString(Prefix)]
  {const auto* A=Weak.Get();if(!A)return FText();auto N=[&](const TCHAR* S){const auto* R=A->PlayerNumbers.FindByPredicate([&](const auto& V){return V.ParameterId==FName(*(P+S));});return R?R->Value:0.;};const double Sum=N(TEXT("windup_s"))+N(TEXT("active_s"))+N(TEXT("recovery_s"));return FText::FromString(FString::Printf(TEXT("当前完整周期 %.6g 秒；阶段合计 %.6g 秒；额外间隔 %.6g 秒。改周期按现有比例缩放阶段，改阶段必要时扩周期。"),N(TEXT("interval_s")),Sum,FMath::Max(0.,N(TEXT("interval_s"))-Sum)));});
 }
}
static FString Unit(EDWEnemyNumericField F)
{
 using E=EDWEnemyNumericField;switch(F){case E::MaxHealth:case E::Damage:return TEXT("点");case E::MoveSpeed:return TEXT("厘米/秒");case E::Range:case E::Height:case E::OutgoingKnockback:case E::AlertRadius:case E::DisengageDistance:case E::PatrolRadius:return TEXT("厘米");case E::FullAngle:return TEXT("度（全角）");case E::WindupLockFraction:return TEXT("比例");case E::IncomingKnockbackMultiplier:case E::AttackKnockbackMultiplier:case E::ReturnSpeedMultiplier:return TEXT("倍率");case E::VariantWeight:return TEXT("权重");default:return TEXT("秒");}
}
static void Enemies(FTree& Tree,TSharedRef<FNumericModel> Model)
{
 const auto Weak=Model->Asset;const auto* A=Weak.Get();if(!A)return;
 for(int32 T=0;T<A->EnemyNumbers.Num();++T)
 {
  const auto& Type=A->EnemyNumbers[T];const FString Root=(Type.bNeutral?TEXT("中立角色|"):TEXT("敌人|"))+Type.DisplayName.ToString();const bool Legacy=Type.Profile==EDWEnemyCombatProfile::Legacy;
  for(int32 R=0;R<Type.Values.Num();++R)
  {
   const auto& Row=Type.Values[R];const auto Field=Row.Field;const FText Label=StaticEnum<EDWEnemyNumericField>()->GetDisplayNameTextByValue(static_cast<int64>(Field));
   if(Type.bNeutral&&Field!=EDWEnemyNumericField::MaxHealth&&Field!=EDWEnemyNumericField::MoveSpeed&&Field!=EDWEnemyNumericField::DeathCleanupSeconds)continue;
   FString Group=Root+TEXT("|");
   Group+=Row.VariantIndex==INDEX_NONE?(Field==EDWEnemyNumericField::MaxHealth||Field==EDWEnemyNumericField::MoveSpeed||Field>=EDWEnemyNumericField::AlertRadius?TEXT("基础与行为"):TEXT("共用攻击判定")):Row.VariantAnimation.ToString().Contains(TEXT("Pounce"))?TEXT("扑击（第1招）"):Row.VariantAnimation.ToString().Contains(TEXT("Tentacle"))?TEXT("触手攻击（第2招）"):FString::Printf(TEXT("攻击段%d"),Row.VariantIndex+1);
   double Low=0.,High=Field==EDWEnemyNumericField::FullAngle?360.:Field==EDWEnemyNumericField::WindupLockFraction?1.:MAX_flt;if(Legacy&&!DWEnemyNumericPolicy::LegacyBounds(Field,Low,High))continue;
   Number(Tree.Get(Group),Label,Unit(Field),FText::FromString(FString::Printf(TEXT("本种类有效数值。允许范围 %.6g—%.6g；变招索引与动作来源保持只读。"),Low,High)),false,Low,High,
    [Weak,T,R]()->TOptional<double>{const auto* S=Weak.Get();return S&&S->EnemyNumbers.IsValidIndex(T)&&S->EnemyNumbers[T].Values.IsValidIndex(R)?TOptional<double>(S->EnemyNumbers[T].Values[R].Value):TOptional<double>();},
    [Model,T,R](double V){Model->Commit([&](auto& S,FString&){if(!S.EnemyNumbers.IsValidIndex(T)||!S.EnemyNumbers[T].Values.IsValidIndex(R))return false;S.EnemyNumbers[T].Values[R].Value=V;return true;});});
  }
  if(!Legacy)Derived(Tree.Get(Root+TEXT("|共用攻击判定")),[Weak,T]
  {const auto* S=Weak.Get();FDWEnemyCombatConfig C;FDWEnemyActorNumericSupplement X;FString E;if(!S||!S->EnemyNumbers.IsValidIndex(T)||!FDWCombatNumericProvider::ProjectEnemy(S->EnemyNumbers[T],C,X,E))return FText::FromString(TEXT("完整周期暂不可用。"));return FText::FromString(FString::Printf(TEXT("完整周期 %.6g 秒＝前摇 %.6g＋有效期 %.6g＋后摇 %.6g＋额外冷却 %.6g 秒（联动）。"),C.AttackCycleSeconds(),C.WindupSeconds,C.ActiveSeconds,C.RecoverySeconds,C.ExtraCooldownSeconds));});
  else Note(Tree.Get(Root),Type.bNeutral?TEXT("中立角色：不主动攻击、原不可攻击/无血条规则保持。巡逻锚点、路线及组件优先级保持，基础速度来自此统一表；不在这里伪造有效攻击。"):TEXT("旧敌人周期沿原攻击起点间隔语义。有变招时，各招伤害、范围及阶段时间分别显示；不新增动作。"));
 }
}
}
TSharedRef<IDetailCustomization> FDWCombatNumericDetails::MakeInstance(){return MakeShared<FDWCombatNumericDetails>();}
void FDWCombatNumericDetails::CustomizeDetails(IDetailLayoutBuilder& Builder)
{
 TArray<TWeakObjectPtr<UObject>> Objects;Builder.GetObjectsBeingCustomized(Objects);
 for(const FName P:{FName(TEXT("NumericSchema")),FName(TEXT("PlayerNumbers")),FName(TEXT("AbilityNumbers")),FName(TEXT("EnemyNumbers")),FName(TEXT("SimpleSpearNumbers")),FName(TEXT("RuleOverrides")),FName(TEXT("A01Overrides")),FName(TEXT("EnemyTypes")),FName(TEXT("bOverrideSimpleSpearDamage")),FName(TEXT("SimpleSpearDamage")),FName(TEXT("bOverrideSimpleSpearInterval")),FName(TEXT("SimpleSpearIntervalSeconds")),FName(TEXT("bOverrideSimpleSpearRange")),FName(TEXT("SimpleSpearRangeCm"))})Builder.HideProperty(P);
 auto& Category=Builder.EditCategory(TEXT("DWCombatNumeric"),FText::FromString(TEXT("统一战斗数值 · 直接编辑当前值")),ECategoryPriority::Important);DWCombatDetails::FTree Tree;
 auto* Asset=Objects.Num()==1?Cast<UDWPlayerCombatTuningSettings>(Objects[0].Get()):nullptr;
 if(!Asset||Asset->NumericSchema!=1)DWCombatDetails::Note(Tree.Box,TEXT("请单独打开已完成保值迁移的战斗数值资产。旧字段仍保留，未自动覆盖或恢复旧值。"));
 else
 {Model=MakeShared<DWCombatDetails::FNumericModel>(Asset);const auto M=Model.ToSharedRef();DWCombatDetails::Derived(Tree.Box,[M]{return FText::FromString(M->Status);});DWCombatDetails::Players(Tree,M);DWCombatDetails::Enemies(Tree,M);}
 Category.AddCustomRow(FText::FromString(TEXT("主角 敌人 战斗 伤害 周期"))).WholeRowContent()[Tree.Box];
}
void RegisterDWCombatNumericDetails()
{if(IsRunningCommandlet())return;auto& Module=FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));Module.RegisterCustomClassLayout(UDWPlayerCombatTuningSettings::StaticClass()->GetFName(),FOnGetDetailCustomizationInstance::CreateStatic(&FDWCombatNumericDetails::MakeInstance));Module.NotifyCustomizationModuleChanged();}
void UnregisterDWCombatNumericDetails()
{if(auto* Module=FModuleManager::GetModulePtr<FPropertyEditorModule>(TEXT("PropertyEditor"))){Module->UnregisterCustomClassLayout(UDWPlayerCombatTuningSettings::StaticClass()->GetFName());Module->NotifyCustomizationModuleChanged();}}

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWNumericEditorCommitTest,"DoughWorld.Combat.UnifiedNumeric.EditorCommitPreflight",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWNumericEditorCommitTest::RunTest(const FString&)
{
 using namespace DWCombatDetails;
 const auto* Installed=LoadObject<FSettings>(nullptr,TEXT("/Game/DoughWorld/Core/Data/DA_DWPlayerCombatTuning.DA_DWPlayerCombatTuning"));
 if(!Installed||Installed->NumericSchema!=1){AddError(TEXT("Root must migrate the actual asset before this pure editor model test."));return false;}
 TStrongObjectPtr<FSettings> Copy(NewObject<FSettings>());Copy->NumericSchema=1;Copy->PlayerNumbers=Installed->PlayerNumbers;Copy->AbilityNumbers=Installed->AbilityNumbers;Copy->EnemyNumbers=Installed->EnemyNumbers;Copy->SimpleSpearNumbers=Installed->SimpleSpearNumbers;
 auto Model=MakeShared<FNumericModel>(Copy.Get());
 TestTrue(TEXT("Current source yields a valid editor projection"),Model->Rules().IsValid());
 Model->Commit([](auto& S,FString& Notice){auto* Row=S.PlayerNumbers.FindByPredicate([](const auto& V){return V.ParameterId==FName(TEXT("weapon.spear.interval_s"));});if(!Row)return false;const double Previous=Row->Value;Row->Value=.4;return DWCombatNumericPolicy::CoordinateTiming(S.PlayerNumbers,Row->ParameterId,Previous,Notice);});
 FDWSpearTuning Attack;FString Error;auto Rules=Model->Rules();
 if(!Rules||!FDWSpearTuning::Build(*Rules,Attack,Error)){AddError(Error);return false;}
 TestTrue(TEXT("Actual native editor commit preserves requested .4s and coordinated phases"),FMath::IsNearlyEqual(Attack.IntervalSeconds,.4,1.e-12)&&Attack.WindupSeconds+Attack.ActiveSeconds+Attack.RecoverySeconds<=.4+1.e-8);
 const auto Before=Copy->PlayerNumbers;
 AddExpectedMessage(TEXT("DW_NUMERIC_EDIT_REJECTED"),ELogVerbosity::Warning,EAutomationExpectedMessageFlags::Contains,1);
 Model->Commit([](auto& S,FString&){for(auto& V:S.PlayerNumbers)if(V.ParameterId==FName(TEXT("weapon.spear.damage"))){V.Value=-1.;return true;}return false;});
 bool Same=Before.Num()==Copy->PlayerNumbers.Num();if(Same)for(int32 I=0;I<Before.Num();++I)Same&=Before[I].ParameterId==Copy->PlayerNumbers[I].ParameterId&&Before[I].Value==Copy->PlayerNumbers[I].Value;
 TestTrue(TEXT("Rejected edit leaves all prior player values unchanged"),Same);
 TestTrue(TEXT("Editor shows a readable rejection instead of committing invalid data"),Model->Status.StartsWith(TEXT("本次输入未采用")));
 int32 RulesCount=0,AbilitiesCount=0;TSet<FName> Seen;
 for(const auto& F:PlayerFields){TestFalse(TEXT("Named controls never repeat an identity"),Seen.Contains(F.Id));Seen.Add(F.Id);TestFalse(TEXT("Internal parameter IDs are not visible labels"),F.Label.ToString().Contains(F.Id.ToString())||F.Hint.ToString().Contains(F.Id.ToString()));TestTrue(TEXT("Chinese player hierarchy retained"),F.Group.StartsWith(TEXT("主角|")));F.bA01?++AbilitiesCount:++RulesCount;}
 TestEqual(TEXT("All player direct controls covered"),RulesCount,147);TestEqual(TEXT("All ability direct controls covered"),AbilitiesCount,17);
 return true;
}
#endif
