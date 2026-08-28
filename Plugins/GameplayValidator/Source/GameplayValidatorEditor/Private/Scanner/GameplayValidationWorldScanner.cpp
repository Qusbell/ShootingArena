#include "Scanner/GameplayValidationWorldScanner.h"

#include "GameplayValidatorSettings.h"
#include "EngineUtils.h"

namespace
{
    struct FResolvedBinding
    {
        FGameplayValidationTargetKey Key;
        UClass* TargetClass = nullptr;
        int32 InheritanceDepth = 0;
    };

    int32 GetInheritanceDepth(const UClass* Class)
    {
        int32 Depth = 0;
        for (const UClass* Current = Class; Current; Current = Current->GetSuperClass())
        {
            ++Depth;
        }
        return Depth;
    }
}

bool FGameplayValidationWorldScanner::BuildContext(
    UWorld* World,
    const FGameplayValidationProviderRegistry& Registry,
    FGameplayValidationContext& OutContext,
    TArray<FGameplayValidationScanMessage>& OutConfigurationMessages)
{
    OutContext = FGameplayValidationContext();
    OutConfigurationMessages.Reset();

    if (!IsValid(World))
    {
        return false;
    }

    OutContext.World = World;

    const UGameplayValidatorSettings* Settings = GetDefault<UGameplayValidatorSettings>();
    TArray<FResolvedBinding> ResolvedBindings;

    for (const TSharedRef<IGameplayValidationProvider>& Provider : Registry.GetProviders())
    {
        TArray<FGameplayValidationTargetSlot> Slots;
        Provider->GetTargetSlots(Slots);

        // 같은 Provider 안의 두 슬롯에 동일한 BP를 등록하면 분류가 모호해지므로
        // 첫 슬롯만 사용하고 Configuration 메시지로 명확히 알려줍니다.
        TMap<UClass*, FName> FirstSlotByClass;

        for (const FGameplayValidationTargetSlot& Slot : Slots)
        {
            const FGameplayValidationTargetKey Key(Provider->GetProviderId(), Slot.SlotId);
            OutContext.ActorsByTarget.FindOrAdd(Key);

            UClass* TargetClass = Settings->LoadTargetClass(Key.ProviderId, Key.SlotId);
            if (!IsValid(TargetClass))
            {
                FGameplayValidationScanMessage& Message = OutConfigurationMessages.AddDefaulted_GetRef();
                Message.Key = Key;
                Message.Message = FText::Format(
                    NSLOCTEXT("GameplayValidator", "TargetNotRegistered", "{0} 검사 Blueprint가 등록되지 않아 이 항목은 건너뜁니다."),
                    Slot.DisplayName);
                continue;
            }

            if (const FName* ExistingSlot = FirstSlotByClass.Find(TargetClass))
            {
                FGameplayValidationScanMessage& Message = OutConfigurationMessages.AddDefaulted_GetRef();
                Message.Key = Key;
                Message.Message = FText::Format(
                    NSLOCTEXT(
                        "GameplayValidator",
                        "DuplicateTargetClassBinding",
                        "같은 Blueprint Class가 동일 Provider의 여러 슬롯에 등록되어 있습니다. {0} 슬롯은 건너뜁니다. 먼저 등록된 Slot: {1}"),
                    Slot.DisplayName,
                    FText::FromName(*ExistingSlot));
                continue;
            }

            FText FailureReason;
            if (!Provider->ValidateTargetClass(Slot.SlotId, TargetClass, FailureReason))
            {
                FGameplayValidationScanMessage& Message = OutConfigurationMessages.AddDefaulted_GetRef();
                Message.Key = Key;
                Message.Message = FailureReason.IsEmpty()
                    ? NSLOCTEXT("GameplayValidator", "InvalidTargetClass", "등록한 Blueprint Class가 해당 검사 Provider와 호환되지 않습니다.")
                    : FailureReason;
                continue;
            }

            FirstSlotByClass.Add(TargetClass, Slot.SlotId);

            FResolvedBinding& Resolved = ResolvedBindings.AddDefaulted_GetRef();
            Resolved.Key = Key;
            Resolved.TargetClass = TargetClass;
            Resolved.InheritanceDepth = GetInheritanceDepth(TargetClass);
        }
    }

    // Actor 전체 순회는 이 한 번뿐입니다.
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        AActor* Actor = *It;
        if (!IsValid(Actor))
        {
            continue;
        }

        // 같은 Provider의 슬롯끼리 상속 관계가 있을 경우 더 구체적인 Class 슬롯 하나만 선택합니다.
        TMap<FName, const FResolvedBinding*> BestMatchByProvider;

        for (const FResolvedBinding& Binding : ResolvedBindings)
        {
            if (!Actor->IsA(Binding.TargetClass))
            {
                continue;
            }

            const FResolvedBinding** Existing = BestMatchByProvider.Find(Binding.Key.ProviderId);
            if (!Existing || Binding.InheritanceDepth > (*Existing)->InheritanceDepth)
            {
                BestMatchByProvider.Add(Binding.Key.ProviderId, &Binding);
            }
        }

        for (const TPair<FName, const FResolvedBinding*>& Match : BestMatchByProvider)
        {
            OutContext.ActorsByTarget.FindOrAdd(Match.Value->Key).Add(Actor);
        }
    }

    return true;
}
