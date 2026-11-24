#if WITH_EDITOR
#include "Widget/OBSubtitleEditorHelpers.h"
#include "Widget/OBHudPanelWidget.h"
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "HAL/IConsoleManager.h"

namespace OBSubtitleEditor
{
bool ValidateEmbeddedSubtitle(UWidgetBlueprint* Overlay)
{
    if (!Overlay || !Overlay->WidgetTree) return false;
    auto* Subtitle=Overlay->WidgetTree->FindWidget(TEXT("Subtitle"));
    auto* Class=LoadClass<UOBSubtitleWidget>(nullptr,TEXT("/Game/Blueprints/Widget/PlayerOverlay/Components/WBP_Subtitle.WBP_Subtitle_C"));
    return Subtitle && Class && Subtitle->IsA(Class);
}

static void Validate()
{
    auto* Overlay=LoadObject<UWidgetBlueprint>(nullptr,TEXT("/Game/Blueprints/Widget/PlayerOverlay/WBP_PlayerOverlay.WBP_PlayerOverlay"));
    const bool OK=ValidateEmbeddedSubtitle(Overlay);
    UE_LOG(LogTemp,Display,TEXT("EMBEDDED_SUBTITLE WBP_PlayerOverlay.Subtitle uses WBP_Subtitle: %s"),OK ? TEXT("PASS") : TEXT("FAIL"));
}
static FAutoConsoleCommand Command(TEXT("OB.UI.ValidateEmbeddedSubtitle"),TEXT("Check that PlayerOverlay embeds the existing WBP_Subtitle. Does not alter widgets, placement or animations."),FConsoleCommandDelegate::CreateStatic(&Validate));
}
#endif