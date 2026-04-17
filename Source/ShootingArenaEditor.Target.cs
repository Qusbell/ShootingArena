// Fill out your copyright notice in the Description page of Project Settings.

using UnrealBuildTool;
using System.Collections.Generic;

public class ShootingArenaEditorTarget : TargetRules
{
	public ShootingArenaEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V5;
		WindowsPlatform.CompilerVersion = "14.38.33130"; // 커스텀 데디케이트 전용 엔진과 같은 vs 버전 사용

        ExtraModuleNames.AddRange( new string[] { "ShootingArena" } );
	}
}
