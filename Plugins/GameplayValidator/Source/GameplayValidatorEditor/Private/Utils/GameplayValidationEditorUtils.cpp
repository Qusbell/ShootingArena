#include "Utils/GameplayValidationEditorUtils.h"

#include "GameFramework/Actor.h"
#include "Editor.h"

void FGameplayValidationEditorUtils::FocusActor(AActor* Actor)
{
    if (!GEditor || !IsValid(Actor))
    {
        return;
    }

    GEditor->SelectNone(false, true, false);
    GEditor->SelectActor(Actor, true, true, true);
    GEditor->MoveViewportCamerasToActor(*Actor, false);
}
