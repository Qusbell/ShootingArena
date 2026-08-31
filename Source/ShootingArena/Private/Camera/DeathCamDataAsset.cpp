#include "Camera/DeathCamDataAsset.h"

UMaterialInterface* UDeathCamDataAsset::GetKillerHighlightMaterial() const
{
	// 기획자가 선택한 표현 방식에 맞는 Material만 외부에 반환합니다.
	// 실제 Material 참조는 Setup 항목에 한 번 지정해두고 이후에는 Type만 변경합니다.
	switch (killerHighlightType)
	{
	case EDeathCamKillerHighlightType::Fill:
		return killerHighlightFillMaterial.Get();

	case EDeathCamKillerHighlightType::Outline:
	default:
		return killerHighlightOutlineMaterial.Get();
	}
}
