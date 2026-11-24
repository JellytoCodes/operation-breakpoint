#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Widget/OBPlayerOverlayWidget.h"
#include "Widget/OBHudPanelWidget.h"
#include "Training/OBTrainingStatusWidget.h"
#include "Training/OBTrainingResultWidget.h"
#include "Training/OBTrainingDirector.h"
#include "Core/OBPlayerController.h"
#include "Widget/OBHUD.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Spacer.h"
#include "Components/Border.h"
#include "Components/SizeBox.h"
#include "Components/ProgressBar.h"
#include "Components/CanvasPanel.h"
#include "Widget/OBRadialProgressBar.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "UObject/UnrealType.h"
#include "InputKeyEventArgs.h"
#if WITH_EDITOR
#include "WidgetBlueprint.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/TextWidgetTypes.h"
#endif

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOBTrainingWidgetBlueprintTest,"OperationBreakpoint.Training.WidgetBlueprints",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FOBTrainingWidgetBlueprintTest::RunTest(const FString&)
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    UClass* OverlayClass=LoadClass<UOBPlayerOverlayWidget>(nullptr,TEXT("/Game/Blueprints/Widget/PlayerOverlay/WBP_PlayerOverlay.WBP_PlayerOverlay_C"));
    if (!TestNotNull(TEXT("PlayerOverlay class"),OverlayClass)) return false;
    auto* Overlay=CreateWidget<UOBPlayerOverlayWidget>(World,OverlayClass); const auto Slate=Overlay->TakeWidget();
    const TCHAR* Names[]={TEXT("Guide"),TEXT("Subtitle"),TEXT("Mission"),TEXT("Timer"),TEXT("Life"),TEXT("TrainingMarkers"),TEXT("TrainingResult")};
    for (const TCHAR* Name:Names)
    {
        auto* Child=Overlay->WidgetTree->FindWidget(Name);
        auto* Property=FindFProperty<FObjectPropertyBase>(Overlay->GetClass(),Name);
        if (!TestNotNull(FString::Printf(TEXT("Overlay child %s"),Name),Child)) return false;
        TestTrue(FString::Printf(TEXT("Overlay binding %s"),Name),Property && Property->GetObjectPropertyValue_InContainer(Overlay)==Child);
        if (FString(Name)!=TEXT("Subtitle")) TestNotNull(FString::Printf(TEXT("Child has editable WBP class %s"),Name),Child->GetClass()->ClassGeneratedBy.Get());
    }
    const EOBHudPanelRole Roles[]={EOBHudPanelRole::Guide,EOBHudPanelRole::Subtitle,EOBHudPanelRole::Mission,EOBHudPanelRole::Timer,EOBHudPanelRole::Life};
    for (int32 I=0;I<5;++I)
    {
        if (I==1) continue; // Subtitle is part of the overlay's own editable hierarchy.
        auto* Panel=Cast<UOBHudPanelWidget>(Overlay->WidgetTree->FindWidget(Names[I]));
        if (!TestNotNull(TEXT("Independent panel C++ type"),Panel)) return false;
        TestEqual(TEXT("Panel role persisted on WBP default"),Panel->Role,Roles[I]);
    }
    TestEqual(TEXT("Stage mode enables only Timer and Life"),Overlay->VisibleElements,48);
    TestEqual(TEXT("Guide hidden in stage mode"),Overlay->WidgetTree->FindWidget(TEXT("Guide"))->GetVisibility(),ESlateVisibility::Collapsed);
    auto* Timer=Cast<UOBHudPanelWidget>(Overlay->WidgetTree->FindWidget(TEXT("Timer")));
    auto* TimerText=Cast<UTextBlock>(Timer->WidgetTree->FindWidget(TEXT("ValueText")));
    if (!TestNotNull(TEXT("Independent timer value"),TimerText)) return false;
    auto Font=TimerText->GetFont(); Font.Size=37; TimerText->SetFont(Font);
    Overlay->SetStageTime(90);
    TestEqual(TEXT("Stage timer update reaches child"),TimerText->GetText().ToString(),FString(TEXT("01 : 30")));
    TestEqual(TEXT("Child data updates preserve Designer font"),TimerText->GetFont().Size,37.f);
    auto* Life=Cast<UOBHudPanelWidget>(Overlay->WidgetTree->FindWidget(TEXT("Life")));
    auto* MaximumText=Cast<UTextBlock>(Life->WidgetTree->FindWidget(TEXT("Text_Life_Max")));
    const FText MaximumLabel=MaximumText ? MaximumText->GetText() : FText::GetEmpty();
    Overlay->SetHealth(40,100);
    auto* HealthText=Cast<UTextBlock>(Life->WidgetTree->FindWidget(TEXT("Text_Life")));
    if (!HealthText) HealthText=Cast<UTextBlock>(Life->WidgetTree->FindWidget(TEXT("TitleText")));
    auto* Heart=Cast<UProgressBar>(Life->WidgetTree->FindWidget(TEXT("HealthProgress")));
    if (!TestNotNull(TEXT("Numeric health display"),HealthText) || !TestNotNull(TEXT("Heart display"),Heart)) return false;
    TestEqual(TEXT("Health label follows current health without a duplicate maximum"),HealthText->GetText().ToString(),FString(TEXT("2")));
    TestEqual(TEXT("Life text uses the requested sRGB color"),HealthText->GetColorAndOpacity().GetSpecifiedColor().ToFColorSRGB(),FColor::FromHex(TEXT("2E5D85FF")));
    TestEqual(TEXT("Heart keeps its shape while masking the fill"),Heart->GetBarFillStyle(),EProgressBarFillStyle::Mask);
    TestEqual(TEXT("Heart fills from the bottom"),Heart->GetBarFillType(),EProgressBarFillType::BottomToTop);
    TestTrue(TEXT("Fill excludes the original texture's transparent top and bottom margins"),Heart->GetBorderPadding().Y>0 && Heart->GetBorderPadding().Y<32);
    TestEqual(TEXT("Empty heart remains behind the fill"),Heart->GetWidgetStyle().BackgroundImage.GetResourceObject()->GetName(),FString(TEXT("HP_None")));
    TestEqual(TEXT("Fill retains the original heart image"),Heart->GetWidgetStyle().FillImage.GetResourceObject()->GetName(),FString(TEXT("HP_Full")));
    Heart->ForceLayoutPrepass();
    TestEqual(TEXT("Heart keeps the authored square size"),Heart->GetDesiredSize(),FVector2D(64,64));
    for (int32 Lives=5;Lives>=0;--Lives)
    {
        Overlay->SetHealth(Lives,5);
        TestEqual(FString::Printf(TEXT("Life label after %d hits"),5-Lives),HealthText->GetText().ToString(),FString::FromInt(Lives));
        TestEqual(FString::Printf(TEXT("Heart fill after %d hits"),5-Lives),Heart->GetPercent(),Lives/5.f);
    }
    Overlay->SetHealth(55,100);
    TestEqual(TEXT("Partial damage uses the exact health ratio"),Heart->GetPercent(),.55f);
    Overlay->SetHealth(0,0);
    TestEqual(TEXT("Pending gameplay health retains the full-heart default"),Heart->GetPercent(),1.f);
    Overlay->SetHealth(100,100);
    if (MaximumText) TestTrue(TEXT("Gameplay updates preserve the authored maximum label"),MaximumText->GetText().EqualTo(MaximumLabel));
    TestEqual(TEXT("Restored health refills the heart"),Heart->GetPercent(),1.f);
    Overlay->SetPanelEnabled(EOBOverlayElement::Life,false);
    TestEqual(TEXT("Central control hides Life"),Life->GetVisibility(),ESlateVisibility::Collapsed);
    auto* Director=World->SpawnActor<AOBTrainingDirector>(); Director->bUseSplinePrototype=true; Director->CurrentStep=EOBTrainingStep::Welcome; Director->CurrentDialogueId=3;
    Overlay->SetTrainingDirector(Director);
    TestTrue(TEXT("Guide visible during briefing"),Overlay->WidgetTree->FindWidget(TEXT("Guide"))->IsVisible());
    TestEqual(TEXT("Life hidden during training"),Life->GetVisibility(),ESlateVisibility::Collapsed);
    Director->bPrototypePaused=true; Overlay->SetTrainingDirector(Director);
    TestTrue(TEXT("Warning result visible when basic practice pauses"),Overlay->GetTrainingResultWidget()->IsVisible());
    TestEqual(TEXT("Guide hidden during pause"),Overlay->WidgetTree->FindWidget(TEXT("Guide"))->GetVisibility(),ESlateVisibility::Collapsed);
    TestNull(TEXT("Obsolete Pause panel is removed"),Overlay->WidgetTree->FindWidget(TEXT("Pause")));
    TestEqual(TEXT("Pause uses the Warning page"),Overlay->GetTrainingResultWidget()->CurrentUIId,3008);
    auto* Subtitle=Cast<UOBSubtitleWidget>(Overlay->WidgetTree->FindWidget(TEXT("Subtitle")));
    if (!TestNotNull(TEXT("Overlay retains the original Subtitle UserWidget"),Subtitle)) return false;
    int32 SubtitleTexts=0;
    Subtitle->WidgetTree->ForEachWidget([&](UWidget* W) { SubtitleTexts+=W->IsA<UTextBlock>(); });
    TestEqual(TEXT("Subtitle exactly two TextBlocks"),SubtitleTexts,2);
    auto* SubtitleSize=Cast<USizeBox>(Subtitle->WidgetTree->FindWidget(TEXT("BaselineSizeBox")));
    if (!TestNotNull(TEXT("Subtitle has editable baseline size container"),SubtitleSize)) return false;
    // Baseline width, alignment wrappers and row slot sizes are authored by the user in UMG.
    TestNotNull(TEXT("Authored subtitle retains speaker field"),Subtitle->GetWidgetFromName(TEXT("Name")));
    TestNotNull(TEXT("Authored subtitle retains script field"),Subtitle->GetWidgetFromName(TEXT("Script")));
    Overlay->ShowPersistentSubtitle(FText::FromString(TEXT("교관")),FText::FromString(TEXT("총기 상태를 확인합니다.")));
    TestEqual(TEXT("Subtitle separates speaker"),Cast<UTextBlock>(Subtitle->GetWidgetFromName(TEXT("Name")))->GetText().ToString(),FString(TEXT("교관")));
    TestEqual(TEXT("Subtitle only outputs script"),Overlay->GetSubtitleText().ToString(),FString(TEXT("총기 상태를 확인합니다.")));
    auto* MissionSlot=Cast<UCanvasPanelSlot>(Overlay->WidgetTree->FindWidget(TEXT("Mission"))->Slot);
    auto* SubtitleSlot=Cast<UCanvasPanelSlot>(Overlay->WidgetTree->FindWidget(TEXT("Subtitle"))->Slot);
    TestEqual(TEXT("Mission anchored top left"),MissionSlot->GetAnchors().Minimum,FVector2D::ZeroVector);
    TestEqual(TEXT("Subtitle anchored bottom"),SubtitleSlot->GetAnchors().Minimum.Y,1.);
    TestEqual(TEXT("Subtitle region is horizontally centered"),SubtitleSlot->GetAnchors().Minimum.X+SubtitleSlot->GetAnchors().Maximum.X,1.);
    auto* Result=Overlay->GetTrainingResultWidget(); Result->bAutomaticNavigation=false;
    for (const TCHAR* FullscreenName : {TEXT("TrainingMarkers"),TEXT("TrainingResult")})
    {
        auto* FullscreenSlot=Cast<UCanvasPanelSlot>(Overlay->WidgetTree->FindWidget(FullscreenName)->Slot);
        TestEqual(TEXT("Marker and result viewport origins preserved"),FullscreenSlot->GetAnchors().Minimum,FVector2D::ZeroVector);
        TestEqual(TEXT("Marker and result remain fullscreen"),FullscreenSlot->GetAnchors().Maximum,FVector2D(1,1));
    }
    FOBTrainingResultData Data; Data.bPrototypeFlow=true; Data.bScenarioCompleted=true; Data.RecordedFireLimitSeconds=120; Data.RecordedFireSeconds=75;
    Overlay->ShowTrainingResult(Data);
    TestTrue(TEXT("Result is owned and displayed by Overlay"),Overlay->IsTrainingResultVisible());
    TestTrue(TEXT("Mockup completion passes independently of certification"),Result->IsScenarioPassed());
    int32 Texts=0,Buttons=0,Images=0;
    Result->WidgetTree->ForEachWidget([&](UWidget* W) { Texts+=W->IsA<UTextBlock>(); Buttons+=W->IsA<UButton>(); Images+=W->IsA<UImage>(); });
    TestTrue(TEXT("Result includes both tutorial and stage report fields"),Texts>=20); TestEqual(TEXT("Action boxes are display-only, with no buttons"),Buttons,0); TestTrue(TEXT("Result action boxes retain artwork"),Images>=2);
    auto* Interaction=Cast<UTextBlock>(Result->WidgetTree->FindWidget(TEXT("InteractionText")));
    if (!TestNotNull(TEXT("Result input guidance is bound"),Interaction)) return false;
    TestTrue(TEXT("Result explains mission travel and retraining"),Interaction->GetText().ToString().Contains(TEXT("미션 이동")) && Interaction->GetText().ToString().Contains(TEXT("재훈련")));
    auto* Header=Cast<UTextBlock>(Result->GetWidgetFromName(TEXT("ResultHeaderText")));
    TestNull(TEXT("Requested English header is removed"),Header);
    auto* ShortHint=Cast<UTextBlock>(Result->GetWidgetFromName(TEXT("NextActionHint")));
    auto* LongHint=Cast<UTextBlock>(Result->GetWidgetFromName(TEXT("RetryActionHint")));
    if (!TestNotNull(TEXT("Short trigger hint exists"),ShortHint) || !TestNotNull(TEXT("Held trigger hint exists"),LongHint)) return false;
    TestTrue(TEXT("Display boxes retain different short and held inputs"),ShortHint->GetText().ToString().Contains(TEXT("한 번")) && LongHint->GetText().ToString().Contains(TEXT("길게")));
    auto* ResultBorder=Cast<UBorder>(Result->WidgetTree->FindWidget(TEXT("ResultBorder")));
    if (!TestNotNull(TEXT("Result background binds"),ResultBorder)) return false;
    TestNotNull(TEXT("Pass border texture loaded"),ResultBorder->Background.GetResourceObject());
    TestEqual(TEXT("Original PASS stamp is retained"),ResultBorder->Background.GetResourceObject()->GetName(),FString(TEXT("SubUI_Pass")));
    Data.RecordedFireSeconds=121; Overlay->ShowTrainingResult(Data);
    TestFalse(TEXT("Over two minutes gets fail stamp"),Result->IsScenarioPassed());
    TestEqual(TEXT("Original FAIL stamp is retained"),ResultBorder->Background.GetResourceObject()->GetName(),FString(TEXT("SubUI_Fail")));
    TestTrue(TEXT("Fail also displays completion screen"),Overlay->IsTrainingResultVisible());
    TestEqual(TEXT("Result suppresses other HUD panels"),Overlay->VisibleElements,128);
    Result->HandleTrigger(true); TestEqual(TEXT("Press does not navigate prematurely"),Result->LastAction,EOBTrainingResultAction::None);
    Result->HandleTrigger(false); TestEqual(TEXT("Short released trigger selects next stage even on failure"),Result->LastAction,EOBTrainingResultAction::NextStage);
    Result->SetStageCompletion(true);
    TestTrue(TEXT("Stage completion displays the supplied success"),Result->IsScenarioPassed());
    TestEqual(TEXT("Stage completion hides the tutorial stamp frame"),ResultBorder->GetVisibility(),ESlateVisibility::Collapsed);
    TestTrue(TEXT("Portrait stage report is shown"),Result->GetWidgetFromName(TEXT("StageResultPanel"))->IsVisible());
    const auto* StageFitSlot=CastChecked<UCanvasPanelSlot>(Result->GetWidgetFromName(TEXT("StageResultPanel"))->Slot);
    TestTrue(TEXT("Stage report scales inside both small and full-screen viewports"),StageFitSlot->GetAnchors().Minimum==FVector2D::ZeroVector && StageFitSlot->GetAnchors().Maximum==FVector2D(1,1));
    Result->HandleTrigger(true); Result->HandleTrigger(false);
    TestEqual(TEXT("Stage short release selects retry"),Result->LastAction,EOBTrainingResultAction::Retry);
    Result->SetStageCompletion(false);
    TestFalse(TEXT("Stage failure displays the supplied failure"),Result->IsScenarioPassed());
    TestEqual(TEXT("Failed rescue also hides the tutorial stamp frame"),ResultBorder->GetVisibility(),ESlateVisibility::Collapsed);
    TestEqual(TEXT("Both rescue outcomes have neutral title"),Cast<UTextBlock>(Result->GetWidgetFromName(TEXT("StageTitleText")))->GetText().ToString(),FString(TEXT("훈련 종료")));
    auto* StageBar=CastChecked<UProgressBar>(Result->GetWidgetFromName(TEXT("StageMenuHoldProgress")));
    auto* StageBarSlot=CastChecked<UCanvasPanelSlot>(StageBar->Slot);
    TestEqual(TEXT("Stage held input fills the whole home box"),StageBarSlot->GetSize(),FVector2D(291,111));
    TestTrue(TEXT("The home icon stays on top of the fill"),CastChecked<UCanvasPanelSlot>(Result->GetWidgetFromName(TEXT("StageMenuIcon"))->Slot)->GetZOrder()>StageBarSlot->GetZOrder());
    TestTrue(TEXT("The home label stays on top of the fill"),CastChecked<UCanvasPanelSlot>(Result->GetWidgetFromName(TEXT("StageMenuText"))->Slot)->GetZOrder()>StageBarSlot->GetZOrder());
    Result->HandleTrigger(true); Result->AdvanceInteraction(1.f);
    TestEqual(TEXT("Stage home gauge is half full after one second"),StageBar->GetPercent(),.5f);
    Result->AdvanceInteraction(1.f);
    TestEqual(TEXT("Stage home gauge fills before navigation"),StageBar->GetPercent(),1.f);
    TestEqual(TEXT("Stage hold selects main menu"),Result->LastAction,EOBTrainingResultAction::NextStage);
    Data.ShotsFired=9; Data.ValidHits=8; Data.TotalSeconds=378;
    Result->SetResult(Data); Result->SetStageCompletion(false,false);
    TestEqual(TEXT("Real hit rate is formatted"),Cast<UTextBlock>(Result->GetWidgetFromName(TEXT("StageAccuracyText")))->GetText().ToString(),FString(TEXT("88.9 %")));
    TestEqual(TEXT("Real elapsed time is formatted"),Cast<UTextBlock>(Result->GetWidgetFromName(TEXT("StageTimeText")))->GetText().ToString(),FString(TEXT("06:18")));
    TestTrue(TEXT("Failed record write is not described as saved"),Cast<UTextBlock>(Result->GetWidgetFromName(TEXT("StageReportText")))->GetText().ToString().Contains(TEXT("못했습니다")));
    TestEqual(TEXT("Unspecified rank stays blank"),Cast<UTextBlock>(Result->GetWidgetFromName(TEXT("StageRankText")))->GetText().ToString(),FString(TEXT("—")));
    Result->SetResult(Data);
    auto* Bar=CastChecked<UProgressBar>(Result->GetWidgetFromName(TEXT("RetryHoldProgress")));
    TestEqual(TEXT("Opening a new result clears gauge"),Bar->GetPercent(),0.f);
    Result->HandleTrigger(true); Result->AdvanceInteraction(.5f);
    TestEqual(TEXT("Retry gauge is quarter full after half a second"),Bar->GetPercent(),.25f);
    Result->CancelTrigger(); TestEqual(TEXT("Losing input clears gauge"),Bar->GetPercent(),0.f);
    Result->HandleTrigger(true); Result->AdvanceInteraction(2.f);
    TestEqual(TEXT("Cancelled held input requires release"),Result->LastAction,EOBTrainingResultAction::None);
    Result->HandleTrigger(false); Result->HandleTrigger(true); Result->AdvanceInteraction(1.f);
    TestEqual(TEXT("Tutorial retry gauge is half full at one second"),Bar->GetPercent(),.5f);
    Result->AdvanceInteraction(1.f);
    TestEqual(TEXT("Tutorial retry gauge reaches full at two seconds"),Bar->GetPercent(),1.f);
    TestEqual(TEXT("Two second tutorial hold retries"),Result->LastAction,EOBTrainingResultAction::Retry);
    auto* PC=World->SpawnActor<AOBPlayerController>();
    PC->InputKey(FInputKeyEventArgs(nullptr,FInputDeviceId::CreateFromInternalId(0),EKeys::LeftMouseButton,IE_Pressed,FPlatformTime::Cycles64()));
    TestTrue(TEXT("Controller tracks physical press"),PC->IsTriggerHeld());
    PC->InputKey(FInputKeyEventArgs(nullptr,FInputDeviceId::CreateFromInternalId(0),EKeys::LeftMouseButton,IE_Released,FPlatformTime::Cycles64()));
    TestFalse(TEXT("Controller tracks physical release"),PC->IsTriggerHeld());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOBMissionObjectivesTest,"OperationBreakpoint.Training.MissionObjectives",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FOBMissionObjectivesTest::RunTest(const FString&)
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    UClass* Class=LoadClass<UOBMissionWidget>(nullptr,TEXT("/Game/Blueprints/Widget/PlayerOverlay/Components/WBP_Mission.WBP_Mission_C"));
    if (!TestNotNull(TEXT("Authored mission WBP loads"),Class)) return false;
    auto* Mission=CreateWidget<UOBMissionWidget>(World,Class); const auto Slate=Mission->TakeWidget();
    TArray<UTextBlock*> Rows;
    TArray<UImage*> Strikes;
    TArray<FSlateColor> PendingColors;
    for (int32 I=1;I<=5;++I)
    {
        auto* Row=Cast<UTextBlock>(Mission->WidgetTree->FindWidget(FName(*FString::Printf(TEXT("BodyText_%d"),I))));
        if (!TestNotNull(TEXT("Authored objective row exists"),Row)) return false;
        Rows.Add(Row);
        PendingColors.Add(Row->GetColorAndOpacity());
        auto* Strike=Cast<UImage>(Mission->WidgetTree->FindWidget(FName(*FString::Printf(TEXT("Strike_%d"),I))));
        if (!TestNotNull(TEXT("Authored completion image exists"),Strike)) return false;
        Strikes.Add(Strike);
    }
    auto* Heading=Cast<UTextBlock>(Mission->WidgetTree->FindWidget(TEXT("MissionTitleText")));
    if (!Heading) Heading=Cast<UTextBlock>(Mission->WidgetTree->FindWidget(TEXT("TitleText")));
    if (!TestNotNull(TEXT("Authored mission heading binds"),Heading)) return false;
    auto Font=Rows[1]->GetFont(); Font.Size=37; Rows[1]->SetFont(Font);
    FOBTrainingResultData Result;
    for (EOBTrainingStep Step : {EOBTrainingStep::WeaponCheck,EOBTrainingStep::Cover})
    { FOBTrainingStepResult Item; Item.Step=Step; Item.Outcome=EOBTrainingOutcome::Passed; Result.Steps.Add(Item); }
    Mission->UpdateTrainingObjectives(Result,true);
    TestEqual(TEXT("Basic training title updates using authored name"),Heading->GetText().ToString(),FString(TEXT("훈련 목표")));
    TestEqual(TEXT("Completed weapon check enables its strike image"),Strikes[0]->GetVisibility(),ESlateVisibility::HitTestInvisible);
    TestEqual(TEXT("Completed text stays readable in gray"),Rows[0]->GetColorAndOpacity().GetSpecifiedColor(),FLinearColor(FColor::FromHex(TEXT("596A7A"))));
    auto Status=[&](int32 I) {return Cast<UTextBlock>(Mission->WidgetTree->FindWidget(*FString::Printf(TEXT("OutcomeText_%d"),I)));};
    if (!TestNotNull(TEXT("Objective status column exists"),Status(1))) return false;
    TestEqual(TEXT("Passed objective has an explicit Pass label"),Status(1)->GetText().ToString(),FString(TEXT("Pass")));
    TestTrue(TEXT("An unattempted objective is not marked as failed"),Status(2)->GetText().IsEmpty());
    FOBTrainingStepResult Failed; Failed.Step=EOBTrainingStep::Reload; Failed.Outcome=EOBTrainingOutcome::Failed; Result.Steps.Add(Failed);
    Mission->UpdateTrainingObjectives(Result,true);
    TestEqual(TEXT("Failed objective has an explicit Fail label"),Status(4)->GetText().ToString(),FString(TEXT("Fail")));
    TestTrue(TEXT("Failure and success use different status colors"),Status(1)->GetColorAndOpacity()!=Status(4)->GetColorAndOpacity());
    TestEqual(TEXT("Failed objective has a completion line"),Strikes[3]->GetVisibility(),ESlateVisibility::HitTestInvisible);
    TestEqual(TEXT("Completion line has no stale green brush tint"),Strikes[3]->GetBrush().TintColor.GetSpecifiedColor(),FLinearColor::White);
    Result.Steps.Pop(); Mission->UpdateTrainingObjectives(Result,true);
    TestEqual(TEXT("Pending aiming keeps its image hidden"),Strikes[1]->GetVisibility(),ESlateVisibility::Hidden);
    TestTrue(TEXT("Pending aiming keeps its authored color"),Rows[1]->GetColorAndOpacity()==PendingColors[1]);
    TestEqual(TEXT("Cover completion enables its own image"),Strikes[2]->GetVisibility(),ESlateVisibility::HitTestInvisible);
    TestEqual(TEXT("Pending reload keeps its image hidden"),Strikes[3]->GetVisibility(),ESlateVisibility::Hidden);
    TestEqual(TEXT("Data updates preserve authored font"),Rows[1]->GetFont().Size,37.f);
    Result.UniqueTargetsHit=3; Result.TotalTargets=6;
    Mission->UpdateTrainingObjectives(Result,false);
    TestEqual(TEXT("Mock training heading updates"),Heading->GetText().ToString(),FString(TEXT("모의 훈련")));
    TestEqual(TEXT("Live hit progress updates independently"),Rows[0]->GetText().ToString(),FString(TEXT("표적 명중 3/6")));
    TestEqual(TEXT("First group completion does not enable the six-target strike"),Strikes[0]->GetVisibility(),ESlateVisibility::Hidden);
    for (int32 I=2;I<5;++I)
    {
        TestEqual(TEXT("Unused rows collapse"),Rows[I]->GetVisibility(),ESlateVisibility::Collapsed);
        TestEqual(TEXT("Unused completion images collapse"),Strikes[I]->GetVisibility(),ESlateVisibility::Collapsed);
    }
    Result.UniqueTargetsHit=6;
    Mission->UpdateTrainingObjectives(Result,false);
    TestEqual(TEXT("All six targets enable the shooting strike"),Strikes[0]->GetVisibility(),ESlateVisibility::HitTestInvisible);
    TestEqual(TEXT("Shooting completion does not enable the breach strike"),Strikes[1]->GetVisibility(),ESlateVisibility::Hidden);
    FOBTrainingStepResult Breach; Breach.Step=EOBTrainingStep::Breach; Breach.Outcome=EOBTrainingOutcome::Passed; Result.Steps.Add(Breach);
    Mission->UpdateTrainingObjectives(Result,false);
    TestEqual(TEXT("Breaching completion enables its own strike"),Strikes[1]->GetVisibility(),ESlateVisibility::HitTestInvisible);
    Result=FOBTrainingResultData(); Mission->UpdateTrainingObjectives(Result,true);
    for (int32 I=0;I<Rows.Num();++I)
    {
        auto* Row=Rows[I];
        TestTrue(TEXT("Retraining restores all five rows"),Row->IsVisible());
        TestEqual(TEXT("Retraining hides all completion images"),Strikes[I]->GetVisibility(),ESlateVisibility::Hidden);
        TestTrue(TEXT("Retraining restores each authored text color"),Row->GetColorAndOpacity()==PendingColors[I]);
        TestTrue(TEXT("Retraining clears previous Pass and Fail labels"),Status(I+1)->GetText().IsEmpty());
        TestNull(TEXT("Built-in text strike brush remains cleared"),Row->GetStrikeBrush().GetResourceObject());
        TestFalse(TEXT("Objective has no checkbox prefix"),Row->GetText().ToString().Contains(TEXT("□")));
        TestFalse(TEXT("Objective keeps automatic wrapping disabled"),Row->GetAutoWrapText());
    }
    Mission->SetMissionObjectives(FText::FromString(TEXT("미션")),{});
    for (auto* Row : Rows) TestEqual(TEXT("Empty mission clears all rows"),Row->GetVisibility(),ESlateVisibility::Collapsed);
    for (auto* Strike : Strikes) TestEqual(TEXT("Empty mission clears all completion images"),Strike->GetVisibility(),ESlateVisibility::Collapsed);
    return true;
}

#if WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOBWidgetNoAutoWrapTest,"OperationBreakpoint.UI.NoAutoWrap",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FOBWidgetNoAutoWrapTest::RunTest(const FString&)
{
    auto& Registry=FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    Registry.SearchAllAssets(true);
    TArray<FAssetData> Widgets; Registry.GetAssetsByClass(UWidgetBlueprint::StaticClass()->GetClassPathName(),Widgets,true);
    int32 WidgetCount=0, TextCount=0;
    for (const auto& Data : Widgets)
    {
        if (!Data.PackageName.ToString().StartsWith(TEXT("/Game/"))) continue;
        auto* BP=Cast<UWidgetBlueprint>(Data.GetAsset());
        if (!TestNotNull(Data.GetSoftObjectPath().ToString(),BP) || !BP->WidgetTree) return false;
        ++WidgetCount;
        BP->WidgetTree->ForEachWidget([&](UWidget* Widget) {
            if (auto* Text=Cast<UTextLayoutWidget>(Widget))
            {
                ++TextCount;
                TestFalse(FString::Printf(TEXT("Auto Wrap disabled: %s.%s"),*BP->GetPathName(),*Text->GetName()),Text->GetAutoWrapText());
            }
        });
    }
    TestTrue(TEXT("All project widgets were inspected"),WidgetCount>=12);
    TestTrue(TEXT("Text controls were inspected"),TextCount>0);
    for (const TCHAR* Name : {TEXT("WBP_Guide"),TEXT("WBP_Mission"),TEXT("WBP_Timer"),TEXT("WBP_Life"),TEXT("WBP_TrainingMarkers"),TEXT("WBP_TrainingResult")})
    {
        const FString Path=FString(TEXT("/Game/Blueprints/Widget/PlayerOverlay/Components"))/Name+TEXT(".")+Name;
        TestNotNull(FString::Printf(TEXT("Component loads from new folder: %s"),Name),LoadObject<UWidgetBlueprint>(nullptr,*Path));
    }
    TestNotNull(TEXT("Overlay loads from its own folder"),LoadObject<UWidgetBlueprint>(nullptr,TEXT("/Game/Blueprints/Widget/PlayerOverlay/WBP_PlayerOverlay.WBP_PlayerOverlay")));
    return true;
}
#endif

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOBTrainingMarkerUMGTest,"OperationBreakpoint.Training.MarkerUMG",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FOBTrainingMarkerUMGTest::RunTest(const FString&)
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    UClass* Class=LoadClass<UOBTrainingStatusWidget>(nullptr,TEXT("/Game/Blueprints/Widget/PlayerOverlay/Components/WBP_TrainingMarkers.WBP_TrainingMarkers_C"));
    if (!TestNotNull(TEXT("Editable marker WBP loads"),Class)) return false;
    auto* Markers=CreateWidget<UOBTrainingStatusWidget>(World,Class); const auto Slate=Markers->TakeWidget();
    auto Find=[&](const TCHAR* Name) { return Markers->WidgetTree->FindWidget(Name); };
    for (const TCHAR* Name : {TEXT("AimLayer"),TEXT("BreachLayer"),TEXT("AimTarget"),TEXT("AimProgress"),TEXT("CameraFrame"),
        TEXT("LeftHandArea"),TEXT("RightHandArea"),TEXT("LeftWrist"),TEXT("RightWrist"),TEXT("CameraLabel")})
        if (!TestNotNull(FString::Printf(TEXT("Editable UMG node %s"),Name),Find(Name))) return false;
    auto* Aim=Cast<UImage>(Find(TEXT("AimTarget")));
    if (!TestNotNull(TEXT("Aim marker is a normal UMG Image"),Aim)) return false;
    const FLinearColor AuthoredColor(.6f,.2f,.8f,1.f); Aim->SetColorAndOpacity(AuthoredColor);
    Markers->UpdateAimMarkers(FVector2D(1280,720),FVector2D(1920,1080),FVector2D(.25,.75),60,.25);
    auto* AimSlot=Cast<UCanvasPanelSlot>(Aim->Slot);
    TestTrue(TEXT("Normalized aim coordinates map to the UMG canvas"),AimSlot->GetPosition().Equals(FVector2D(320,540),.001));
    TestTrue(TEXT("Visual circle radius matches the 60 physical pixel acceptance radius"),FMath::IsNearlyEqual(AimSlot->GetSize().X*.45*1.5,60.0,.001));
    TestEqual(TEXT("Geometry updates preserve authored image tint"),Aim->GetColorAndOpacity(),AuthoredColor);
    auto* ProgressBar=Cast<UOBRadialProgressBar>(Find(TEXT("AimProgress")));
    if (!TestNotNull(TEXT("Aim gauge is an editable ProgressBar"),ProgressBar)) return false;
    TestEqual(TEXT("Aim hold updates ProgressBar progress"),ProgressBar->GetPercent(),.25f);
    TestEqual(TEXT("Incomplete marker uses requested red image"),Aim->GetBrush().GetResourceObject()->GetName(),FString(TEXT("Aim_Red")));
    Markers->UpdateAimMarkers(FVector2D(1280,720),FVector2D(1920,1080),FVector2D(.25,.75),60,1.f);
    TestEqual(TEXT("100 percent selects the requested green image"),Aim->GetBrush().GetResourceObject()->GetName(),FString(TEXT("Aim_Green")));
    TestFalse(TEXT("Complete marker hides the overlay gauge"),ProgressBar->IsVisible());
    Markers->UpdateAimMarkers(FVector2D(1280,720),FVector2D(1920,1080),FVector2D(.75,.25),60,0.f);
    TestEqual(TEXT("A new point starts red again"),Aim->GetBrush().GetResourceObject()->GetName(),FString(TEXT("Aim_Red")));
    TestEqual(TEXT("A new point clears the gauge"),ProgressBar->GetPercent(),0.f);
    TestEqual(TEXT("Aiming hides breaching layer"),Find(TEXT("BreachLayer"))->GetVisibility(),ESlateVisibility::Collapsed);
    FOBBreachGuideState State; State.bFresh=State.bBreachMode=State.bPlayerTracked=true;
    State.LeftWrist=(State.LeftMin+State.LeftMax)*.5; State.RightWrist=(State.RightMin+State.RightMax)*.5;
    State.LeftVisibility=State.RightVisibility=1; State.bLeftInside=true; State.Progress=.6f;
    auto* WristSlot=Cast<UCanvasPanelSlot>(Find(TEXT("LeftWrist"))->Slot); WristSlot->SetSize(FVector2D(42,42));
    auto* Label=Cast<UTextBlock>(Find(TEXT("LeftHandLabel"))); auto Font=Label->GetFont(); Font.Size=37; Label->SetFont(Font);
    Markers->UpdateBreachMarkers(FVector2D(1920,1080),State,FText::FromString(TEXT("손목 인식")));
    const FBox2D Camera=FOBBreachGuideLayout::CameraRect(FVector2D(1920,1080),State.CameraSize);
    auto* CameraSlot=Cast<UCanvasPanelSlot>(Find(TEXT("CameraFrame"))->Slot);
    TestEqual(TEXT("4:3 camera bounds map to UMG frame with pillarboxing"),CameraSlot->GetPosition(),FVector2D(240,0));
    auto* AreaSlot=Cast<UCanvasPanelSlot>(Find(TEXT("LeftHandArea"))->Slot);
    TestTrue(TEXT("UMG ROI starts at detector boundary"),AreaSlot->GetPosition().Equals(FOBBreachGuideLayout::MapPoint(State.LeftMin,Camera),.001));
    TestTrue(TEXT("UMG ROI size matches detector bounds"),AreaSlot->GetSize().Equals((State.LeftMax-State.LeftMin)*Camera.GetSize(),.001));
    TestTrue(TEXT("Wrist location is mapped once without another mirror"),WristSlot->GetPosition().Equals(FOBBreachGuideLayout::MapPoint(State.LeftWrist,Camera),.001));
    TestEqual(TEXT("Wrist image size stays authored in UMG"),WristSlot->GetSize(),FVector2D(42,42));
    TestEqual(TEXT("Label font stays authored in UMG"),Label->GetFont().Size,37.f);
    TestEqual(TEXT("Inside left hand selects its UMG success image"),Find(TEXT("LeftHandSuccess"))->GetVisibility(),ESlateVisibility::HitTestInvisible);
    TestEqual(TEXT("Outside right hand hides its success image"),Find(TEXT("RightHandSuccess"))->GetVisibility(),ESlateVisibility::Collapsed);
    TestNull(TEXT("The breach progress bar is removed from the widget tree"),Find(TEXT("BreachProgress")));
    State.bFresh=false; Markers->UpdateBreachMarkers(FVector2D(1920,1080),State,FText::GetEmpty());
    TestEqual(TEXT("Stale camera feedback hides wrist markers"),Find(TEXT("LeftWrist"))->GetVisibility(),ESlateVisibility::Collapsed);
    TestEqual(TEXT("Stale feedback removes success appearance"),Find(TEXT("LeftHandSuccess"))->GetVisibility(),ESlateVisibility::Collapsed);
    TestNull(TEXT("Stale feedback cannot recreate the removed progress bar"),Find(TEXT("BreachProgress")));
    Markers->UpdateAimMarkers(FVector2D::ZeroVector,FVector2D(1920,1080),FVector2D(.5,.5),60,1);
    TestEqual(TEXT("Invalid viewport hides marker layer"),Find(TEXT("AimLayer"))->GetVisibility(),ESlateVisibility::Collapsed);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOBTrainingResultTriggerTest,"OperationBreakpoint.Training.ResultTrigger",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FOBTrainingResultTriggerTest::RunTest(const FString&)
{
    FOBTrainingResultTrigger Gate; Gate.Reset(false);
    TestEqual(TEXT("Pressed alone does not select"),Gate.Change(true,2),EOBTrainingResultAction::None);
    TestEqual(TEXT("Below threshold does not select"),Gate.Advance(1.99f,2),EOBTrainingResultAction::None);
    TestEqual(TEXT("Short release chooses next"),Gate.Change(false,2),EOBTrainingResultAction::NextStage);
    Gate.Reset(false); Gate.Change(true,2);
    TestEqual(TEXT("Exactly two seconds chooses retry"),Gate.Advance(2,2),EOBTrainingResultAction::Retry);
    TestEqual(TEXT("Release following retry never chooses next"),Gate.Change(false,2),EOBTrainingResultAction::None);
    TestEqual(TEXT("Repeated input cannot navigate twice"),Gate.Change(true,2),EOBTrainingResultAction::None);
    Gate.Reset(true); Gate.Advance(3,2);
    TestEqual(TEXT("Held input on opening ignored"),Gate.Change(true,2),EOBTrainingResultAction::None);
    TestEqual(TEXT("Initial release only arms"),Gate.Change(false,2),EOBTrainingResultAction::None);
    Gate.Change(true,2); Gate.Advance(.2f,2);
    TestEqual(TEXT("Fresh gesture allowed after release"),Gate.Change(false,2),EOBTrainingResultAction::NextStage);
    Gate.Reset(false); Gate.Change(true,2); Gate.Advance(1,2); Gate.Cancel();
    TestEqual(TEXT("Cancellation is not a short selection"),Gate.Change(false,2),EOBTrainingResultAction::None);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOBOverlayDesignerTest,"OperationBreakpoint.Training.OverlayDesigner",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FOBOverlayDesignerTest::RunTest(const FString&)
{
    UClass* Class=LoadClass<UOBPlayerOverlayWidget>(nullptr,TEXT("/Game/Blueprints/Widget/PlayerOverlay/WBP_PlayerOverlay.WBP_PlayerOverlay_C"));
    if (!TestNotNull(TEXT("Overlay Blueprint"),Class)) return false;
    auto* Overlay=NewObject<UOBPlayerOverlayWidget>(GetTransientPackage(),Class);
    // Designer previews must also work when Blueprint PreConstruct evaluation is disabled.
    Overlay->SetDesignerFlags(EWidgetDesignFlags::Designing | EWidgetDesignFlags::Previewing);
    if (!TestTrue(TEXT("Designer initializes without gameplay context"),Overlay->Initialize())) return false;
    const auto Slate=Overlay->TakeWidget(); Overlay->SynchronizeProperties();
    TestEqual(TEXT("Layout preview shows five editable panels"),Overlay->VisibleElements,61);
    auto* SystemMessage=Overlay->WidgetTree->FindWidget(TEXT("SystemMessage"));
    if (!TestNotNull(TEXT("Renamed SystemMessage exists"),SystemMessage)) return false;
    TestFalse(TEXT("System message does not cover unrelated preview panels"),SystemMessage->IsVisible());
    for (const TCHAR* Name : {TEXT("Guide"),TEXT("Subtitle"),TEXT("Mission"),TEXT("Timer"),TEXT("Life")})
    {
        auto* Panel=Overlay->WidgetTree->FindWidget(Name);
        if (!TestNotNull(TEXT("Designer panel exists"),Panel)) return false;
        TestFalse(FString::Printf(TEXT("%s is not hidden by Designer eye setting"),Name),Panel->bHiddenInDesigner);
        TestTrue(FString::Printf(TEXT("%s renders in layout preview"),Name),Panel->IsVisible());
    }
    const EOBOverlayPreviewMode Modes[]={EOBOverlayPreviewMode::Stage,EOBOverlayPreviewMode::TrainingGuide,
        EOBOverlayPreviewMode::TrainingPractice,EOBOverlayPreviewMode::Pause,EOBOverlayPreviewMode::ResultPass,EOBOverlayPreviewMode::ResultFail,
        EOBOverlayPreviewMode::MarkersAim,EOBOverlayPreviewMode::MarkersBreach};
    const int32 Masks[]={48,5,28,128,128,128,64,64};
    for (int32 I=0;I<8;++I)
    {
        Overlay->DesignerPreview=Modes[I]; Overlay->SynchronizeProperties();
        TestEqual(TEXT("Preview mode selects its panels"),Overlay->VisibleElements,Masks[I]);
        if (I<6) TestFalse(TEXT("Unrelated previews do not draw the marker sample"),Overlay->GetTrainingMarkers()->IsVisible());
        if (I==4 || I==5)
        {
            auto* Result=Overlay->GetTrainingResultWidget();
            TestTrue(TEXT("Result visible without finishing a game"),Result->IsVisible());
            TestEqual(TEXT("Pass and fail sample stamp data"),Result->IsScenarioPassed(),I==4);
            auto* Border=Cast<UBorder>(Result->WidgetTree->FindWidget(TEXT("ResultBorder")));
            if (TestNotNull(TEXT("Designer result Border exists"),Border)) TestNotNull(TEXT("Designer result texture is populated"),Border->Background.GetResourceObject());
        }
        if (I>=6) TestTrue(TEXT("Markers visible without a training Director"),Overlay->GetTrainingMarkers()->IsVisible());
    }
    Overlay->DesignerPreview=EOBOverlayPreviewMode::Layout;
    Overlay->GetTrainingResultWidget()->SelectByDesigner();
    TestEqual(TEXT("Selecting result in hierarchy opens its preview"),Overlay->VisibleElements,128);
    Overlay->GetTrainingResultWidget()->DeselectByDesigner();
    TestEqual(TEXT("Deselecting restores chosen preview"),Overlay->VisibleElements,61);
    Overlay->DesignerPreview=EOBOverlayPreviewMode::ResultFail;
    Overlay->GetTrainingResultWidget()->SelectByDesigner();
    TestFalse(TEXT("Hierarchy selection preserves chosen fail stamp"),Overlay->GetTrainingResultWidget()->IsScenarioPassed());
    Overlay->GetTrainingResultWidget()->DeselectByDesigner(); Overlay->DesignerPreview=EOBOverlayPreviewMode::Layout;
    Overlay->GetTrainingMarkers()->SelectByDesigner();
    TestEqual(TEXT("Selecting markers in hierarchy opens its preview"),Overlay->VisibleElements,64);
    Overlay->GetTrainingMarkers()->DeselectByDesigner();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOBPauseInteractionTest,"OperationBreakpoint.Training.PauseInteraction",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FOBPauseInteractionTest::RunTest(const FString&)
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    auto* Class=LoadClass<UOBPlayerOverlayWidget>(nullptr,TEXT("/Game/Blueprints/Widget/PlayerOverlay/WBP_PlayerOverlay.WBP_PlayerOverlay_C"));
    if (!Class) return false;
    auto* Overlay=CreateWidget<UOBPlayerOverlayWidget>(World,Class); const auto Slate=Overlay->TakeWidget();
    auto* Director=World->SpawnActor<AOBTrainingDirector>(); Director->bUseSplinePrototype=true; Director->bPrototypePaused=true;
    Overlay->SetTrainingDirector(Director);
    TestNull(TEXT("No hidden Pause widget remains"),Overlay->WidgetTree->FindWidget(TEXT("Pause")));
    auto* Pause=Overlay->GetTrainingResultWidget(); Pause->bAutomaticNavigation=false;
    int32 Buttons=0; Pause->WidgetTree->ForEachWidget([&](UWidget* W) { Buttons+=W->IsA<UButton>(); });
    TestEqual(TEXT("Pause is fully button-free"),Buttons,0);
    TestTrue(TEXT("Pause is an active trigger interaction"),Overlay->IsTrainingPauseVisible());
    TestEqual(TEXT("Long pause input returns to TitleMap"),Pause->MainMenu.ToSoftObjectPath().GetAssetName(),FString(TEXT("TitleMap")));
    auto* PC=World->SpawnActor<AOBPlayerController>();
    FActorSpawnParameters Params; Params.Owner=PC; auto* HUD=World->SpawnActor<AOBHUD>(Params);
    FindFProperty<FObjectPropertyBase>(APlayerController::StaticClass(),TEXT("MyHUD"))->SetObjectPropertyValue_InContainer(PC,HUD);
    FindFProperty<FObjectPropertyBase>(AOBHUD::StaticClass(),TEXT("PlayerOverlayWidget"))->SetObjectPropertyValue_InContainer(HUD,Overlay);
    TestTrue(TEXT("Pause blocks gameplay firing through HUD"),HUD->IsMenuVisible());
    const FInputDeviceId Device=FInputDeviceId::CreateFromInternalId(0);
    PC->InputKey(FInputKeyEventArgs(nullptr,Device,EKeys::LeftMouseButton,IE_Pressed,FPlatformTime::Cycles64()));
    TestEqual(TEXT("Press alone does not resume"),Pause->LastWarningAction,EOBTrainingWarningAction::None);
    PC->InputKey(FInputKeyEventArgs(nullptr,Device,EKeys::LeftMouseButton,IE_Released,FPlatformTime::Cycles64()));
    TestEqual(TEXT("Short physical input routes to continue"),Pause->LastWarningAction,EOBTrainingWarningAction::Continue);
    Pause->PrepareWarningInteraction(false); Pause->HandleTrigger(true); Pause->AdvanceInteraction(2);
    TestEqual(TEXT("Two-second hold routes to menu"),Pause->LastWarningAction,EOBTrainingWarningAction::ExitToMenu);
    Pause->HandleTrigger(false); TestEqual(TEXT("Release after hold never resumes"),Pause->LastWarningAction,EOBTrainingWarningAction::ExitToMenu);
    Pause->PrepareWarningInteraction(true); Pause->HandleTrigger(false);
    TestEqual(TEXT("Release of a pre-existing hold does not resume"),Pause->LastWarningAction,EOBTrainingWarningAction::None);
    return true;
}
#endif
