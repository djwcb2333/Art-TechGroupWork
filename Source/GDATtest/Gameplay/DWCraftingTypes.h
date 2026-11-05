#pragma once
#include "CoreMinimal.h"
#include "DWGameplayTypes.h"
#include "DWSugarMaterialPolicy.h"
#include "DWCraftingTypes.generated.h"

UENUM()
enum class EDWCraftingCategory:uint8 {All,Weapons,Armour,Tools,Consumables,Materials,Buildings,Other};

namespace DWCrafting
{
    constexpr int32 HardBatchLimit=99;
    constexpr int32 IngredientColumns=2;
    // Matches live DWUIEntryWidgets / DWGameplayBook: 88 fill, 64 icon, 4px each side.
    constexpr float BackpackSlotExtent=88.f,BackpackIconExtent=64.f,BackpackCellExtent=96.f;
    constexpr int32 RecipeColumns=5;
    constexpr float MaterialCardWidth=230.f,MaterialCardHeight=58.f;
    constexpr float MaterialSlotExtent=44.f,MaterialIconExtent=32.f,MaterialRowStride=66.f,MaterialViewportHeight=198.f;
    constexpr int32 MaterialRowsWithoutScroll=3;
    // Presentation and payment use one canonical craft-only material identity.
    inline bool Scale(const TArray<FDWItemStack>& Source,int32 Count,bool bCanonical,TArray<FDWItemStack>& Out)
    {
        Out.Reset();if(Count<1||Count>HardBatchLimit||Source.IsEmpty()||Source.Num()>128)return false;
        for(const auto& S:Source)
        {
            if(S.ItemId.IsNone()||S.Quantity<1){Out.Reset();return false;}
            const FName Id=bCanonical?DWSugarMaterial::Canonical(S.ItemId):S.ItemId;
            auto* Existing=Out.FindByPredicate([Id](const auto& X){return X.ItemId==Id;});
            const int64 Total=int64(S.Quantity)*Count+(Existing?int64(Existing->Quantity):0);
            if(Total>MAX_int32){Out.Reset();return false;}
            if(Existing)Existing->Quantity=int32(Total);else Out.Emplace(Id,int32(Total));
        }
        return true;
    }
    inline bool Matches(EDWCraftingCategory Selected,const TArray<EDWCraftingCategory>& Outputs)
    {return Selected==EDWCraftingCategory::All||Outputs.Contains(Selected);}
    struct FCategoryFacts {bool Weapon=false,Armour=false,Tool=false,Consumable=false,Material=false,Building=false;};
    inline EDWCraftingCategory ResolveCategory(EDWItemCategory Explicit,const FCategoryFacts& F)
    {
        switch(Explicit)
        {
        case EDWItemCategory::Weapon:return EDWCraftingCategory::Weapons;case EDWItemCategory::Armour:return EDWCraftingCategory::Armour;
        case EDWItemCategory::Tool:return EDWCraftingCategory::Tools;case EDWItemCategory::Consumable:return EDWCraftingCategory::Consumables;
        case EDWItemCategory::Material:return EDWCraftingCategory::Materials;case EDWItemCategory::Building:return EDWCraftingCategory::Buildings;
        case EDWItemCategory::Other:return EDWCraftingCategory::Other;default:break;
        }
        if(F.Weapon)return EDWCraftingCategory::Weapons;if(F.Armour)return EDWCraftingCategory::Armour;
        if(F.Building)return EDWCraftingCategory::Buildings;if(F.Tool)return EDWCraftingCategory::Tools;
        if(F.Consumable)return EDWCraftingCategory::Consumables;if(F.Material)return EDWCraftingCategory::Materials;
        return EDWCraftingCategory::Other;
    }
    struct FRect {float X,Y,W,H;};
    inline FRect IngredientCell(int32 Index)
    {return {float(Index%2)*242.f,float(Index/2)*MaterialRowStride,MaterialCardWidth,MaterialCardHeight};}
    inline bool Overlaps(FRect A,FRect B)
    {return A.X<B.X+B.W&&A.X+A.W>B.X&&A.Y<B.Y+B.H&&A.Y+A.H>B.Y;}
}
