#pragma once

#include "CoreMinimal.h"

class UActorComponent;
class UPrimitiveComponent;
class AActor;

/** Blueprint Actor를 수정하지 않고 읽기 전용으로 검사하기 위한 Reflection Helper입니다. */
class FGameplayValidationReflectionUtils
{
public:
    static bool HasProperty(const UClass* Class, const TArray<FName>& CandidateNames);
    static UObject* GetObjectProperty(const UObject* Object, const TArray<FName>& CandidateNames);
    static AActor* GetActorProperty(const UObject* Object, const TArray<FName>& CandidateNames);

    static UActorComponent* FindComponent(const AActor* Actor, const TArray<FName>& CandidateNames);
    static UPrimitiveComponent* FindPrimitiveComponent(const AActor* Actor, const TArray<FName>& CandidateNames);

private:
    static FProperty* FindProperty(const UClass* Class, const TArray<FName>& CandidateNames);
    static bool MatchesName(const FProperty* Property, FName Candidate);
};
