#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Camera/CameraComponent.h"
#include "Character/OBActionComponent.h"
#include "Character/OBPlayerCharacter.h"
#include "Character/OBPlayerSplineComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SplineComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "World/OBRailPath.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOBRoutePreviewTest, "OperationBreakpoint.Rail.RoutePreviewXYZ",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOBRoutePreviewTest::RunTest(const FString&)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    auto* Player = World->SpawnActor<AOBPlayerCharacter>();
    auto* Rail = World->SpawnActor<AOBRailPath>();
    Rail->bPreviewEntireRoute = true;
    Rail->bExternallyControlled = true; // Preview takes priority over the normal director wait.
    auto* Curve = Rail->GetSpline();
    Curve->ClearSplinePoints();
    for (const FVector Point : {FVector(0,0,0), FVector(400,0,800), FVector(1000,0,100)})
        Curve->AddSplinePoint(Point, ESplineCoordinateSpace::World, false);
    for (int32 I=0; I<3; ++I) Curve->SetSplinePointType(I, ESplinePointType::Linear, false);
    Curve->UpdateSpline();
    auto* Component = Player->GetPlayerSplineComponent();
    Component->PlayerCharacter = Player;
    Component->RailPath = Rail;
    Component->PlayerCamera = Player->GetPlayerCamera();
    Component->FirstPersonCameraLocation = Component->PlayerCamera->GetRelativeLocation();
    Component->FirstPersonCameraRotation = Component->PlayerCamera->GetRelativeRotation();
    Component->TravelCameraRelativeLocation = FVector(-350.f,35.f,155.f);
    Component->TravelCameraRelativeRotation = FRotator(-6.f,3.f,0.f);
    Component->TravelFOV = 95.f;
    int32 ActionCount=0, CompletionCount=0;
    Component->OnActionPointReached.AddLambda([&ActionCount](int32) { ++ActionCount; });
    Component->OnRoutePreviewCompleted.AddLambda([&CompletionCount]() { ++CompletionCount; });
    for (const int32 Rate : {30,120})
    {
        Component->StartRoutePreview();
        TestTrue(TEXT("Preview starts at the configured third-person travel camera location"),
            Component->PlayerCamera->GetRelativeLocation().Equals(Component->TravelCameraRelativeLocation));
        TestTrue(TEXT("Preview starts at the configured travel camera rotation"),
            Component->PlayerCamera->GetRelativeRotation().Equals(Component->TravelCameraRelativeRotation));
        TestEqual(TEXT("Preview starts at travel FOV"),Component->PlayerCamera->FieldOfView,95.f);
        // A later camera writer must not leave route preview stuck in first person.
        Component->PlayerCamera->SetRelativeLocation(Component->FirstPersonCameraLocation);
        Component->PlayerCamera->SetRelativeRotation(Component->FirstPersonCameraRotation);
        Component->PlayerCamera->SetFieldOfView(70.f);
        TestTrue(TEXT("Preview drives the player moving animation state"), Component->IsMoving());
        TestEqual(TEXT("Gravity and walking floor correction are disabled"),
            Player->GetCharacterMovement()->MovementMode, MOVE_None);
        for (int32 Frame=0; Frame<Rate; ++Frame) Component->UpdateRoutePreview(1.f/Rate);
        TestTrue(TEXT("Travel speed is stable in 3D at 30/120 fps"),
            FMath::IsNearlyEqual(Component->GetDistanceAlongRoute(),300.f,.01f));
        for (int32 Frame=0; !Component->HasFinishedRoutePreview() && Frame<Rate*10; ++Frame)
        {
            Component->TickComponent(1.f/Rate,LEVELTICK_All,&Component->PrimaryComponentTick);
            if (Component->IsMoving())
                TestTrue(TEXT("Spline movement exposes velocity to animation"), Player->GetVelocity().Size()>0.f && Player->GetVelocity().Size()<=300.1f);
            FVector Feet=Player->GetActorLocation();
            Feet.Z-=Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
            TestTrue(TEXT("Feet follow spline X/Y/Z on ascent and descent"),
                Feet.Equals(Curve->GetLocationAtDistanceAlongSpline(Component->GetDistanceAlongRoute(),ESplineCoordinateSpace::World),.01f));
            TestTrue(TEXT("Body stays upright on steep sections"),
                FMath::IsNearlyZero(Player->GetActorRotation().Pitch) && FMath::IsNearlyZero(Player->GetActorRotation().Roll));
        }
        TestTrue(TEXT("Route passes intermediate stop and completes"),Component->HasFinishedRoutePreview());
        TestTrue(TEXT("Completed preview has zero animation velocity"),Player->GetVelocity().IsNearlyZero());
        const FVector End=Player->GetActorLocation();
        for (int32 I=0; I<10; ++I) Component->TickComponent(1.f,LEVELTICK_All,&Component->PrimaryComponentTick);
        TestTrue(TEXT("Completed preview holds its final location"),End.Equals(Player->GetActorLocation(),.01f));
        TestTrue(TEXT("Preview ticks restore and retain third-person travel camera at the endpoint"),
            Component->PlayerCamera->GetRelativeLocation().Equals(Component->TravelCameraRelativeLocation,.01f)
            && Component->PlayerCamera->GetRelativeRotation().Equals(Component->TravelCameraRelativeRotation,.01f));
        TestEqual(TEXT("Completed preview retains travel FOV"),Component->PlayerCamera->FieldOfView,95.f);
    }
    TestEqual(TEXT("No encounter arrival events are emitted"),ActionCount,0);
    TestEqual(TEXT("Completion fires once per run"),CompletionCount,2);
    TestEqual(TEXT("Route preview keeps combat gated"),Player->GetActionComponent()->GetActionState(),EOBActionState::Prepare);
    World->DestroyWorld(false);
    GEngine->DestroyWorldContext(World);
    return !HasAnyErrors();
}

#if WITH_EDITOR
#include "Editor.h"
#include "LevelEditor.h"
#include "EngineUtils.h"
#include "Containers/Ticker.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
#include "UnrealClient.h"
#include "Core/OBPlayerController.h"
#include "Widget/OBHUD.h"

static void RunStageRouteSmoke(UWorld* World)
{
    struct FState { double Started=0, FinishedAt=-1; float Timeout=30, MaxFootError=0; bool bPass=true, bShot=false; FTimerHandle Timer; };
    auto S=MakeShared<FState>(); S->Started=World->GetTimeSeconds();
    const FString Shot=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("Tests/StageRoute/RouteEnd.png"));
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Shot),true);
    TWeakObjectPtr<UWorld> WeakWorld=World;
    World->GetTimerManager().SetTimer(S->Timer,FTimerDelegate::CreateLambda([S,WeakWorld,Shot]() {
        auto* W=WeakWorld.Get(); if (!W) return;
        auto Finish=[&](bool bPass) {
            W->GetTimerManager().ClearTimer(S->Timer);
            UE_LOG(LogTemp,Display,TEXT("STAGE_ROUTE_MAP outcome=%s max_foot_error=%.4f"),bPass ? TEXT("PASS") : TEXT("FAIL"),S->MaxFootError);
            GEditor->RequestEndPlayMap();
            FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([bPass](float) {
                FPlatformMisc::RequestExitWithStatus(false,bPass ? 0 : 1); return false;
            }),1.f);
        };
        if (W->GetTimeSeconds()-S->Started>S->Timeout) { Finish(false); return; }
        auto* PC=Cast<AOBPlayerController>(W->GetFirstPlayerController());
        auto* P=PC ? Cast<AOBPlayerCharacter>(PC->GetPawn()) : nullptr;
        if (!P) return;
        auto* C=P->GetPlayerSplineComponent();
        if (!C->IsRoutePreview() || !C->GetRailPath()) { Finish(false); return; }
        S->Timeout=FMath::Max(30.f,C->GetRailPath()->GetSpline()->GetSplineLength()
            / FMath::Max(1.f,C->GetRailPath()->RoutePreviewSpeed)+15.f);
        FVector Feet=P->GetActorLocation(); Feet.Z-=P->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
        S->MaxFootError=FMath::Max(S->MaxFootError,static_cast<float>(FVector::Distance(Feet,
            C->GetRailPath()->GetSpline()->GetLocationAtDistanceAlongSpline(C->GetDistanceAlongRoute(),ESplineCoordinateSpace::World))));
        S->bPass &= S->MaxFootError<1.f && P->GetCharacterMovement()->MovementMode==MOVE_None
            && FMath::IsNearlyZero(P->GetActorRotation().Pitch) && FMath::IsNearlyZero(P->GetActorRotation().Roll)
            && P->GetActionComponent()->GetActionState()==EOBActionState::Prepare && PC->GetViewTarget()==P
            && P->GetPlayerCamera()->GetRelativeLocation().X < -120.f;
        if (!C->HasFinishedRoutePreview()) return;
        if (S->FinishedAt<0) { S->FinishedAt=W->GetTimeSeconds(); return; }
        if (!S->bShot && W->GetTimeSeconds()-S->FinishedAt>.5) {
            FScreenshotRequest::RequestScreenshot(Shot,false,false,false,FIntRect(),true); S->bShot=true; return;
        }
        if (S->bShot && W->GetTimeSeconds()-S->FinishedAt>1.5)
            Finish(S->bPass && IFileManager::Get().FileSize(*Shot)>0);
    }),.05f,true);
}

static FAutoConsoleCommand GOBStageRoutePIE(TEXT("OB.Stage.RoutePreviewPIE"),
    TEXT("Verify Stage_01 automatic route preview XYZ and endpoint in PIE; exits its process."),
    FConsoleCommandDelegate::CreateLambda([]() {
        if (!GEditor) return;
        FRequestPlaySessionParams Params; Params.GlobalMapOverride=TEXT("/Game/Maps/Stage_01");
        Params.DestinationSlateViewport=FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor")).GetFirstActiveViewport();
        Params.bAllowOnlineSubsystem=false; GEditor->RequestPlaySession(Params);
        const double Start=FPlatformTime::Seconds();
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Start](float) {
            if (GEditor && GEditor->PlayWorld) { RunStageRouteSmoke(GEditor->PlayWorld); return false; }
            if (FPlatformTime::Seconds()-Start>45) { FPlatformMisc::RequestExitWithStatus(false,1); return false; }
            return true;
        }),.05f);
    }));
#endif
#endif
