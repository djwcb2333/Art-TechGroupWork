using UnrealBuildTool;
public class DWBuildingAuthoringEditor : ModuleRules
{
    public DWBuildingAuthoringEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "GDATtest" });
        PrivateDependencyModuleNames.AddRange(new[] { "UnrealEd", "Slate", "SlateCore", "InputCore", "ToolMenus", "PropertyEditor", "AssetTools", "AssetRegistry", "ContentBrowser", "AdvancedPreviewScene", "Kismet", "KismetCompiler", "BlueprintGraph", "DesktopPlatform", "ApplicationCore", "Json", "JsonUtilities", "RenderCore", "Landscape", "PhysicsCore" });
    }
}
