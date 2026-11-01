#include "DWAttackVfxAuthoringAdapter.h"
#include "DWAttackVfxAuthoringEditor.h"
#include "DWAttackVfxProfile.h"
#include "DWPlayerCharacter.h"
#include "AssetTypeActions_Base.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Misc/MessageDialog.h"
#include "ToolMenus.h"

namespace
{
 TSharedPtr<IAssetTypeActions> ProfileActions;
 class FProfileActions final:public FAssetTypeActions_Base
 {
 public:
  virtual FText GetName() const override{return FText::FromString(TEXT("Dough World Attack VFX Profile"));}
  virtual FColor GetTypeColor() const override{return FColor(238,154,50);}
  virtual UClass* GetSupportedClass() const override{return UDWAttackVfxProfile::StaticClass();}
  virtual uint32 GetCategories() override{return EAssetTypeCategories::Misc;}
  virtual void OpenAssetEditor(const TArray<UObject*>& Objects,TSharedPtr<IToolkitHost>) override
  {for(UObject* O:Objects)if(auto* P=Cast<UDWAttackVfxProfile>(O))OpenDWAttackVfxProfile(P);}
 };
}
void OpenDWAttackVfxProfile(UDWAttackVfxProfile* Profile)
{
 FString Error;auto Bridge=MakeDWAttackVfxAuthoringBridge(Profile,Error);
 if(!Bridge){FMessageDialog::Open(EAppMsgType::Ok,FText::FromString(Error));return;}
 auto Editor=MakeShared<FDWAttackVfxAuthoringEditor>();Editor->Init(Bridge.ToSharedRef());
}
void OpenDefaultDWAttackVfxProfile()
{
 const auto All=GetDWAttackVfxProfileCatalog();
 if(All.IsEmpty()){FMessageDialog::Open(EAppMsgType::Ok,FText::FromString(TEXT("尚未初始化正式表现Profile。请先完成当前Effects/8Set等价迁移，不创建空白预览。")));return;}
 // Prefer the intended Hero profile explicitly, then a Hero event profile.
 for(const auto& A:All)if(A.AssetName==TEXT("DA_DWAttackVfx_HeroEvents"))
  if(auto* P=Cast<UDWAttackVfxProfile>(A.GetAsset());P&&!P->EventOverrides.IsEmpty())
   {OpenDWAttackVfxProfile(P);return;}
 for(const auto& A:All)if(auto* P=Cast<UDWAttackVfxProfile>(A.GetAsset());P&&!P->EventOverrides.IsEmpty()&&P->PreviewSourceClass.LoadSynchronous()&&P->PreviewSourceClass.Get()->IsChildOf(ADWPlayerCharacter::StaticClass()))
 {OpenDWAttackVfxProfile(P);return;}
 // If no Hero profile exists, prefer another actual event profile before any row.
 for(const auto& A:All)if(auto* P=Cast<UDWAttackVfxProfile>(A.GetAsset());P&&!P->EventOverrides.IsEmpty())
  {OpenDWAttackVfxProfile(P);return;}
 OpenDWAttackVfxProfile(Cast<UDWAttackVfxProfile>(All[0].GetAsset()));
}
void RegisterDWAttackVfxProfileAssetActions()
{
 if(ProfileActions)return;
 ProfileActions=MakeShared<FProfileActions>();
 FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get().RegisterAssetTypeActions(ProfileActions.ToSharedRef());
}
void UnregisterDWAttackVfxProfileAssetActions()
{
 if(ProfileActions&&FModuleManager::Get().IsModuleLoaded(TEXT("AssetTools")))FModuleManager::GetModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get().UnregisterAssetTypeActions(ProfileActions.ToSharedRef());
 ProfileActions.Reset();
}
// Called from the existing module's RegisterMenus, under its FToolMenuOwnerScoped.
void RegisterDWAttackVfxProfileCatalogMenu()
{
 auto& Section=UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.MainMenu.Tools"))->FindOrAddSection(TEXT("DoughWorldTools"));
 Section.AddMenuEntry(TEXT("DWAttackVfxOpenDefault"),FText::FromString(TEXT("攻击特效编辑 / Attack VFX Authoring")),
  FText::FromString(TEXT("自动读取实际角色/武器/材质与同份正式Profile；不运行游戏。")),FSlateIcon(),FUIAction(FExecuteAction::CreateStatic(&OpenDefaultDWAttackVfxProfile)));
 Section.AddSubMenu(TEXT("DWAttackVfxProfiles"),FText::FromString(TEXT("全部攻击特效配置 / All Attack VFX Profiles")),FText::GetEmpty(),
 FNewToolMenuDelegate::CreateLambda([](UToolMenu* Sub)
 {
  auto& S=Sub->AddSection(TEXT("Profiles"));const auto All=GetDWAttackVfxProfileCatalog();
  for(const auto& A:All)
   S.AddMenuEntry(A.AssetName,FText::FromName(A.AssetName),FText::FromString(A.GetObjectPathString()),FSlateIcon(),
    FUIAction(FExecuteAction::CreateLambda([A](){OpenDWAttackVfxProfile(Cast<UDWAttackVfxProfile>(A.GetAsset()));})));
 }));
}
