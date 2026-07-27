// Copyright Epic Games, Inc. All Rights Reserved.

#include "JsonApplyRegistry.h"

/*
 * 현재 UJsonApplyRegistry는 데이터 보관 역할만 담당하므로
 * 별도의 함수 구현이 필요하지 않다.
 *
 * Registry 검사, JSON 파일 읽기, DataTable/DataAsset 적용은
 * 이후 생성할 FJsonApplyService에서 담당한다.
 *
 * 구현 파일을 별도로 유지하는 이유는 나중에 Registry 전용 검증,
 * 에디터 변경 대응, Primary Asset 관련 함수가 추가될 경우
 * 기존 헤더 구조를 바꾸지 않고 확장하기 위해서다.
 */