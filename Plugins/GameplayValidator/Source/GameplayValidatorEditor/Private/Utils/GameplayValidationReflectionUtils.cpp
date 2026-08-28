#include "Utils/GameplayValidationReflectionUtils.h"

#include "GameFramework/Actor.h"
#include "Components/ActorComponent.h"
#include "Components/PrimitiveComponent.h"
#include "UObject/UnrealType.h"

bool FGameplayValidationReflectionUtils::MatchesName(const FProperty* Property, const FName Candidate)
{
    if (!Property)
    {
        return false;
    }

    if (Property->GetFName() == Candidate)
    {
        return true;
    }

    // Blueprint 변수의 Display Name에 공백이 들어간 경우까지 대응합니다.
    return Property->GetDisplayNameText().ToString().Equals(Candidate.ToString(), ESearchCase::IgnoreCase);
}

FProperty* FGameplayValidationReflectionUtils::FindProperty(
    const UClass* Class,
    const TArray<FName>& CandidateNames)
{
    if (!IsValid(Class))
    {
        return nullptr;
    }

    for (TFieldIterator<FProperty> It(Class, EFieldIterationFlags::IncludeSuper); It; ++It)
    {
        for (const FName Candidate : CandidateNames)
        {
            if (MatchesName(*It, Candidate))
            {
                return *It;
            }
        }
    }

    return nullptr;
}


bool FGameplayValidationReflectionUtils::HasProperty(
    const UClass* Class,
    const TArray<FName>& CandidateNames)
{
    return FindProperty(Class, CandidateNames) != nullptr;
}

UObject* FGameplayValidationReflectionUtils::GetObjectProperty(
    const UObject* Object,
    const TArray<FName>& CandidateNames)
{
    if (!IsValid(Object))
    {
        return nullptr;
    }

    FProperty* Property = FindProperty(Object->GetClass(), CandidateNames);
    const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property);
    if (!ObjectProperty)
    {
        return nullptr;
    }

    return ObjectProperty->GetObjectPropertyValue_InContainer(Object);
}

AActor* FGameplayValidationReflectionUtils::GetActorProperty(
    const UObject* Object,
    const TArray<FName>& CandidateNames)
{
    return Cast<AActor>(GetObjectProperty(Object, CandidateNames));
}

UActorComponent* FGameplayValidationReflectionUtils::FindComponent(
    const AActor* Actor,
    const TArray<FName>& CandidateNames)
{
    if (!IsValid(Actor))
    {
        return nullptr;
    }

    TArray<UActorComponent*> Components;
    Actor->GetComponents(Components);

    for (UActorComponent* Component : Components)
    {
        if (!IsValid(Component))
        {
            continue;
        }

        for (const FName Candidate : CandidateNames)
        {
            if (Component->GetFName() == Candidate
                || Component->GetName().Equals(Candidate.ToString(), ESearchCase::IgnoreCase))
            {
                return Component;
            }
        }
    }

    return nullptr;
}

UPrimitiveComponent* FGameplayValidationReflectionUtils::FindPrimitiveComponent(
    const AActor* Actor,
    const TArray<FName>& CandidateNames)
{
    return Cast<UPrimitiveComponent>(FindComponent(Actor, CandidateNames));
}
