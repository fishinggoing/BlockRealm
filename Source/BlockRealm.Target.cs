using UnrealBuildTool;
using System.Collections.Generic;
public class BlockRealmTarget : TargetRules
{
    public BlockRealmTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Game;
        DefaultBuildSettings = BuildSettingsVersion.Latest;
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
        ExtraModuleNames.Add("BlockRealm");
    }
}
