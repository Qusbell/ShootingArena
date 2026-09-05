// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * 외부 데이터 편집기용 Manifest 생성 결과다.
 */
struct FJsonAssetSyncManifestExportResult
{
	/** Manifest 생성/검증이 성공했는지 나타낸다. */
	bool success = false;

	/** 기존 Manifest와 내용이 달라 실제 파일을 갱신했는지 나타낸다. */
	bool changed = false;

	/** 생성 대상으로 사용한 Manifest의 절대 파일 경로다. */
	FString manifestPath;

	/** JSON Relative Path가 비어 있어 자동으로 채운 Binding 개수다. */
	int32 autoFilledPathCount = 0;

	/** 자동 경로 처리 과정에서 새로 생성한 JSON 파일 개수다. */
	int32 createdJsonFileCount = 0;

	/** 생성 과정에서 발견된 오류 메시지다. */
	TArray<FString> errors;
};

/** 에디터에서 저장한 대상 에셋의 JSON 반영 결과다. */
struct FJsonAssetSyncAssetExportResult
{
	/** 저장된 에셋이 Registry의 JSON 동기화 대상이었는지 나타낸다. */
	bool wasHandled = false;

	/** 대상 Binding의 JSON 기록이 모두 성공했는지 나타낸다. */
	bool success = true;

	/** 실제로 갱신한 JSON 파일 개수다. */
	int32 updatedJsonFileCount = 0;

	/** 내보내기 과정에서 발생한 오류 메시지다. */
	TArray<FString> errors;
};

/**
 * Registry에 등록된 DataTable/DataAsset의 실제 Unreal Reflection 정보를 읽어
 * 외부 데이터 편집기가 사용할 JsonAssetSyncManifest.json을 생성한다.
 *
 * 이 클래스는 에디터 전용이며 기존 JSON Apply/Save 경로와 독립적으로 동작한다.
 */
class FJsonAssetSyncSchemaExporter final
{
public:
	/**
	 * 현재 Registry를 기준으로 Manifest를 생성한다.
	 *
	 * @param forceRewrite true면 기존 내용과 같아도 다시 저장한다.
	 */
	static FJsonAssetSyncManifestExportResult ExportManifest(
		bool forceRewrite = false
	);

	/**
	 * Registry에 등록된 대상 에셋이 Unreal Editor에서 저장됐을 때,
	 * 그 에셋의 현재 값을 연결된 외부 JSON 파일에 기록한다.
	 */
	static FJsonAssetSyncAssetExportResult ExportSavedAssetJson(
		UObject* savedAsset
	);
};
