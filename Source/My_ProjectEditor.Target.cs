// Fill out your copyright notice in the Description page of Project Settings.

using UnrealBuildTool;
using System.Collections.Generic;

public class My_ProjectEditorTarget : TargetRules
{
	public My_ProjectEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V5;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_6;
		bOverrideBuildEnvironment = true;
		AdditionalCompilerArguments = "-Wno-error=implicit-int-float-conversion -Wno-error=implicit-int-conversion -Wno-error=implicit-int-conversion-on-negation";

		ExtraModuleNames.AddRange( new string[] { "My_Project" } );
	}
}
