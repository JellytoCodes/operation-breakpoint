#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Character/OBPlayerCharacter.h"
#include "Character/OBPlayerSplineComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "World/OBRailPath.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOBRailCameraFOVTest,
    "OperationBreakpoint.Camera.RailStateFOV",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOBRailCameraFOVTest::RunTest(const FString& Parameters)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    AOBPlayerCharacter* Player = World->SpawnActor<AOBPlayerCharacter>();
    AOBRailPath* Rail = World->SpawnActor<AOBRailPath>();
    if (!TestNotNull(TEXT("Player spawns"), Player) || !TestNotNull(TEXT("Rail spawns"), Rail))
    {
        World->DestroyWorld(false);
        GEngine->DestroyWorldContext(World);
        return false;
    }
    UOBPlayerSplineComponent* Spline = Player->GetPlayerSplineComponent();
    UCameraComponent* Camera = Player->GetPlayerCamera();
    Camera->Activate();
    Spline->PlayerCharacter = Player;
    Spline->PlayerCamera = Camera;
    Spline->RailPath = Rail;
    Spline->FirstPersonCameraLocation = Camera->GetRelativeLocation();
    Spline->FirstPersonCameraRotation = Camera->GetRelativeRotation();
    using EState = UOBPlayerSplineComponent::EOBRailState;

    Camera->SetFieldOfView(70.0f);
    Spline->RailState = EState::Moving;
    Spline->UpdateCameraFOV(1.0f / 60.0f);
    TestTrue(TEXT("Travel starts widening smoothly"), Camera->FieldOfView > 70.0f && Camera->FieldOfView < Spline->TravelFOV);
    for (int32 I = 0; I < 120; ++I) Spline->UpdateCameraFOV(1.0f / 60.0f);
    TestEqual(TEXT("Travel settles at configured FOV"), Camera->FieldOfView, Spline->TravelFOV);
    Spline->RailState = EState::AligningAtStop;
    Spline->UpdateCameraFOV(1.0f);
    TestEqual(TEXT("Stop alignment retains travel FOV until Action"), Camera->FieldOfView, Spline->TravelFOV);

    Spline->BeginAction();
    TestEqual(TEXT("Action entry does not snap FOV"), Camera->FieldOfView, Spline->TravelFOV);
    // Simulate crouch after entry; Action ticks must not overwrite this camera transform.
    Camera->SetRelativeLocation(FVector(60.0f, 0.0f, 28.0f));
    const FTransform CrouchTransform = Camera->GetRelativeTransform();
    Spline->TickComponent(1.0f / 60.0f, LEVELTICK_All, &Spline->PrimaryComponentTick);
    TestTrue(TEXT("Action tick blends even though movement tick returns early"), Camera->FieldOfView > 70.0f && Camera->FieldOfView < Spline->TravelFOV);
    for (int32 I = 0; I < 120; ++I)
        Spline->TickComponent(1.0f / 60.0f, LEVELTICK_All, &Spline->PrimaryComponentTick);
    TestEqual(TEXT("Action settles at 70"), Camera->FieldOfView, 70.0f);
    TestTrue(TEXT("Action FOV blend preserves crouch transform"), Camera->GetRelativeTransform().Equals(CrouchTransform));
    FMinimalViewInfo View;
    Player->CalcCamera(1.0f / 60.0f, View);
    TestEqual(TEXT("Action component FOV reaches final player view"), View.FOV, 70.0f);
    TestTrue(TEXT("First-person crouch hides the owner's weapon"),Player->GetWeaponMesh()->bOwnerNoSee);

    Spline->RailState = EState::Moving;
    for (int32 I = 0; I < 120; ++I) Spline->UpdateCameraFOV(1.0f / 60.0f);
    TestEqual(TEXT("Next travel restores configured FOV"), Camera->FieldOfView, Spline->TravelFOV);

    float FOVAt30 = 0.0f;
    Spline->RailState = EState::Action;
    for (int32 Rate : {30, 120})
    {
        Camera->SetFieldOfView(Spline->TravelFOV);
        for (int32 I = 0; I < Rate / 5; ++I) Spline->UpdateCameraFOV(1.0f / Rate);
        if (Rate == 30) FOVAt30 = Camera->FieldOfView;
        else TestTrue(TEXT("Transition speed is stable at 30 and 120 fps"), FMath::IsNearlyEqual(FOVAt30, Camera->FieldOfView, 0.001f));
    }

    Camera->SetRelativeRotation(FRotator(3, 4, 1));
    const FRotator OriginalRotation = Camera->GetRelativeRotation();
    const FVector OriginalLocation = Camera->GetRelativeLocation();
    const float OriginalFOV = Camera->FieldOfView;
    Spline->FocusTrainingCamera(-25, 50, .65f, -5.5f);
    Spline->TickComponent(.325f, LEVELTICK_All, &Spline->PrimaryComponentTick);
    TestTrue(TEXT("Weapon view pitch moves smoothly downward"), Camera->GetRelativeRotation().Pitch < OriginalRotation.Pitch && Camera->GetRelativeRotation().Pitch > -22);
    TestTrue(TEXT("Weapon view FOV moves smoothly toward zoom"), Camera->FieldOfView < OriginalFOV && Camera->FieldOfView > 50);
    Spline->TickComponent(.325f, LEVELTICK_All, &Spline->PrimaryComponentTick);
    TestTrue(TEXT("Weapon view holds its downward pitch"), FMath::IsNearlyEqual(Camera->GetRelativeRotation().Pitch, -22.0, .001));
    TestEqual(TEXT("Weapon view holds its zoom despite normal rail FOV logic"), Camera->FieldOfView, 50.f);
    TestTrue(TEXT("Inspection centers the gun with camera yaw without turning the body"), FMath::IsNearlyEqual(Camera->GetRelativeRotation().Yaw, -1.5, .001));
    TestTrue(TEXT("Inspection does not overwrite crouch camera location"), Camera->GetRelativeLocation().Equals(OriginalLocation));
    Player->CalcCamera(1.f / 60, View);
    TestEqual(TEXT("Rendered weapon view uses inspection FOV"), View.FOV, 50.f);
    TestTrue(TEXT("Weapon inspection also hides the player's held weapon"),Player->GetWeaponMesh()->bOwnerNoSee);
    Spline->ClearTrainingCameraFocus();
    TestTrue(TEXT("Returning view remains controlled during the blend"), Spline->IsTrainingCameraFocusActive());
    Spline->TickComponent(.65f, LEVELTICK_All, &Spline->PrimaryComponentTick);
    TestFalse(TEXT("Restored view releases camera focus"), Spline->IsTrainingCameraFocusActive());
    TestTrue(TEXT("Return restores the original camera orientation"), Camera->GetRelativeRotation().Equals(OriginalRotation, .001));
    TestEqual(TEXT("Return restores the normal action FOV"), Camera->FieldOfView, 70.f);
    Player->CalcCamera(1.f/60,View);
    TestTrue(TEXT("Returning to first-person hides the weapon again"),Player->GetWeaponMesh()->bOwnerNoSee);
    Rail->bExternallyControlled = true;
    Spline->FocusTrainingCamera(-25, 50, .65f);
    Spline->TickComponent(.65f, LEVELTICK_All, &Spline->PrimaryComponentTick);
    Spline->TurnTrainingToYaw(90);
    TestFalse(TEXT("Later route turns cancel the inspection view"), Spline->IsTrainingCameraFocusActive());
    TestTrue(TEXT("Route turn starts with the camera orientation restored"), Camera->GetRelativeRotation().Equals(OriginalRotation, .001));

    World->DestroyWorld(false);
    GEngine->DestroyWorldContext(World);
    return true;
}

#endif
