// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "JumpPadCommonSettings.generated.h"

/**
 * 모든 점프패드가 공유하는 공통 설정.
 * 패드마다 값이 갈리지 않는 항목만 여기에 둔다. (패드 고유 값 - 목표 지점, ApexTime 등 - 은 패드 액터에 유지)
 *
 * 사용 흐름:
 *   1) 콘텐츠 브라우저에서 이 클래스로 Data Asset 을 1개 생성 (예: DA_JumpPadCommon)
 *   2) 점프패드 BP 에 이 타입의 변수(예: CommonSettings)를 두고 위 에셋을 기본값으로 지정
 *   3) BP 에서 SetInputStopTimer 의 Duration 을 CommonSettings->IgnoreInputTime 으로 연결
 */
UCLASS(BlueprintType)
class SHOOTINGARENA_API UJumpPadCommonSettings : public UDataAsset
{
	GENERATED_BODY()

public:
	/**
	 * 점프패드로 발사된 직후 캐릭터 입력을 무시할 시간(초).
	 * BPI_BodyInfo::SetInputStopTimer 로 넘어간다. 모든 점프패드 공통.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "JumpPad",
		meta = (ClampMin = "0.0", UIMin = "0.0", ForceUnits = "s"))
	float IgnoreInputTime = 0.3f;
};
