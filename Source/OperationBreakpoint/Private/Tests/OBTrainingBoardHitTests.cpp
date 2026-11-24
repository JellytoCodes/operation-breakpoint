#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Training/OBTrainingTarget.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "PhysicsEngine/BodySetup.h"
#if WITH_EDITOR
#include "StaticMeshCompiler.h"
#endif

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOBTrainingBoardHitTest, "OperationBreakpoint.Training.BoardMeshHits",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOBTrainingBoardHitTest::RunTest(const FString&)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    int32 Index = 0;
    for (const TCHAR* Path : {
        TEXT("/Game/Blueprints/Training/BP_OBPopUpTrainingTarget.BP_OBPopUpTrainingTarget_C"),
        TEXT("/Game/Blueprints/Training/BP_OBRailTrainingTarget.BP_OBRailTrainingTarget_C"),
        TEXT("/Game/Blueprints/Training/BP_TargetActor.BP_TargetActor_C")})
    {
        UClass* Class = LoadClass<AOBTrainingTarget>(nullptr, Path);
        TestNotNull(TEXT("Target Blueprint loads"), Class);
        if (!Class) return false;
#if WITH_EDITOR
        // Cold-loaded meshes defer component physics until editor compilation completes.
        FStaticMeshCompilingManager::Get().FinishAllCompilation();
#endif
        for (float Yaw : {0.f, 180.f})
        {
            auto* Target = World->SpawnActor<AOBTrainingTarget>(Class, FVector(++Index * 1000, 3000, 0), FRotator(0, Yaw, 0));
            Target->SetTargetEnabled(true, true);
            TArray<USceneComponent*> Parts;
            Target->GetComponents(Parts);
            TestEqual(TEXT("Saved target contains only base, pivot, arm and board"), Parts.Num(), 4);
            const FVector Normal = Target->GetScoringPlaneNormal();
            auto Trace = [&](const FVector& Aim) {
                FHitResult Hit;
                World->LineTraceSingleByChannel(Hit, Aim + Normal * 300, Aim - Normal * 300,
                    ECC_Visibility, FCollisionQueryParams(NAME_None, true));
                return Hit;
            };
            FHitResult CenterHit = Trace(Target->BoardMesh->Bounds.Origin);
            if (!Target->IsScoringHit(CenterHit))
            {
                FHitResult Reverse;
                const FVector Aim = Target->BoardMesh->Bounds.Origin;
                World->LineTraceSingleByChannel(Reverse, Aim - Normal * 300, Aim + Normal * 300,
                    ECC_Visibility, FCollisionQueryParams(NAME_None, true));
                FHitResult Simple;
                World->LineTraceSingleByChannel(Simple, Aim + Normal * 300, Aim - Normal * 300,
                    ECC_Visibility, FCollisionQueryParams(NAME_None, false));
                UStaticMesh* Mesh = Target->BoardMesh->GetStaticMesh();
                AddInfo(FString::Printf(TEXT("Board diagnostic: %s yaw=%.0f normal=%s reverse=%s simple=%s doublesided=%d complexity=%d triangledata=%d collision=%d actorcollision=%d physics=%d"), Path, Yaw,
                    *Normal.ToString(), *GetNameSafe(Reverse.GetComponent()), *GetNameSafe(Simple.GetComponent()),
                    Mesh->GetBodySetup()->bDoubleSidedGeometry, int32(Mesh->GetBodySetup()->CollisionTraceFlag.GetValue()),
                    Mesh->ContainsPhysicsTriMeshData(true), int32(Target->BoardMesh->GetCollisionEnabled()),
                    Target->GetActorEnableCollision(), Target->BoardMesh->IsPhysicsStateCreated()));
            }
            TestTrue(TEXT("Actual mesh ray counts a hit at either actor yaw"), Target->IsScoringHit(CenterHit));
            TestTrue(TEXT("Scoring ray hits BoardMesh directly"), CenterHit.GetComponent() == Target->BoardMesh);
            CenterHit.Component = Target->ArmMesh;
            TestFalse(TEXT("A support-component hit cannot score"), Target->IsScoringHit(CenterHit));
            const FBox Bounds = Target->BoardMesh->GetStaticMesh()->GetBoundingBox();
            FVector EmptyCorner = Bounds.GetCenter();
            EmptyCorner.X = Bounds.Min.X + Bounds.GetSize().X * 0.05;
            EmptyCorner.Z = Bounds.Max.Z - Bounds.GetSize().Z * 0.02;
            const bool bStaticBoard = Target->BoardMesh->GetStaticMesh()->GetName() == TEXT("Target_board2");
            const bool bCornerHit = Target->IsScoringHit(Trace(Target->BoardMesh->GetComponentTransform().TransformPosition(EmptyCorner)));
            if (bStaticBoard)
            {
                const FVector Offset = Bounds.GetCenter() + FVector(Bounds.GetSize().X * 0.2, 0, 0);
                TestTrue(TEXT("Zeroing board accepts an actual off-center mesh hit"),
                    Target->IsScoringHit(Trace(Target->BoardMesh->GetComponentTransform().TransformPosition(Offset))));
                TestTrue(TEXT("Zeroing board has no folding motion"), Target->FoldRotation.IsNearlyZero() && Target->RaiseSeconds == 0 && Target->LowerSeconds == 0);
            }
            else TestFalse(TEXT("Empty space beside the head cannot score through an old box"), bCornerHit);
            const FTransform BeforeDisable = Target->BoardMesh->GetComponentTransform();
            Target->SetTargetEnabled(false, true);
            if (bStaticBoard) TestTrue(TEXT("Deactivating zeroing scoring preserves its board pose"), Target->BoardMesh->GetComponentTransform().Equals(BeforeDisable));
            TestFalse(TEXT("Disabled board does not score"), Target->IsScoringHit(Trace(Target->BoardMesh->Bounds.Origin)));
        }
    }
    return true;
}
#endif
