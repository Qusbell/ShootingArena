// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "JsonApplyTypes.h"
#include "Subsystems/EngineSubsystem.h"
#include "JsonAssetSyncSubsystem.generated.h"

/**
 * JSON 전체 처리 완료 시 호출되는 네이티브 이벤트다.
 *
 * 에디터 전용 알림 모듈이 이 이벤트를 구독하여
 * 성공 또는 실패 알림과 Message Log를 표시한다.
 */
DECLARE_MULTICAST_DELEGATE_OneParam(
	FOnJsonAssetSyncProcessingCompleted,
	const FJsonApplySummary&
);

/**
 * JSON Asset Sync 시스템의 런타임 진입점을 담당한다.
 */
UCLASS()
class JSONASSETSYNC_API UJsonAssetSyncSubsystem :
	public UEngineSubsystem
{
	GENERATED_BODY()

public:
	/**
	 * Engine Subsystem 초기화 시 호출된다.
	 */
	virtual void Initialize(
		FSubsystemCollectionBase& collection
	) override;

	/**
	 * Engine Subsystem 종료 시 호출된다.
	 */
	virtual void Deinitialize() override;

	/**
	 * Registry 전체를 검사한다.
	 *
	 * 실제 DataTable 또는 DataAsset 값은 변경하지 않는다.
	 */
	UFUNCTION(
		BlueprintCallable,
		Category = "JSON Asset Sync",
		meta = (DisplayName = "Validate All JSON Now")
	)
	FJsonApplySummary ValidateAllNow();

	/**
	 * Registry 전체를 검사하고 실제 대상 에셋에 적용한다.
	 */
	UFUNCTION(
		BlueprintCallable,
		Category = "JSON Asset Sync",
		meta = (DisplayName = "Apply All JSON Now")
	)
	FJsonApplySummary ApplyAllNow();

	/**
	 * 가장 최근의 JSON 검사 또는 적용 결과를 반환한다.
	 */
	UFUNCTION(
		BlueprintPure,
		Category = "JSON Asset Sync",
		meta = (DisplayName = "Get Last JSON Processing Summary")
	)
	FJsonApplySummary GetLastSummary() const
	{
		return lastSummary;
	}

	/**
	 * 시작 시 JSON 자동 처리 여부 확인이 끝났는지를 반환한다.
	 */
	UFUNCTION(
		BlueprintPure,
		Category = "JSON Asset Sync",
		meta = (DisplayName = "Has Completed Initial JSON Processing")
	)
	bool HasCompletedInitialValidation() const
	{
		return initialValidationCompleted;
	}

	/**
	 * JSON 전체 처리 완료 이벤트를 반환한다.
	 *
	 * Blueprint용 Delegate가 아니라 C++ 모듈 간 통신을 위한
	 * 네이티브 Delegate다.
	 */
	FOnJsonAssetSyncProcessingCompleted&
		OnProcessingCompleted()
	{
		return processingCompletedEvent;
	}

private:
	/**
	 * 엔진 초기화가 끝났을 때 호출된다.
	 */
	void HandleEngineLoopInitComplete();

	/**
	 * JSON 검사 또는 적용을 실제로 실행한다.
	 *
	 * @param applyChanges true면 실제 적용, false면 검사만 실행
	 */
	FJsonApplySummary RunProcessing(bool applyChanges);

	/**
	 * 최근 처리 결과를 저장하고 완료 이벤트를 호출한다.
	 *
	 * @param summary 이번에 완료된 JSON 처리 결과
	 */
	void CompleteProcessing(
		const FJsonApplySummary& summary
	);

private:
	/** 가장 최근 JSON 처리 결과다. */
	UPROPERTY(Transient)
	FJsonApplySummary lastSummary;

	/**
	 * 시작 시 자동 처리 여부 확인이 끝났는지 저장한다.
	 */
	UPROPERTY(Transient)
	bool initialValidationCompleted = false;

	/** 엔진 초기화 완료 Delegate Handle이다. */
	FDelegateHandle engineLoopInitCompleteHandle;

	/**
	 * JSON 전체 처리 완료를 알리는 네이티브 이벤트다.
	 */
	FOnJsonAssetSyncProcessingCompleted
		processingCompletedEvent;
};