#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Widget/OBTitleWidget.h"
#include "Components/Image.h"
#include "FileMediaSource.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOBTitleEntranceTimingTest,"OperationBreakpoint.UI.TitleEntranceTiming",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FOBTitleEntranceTimingTest::RunTest(const FString&)
{
    auto* World=UWorld::CreateWorld(EWorldType::Game,false);GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { World->DestroyWorld(false);GEngine->DestroyWorldContext(World); };
    auto* Class=LoadClass<UOBTitleWidget>(nullptr,TEXT("/Game/Blueprints/Widget/WBP_Title.WBP_Title_C"));
    if(!TestNotNull(TEXT("User title WBP"),Class))return false;
    auto* Title=CreateWidget<UOBTitleWidget>(World,Class);
    TestEqual(TEXT("Full idle source is TitleLoop"),Title->TitleSource.ToSoftObjectPath().GetAssetName(),FString(TEXT("TitleLoop")));
    TestEqual(TEXT("Entrance source is separate TravleToTraining"),Title->EntranceSource.ToSoftObjectPath().GetAssetName(),FString(TEXT("TravleToTraining")));
    TestEqual(TEXT("Travel waits one second after voice"),Title->TrainingTravelDelaySeconds,1.f);
    for (const auto& Entry : {
        TPair<UMediaSource*, const TCHAR*>(Title->TitleSource.LoadSynchronous(),TEXT("TitleLoop.mp4")),
        TPair<UMediaSource*, const TCHAR*>(Title->EntranceSource.LoadSynchronous(),TEXT("TravleToTraining.mp4"))})
    {
        auto* Authored=Cast<UFileMediaSource>(Entry.Key);
        if (!TestNotNull(TEXT("Built-in movie uses a FileMediaSource"),Authored)) return false;
        const FString Relative=FString(TEXT("./Movies/"))+Entry.Value;
        TestEqual(TEXT("Saved media source is portable"),Authored->FilePath,Relative);
        TestTrue(TEXT("Saved project movie exists"),Authored->Validate());
        TestTrue(TEXT("Valid authored sources are reused"),Title->ResolvePortableMovie(Authored,*Authored->GetPathName(),Entry.Value)==Authored);
        auto* Broken=DuplicateObject<UFileMediaSource>(Authored,GetTransientPackage());
        Broken->SetFilePath(TEXT("C:/MissingImportFolder/TitleMediaUnavailable.mp4"));
        const FString Identity=Broken->GetPathName();
        auto* Repaired=Cast<UFileMediaSource>(Title->ResolvePortableMovie(Broken,*Identity,Entry.Value));
        TestTrue(TEXT("An old packaged source falls back to the included movie"),Repaired!=Broken && Repaired && Repaired->Validate());
        TestEqual(TEXT("The fallback keeps a relative Movies path"),Repaired->FilePath,Relative);
        TestEqual(TEXT("Runtime repair never mutates the original media source"),Broken->FilePath,FString(TEXT("C:/MissingImportFolder/TitleMediaUnavailable.mp4")));
        TestTrue(TEXT("Custom source identities are not substituted"),Title->ResolvePortableMovie(Broken,TEXT("/Game/Custom/OtherMovie.OtherMovie"),Entry.Value)==Broken);
        TestTrue(TEXT("A missing packaged movie cannot produce a false success"),Title->ResolvePortableMovie(Broken,*Identity,TEXT("MissingTitleMovie.mp4"))==Broken);
    }
    if(!TestNotNull(TEXT("Video image binding"),Title->BackgroundVideo.Get()))return false;
    Title->bEntrancePlaying=true;
    Title->HandleMediaEnded();
    TestTrue(TEXT("Early video end holds a black screen"),Title->bEntranceVideoFinished && Title->BackgroundVideo->GetColorAndOpacity()==FLinearColor::Black);
    TestFalse(TEXT("Video completion cannot navigate before voice completion"),Title->IsEntranceTravelReady(FPlatformTime::Seconds()+100));
    Title->HandleEntranceVoiceFinished();const double At=Title->TrainingTravelAt;
    TestTrue(TEXT("Delay is measured from actual audio completion"),FMath::IsNearlyEqual(At-Title->EntranceVoiceFinishedAt,1.,.001));
    TestFalse(TEXT("Voice completion alone cannot navigate"),Title->IsEntranceTravelReady(At-.001));
    TestTrue(TEXT("Travel becomes ready exactly one second later"),Title->IsEntranceTravelReady(At));
    Title->HandleEntranceVoiceFinished();TestEqual(TEXT("Duplicate audio callback cannot extend the delay"),Title->TrainingTravelAt,At);
    Title->bEntranceVideoFinished=false;
    TestTrue(TEXT("A longer video cannot postpone the voice-driven transition"),Title->IsEntranceTravelReady(At));
    Title->bNavigationRequested=true;TestFalse(TEXT("Map travel cannot repeat"),Title->IsEntranceTravelReady(At+10));
    return !HasAnyErrors();
}
#endif
