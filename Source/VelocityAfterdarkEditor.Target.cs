using UnrealBuildTool;
using System.Collections.Generic;

public class VelocityAfterdarkEditorTarget : TargetRules
{
    public VelocityAfterdarkEditorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
        DefaultBuildSettings = BuildSettingsVersion.V7;
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
        ExtraModuleNames.Add("VelocityAfterdark");
    }
}
