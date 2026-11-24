#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Training/OBTrainingUIData.h"
#include "Training/OBTrainingDirector.h"
#include "Training/OBTrainingResultWidget.h"
#include "Widget/OBPlayerOverlayWidget.h"
#include "Widget/OBHudPanelWidget.h"
#include "Character/OBPlayerCharacter.h"
#include "Character/OBActionComponent.h"
#include "Core/OBPlayerController.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Components/Border.h"
#include "Components/CanvasPanelSlot.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/Texture2D.h"
#include "TimerManager.h"
#include "Widget/OBUIVideo.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Slate/WidgetRenderer.h"
#include "Engine/TextureRenderTarget2D.h"
#include "AssetCompilingManager.h"
#include "ShaderCompiler.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "RenderingThread.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOBTrainingUIPagesTest,"OperationBreakpoint.Training.UIPages",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FOBTrainingUIPagesTest::RunTest(const FString&)
{
    auto* World=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    auto* D=World->SpawnActor<AOBTrainingDirector>();
    ON_SCOPE_EXIT { D->StopPrototypeNarration(); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    auto* Class=LoadClass<UOBPlayerOverlayWidget>(nullptr,TEXT("/Game/Blueprints/Widget/PlayerOverlay/WBP_PlayerOverlay.WBP_PlayerOverlay_C"));
    if (!TestNotNull(TEXT("Saved overlay"),Class)) return false;
    auto* Overlay=CreateWidget<UOBPlayerOverlayWidget>(World,Class); const auto Slate=Overlay->TakeWidget();
    auto* Guide=Cast<UOBGuideWidget>(Overlay->WidgetTree->FindWidget(TEXT("Guide")));
    auto* Result=Overlay->GetTrainingResultWidget();
    auto* Table=LoadObject<UDataTable>(nullptr,TEXT("/Game/Blueprints/Training/Data/DT_TrainingUI.DT_TrainingUI"));
    if (!TestNotNull(TEXT("Guide native class"),Guide) || !TestNotNull(TEXT("Result"),Result) || !TestNotNull(TEXT("Separate UI data table"),Table)) return false;
    TestEqual(TEXT("All 10 UI rows exist"),Table->GetRowMap().Num(),10);
    auto* Title=Cast<UTextBlock>(Guide->WidgetTree->FindWidget(TEXT("TitleText")));
    auto* Body=Cast<UTextBlock>(Guide->WidgetTree->FindWidget(TEXT("BodyText")));
    auto* Image=Cast<UImage>(Guide->WidgetTree->FindWidget(TEXT("BodyImage")));
    auto* Trigger=Cast<UImage>(Guide->WidgetTree->FindWidget(TEXT("TriggerImage")));
    if (!TestNotNull(TEXT("Title"),Title) || !TestNotNull(TEXT("Body"),Body) || !TestNotNull(TEXT("Image"),Image) || !TestNotNull(TEXT("Trigger image"),Trigger)) return false;
    auto* AdvancePrompt=Guide->WidgetTree->FindWidget(TEXT("AdvancePrompt"));
    auto* PromptBackground=Guide->WidgetTree->FindWidget(TEXT("GuidePromptBackground"));
    if (!TestNotNull(TEXT("Advance prompt container"),AdvancePrompt) || !TestNotNull(TEXT("Advance prompt border"),PromptBackground)) return false;
    TestFalse(TEXT("Guide initializes with the entire advance prompt hidden"),AdvancePrompt->IsVisible());
    TestEqual(TEXT("Hidden prompt releases its layout space instead of exposing the grey footer"),AdvancePrompt->GetVisibility(),ESlateVisibility::Collapsed);
    TestEqual(TEXT("Prompt border belongs to the hidden prompt container"),PromptBackground->GetParent(),Cast<UPanelWidget>(AdvancePrompt));
    TestNull(TEXT("Guide no longer contains PromptText"),Guide->WidgetTree->FindWidget(TEXT("PromptText")));
    TestNotNull(TEXT("Trigger has a video material"),Cast<UMaterialInterface>(Trigger->GetBrush().GetResourceObject()));
    TestTrue(TEXT("Trigger uses the existing TriggerPress movie"),Guide->TriggerSource.ToSoftObjectPath().GetAssetName()==TEXT("TriggerPress"));
    auto* TriggerSlot=Cast<UCanvasPanelSlot>(Trigger->Slot);
    const FVector2D TriggerPosition=TriggerSlot ? TriggerSlot->GetPosition() : FVector2D::ZeroVector;
    const FVector2D TriggerSize=TriggerSlot ? TriggerSlot->GetSize() : FVector2D::ZeroVector;
    const int32 DialogueIds[]={3,4,5,6,7,17,18};
    D->bUseSplinePrototype=true; D->CurrentStep=EOBTrainingStep::Welcome; D->Overlay=Overlay;
    for (int32 I=0;I<7;++I)
    {
        D->CurrentDialogueId=DialogueIds[I]; D->bGuideAdvanceReady=false;
        Overlay->SetTrainingDirector(D);
        const int32 Id=3001+I;
        const auto* Row=Table->FindRow<FOBTrainingUIRow>(FName(*FString::FromInt(Id)),TEXT("UI test"));
        if (!TestNotNull(TEXT("UI row"),Row)) return false;
        TestEqual(TEXT("Narration maps to the separate guide ID"),D->GetGuideUIId(),Id);
        TestTrue(TEXT("Page title comes from UI table"),Title->GetText().EqualTo(Row->Title));
        TestTrue(TEXT("Page body comes from UI table"),Body->GetText().EqualTo(Row->Body));
        TestEqual(TEXT("Page image comes from UI table"),Image->GetBrush().GetResourceObject(),static_cast<UObject*>(Row->Image.LoadSynchronous()));
        TestFalse(TEXT("Each new page starts with hidden trigger"),Trigger->IsVisible());
        TestFalse(TEXT("Narration hides the entire prompt including its border"),AdvancePrompt->IsVisible());
        TestEqual(TEXT("Narration leaves no empty prompt background"),AdvancePrompt->GetVisibility(),ESlateVisibility::Collapsed);
        Guide->SetGuidePage(Id,true); TestTrue(TEXT("Ready page displays trigger image"),Trigger->IsVisible());
        TestTrue(TEXT("Ready page displays the entire advance prompt"),AdvancePrompt->IsVisible());
        TestNotNull(TEXT("Ready page owns an independent video player"),Guide->TriggerVideo.Get());
        TestNotNull(TEXT("Playback uses a per-widget dynamic material"),Cast<UMaterialInstanceDynamic>(Trigger->GetBrush().GetResourceObject()));
        if (TriggerSlot) { TestEqual(TEXT("Page changes preserve trigger position"),TriggerSlot->GetPosition(),TriggerPosition); TestEqual(TEXT("Page changes preserve trigger size"),TriggerSlot->GetSize(),TriggerSize); }
    }
    TestTrue(TEXT("Spreadsheet line breaks survive UI binding"),Table->FindRow<FOBTrainingUIRow>(TEXT("3003"),TEXT("UI test"))->Body.ToString().Contains(TEXT("\n")));
    auto TickTimers=[&](float Seconds) { for (float T=0;T<Seconds;T+=.01f) { ++GFrameCounter; World->GetTimerManager().Tick(.01f); } };
    D->CurrentDialogueId=3; D->GuideTriggerDelaySeconds=1.f; D->bGuideAdvanceReady=false;
    D->bPrototypeNarrationActive=true; D->PrototypeMessage=AOBTrainingDirector::EPrototypeMessage::Narration;
    Overlay->RefreshTrainingGuide(); D->FinishPrototypeNarration();
    TestFalse(TEXT("Voice completion does not unlock the trigger immediately"),D->bGuideAdvanceReady);
    TickTimers(.5f); TestFalse(TEXT("Trigger remains hidden during post-voice delay"),Trigger->IsVisible());
    TestFalse(TEXT("Prompt border stays hidden during post-voice delay"),AdvancePrompt->IsVisible());
    TestEqual(TEXT("Post-voice delay leaves no empty prompt background"),AdvancePrompt->GetVisibility(),ESlateVisibility::Collapsed);
    TickTimers(.6f); TestTrue(TEXT("Delay completion unlocks both input and image"),D->bGuideAdvanceReady && Trigger->IsVisible());
    TestTrue(TEXT("Voice delay completion reveals the entire prompt"),AdvancePrompt->IsVisible());
    D->StopPrototypeNarration(); D->CurrentDialogueId=4; Overlay->RefreshTrainingGuide();
    TestFalse(TEXT("Page transition immediately hides trigger"),Trigger->IsVisible());
    TestFalse(TEXT("Page transition immediately hides the entire prompt"),AdvancePrompt->IsVisible());
    D->bPrototypeNarrationActive=true; D->PrototypeMessage=AOBTrainingDirector::EPrototypeMessage::Narration;
    D->FinishPrototypeNarration(); D->StopPrototypeNarration(); D->CurrentDialogueId=5; Overlay->RefreshTrainingGuide(); TickTimers(2);
    TestFalse(TEXT("Cancelled old-page timer cannot unlock a later page"),D->bGuideAdvanceReady || Trigger->IsVisible());

    auto* ResultTitle=Cast<UTextBlock>(Result->WidgetTree->FindWidget(TEXT("TitleText")));
    auto* ResultBody=Cast<UTextBlock>(Result->WidgetTree->FindWidget(TEXT("BodyText")));
    auto* Border=Cast<UBorder>(Result->WidgetTree->FindWidget(TEXT("ResultBorder")));
    if (!TestNotNull(TEXT("Result title"),ResultTitle) || !TestNotNull(TEXT("Result body"),ResultBody) || !TestNotNull(TEXT("Result Border"),Border)) return false;
    FOBTrainingResultData Data; Data.bPrototypeFlow=true; Data.bScenarioCompleted=true; Data.RecordedFireLimitSeconds=120; Data.RecordedFireSeconds=75;
    for (int32 Id : {3008,3009,3010})
    {
        Data.bScenarioCompleted=Id==3009;
        if (Id==3008) Result->SetWarning(Data); else Result->SetResult(Data);
        const auto* Row=Table->FindRow<FOBTrainingUIRow>(FName(*FString::FromInt(Id)),TEXT("UI test"));
        TestEqual(TEXT("Warning/pass/fail selects correct UI ID"),Result->CurrentUIId,Id);
        TestTrue(TEXT("Result title and body use UI table"),ResultTitle->GetText().EqualTo(Row->Title) && ResultBody->GetText().EqualTo(Row->Body));
        TestEqual(TEXT("Result swaps the Border brush texture"),Border->Background.GetResourceObject(),static_cast<UObject*>(Row->Image.LoadSynchronous()));
    }
    D->CurrentStep=EOBTrainingStep::WeaponCheck; D->CurrentDialogueId=0; D->bPrototypePaused=true;
    D->Controller=World->SpawnActor<AOBPlayerController>(); auto* Player=World->SpawnActor<AOBPlayerCharacter>();
    D->Controller->Possess(Player); D->Action=Player->GetActionComponent();
    Overlay->SetTrainingDirector(D);
    TestTrue(TEXT("3008 appears as warning while retaining pause interaction"),Result->IsVisible() && Result->CurrentUIId==3008 && Overlay->IsTrainingPauseVisible());
    Overlay->HandleTrainingTrigger(true); Overlay->HandleTrainingTrigger(false);
    TestFalse(TEXT("A short trigger resumes the existing training instead of entering a mission"),D->bPrototypePaused);
    D->bPrototypePaused=true; Overlay->SetTrainingDirector(D);
    TestNull(TEXT("Obsolete Pause widget is removed"),Overlay->WidgetTree->FindWidget(TEXT("Pause")));
    Result->bAutomaticNavigation=false;
    Result->PrepareWarningInteraction(false); Overlay->HandleTrainingTrigger(true); Result->AdvanceInteraction(2.1f);
    TestTrue(TEXT("A long warning trigger retains the Main Menu action"),Result->LastWarningAction==EOBTrainingWarningAction::ExitToMenu);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOBGuidePromptRenderTest,"OperationBreakpoint.Training.GuidePromptRender",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FOBGuidePromptRenderTest::RunTest(const FString&)
{
    auto* World=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    auto* Class=LoadClass<UOBGuideWidget>(nullptr,TEXT("/Game/Blueprints/Widget/PlayerOverlay/Components/WBP_Guide.WBP_Guide_C"));
    if(!TestNotNull(TEXT("Saved guide blueprint"),Class))return false;
    auto* Guide=CreateWidget<UOBGuideWidget>(World,Class);
    const auto Slate=Guide->TakeWidget();
    const FIntPoint Size(1400,780);
    const FString Folder=FPaths::ProjectSavedDir()/TEXT("Tests/AdvancePrompt/Rendered");
    IFileManager::Get().MakeDirectory(*Folder,true);
    FWidgetRenderer Renderer(true);
    for(bool Ready : {false,true})
    {
        Guide->SetGuidePage(3001,Ready);
        Guide->StopAllAnimations(); Guide->SetRenderOpacity(1.f);
        FAssetCompilingManager::Get().FinishAllCompilation();
        if(GShaderCompilingManager)GShaderCompilingManager->FinishAllCompilation();
        Renderer.DrawWidget(Slate,FVector2D(Size)); FlushRenderingCommands();
        auto* Target=Renderer.DrawWidget(Slate,FVector2D(Size)); FlushRenderingCommands();
        TArray<FColor> Pixels;
        if(!TestTrue(TEXT("Guide renders into a readable image"),Target && Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels)))return false;
        TArray64<uint8> Png; FImageUtils::PNGCompressImageArray(Size.X,Size.Y,Pixels,Png);
        TestTrue(TEXT("Guide comparison image saved"),FFileHelper::SaveArrayToFile(Png,*(Folder/(Ready?TEXT("Ready.png"):TEXT("Narrating.png")))));
        // The old Hidden layout exposed B5C0CC here, outside the hidden prompt's children.
        const auto Pixel=Pixels[655*Size.X+1250];
        AddInfo(FString::Printf(TEXT("Guide footer ready=%d sample=%s"),Ready,*Pixel.ToString()));
        if(!Ready)TestTrue(TEXT("Narration has a white footer with no grey prompt block"),Pixel.R>=245 && Pixel.G>=245 && Pixel.B>=245);
        else TestTrue(TEXT("Ready prompt restores the blue background"),Pixel.B>Pixel.R && Pixel.R<100);
    }
    return !HasAnyErrors();
}
#endif
