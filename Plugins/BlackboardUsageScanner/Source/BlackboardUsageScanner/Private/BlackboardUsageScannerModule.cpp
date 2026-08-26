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
            TEXT(
                "Scan Blackboard usage. "
                "Optional argument filters Blackboard asset name/path. "
                "Example: BB.ScanUsage BB_QuakeBoard"),
            FConsoleCommandWithArgsDelegate::CreateStatic(
                &FBlackboardUsageScanner::Run),
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
