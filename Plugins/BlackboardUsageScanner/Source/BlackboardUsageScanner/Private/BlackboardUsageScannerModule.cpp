#include "BlackboardUsageScanner.h"

#include "HAL/IConsoleManager.h"
#include "Modules/ModuleManager.h"

class FBlackboardUsageScannerModule final : public IModuleInterface
{
public:
    virtual void StartupModule() override
    {
        ScanCommand = IConsoleManager::Get().RegisterConsoleCommand(
            TEXT("BB.ScanUsage"),
            TEXT("Scan /Game Behavior Trees for exact FBlackboardKeySelector usages and export CSV reports."),
            FConsoleCommandDelegate::CreateStatic(&FBlackboardUsageScanner::Run),
            ECVF_Default);
    }

    virtual void ShutdownModule() override
    {
        if (ScanCommand)
        {
            IConsoleManager::Get().UnregisterConsoleObject(ScanCommand);
            ScanCommand = nullptr;
        }
    }

private:
    IConsoleObject* ScanCommand = nullptr;
};

IMPLEMENT_MODULE(FBlackboardUsageScannerModule, BlackboardUsageScanner)
