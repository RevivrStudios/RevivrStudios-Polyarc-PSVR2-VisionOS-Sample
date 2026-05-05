// Fill out your copyright notice in the Description page of Project Settings.

using UnrealBuildTool;
using System.Collections.Generic;

public class My_ProjectTarget : TargetRules
{
	public My_ProjectTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V5;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_6;
		AdditionalCompilerArguments = "-Wno-error=implicit-int-float-conversion -Wno-error=implicit-int-conversion -Wno-error=implicit-int-conversion-on-negation";

		ExtraModuleNames.AddRange( new string[] { "My_Project" } );
	}
}
