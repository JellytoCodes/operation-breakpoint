#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Editor.h"
#include "LevelEditor.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "UnrealClient.h"
#include "Containers/Ticker.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CoreDelegates.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ContentStreaming.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "NavMesh/RecastNavMesh.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Character/OBEnemyCharacter.h"
#include "World/OBEncounterSpawner.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"

namespace OBRepairDiagnostics
{
static TSharedPtr<FJsonValue> Vec(FVector V)
{
    return MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{
        MakeShared<FJsonValueNumber>(V.X),MakeShared<FJsonValueNumber>(V.Y),MakeShared<FJsonValueNumber>(V.Z)});
}
static TSharedPtr<FJsonObject> DescribeHit(const FHitResult& Hit)
{
    auto R=MakeShared<FJsonObject>();
    R->SetBoolField(TEXT("hit"),Hit.bBlockingHit);
    R->SetStringField(TEXT("actor"),Hit.GetActor() ? Hit.GetActor()->GetActorLabel() : TEXT(""));
    R->SetStringField(TEXT("path"),GetPathNameSafe(Hit.GetActor()));
    R->SetStringField(TEXT("component"),GetNameSafe(Hit.GetComponent()));
    R->SetField(TEXT("point"),Vec(Hit.ImpactPoint)); R->SetField(TEXT("normal"),Vec(Hit.ImpactNormal));
    R->SetBoolField(TEXT("penetrating"),Hit.bStartPenetrating);
    if (Hit.GetComponent())
    {
        R->SetBoolField(TEXT("affects_navigation"),Hit.GetComponent()->CanEverAffectNavigation());
        R->SetStringField(TEXT("profile"),Hit.GetComponent()->GetCollisionProfileName().ToString());
    }
    return R;
}
static void Save(UWorld* W,const FString& Map)
{
    auto Root=MakeShared<FJsonObject>();
    Root->SetStringField(TEXT("map"),Map);
    Root->SetNumberField(TEXT("lighting_unbuilt"),W->NumLightingUnbuiltObjects);
    Root->SetNumberField(TEXT("reflection_captures_unbuilt"),W->NumUnbuiltReflectionCaptures);
    Root->SetNumberField(TEXT("texture_components_unbuilt"),W->NumTextureStreamingUnbuiltComponents);
    Root->SetNumberField(TEXT("texture_resources_dirty"),W->NumTextureStreamingDirtyResources);
    Root->SetNumberField(TEXT("streaming_over_budget_bytes"),IStreamingManager::Get().GetTextureStreamingManager().GetMemoryOverBudget());
    for (const TCHAR* Name : {TEXT("r.Streaming.PoolSize"),TEXT("r.AllowStaticLighting"),TEXT("sg.TextureQuality"),TEXT("sg.ShadowQuality")})
        if (auto* C=IConsoleManager::Get().FindConsoleVariable(Name)) Root->SetStringField(Name,C->GetString());
    FCoreDelegates::FSeverityMessageMap Messages;
    FCoreDelegates::OnGetOnScreenMessages.Broadcast(Messages);
    TArray<TSharedPtr<FJsonValue>> Extra;
    for (const auto& M : Messages)
    {
        auto R=MakeShared<FJsonObject>(); R->SetNumberField(TEXT("severity"),static_cast<int32>(M.Key));
        R->SetStringField(TEXT("message"),M.Value.ToString()); Extra.Add(MakeShared<FJsonValueObject>(R));
    }
    Root->SetArrayField(TEXT("screen_messages"),Extra);
    auto* Class=LoadClass<AOBEnemyCharacter>(nullptr,TEXT("/Game/Blueprints/Enemy/BP_EnemyCharacter.BP_EnemyCharacter_C"));
    auto* Enemy=Class ? Class->GetDefaultObject<AOBEnemyCharacter>() : nullptr;
    if (Enemy)
    {
        const float Radius=Enemy->GetCapsuleComponent()->GetScaledCapsuleRadius();
        const float HH=Enemy->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
        Root->SetNumberField(TEXT("capsule_radius"),Radius); Root->SetNumberField(TEXT("capsule_half_height"),HH);
        Root->SetNumberField(TEXT("max_step_height"),Enemy->GetCharacterMovement()->MaxStepHeight);
        Root->SetNumberField(TEXT("movement_nav_radius"),Enemy->GetCharacterMovement()->GetNavAgentPropertiesRef().AgentRadius);
        Root->SetNumberField(TEXT("movement_nav_height"),Enemy->GetCharacterMovement()->GetNavAgentPropertiesRef().AgentHeight);
        auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(W);
        for (TActorIterator<ARecastNavMesh> It(W);It;++It)
        {
            Root->SetNumberField(TEXT("nav_radius"),It->GetConfig().AgentRadius);
            Root->SetNumberField(TEXT("nav_height"),It->GetConfig().AgentHeight);
        }
        TArray<TSharedPtr<FJsonValue>> Rows;
        for (TActorIterator<AOBEncounterSpawner> It(W);Nav && It;++It)
        {
            auto* S=*It;
            if (!S->bMoveToStagingPoint || !S->GetFolderPath().ToString().EndsWith(TEXT("/Move"))) continue;
            auto R=MakeShared<FJsonObject>(); R->SetStringField(TEXT("name"),S->GetActorLabel());
            const FVector Start=S->GetActorLocation()-FVector(0,0,HH),Goal=S->GetStagingTransform().GetLocation();
            R->SetField(TEXT("start"),Vec(Start));R->SetField(TEXT("goal"),Vec(Goal));
            FNavLocation A,B;
            const bool bA=Nav->ProjectPointToNavigation(Start,A,FVector(100,100,120));
            const bool bB=Nav->ProjectPointToNavigation(Goal,B,FVector(100,100,120));
            auto* Path=bA && bB ? UNavigationSystemV1::FindPathToLocationSynchronously(W,A.Location,B.Location) : nullptr;
            R->SetBoolField(TEXT("partial"),Path && Path->IsPartial());
            TArray<TSharedPtr<FJsonValue>> Points,Sweeps,Floors;
            FCollisionQueryParams Q(SCENE_QUERY_STAT(OBStageRepair),false);
            for (TActorIterator<ACharacter> Pawn(W);Pawn;++Pawn) Q.AddIgnoredActor(*Pawn);
            if (Path)
            {
                for (const auto& P : Path->PathPoints) Points.Add(Vec(P));
                for (int32 I=1;I<Path->PathPoints.Num();++I)
                {
                    FHitResult Hit;
                    W->SweepSingleByProfile(Hit,Path->PathPoints[I-1]+FVector(0,0,HH+3),Path->PathPoints[I]+FVector(0,0,HH+3),
                        FQuat::Identity,TEXT("Pawn"),FCollisionShape::MakeCapsule(Radius,HH),Q);
                    Sweeps.Add(MakeShared<FJsonValueObject>(DescribeHit(Hit)));
                }
            }
            const int32 Steps=FMath::Max(2,FMath::CeilToInt(FVector::Dist2D(Start,Goal)/100.f));
            for (int32 I=0;I<=Steps;++I)
            {
                const FVector P=FMath::Lerp(Start,Goal,float(I)/Steps); FHitResult Hit;
                W->LineTraceSingleByChannel(Hit,P+FVector(0,0,150),P-FVector(0,0,250),ECC_Pawn,Q);
                auto Floor=DescribeHit(Hit);Floor->SetField(TEXT("sample"),Vec(P));
                FNavLocation N;Floor->SetBoolField(TEXT("nav"),Nav->ProjectPointToNavigation(P,N,FVector(50,50,100)));
                Floors.Add(MakeShared<FJsonValueObject>(Floor));
            }
            R->SetArrayField(TEXT("points"),Points);R->SetArrayField(TEXT("sweeps"),Sweeps);R->SetArrayField(TEXT("floors"),Floors);
            Rows.Add(MakeShared<FJsonValueObject>(R));
        }
        Root->SetArrayField(TEXT("spawners"),Rows);
    }
    const FString Folder=FPaths::ProjectDir()/TEXT("Build/Testing/Verification/StageRepair");
    IFileManager::Get().MakeDirectory(*Folder,true);
    FString Text; FJsonSerializer::Serialize(Root,TJsonWriterFactory<>::Create(&Text));
    FFileHelper::SaveStringToFile(Text,*(Folder/(Map+TEXT("-diagnostics.json"))));
    FScreenshotRequest::RequestScreenshot(Folder/(Map+TEXT("-before.png")),true,false);
    UE_LOG(LogTemp,Display,TEXT("MAP_REPAIR_DIAGNOSTICS map=%s lighting=%d texture_unbuilt=%d dirty=%d over_budget=%lld"),
        *Map,W->NumLightingUnbuiltObjects,W->NumTextureStreamingUnbuiltComponents,W->NumTextureStreamingDirtyResources,
        int64(IStreamingManager::Get().GetTextureStreamingManager().GetMemoryOverBudget()));
}
}
static FAutoConsoleCommand GOBMapRepairDiagnostics(TEXT("OB.Map.RepairDiagnosticsPIE"),
    TEXT("Read map warnings, staging paths and capsule sweeps, screenshot and exit this disposable editor."),
    FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args) {
        if (!GEditor) return;
        const FString Map=Args.IsEmpty() ? TEXT("Stage_01") : Args[0];
        FRequestPlaySessionParams Params;Params.GlobalMapOverride=TEXT("/Game/Maps/")+Map;
        Params.DestinationSlateViewport=FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor")).GetFirstActiveViewport();
        Params.bAllowOnlineSubsystem=false;GEditor->RequestPlaySession(Params);
        const double Started=FPlatformTime::Seconds();
        auto Phase=MakeShared<int32>(0);
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Map,Started,Phase](float) {
            UWorld* W=GEditor ? GEditor->PlayWorld.Get() : nullptr;
            if (*Phase==0 && W && W->GetTimeSeconds()>10)
            { OBRepairDiagnostics::Save(W,Map);*Phase=1;return true; }
            if (*Phase==1) { GEditor->RequestEndPlayMap();*Phase=2;return true; }
            if (*Phase==2 || FPlatformTime::Seconds()-Started>90)
            { FPlatformMisc::RequestExitWithStatus(false,*Phase==2 ? 0 : 1);return false; }
            return true;
        }),.5f);
    }));
#endif
