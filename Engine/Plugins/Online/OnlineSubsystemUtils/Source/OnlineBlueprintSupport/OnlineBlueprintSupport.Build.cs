// Copyright 1998-2017 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class OnlineBlueprintSupport : ModuleRules
{
	public OnlineBlueprintSupport(TargetInfo Target)
	{
        PrivateDependencyModuleNames.AddRange(
			new string[] { 
				"Core", 
				"CoreUObject", 
				"Engine",
				"BlueprintGraph",
				"OnlineSubsystem",
				"OnlineSubsystemUtils",
				"Sockets",
				"Json"
			}
		);
	}
}
