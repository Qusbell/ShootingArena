#pragma once

#include "Modules/ModuleManager.h"

/**
 * JsonAssetSync Runtime 모듈의 진입점이다.
 *
 * 이 클래스는 플러그인 모듈이 메모리에 로드되거나 해제될 때 호출된다.
 * 실제 JSON 적용 기능은 이후 별도의 Subsystem과 Service 클래스로 분리한다.
 */
class FJsonAssetSyncModule : public IModuleInterface
{
public:
	/**
	 * JsonAssetSync 모듈이 로드될 때 호출된다.
	 */
	virtual void StartupModule() override;

	/**
	 * JsonAssetSync 모듈이 해제될 때 호출된다.
	 */
	virtual void ShutdownModule() override;
};