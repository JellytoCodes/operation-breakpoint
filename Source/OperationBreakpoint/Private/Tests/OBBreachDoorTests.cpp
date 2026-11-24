#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Misc/PackageName.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollection/GeometryCollectionObject.h"
#include "GeometryCollection/GeometryCollectionComponent.h"
#include "GeometryCollection/Facades/CollectionAnchoringFacade.h"
#include "World/OBBreachDoor.h"
#include "World/OBBreachDoorEditorLibrary.h"
#include "UObject/Package.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOBBreachDoorTest, "OperationBreakpoint.Stage.BreachDoor",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOBBreachDoorTest::RunTest(const FString&)
{
    auto* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    if (!World->GetPhysicsScene()) World->CreatePhysicsScene();
    ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    auto* Door = World->SpawnActor<AOBBreachDoor>();
    auto* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    if (!TestNotNull(TEXT("A source mesh loads"), Cube)) return false;
    TestFalse(TEXT("Missing configured geometry cannot report a successful breach"), Door->TryBreach());
    Door->GetIntactDoorMesh()->SetStaticMesh(Cube);
    TestFalse(TEXT("A door without fractured rest geometry cannot begin"), Door->TryBreach());
    TestTrue(TEXT("An invalid request leaves the intact collision in place"), Door->GetIntactDoorMesh()->GetCollisionEnabled() == ECollisionEnabled::QueryAndPhysics);
    TestFalse(TEXT("An invalid request cannot report completion"), Door->IsBreached());

    // /Temp is a read-only mount in UE 5.8; the authoring helper correctly rejects it.
    // Use a writable package name, while keeping this test asset entirely in memory.
    const FString PackagePath = TEXT("/Game/Automation/OBBreachDoorTest_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    if (!TestTrue(TEXT("The generation fixture uses a writable authoring package"), FPackageName::IsValidLongPackageName(PackagePath))) return false;
    const FBox SourceBounds = Cube->GetBoundingBox();
    const int32 SourceMaterials = Cube->GetStaticMaterials().Num();
    const TArray<UMaterialInterface*> Materials;
    auto* Asset = UOBBreachDoorEditorLibrary::CreateFracturedDoor(Cube, PackagePath, Materials, 1, 2, 42);
    if (!TestNotNull(TEXT("Planar cuts generate a genuine Geometry Collection"), Asset)) return false;
    ON_SCOPE_EXIT {
        FAssetRegistryModule::AssetDeleted(Asset);
        Asset->ClearFlags(RF_Public | RF_Standalone);
        Asset->SetFlags(RF_Transient);
        Asset->GetPackage()->SetDirtyFlag(false);
    };
    const auto Collection = Asset->GetGeometryCollection();
    if (!TestTrue(TEXT("The generated asset contains collection data"), Collection.IsValid())) return false;
    int32 Rigids = 0, Roots = 0;
    for (int32 Index = 0; Index < Collection->SimulationType.Num(); ++Index)
    {
        if (Collection->IsRigid(Index)) ++Rigids;
        if (Collection->Parent[Index] == INDEX_NONE) ++Roots;
    }
    TestTrue(TEXT("The original mesh was cut into multiple rigid pieces"), Rigids >= 6);
    TestEqual(TEXT("The rest asset has one cluster root"), Roots, 1);
    TestTrue(TEXT("The cached root represents a cluster"), Collection->IsClustered(Asset->GetRootIndex()));
    TestTrue(TEXT("The fracture retains UV layers"), Collection->NumUVLayers() > 0);
    bool bFiniteGeometry = true, bFiniteUVs = true, bValidMaterials = true;
    for (int32 Index = 0; Index < Collection->Vertex.Num(); ++Index)
    {
        bFiniteGeometry &= !Collection->Vertex[Index].ContainsNaN();
        for (int32 Layer = 0; Layer < Collection->NumUVLayers(); ++Layer)
            bFiniteUVs &= !Collection->GetUV(Index, Layer).ContainsNaN();
    }
    for (int32 Material : Collection->MaterialID) bValidMaterials &= Asset->Materials.IsValidIndex(Material);
    TestTrue(TEXT("Fragment vertices and UVs remain finite"), bFiniteGeometry && bFiniteUVs);
    TestTrue(TEXT("Every fragment face retains a valid material slot"), bValidMaterials);
    TestTrue(TEXT("Generating fracture data preserves the source mesh bounds"), Cube->GetBoundingBox().Equals(SourceBounds));
    TestEqual(TEXT("Generating fracture data preserves source material slots"), Cube->GetStaticMaterials().Num(), SourceMaterials);
    TestNull(TEXT("The editor helper will not overwrite a loaded asset package"),
        UOBBreachDoorEditorLibrary::CreateFracturedDoor(Cube, PackagePath, Materials, 1, 2, 42));

    const FTransform LeafTransform(FRotator(0, 35, 0), FVector(10, 20, 30), FVector(.8f, .9f, 1.2f));
    Door->GetIntactDoorMesh()->SetRelativeTransform(LeafTransform);
    Door->FracturedDoorAsset = Asset;
    TestTrue(TEXT("A configured door accepts the breach once"), Door->TryBreach(FVector::ForwardVector));
    TestTrue(TEXT("The fractured leaf inherits the intact leaf's full local transform"), Door->GetFracturedDoor()->GetRelativeTransform().Equals(LeafTransform));
    TestTrue(TEXT("Accepted breach is in progress until the actual solver reports a broken root"), Door->IsBreachInProgress() && !Door->IsBreached());
    TestFalse(TEXT("A duplicate gesture cannot start another destruction"), Door->TryBreach());
    TestTrue(TEXT("The doorway's intact leaf no longer blocks movement or shots"), Door->GetIntactDoorMesh()->GetCollisionEnabled() == ECollisionEnabled::NoCollision);
    auto* Debris = Door->GetFracturedDoor();
    TestTrue(TEXT("Debris is the generated collection rather than independent replacement primitives"), Debris->GetRestCollection() == Asset);
    TestTrue(TEXT("Debris retains world-static and world-dynamic physics"),
        Debris->GetCollisionResponseToChannel(ECC_WorldStatic) == ECR_Block && Debris->GetCollisionResponseToChannel(ECC_WorldDynamic) == ECR_Block);
    TestTrue(TEXT("Debris cannot block player, camera or bullet visibility traces"),
        Debris->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Ignore && Debris->GetCollisionResponseToChannel(ECC_Camera) == ECR_Ignore
        && Debris->GetCollisionResponseToChannel(ECC_Visibility) == ECR_Ignore);
    TestTrue(TEXT("The preserved frame still retains its authored blocking collision"), Door->GetFrameMesh()->GetCollisionEnabled() == ECollisionEnabled::QueryAndPhysics);
    auto* Wall=UOBBreachDoorEditorLibrary::CreateFracturedWall(Cube,PackagePath+TEXT("Wall"),Materials,FVector::ZeroVector,FVector(20,20,30),5.f);
    if (TestNotNull(TEXT("A wall can fracture around a limited opening"),Wall))
    {
        const auto Rest=Wall->GetGeometryCollection();
        const Chaos::Facades::FCollectionAnchoringFacade Anchors(static_cast<const FManagedArrayCollection&>(*Rest));
        int32 Fixed=0,Moving=0;
        for (int32 I=0;I<Rest->SimulationType.Num();++I)
            if (Rest->IsRigid(I)) { if (Anchors.IsAnchored(I)) ++Fixed;else ++Moving; }
        TestTrue(TEXT("The wall retains outer anchors and at least two detachable fragments"),Fixed>0 && Moving>=2);
        FAssetRegistryModule::AssetDeleted(Wall);Wall->ClearFlags(RF_Public|RF_Standalone);Wall->SetFlags(RF_Transient);Wall->GetPackage()->SetDirtyFlag(false);
    }
    return !HasAnyErrors();
}
#endif
