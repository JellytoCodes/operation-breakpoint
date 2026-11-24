#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "Core/OBGameInstance.h"
#include "Training/OBTrainingDirector.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "JsonObjectConverter.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOBLauncherIdentityTest, "OperationBreakpoint.Launcher.LearnerIdentity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOBLauncherIdentityTest::RunTest(const FString&)
{
    const FString Temporary = FPaths::CreateTempFilename(*FPaths::ProjectSavedDir(), TEXT("learner-test-"), TEXT(".json"));
    auto* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT
    {
        World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
        IFileManager::Get().Delete(*Temporary);
    };
    auto Write = [&](const FString& Json) {
        return FFileHelper::SaveStringToFile(Json, *Temporary, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    };
    auto* Instance = NewObject<UOBGameInstance>(GEngine);
    World->SetGameInstance(Instance);
    for (const FString& Arguments : {
        FString(TEXT("\"-OBLearnerProfile=D:\\Launcher folder\\교육생.json\" -windowed")),
        FString(TEXT("-OBLearnerProfile=\"D:\\Launcher folder\\교육생.json\" -windowed"))})
    {
        FString Parsed;
        TestTrue(TEXT("Both Windows subprocess and Unreal quoted argument forms are accepted"),
            Instance->ReadLearnerProfileArgument(*Arguments, Parsed));
        TestEqual(TEXT("Argument parsing preserves the entire path"), Parsed, FString(TEXT("D:\\Launcher folder\\교육생.json")));
    }
    const FString Valid = TEXT("{\"schema_version\":1,\"learner_name\":\"런처 검증\",\"learner_id\":\"06-71000001\"}");
    if (!TestTrue(TEXT("UTF8 profile fixture is saved"), Write(Valid))) return false;
    FString Input = Temporary;
    Instance->ReadLearnerProfileArgument(FCommandLine::Get(), Input);
    if (!TestTrue(TEXT("The game reads the launcher profile, including paths with spaces and Korean text"), Instance->LoadLearnerProfile(Input))) return false;
    TestEqual(TEXT("Korean learner name arrives intact"), Instance->GetLearnerName(), FString(TEXT("런처 검증")));
    TestEqual(TEXT("Leading zeroes and the canonical service number arrive intact"), Instance->GetLearnerId(), FString(TEXT("06-71000001")));

    auto* Director = World->SpawnActor<AOBTrainingDirector>();
    Director->LearnerName = TEXT("old map name"); Director->LearnerId = TEXT("old map id");
    Director->StartTraining();
    const auto Original = Director->GetTrainingResult();
    TestEqual(TEXT("A new training record uses the launcher name instead of an old map value"), Original.LearnerName, Instance->GetLearnerName());
    TestEqual(TEXT("A new training record stores the entered service number"), Original.LearnerId, Instance->GetLearnerId());
    FString ResultJson;
    FOBTrainingResultData Persisted;
    TestTrue(TEXT("Training identity survives the same JSON serialization used for saved records"),
        FJsonObjectConverter::UStructToJsonObjectString(Original, ResultJson)
        && FJsonObjectConverter::JsonObjectStringToUStruct(ResultJson, &Persisted));
    TestEqual(TEXT("Certificate source data retains the learner name"), Persisted.LearnerName, Original.LearnerName);
    TestEqual(TEXT("Certificate source data retains the learner id"), Persisted.LearnerId, Original.LearnerId);
    Write(TEXT("{\"schema_version\":1,\"learner_name\":\"다음 교육생\",\"learner_id\":\"26-71000002\"}"));
    TestTrue(TEXT("A later profile can be validated independently"), Instance->LoadLearnerProfile(Temporary));
    TestEqual(TEXT("A later learner cannot alter the original record"), Director->GetTrainingResult().LearnerName, Original.LearnerName);
    TestEqual(TEXT("A later learner cannot alter the original record's service number"), Director->GetTrainingResult().LearnerId, Original.LearnerId);

    for (const FString& Invalid : {
        FString(TEXT("{}")), FString(TEXT("not json")),
        FString(TEXT("{\"schema_version\":2,\"learner_name\":\"검증\",\"learner_id\":\"26-71000001\"}")),
        FString(TEXT("{\"schema_version\":1,\"learner_name\":\"  \",\"learner_id\":\"26-71000001\"}")),
        FString(TEXT("{\"schema_version\":1,\"learner_name\":\"검증\\n\",\"learner_id\":\"26-71000001\"}")),
        FString(TEXT("{\"schema_version\":1,\"learner_name\":\"검증\",\"learner_id\":\"26-10001\"}")),
        FString(TEXT("{\"schema_version\":1,\"learner_name\":\"검증\",\"learner_id\":\"26-200001\"}")),
        FString(TEXT("{\"schema_version\":1,\"learner_name\":\"검증\",\"learner_id\":\"26-7100000a\"}")),
        FString(TEXT("{\"schema_version\":1,\"learner_name\":\"검증\",\"learner_id\":2671000001}"))})
    {
        Write(Invalid);
        TestFalse(TEXT("Invalid or non-enlisted runtime profiles are rejected"), Instance->LoadLearnerProfile(Temporary));
        TestFalse(TEXT("Rejected input never reuses a previous learner"), Instance->HasLearnerProfile());
    }
    TestFalse(TEXT("A missing profile is rejected"), Instance->LoadLearnerProfile(Temporary + TEXT(".missing")));
    return !HasAnyErrors();
}
#endif
