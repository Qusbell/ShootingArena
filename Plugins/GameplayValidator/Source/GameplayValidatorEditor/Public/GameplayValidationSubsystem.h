#pragma once

#include "CoreMinimal.h"
#include "EditorSubsystem.h"
#include "Core/GameplayValidationProviderRegistry.h"
#include "Core/GameplayValidationReport.h"
#include "Scanner/GameplayValidationWorldScanner.h"
#include "GameplayValidationSubsystem.generated.h"

DECLARE_MULTICAST_DELEGATE(FOnGameplayValidationReportChanged);

/** 검사 실행과 결과 수명만 관리합니다. 실제 규칙은 Provider에 있습니다. */
UCLASS()
class GAMEPLAYVALIDATOREDITOR_API UGameplayValidationSubsystem : public UEditorSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    const FGameplayValidationReport& ValidateCurrentLevel();
    const FGameplayValidationReport& GetLastReport() const { return LastReport; }
    const TArray<FGameplayValidationScanMessage>& GetConfigurationMessages() const { return ConfigurationMessages; }
    const FGameplayValidationProviderRegistry& GetProviderRegistry() const { return ProviderRegistry; }

    void ClearReport();
    FOnGameplayValidationReportChanged& OnReportChanged() { return ReportChanged; }

private:
    UWorld* GetEditorWorld() const;
    void HandleMapOpened(const FString& Filename, bool bAsTemplate);

    FGameplayValidationProviderRegistry ProviderRegistry;
    FGameplayValidationReport LastReport;
    TArray<FGameplayValidationScanMessage> ConfigurationMessages;
    FOnGameplayValidationReportChanged ReportChanged;
};
