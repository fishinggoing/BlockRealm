using UnrealBuildTool;
using System.Collections.Generic;
public class BlockRealmEditorTarget : TargetRules
{
    public BlockRealmEditorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
        DefaultBuildSettings = BuildSettingsVersion.Latest;
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
        ExtraModuleNames.Add("BlockRealm");
    }
}
