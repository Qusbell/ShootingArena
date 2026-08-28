#include "Modules/ModuleManager.h"

#include "LevelEditor.h"
#include "Styling/AppStyle.h"
#include "ToolMenus.h"
#include "UI/SGameplayValidatorPanel.h"
#include "Widgets/Docking/SDockTab.h"

#define LOCTEXT_NAMESPACE "GameplayValidatorEditorModule"

namespace GameplayValidatorEditor
{
    static const FName TabName(TEXT("GameplayValidator"));
}

class FGameplayValidatorEditorModule final : public IModuleInterface
{
public:
    virtual void StartupModule() override
    {
        FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
            GameplayValidatorEditor::TabName,
            FOnSpawnTab::CreateRaw(this, &FGameplayValidatorEditorModule::SpawnValidatorTab))
            .SetDisplayName(LOCTEXT("TabTitle", "Gameplay Validator"))
            .SetMenuType(ETabSpawnerMenuType::Hidden);

        UToolMenus::RegisterStartupCallback(
            FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FGameplayValidatorEditorModule::RegisterMenus));
    }

    virtual void ShutdownModule() override
    {
        UToolMenus::UnRegisterStartupCallback(this);
        UToolMenus::UnregisterOwner(this);
        FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(GameplayValidatorEditor::TabName);
    }

private:
    void RegisterMenus()
    {
        FToolMenuOwnerScoped OwnerScoped(this);

        UToolMenu* Menu = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.MainMenu.Tools"));
        FToolMenuSection& Section = Menu->FindOrAddSection(TEXT("GameplayValidator"));
        Section.AddMenuEntry(
            TEXT("OpenGameplayValidator"),
            LOCTEXT("OpenGameplayValidator", "Gameplay Validator"),
            LOCTEXT("OpenGameplayValidatorTooltip", "Open the Gameplay Validator window."),
            FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Icons.Search")),
            FUIAction(FExecuteAction::CreateRaw(this, &FGameplayValidatorEditorModule::OpenValidatorTab)));
    }

    void OpenValidatorTab()
    {
        FGlobalTabmanager::Get()->TryInvokeTab(GameplayValidatorEditor::TabName);
    }

    TSharedRef<SDockTab> SpawnValidatorTab(const FSpawnTabArgs& Args)
    {
        return SNew(SDockTab)
            .TabRole(ETabRole::NomadTab)
            [
                SNew(SGameplayValidatorPanel)
            ];
    }
};

IMPLEMENT_MODULE(FGameplayValidatorEditorModule, GameplayValidatorEditor)

#undef LOCTEXT_NAMESPACE
