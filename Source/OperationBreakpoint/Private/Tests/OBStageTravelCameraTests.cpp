#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "UObject/Package.h"
#include "Camera/CameraComponent.h"
#include "UObject/UnrealType.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SplineComponent.h"
#include "Character/OBPlayerCharacter.h"
#include "Character/OBPlayerSplineComponent.h"
#include "World/OBRailPath.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOBStageTravelCameraTest,"OperationBreakpoint.Camera.Stage01FirstPersonTravel",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FOBStageTravelCameraTest::RunTest(const FString&)
{
    struct FCase { const TCHAR* Map;const TCHAR* Prefix;bool bFirstPerson; };
    for (const FCase& Case : {FCase{TEXT("Stage_01"),TEXT(""),true},
        FCase{TEXT("UEDPIE_4_Stage_01"),TEXT("UEDPIE_4_"),true},
        FCase{TEXT("TutorialMap"),TEXT(""),false},FCase{TEXT("Stage_02"),TEXT(""),false}})
    {
        auto* Package=CreatePackage(*(FString(TEXT("/Temp/OBStageTravelCamera/"))+Case.Map));
        auto* W=UWorld::CreateWorld(EWorldType::Game,false,FName(Case.Map),Package);
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(W);
        ON_SCOPE_EXIT { W->DestroyWorld(false);GEngine->DestroyWorldContext(W); };
        W->StreamingLevelsPrefix=Case.Prefix;
        auto* Player=W->SpawnActor<AOBPlayerCharacter>();
        auto* Rail=W->SpawnActor<AOBRailPath>();
        auto* C=Player->GetPlayerSplineComponent();auto* Camera=Player->GetPlayerCamera();
        Camera->Activate();C->PlayerCharacter=Player;C->PlayerCamera=Camera;C->RailPath=Rail;
        C->FirstPersonCameraLocation=FVector(55,0,50);C->FirstPersonCameraRotation=FRotator(2,0,0);
        C->TravelFOV=86;C->ActionFOV=57;
        using EState=UOBPlayerSplineComponent::EOBRailState;
        TestEqual(FString::Printf(TEXT("First-person travel is scoped to %s"),Case.Map),C->UsesFirstPersonTravelCamera(),Case.bFirstPerson);
        Camera->SetRelativeLocation(C->TravelCameraRelativeLocation);
        Camera->SetRelativeRotation(C->TravelCameraRelativeRotation);
        C->BeginTravel();
        if (Case.bFirstPerson)
            TestTrue(TEXT("Stage travel enters first person before the first movement frame"),Camera->GetRelativeLocation().Equals(C->FirstPersonCameraLocation));
        for (EState State : {EState::Moving,EState::AligningForTravel,EState::RoutePreview,EState::RoutePreviewComplete})
        {
            C->RailState=State;
            for (int32 I=0;I<120;++I) { C->UpdateCamera(1.f/60);C->UpdateCameraFOV(1.f/60); }
            TestTrue(TEXT("Travel and return turns retain the map-specific view"),
                Camera->GetRelativeLocation().Equals(Case.bFirstPerson ? C->FirstPersonCameraLocation : C->TravelCameraRelativeLocation,.01)
                && Camera->GetRelativeRotation().Equals(Case.bFirstPerson ? C->FirstPersonCameraRotation : C->TravelCameraRelativeRotation,.01));
            TestEqual(TEXT("Travel FOV remains independently configured"),Camera->FieldOfView,86.f);
        }
        FMinimalViewInfo View;Player->CalcCamera(1.f/60,View);
        TestEqual(TEXT("The rendered travel FOV uses the configured value"),View.FOV,86.f);
        TestEqual(TEXT("The held weapon is hidden for Stage first person only"),bool(Player->GetWeaponMesh()->bOwnerNoSee),Case.bFirstPerson);
        C->BeginAction();
        Camera->SetRelativeLocation(FVector(60,4,28));
        Camera->SetRelativeRotation(FRotator(-4,6,0));
        const FTransform StopView=Camera->GetRelativeTransform();
        for (int32 I=0;I<120;++I) C->TickComponent(1.f/60,LEVELTICK_All,&C->PrimaryComponentTick);
        TestTrue(TEXT("Stopped camera/posture adjustments are not overwritten by travel mode"),Camera->GetRelativeTransform().Equals(StopView));
        TestEqual(TEXT("Stopped FOV remains separate from travel FOV"),Camera->FieldOfView,57.f);
        auto& Stops=*FindFProperty<FMapProperty>(AOBRailPath::StaticClass(),TEXT("StopPointSettings"))
            ->ContainerPtrToValuePtr<TMap<int32,FOBRailStopSettings>>(Rail);
        auto& FirstZone=Stops.FindOrAdd(1);
        FirstZone.bOverrideCameraView=true;FirstZone.CameraFOV=48;
        FirstZone.CameraRotationOffset=FRotator(-12,8,3);
        C->CurrentPointIndex=1;C->BeginAction();
        for (int32 I=0;I<120;++I) C->TickComponent(1.f/60,LEVELTICK_All,&C->PrimaryComponentTick);
        const FRotator FirstZoneRotation=(C->FirstPersonCameraRotation+FirstZone.CameraRotationOffset).GetNormalized();
        TestEqual(TEXT("A stop supplies its own FOV to the same camera"),Camera->FieldOfView,48.f);
        TestTrue(TEXT("A stop supplies an angle offset from the original camera"),Camera->GetRelativeRotation().Equals(FirstZoneRotation,.01));
        Camera->SetRelativeLocation(FVector(60,0,28));
        C->FocusTrainingCamera(-5,35,.1f);C->TickComponent(.1f,LEVELTICK_All,&C->PrimaryComponentTick);
        C->ClearTrainingCameraFocus();C->TickComponent(.65f,LEVELTICK_All,&C->PrimaryComponentTick);
        TestEqual(TEXT("Inspection returns to the zone FOV"),Camera->FieldOfView,48.f);
        TestTrue(TEXT("Inspection preserves the zone angle and crouched camera height"),
            Camera->GetRelativeRotation().Equals(FirstZoneRotation,.01) && Camera->GetRelativeLocation().Equals(FVector(60,0,28)));
        C->BeginTravel();
        for (int32 I=0;I<120;++I) { C->UpdateCamera(1.f/60);C->UpdateCameraFOV(1.f/60); }
        TestEqual(TEXT("Leaving a custom stop restores travel FOV"),Camera->FieldOfView,86.f);
        TestTrue(TEXT("Leaving a custom stop restores the map's travel angle"),Camera->GetRelativeRotation().Equals(
            Case.bFirstPerson ? C->FirstPersonCameraRotation : C->TravelCameraRelativeRotation,.01));
        auto& SecondZone=Stops.FindOrAdd(2);SecondZone.bOverrideCameraView=true;
        SecondZone.CameraFOV=61;SecondZone.CameraRotationOffset=FRotator(4,-6,0);
        C->CurrentPointIndex=2;C->BeginAction();
        for (int32 I=0;I<120;++I) C->TickComponent(1.f/60,LEVELTICK_All,&C->PrimaryComponentTick);
        TestEqual(TEXT("The next stop has an independent FOV"),Camera->FieldOfView,61.f);
        TestTrue(TEXT("The next stop has an independent angle"),Camera->GetRelativeRotation().Equals(
            (C->FirstPersonCameraRotation+SecondZone.CameraRotationOffset).GetNormalized(),.01));
        C->CurrentPointIndex=3;C->BeginAction();
        for (int32 I=0;I<120;++I) C->TickComponent(1.f/60,LEVELTICK_All,&C->PrimaryComponentTick);
        TestTrue(TEXT("An unconfigured stop restores original angle and default action FOV"),
            Camera->GetRelativeRotation().Equals(C->FirstPersonCameraRotation,.01) && Camera->FieldOfView==57.f);
        Rail->bPreviewEntireRoute=true;
        Rail->GetSpline()->ClearSplinePoints(false);
        Rail->GetSpline()->AddSplinePoint(FVector::ZeroVector,ESplineCoordinateSpace::World,false);
        Rail->GetSpline()->AddSplinePoint(FVector(300,0,100),ESplineCoordinateSpace::World,false);
        Rail->GetSpline()->UpdateSpline();C->StartRoutePreview();
        TestTrue(TEXT("Route preview starts with the same map-specific perspective"),
            Camera->GetRelativeLocation().Equals(Case.bFirstPerson ? C->FirstPersonCameraLocation : C->TravelCameraRelativeLocation));
        TestEqual(TEXT("Route preview retains travel FOV"),Camera->FieldOfView,86.f);
    }
    return !HasAnyErrors();
}
#endif
