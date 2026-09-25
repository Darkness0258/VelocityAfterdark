using UnrealBuildTool;

public class VelocityAfterdark : ModuleRules
{
    public VelocityAfterdark(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        CppStandard = CppStandardVersion.Cpp20;
        PublicDependencyModuleNames.AddRange(new[] {
            "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput",
            "PhysicsCore", "ProceduralMeshComponent", "AudioMixer"
        });
        PrivateDependencyModuleNames.AddRange(new[] {
            "Json", "JsonUtilities", "Slate", "SlateCore", "ApplicationCore"
        });
        if (Target.bBuildEditor)
        {
            PrivateDependencyModuleNames.Add("UnrealEd");
        }
    }
}
