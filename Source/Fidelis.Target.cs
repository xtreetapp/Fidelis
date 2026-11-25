using UnrealBuildTool;

public class FidelisTarget : TargetRules
{
	public FidelisTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("Fidelis");
	}
}
