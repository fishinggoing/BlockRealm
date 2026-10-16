using UnrealBuildTool;
public class BlockRealm : ModuleRules
{
    public BlockRealm(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new string[] {
            "Core", "CoreUObject", "Engine", "InputCore", "Slate", "SlateCore",
            "ProceduralMeshComponent", "Json", "JsonUtilities"
        });
        PrivateDependencyModuleNames.AddRange(new string[] {
            "DLSSBlueprint", "StreamlineBlueprint", "StreamlineDLSSGBlueprint", "StreamlineReflexBlueprint", "RHI", "RenderCore", "ApplicationCore"
        });
    }
}
