#include "Modules/ModuleManager.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/AssetData.h"

#include "Engine/Blueprint.h"

#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/CompilerResultsLog.h"

#include "FileHelpers.h"

#include "HAL/IConsoleManager.h"
#include "Misc/MessageDialog.h"
#include "Misc/ScopedSlowTask.h"

#include "Editor.h"
#include "UObject/StrongObjectPtr.h"


DEFINE_LOG_CATEGORY_STATIC(LogBPRefreshTool, Log, All);


class FShootingArenaEditorToolsModule : public IModuleInterface
{
public:

    virtual void StartupModule() override
    {
        RefreshBlueprintsCommand =
            MakeUnique<FAutoConsoleCommand>(
                TEXT("ShootingArena.RefreshBlueprints"),

                TEXT(
                    "Refresh, compile and save Blueprint assets.\n"
                    "Usage:\n"
                    "  ShootingArena.RefreshBlueprints\n"
                    "  ShootingArena.RefreshBlueprints /Game/SomeFolder\n"
                    "  ShootingArena.RefreshBlueprints /Game/SomeFolder resaveall"
                ),

                FConsoleCommandWithArgsDelegate::CreateRaw(
                    this,
                    &FShootingArenaEditorToolsModule::RefreshBlueprints
                ),

                ECVF_Default
            );
    }


    virtual void ShutdownModule() override
    {
        RefreshBlueprintsCommand.Reset();
    }


private:

    TUniquePtr<FAutoConsoleCommand> RefreshBlueprintsCommand;


    void RefreshBlueprints(const TArray<FString>& Args)
    {
        // PIE 중에는 실행 금지
        if (GEditor && GEditor->PlayWorld != nullptr)
        {
            UE_LOG(
                LogBPRefreshTool,
                Error,
                TEXT("Cannot refresh Blueprints while PIE is running.")
            );

            return;
        }


        // ------------------------------------------------------------
        // Arguments
        //
        // 기본:
        //   ShootingArena.RefreshBlueprints
        //
        // 특정 폴더:
        //   ShootingArena.RefreshBlueprints /Game/QuakeLike_1_0
        //
        // 강제 재저장:
        //   ShootingArena.RefreshBlueprints /Game/QuakeLike_1_0 resaveall
        // ------------------------------------------------------------

        FString RootPath = TEXT("/Game");
        bool bForceResaveAll = false;

        for (const FString& Arg : Args)
        {
            if (Arg.StartsWith(TEXT("/")))
            {
                RootPath = Arg;
            }
            else if (Arg.Equals(TEXT("resaveall"), ESearchCase::IgnoreCase))
            {
                bForceResaveAll = true;
            }
        }


        UE_LOG(
            LogBPRefreshTool,
            Display,
            TEXT("============================================================")
        );

        UE_LOG(
            LogBPRefreshTool,
            Display,
            TEXT("Blueprint refresh started. RootPath = %s, ForceResave = %s"),
            *RootPath,
            bForceResaveAll ? TEXT("true") : TEXT("false")
        );


        // ------------------------------------------------------------
        // Asset Registry
        // ------------------------------------------------------------

        FAssetRegistryModule& AssetRegistryModule =
            FModuleManager::LoadModuleChecked<FAssetRegistryModule>(
                TEXT("AssetRegistry")
            );

        IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();


        FARFilter Filter;

        Filter.PackagePaths.Add(FName(*RootPath));

        Filter.ClassPaths.Add(
            UBlueprint::StaticClass()->GetClassPathName()
        );

        Filter.bRecursivePaths = true;
        Filter.bRecursiveClasses = true;


        TArray<FAssetData> AssetDataList;

        AssetRegistry.GetAssets(
            Filter,
            AssetDataList
        );


        UE_LOG(
            LogBPRefreshTool,
            Display,
            TEXT("Found %d Blueprint assets."),
            AssetDataList.Num()
        );


        if (AssetDataList.IsEmpty())
        {
            UE_LOG(
                LogBPRefreshTool,
                Warning,
                TEXT("No Blueprints found under %s."),
                *RootPath
            );

            return;
        }


        // ------------------------------------------------------------
        // 확인창
        // ------------------------------------------------------------

        const FString ConfirmationMessage = FString::Printf(
            TEXT(
                "%d Blueprint assets will be refreshed and compiled.\n\n"
                "Root: %s\n"
                "Force Resave: %s\n\n"
                "Continue?"
            ),
            AssetDataList.Num(),
            *RootPath,
            bForceResaveAll ? TEXT("Yes") : TEXT("No")
        );


        if (FMessageDialog::Open(
            EAppMsgType::YesNo,
            FText::FromString(ConfirmationMessage)
        ) != EAppReturnType::Yes)
        {
            UE_LOG(
                LogBPRefreshTool,
                Warning,
                TEXT("Cancelled by user.")
            );

            return;
        }


        // ------------------------------------------------------------
        // Blueprint Load
        //
        // TStrongObjectPtr를 사용해서 일괄 컴파일 도중 GC가 발생하더라도
        // Blueprint 객체가 살아 있도록 유지합니다.
        // ------------------------------------------------------------

        TArray<TStrongObjectPtr<UBlueprint>> Blueprints;

        Blueprints.Reserve(AssetDataList.Num());


        for (const FAssetData& AssetData : AssetDataList)
        {
            UObject* Asset = AssetData.GetAsset();

            UBlueprint* Blueprint = Cast<UBlueprint>(Asset);

            if (!Blueprint)
            {
                UE_LOG(
                    LogBPRefreshTool,
                    Warning,
                    TEXT("Failed to load Blueprint: %s"),
                    *AssetData.GetObjectPathString()
                );

                continue;
            }

            Blueprints.Emplace(Blueprint);
        }


        UE_LOG(
            LogBPRefreshTool,
            Display,
            TEXT("Loaded %d Blueprint assets."),
            Blueprints.Num()
        );


        // 3단계 작업이므로 대략 * 3
        FScopedSlowTask SlowTask(
            Blueprints.Num() * 3.0f,
            FText::FromString(TEXT("Refreshing Blueprints..."))
        );

        SlowTask.MakeDialog(true);


        // ------------------------------------------------------------
        // PASS 1
        //
        // Refresh All Nodes
        // ------------------------------------------------------------

        UE_LOG(
            LogBPRefreshTool,
            Display,
            TEXT("PASS 1: Refresh All Nodes")
        );


        for (const TStrongObjectPtr<UBlueprint>& BlueprintPtr : Blueprints)
        {
            if (SlowTask.ShouldCancel())
            {
                UE_LOG(
                    LogBPRefreshTool,
                    Warning,
                    TEXT("Cancelled. Nothing will be saved.")
                );

                return;
            }


            UBlueprint* Blueprint = BlueprintPtr.Get();

            if (!Blueprint)
            {
                continue;
            }


            SlowTask.EnterProgressFrame(
                1.0f,
                FText::FromString(Blueprint->GetPathName())
            );


            UE_LOG(
                LogBPRefreshTool,
                Verbose,
                TEXT("Refreshing: %s"),
                *Blueprint->GetPathName()
            );


            FBlueprintEditorUtils::RefreshAllNodes(Blueprint);
        }


        // ------------------------------------------------------------
        // PASS 2
        //
        // Compile
        //
        // SkipSave를 사용합니다.
        // 컴파일 중 Save On Compile 설정 때문에 일부 BP만 먼저 저장되는
        // 상황을 방지합니다.
        // ------------------------------------------------------------

        UE_LOG(
            LogBPRefreshTool,
            Display,
            TEXT("PASS 2: Compile Blueprints")
        );


        int32 CompileErrorCount = 0;


        for (const TStrongObjectPtr<UBlueprint>& BlueprintPtr : Blueprints)
        {
            if (SlowTask.ShouldCancel())
            {
                UE_LOG(
                    LogBPRefreshTool,
                    Warning,
                    TEXT("Cancelled. Nothing will be saved.")
                );

                return;
            }


            UBlueprint* Blueprint = BlueprintPtr.Get();

            if (!Blueprint)
            {
                continue;
            }


            SlowTask.EnterProgressFrame(
                1.0f,
                FText::FromString(Blueprint->GetPathName())
            );


            FCompilerResultsLog Results;

            Results.bSilentMode = true;


            FKismetEditorUtilities::CompileBlueprint(
                Blueprint,
                EBlueprintCompileOptions::SkipSave,
                &Results
            );


            if (Results.NumErrors > 0 || Blueprint->Status == BS_Error)
            {
                ++CompileErrorCount;

                UE_LOG(
                    LogBPRefreshTool,
                    Error,
                    TEXT(
                        "Compile FAILED: %s "
                        "(Errors=%d, Warnings=%d)"
                    ),
                    *Blueprint->GetPathName(),
                    Results.NumErrors,
                    Results.NumWarnings
                );
            }
            else
            {
                UE_LOG(
                    LogBPRefreshTool,
                    Verbose,
                    TEXT(
                        "Compile OK: %s "
                        "(Warnings=%d)"
                    ),
                    *Blueprint->GetPathName(),
                    Results.NumWarnings
                );
            }
        }


        // 하나라도 컴파일 실패하면 자동 저장하지 않음
        if (CompileErrorCount > 0)
        {
            UE_LOG(
                LogBPRefreshTool,
                Error,
                TEXT(
                    "%d Blueprint(s) failed to compile. "
                    "Automatic save has been aborted."
                ),
                CompileErrorCount
            );

            FMessageDialog::Open(
                EAppMsgType::Ok,
                FText::FromString(
                    FString::Printf(
                        TEXT(
                            "%d Blueprint(s) failed to compile.\n\n"
                            "Nothing was automatically saved.\n"
                            "Check Output Log for details."
                        ),
                        CompileErrorCount
                    )
                )
            );

            return;
        }


        // ------------------------------------------------------------
        // PASS 3
        //
        // User Defined Struct 등의 최신 상태를 저장 전에 보장
        // ------------------------------------------------------------

        UE_LOG(
            LogBPRefreshTool,
            Display,
            TEXT("PASS 3: Prepare Blueprints for save")
        );


        TArray<UPackage*> PackagesToSave;


        for (const TStrongObjectPtr<UBlueprint>& BlueprintPtr : Blueprints)
        {
            if (SlowTask.ShouldCancel())
            {
                UE_LOG(
                    LogBPRefreshTool,
                    Warning,
                    TEXT("Cancelled. Nothing will be saved.")
                );

                return;
            }


            UBlueprint* Blueprint = BlueprintPtr.Get();

            if (!Blueprint)
            {
                continue;
            }


            SlowTask.EnterProgressFrame(
                1.0f,
                FText::FromString(Blueprint->GetPathName())
            );


            // // 중요:
            // // User Defined Struct 최신화 문제와 직접 관계된 UE 함수
            // FBlueprintEditorUtils::RecompileBeforeSaveIfNeeded(
            //     Blueprint
            // );


            // resaveall 옵션이면 변경 여부와 관계없이 패키지 재저장
            if (bForceResaveAll)
            {
                Blueprint->MarkPackageDirty();
            }


            PackagesToSave.AddUnique(
                Blueprint->GetOutermost()
            );
        }


        // RecompileBeforeSaveIfNeeded 이후 상태 재확인
        for (const TStrongObjectPtr<UBlueprint>& BlueprintPtr : Blueprints)
        {
            UBlueprint* Blueprint = BlueprintPtr.Get();

            if (Blueprint && Blueprint->Status == BS_Error)
            {
                UE_LOG(
                    LogBPRefreshTool,
                    Error,
                    TEXT(
                        "Blueprint entered error state before save: %s. "
                        "Save aborted."
                    ),
                    *Blueprint->GetPathName()
                );

                return;
            }
        }


        // ------------------------------------------------------------
        // SAVE
        // ------------------------------------------------------------

        UE_LOG(
            LogBPRefreshTool,
            Display,
            TEXT(
                "Saving dirty packages... "
                "Packages considered: %d"
            ),
            PackagesToSave.Num()
        );


        const bool bSaveSucceeded =
            UEditorLoadingAndSavingUtils::SavePackages(
                PackagesToSave,
                true       // Dirty package만 저장
            );


        if (!bSaveSucceeded)
        {
            UE_LOG(
                LogBPRefreshTool,
                Error,
                TEXT("One or more packages failed to save.")
            );

            FMessageDialog::Open(
                EAppMsgType::Ok,
                FText::FromString(
                    TEXT(
                        "One or more Blueprint packages failed to save.\n"
                        "Check Output Log."
                    )
                )
            );

            return;
        }


        UE_LOG(
            LogBPRefreshTool,
            Display,
            TEXT("Blueprint refresh completed successfully.")
        );

        UE_LOG(
            LogBPRefreshTool,
            Display,
            TEXT("============================================================")
        );


        FMessageDialog::Open(
            EAppMsgType::Ok,
            FText::FromString(
                FString::Printf(
                    TEXT(
                        "Blueprint refresh completed.\n\n"
                        "Processed: %d\n"
                        "Root: %s\n\n"
                        "Check Git status before committing."
                    ),
                    Blueprints.Num(),
                    *RootPath
                )
            )
        );
    }
};


IMPLEMENT_MODULE(
    FShootingArenaEditorToolsModule,
    ShootingArenaEditorTools
)