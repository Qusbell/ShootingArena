#include "JsonAssetSync.h"

#define LOCTEXT_NAMESPACE "FJsonAssetSyncModule"

void FJsonAssetSyncModule::StartupModule()
{
	/*
	 * Runtime 모듈이 메모리에 로드된 직후 호출된다.
	 *
	 * 실제 JSON 적용은 UEngineSubsystem에서 처리할 예정이므로,
	 * 현재 모듈 진입점에서는 별도 초기화 작업을 수행하지 않는다.
	 */
}

void FJsonAssetSyncModule::ShutdownModule()
{
	/*
	 * 플러그인 비활성화, 에디터 종료 또는 모듈 리로드 과정에서 호출된다.
	 *
	 * 현재는 직접 해제해야 할 Delegate나 외부 자원이 없으므로
	 * 별도의 종료 처리를 수행하지 않는다.
	 */
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FJsonAssetSyncModule, JsonAssetSync)