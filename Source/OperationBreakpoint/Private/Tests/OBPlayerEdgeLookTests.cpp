#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Character/OBPlayerCharacter.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOBPlayerEdgeLookTest,
    "OperationBreakpoint.Camera.EdgeLook",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOBPlayerEdgeLookTest::RunTest(const FString& Parameters)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    AOBPlayerCharacter* Player = World->SpawnActor<AOBPlayerCharacter>();
    if (!TestNotNull(TEXT("Player spawns"), Player))
    {
        World->DestroyWorld(false);
        GEngine->DestroyWorldContext(World);
        return false;
    }

    Player->SetActorRotation(FRotator(0.0f, 170.0f, 0.0f));
    UCameraComponent* Camera = Player->GetPlayerCamera();
    Camera->Activate(); // This isolated world does not run BeginPlay/automatic component activation.
    Camera->SetFieldOfView(90.0f);
    Camera->SetRelativeRotation(FRotator(-3.0f, 0.0f, 0.0f));
    FMinimalViewInfo Base;
    Player->CalcCamera(1.0f / 60.0f, Base);
    TestEqual(TEXT("Camera component FOV reaches final view unchanged"), Base.FOV, 90.0f);
    Player->CalcCamera(1.0f / 60.0f, Base);
    TestEqual(TEXT("FOV does not accumulate across evaluations"), Base.FOV, 90.0f);
    TestEqual(TEXT("Component FOV remains authored"), Camera->FieldOfView, 90.0f);
    Camera->Deactivate();
    Player->CalcCamera(1.0f / 60.0f, Base);
    TestEqual(TEXT("Inactive camera does not apply another FOV offset"), Base.FOV, 90.0f);
    Camera->Activate();
    Player->CalcCamera(1.0f / 60.0f, Base);

    // Regression: use the actual BP (including its saved component overrides), then exercise
    // the engine camera manager which supplies the rendered and cursor-deprojection FOV.
    UClass* BPClass = LoadClass<AOBPlayerCharacter>(nullptr,
        TEXT("/Game/Blueprints/Player/BP_PlayerCharacter.BP_PlayerCharacter_C"));
    UClass* PCClass = LoadClass<APlayerController>(nullptr,
        TEXT("/Game/Blueprints/Player/BP_OBPlayerController.BP_OBPlayerController_C"));
    if (TestNotNull(TEXT("Player BP loads"), BPClass) && TestNotNull(TEXT("Controller BP loads"), PCClass))
    {
        AOBPlayerCharacter* BPPlayer = World->SpawnActor<AOBPlayerCharacter>(BPClass);
        APlayerController* PC = World->SpawnActor<APlayerController>(PCClass);
        if (TestNotNull(TEXT("BP player spawns"), BPPlayer) && TestNotNull(TEXT("BP controller spawns"), PC))
        {
            UCameraComponent* BPCamera = BPPlayer->GetPlayerCamera();
            BPCamera->Activate();
            if (!PC->PlayerCameraManager) PC->SpawnPlayerCameraManager();
            if (TestNotNull(TEXT("Actual controller creates camera manager"), PC->PlayerCameraManager.Get()))
            {
                PC->PlayerCameraManager->bUseClientSideCameraUpdates = false; // No local viewport in this test world.
                PC->SetViewTarget(BPPlayer);
                TestTrue(TEXT("View target is the real player BP"), PC->GetViewTarget() == BPPlayer);
                for (float FOV : {BPCamera->FieldOfView, 60.0f, 75.0f, 85.0f})
                {
                    BPCamera->SetFieldOfView(FOV);
                    for (int32 Frame = 0; Frame < 3; ++Frame)
                    {
                        PC->PlayerCameraManager->UpdateCamera(1.0f / 60.0f);
                        TestEqual(TEXT("BP Camera FOV reaches engine view cache"),
                            PC->PlayerCameraManager->GetCameraCacheView().FOV, FOV);
                        TestEqual(TEXT("BP Camera FOV reaches projection FOV"),
                            PC->PlayerCameraManager->GetFOVAngle(), FOV);
                    }
                }
                AddInfo(TEXT("Actual BP -> PlayerCameraManager cache/projection FOV verified at 60, 75 and 85 degrees."));
            }
        }
    }

    auto Step = [&](float X, float Dt, bool bCombat = true, bool bCursor = true)
    {
        FMinimalViewInfo View;
        Camera->GetCameraView(Dt, View);
        Player->ApplyCombatCamera(Dt, bCombat, bCursor, X, View);
        return View;
    };

    for (float X : {0.15f, 0.3f, 0.5f, 0.7f, 0.85f})
    {
        TestTrue(TEXT("Central 70 percent has no yaw offset"),
            Step(X, 1.0f).Rotation.Equals(Base.Rotation, 0.001f));
    }
    FMinimalViewInfo View = Step(1.0f, 1.0f / 60.0f);
    TestTrue(TEXT("Edge turn starts smoothly"), Player->CurrentEdgeLookYaw > 0.0f && Player->CurrentEdgeLookYaw < 10.0f);
    for (int32 I = 0; I < 600; ++I) View = Step(1.0f, 1.0f / 60.0f);
    TestEqual(TEXT("Holding right edge stops at ten degrees"), Player->CurrentEdgeLookYaw, 10.0f);
    TestTrue(TEXT("Final view turns right"), FMath::IsNearlyZero(FMath::FindDeltaAngleDegrees(View.Rotation.Yaw, 180.0f)));
    TestEqual(TEXT("Pitch is preserved"), View.Rotation.Pitch, Base.Rotation.Pitch);
    TestTrue(TEXT("Actor heading is preserved"), FMath::IsNearlyEqual(Player->GetActorRotation().Yaw, 170.0f));
    TestTrue(TEXT("Camera component yaw is preserved"), FMath::IsNearlyZero(Camera->GetRelativeRotation().Yaw));

    for (int32 I = 0; I < 120; ++I) View = Step(0.0f, 1.0f / 60.0f);
    TestEqual(TEXT("Left edge reaches minus ten degrees"), Player->CurrentEdgeLookYaw, -10.0f);
    Step(0.5f, 1.0f / 60.0f);
    TestTrue(TEXT("Center returns smoothly rather than snapping"), Player->CurrentEdgeLookYaw > -10.0f && Player->CurrentEdgeLookYaw < 0.0f);
    for (int32 I = 0; I < 120; ++I) Step(0.5f, 1.0f / 60.0f);
    TestEqual(TEXT("Center settles at zero"), Player->CurrentEdgeLookYaw, 0.0f);
    for (int32 I = 0; I < 120; ++I) Step(0.925f, 1.0f / 60.0f);
    TestTrue(TEXT("Halfway through right edge zone gives half turn"), FMath::IsNearlyEqual(Player->CurrentEdgeLookYaw, 5.0f, 0.001f));

    // A crouch/location change must survive edge look; it is applied only to the evaluated view rotation.
    Camera->SetRelativeLocation(FVector(60.0f, 0.0f, 28.0f));
    View = Step(1.0f, 1.0f);
    TestTrue(TEXT("Crouch camera position is retained"), View.Location.Equals(Camera->GetComponentLocation(), 0.001f));
    for (int32 I = 0; I < 120; ++I) Step(1.0f, 1.0f / 60.0f, true, false);
    TestEqual(TEXT("Lost cursor/focus recenters"), Player->CurrentEdgeLookYaw, 0.0f);

    Step(1.0f, 1.0f);
    View = Step(1.0f, 0.016f, false);
    TestEqual(TEXT("Travel/alignment clears edge offset"), Player->CurrentEdgeLookYaw, 0.0f);
    TestTrue(TEXT("Travel keeps its authored view rotation"), View.Rotation.Equals(Camera->GetComponentRotation(), 0.001f));
    Step(1.0f, 1.0f);
    Player->bEnableEdgeLook = false;
    Step(1.0f, 0.016f);
    TestEqual(TEXT("Disabling clears offset"), Player->CurrentEdgeLookYaw, 0.0f);
    Player->bEnableEdgeLook = true;

    float YawAt30 = 0.0f;
    for (int32 Rate : {30, 120})
    {
        Player->CurrentEdgeLookYaw = 0.0f;
        for (int32 I = 0; I < Rate / 5; ++I) Step(1.0f, 1.0f / Rate);
        if (Rate == 30) YawAt30 = Player->CurrentEdgeLookYaw;
        else TestTrue(TEXT("Equal elapsed time matches at 30 and 120 fps"),
            FMath::IsNearlyEqual(YawAt30, Player->CurrentEdgeLookYaw, 0.001f));
    }

    World->DestroyWorld(false);
    GEngine->DestroyWorldContext(World);
    return true;
}

#endif
