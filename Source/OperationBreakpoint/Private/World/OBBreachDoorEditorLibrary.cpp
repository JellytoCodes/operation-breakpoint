#include "World/OBBreachDoorEditorLibrary.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/StaticMesh.h"
#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollection/GeometryCollectionClusteringUtility.h"
#include "GeometryCollection/GeometryCollectionAlgo.h"
#include "GeometryCollection/Facades/CollectionAnchoringFacade.h"
#include "GeometryCollection/GeometryCollectionEngineConversion.h"
#include "GeometryCollection/GeometryCollectionObject.h"
#include "GeometryCollection/GeometryCollectionSimulationTypes.h"
#include "Misc/PackageName.h"
#include "PlanarCut.h"
#include "UObject/Package.h"
#endif

UGeometryCollection* UOBBreachDoorEditorLibrary::CreateFracturedWall(UStaticMesh* SourceMesh,const FString& PackagePath,
    const TArray<UMaterialInterface*>& OverrideMaterials,FVector OpeningCenter,FVector OpeningExtent,float DamageMargin)
{
#if WITH_EDITOR
    auto* Asset=CreateFracturedDoor(SourceMesh,PackagePath,OverrideMaterials,6,6,261102);
    if (!Asset) return nullptr;
    const auto Collection=Asset->GetGeometryCollection();
    TArray<FTransform3f> Global;
    GeometryCollectionAlgo::GlobalMatrices(Collection->Transform,Collection->Parent,Global);
    Chaos::Facades::FCollectionAnchoringFacade Anchors(*Collection);
    Anchors.AddAnchoredAttribute();
    const FBox DamageBox(OpeningCenter-OpeningExtent-FVector(DamageMargin),OpeningCenter+OpeningExtent+FVector(DamageMargin));
    int32 AnchoredCount=0,MovingCount=0;
    for (int32 Index=0;Index<Collection->SimulationType.Num();++Index)
    {
        if (!Collection->IsRigid(Index)) continue;
        const int32 Geometry=Collection->TransformToGeometryIndex[Index];
        if (!Collection->BoundingBox.IsValidIndex(Geometry)) continue;
        const FVector Center(Global[Index].TransformPosition(FVector3f(Collection->BoundingBox[Geometry].GetCenter())));
        const bool Fixed=!DamageBox.IsInsideOrOn(Center);
        Anchors.SetAnchored(Index,Fixed);
        if (Fixed) ++AnchoredCount;else ++MovingCount;
    }
    if (!AnchoredCount || MovingCount<2) return nullptr;
    Asset->Mass=90.f;
    if (!ConfigureWallAnchors(Asset)) return nullptr;
    return Asset;
#else
    return nullptr;
#endif
}

bool UOBBreachDoorEditorLibrary::ConfigureWallAnchors(UGeometryCollection* Asset)
{
#if WITH_EDITOR
    if (!Asset || !Asset->GetGeometryCollection()) return false;
    const auto Collection=Asset->GetGeometryCollection();
    Chaos::Facades::FCollectionAnchoringFacade Anchors(*Collection);
    if (!Anchors.IsValid()) return false;
    int32 Fixed=0,Moving=0;
    for (int32 Index=0;Index<Collection->SimulationType.Num();++Index)
    {
        if (!Collection->IsRigid(Index)) continue;
        const bool bFixed=Anchors.IsAnchored(Index);
        Anchors.SetInitialDynamicState(Index,bFixed ? Chaos::EObjectStateType::Kinematic : Chaos::EObjectStateType::Dynamic);
        if (bFixed) ++Fixed;else ++Moving;
    }
    if (!Fixed || Moving<2) return false;
    Anchors.SetInitialDynamicState(Asset->GetRootIndex(),Chaos::EObjectStateType::Kinematic);
    Asset->InvalidateCollection();Asset->CreateSimulationData();Asset->RebuildRenderData();Asset->MarkPackageDirty();
    return true;
#else
    return false;
#endif
}

UGeometryCollection* UOBBreachDoorEditorLibrary::CreateFracturedDoor(UStaticMesh* SourceMesh, const FString& PackagePath,
    const TArray<UMaterialInterface*>& OverrideMaterials, int32 WidthCuts, int32 HeightCuts, int32 Seed)
{
#if WITH_EDITOR
    if (!SourceMesh || !FPackageName::IsValidLongPackageName(PackagePath) || FPackageName::DoesPackageExist(PackagePath)
        || FindObject<UPackage>(nullptr, *PackagePath)) return nullptr;
    const FBox Bounds = SourceMesh->GetBoundingBox();
    if (!Bounds.IsValid) return nullptr;
    TArray<UMaterialInterface*> Materials;
    for (int32 Index = 0; Index < SourceMesh->GetStaticMaterials().Num(); ++Index)
        Materials.Add(OverrideMaterials.IsValidIndex(Index) && OverrideMaterials[Index] ? OverrideMaterials[Index] : SourceMesh->GetMaterial(Index));
    auto* Package = CreatePackage(*PackagePath);
    auto* Asset = NewObject<UGeometryCollection>(Package, *FPackageName::GetLongPackageAssetName(PackagePath), RF_Public | RF_Standalone | RF_Transactional);
    if (!FGeometryCollectionEngineConversion::AppendStaticMesh(SourceMesh, Materials, FTransform::Identity, Asset, true, false, false)) return nullptr;
    const auto Collection = Asset->GetGeometryCollection();
    if (!Collection) return nullptr;

    TArray<int32> Selection;
    for (int32 Index = 0; Index < Collection->SimulationType.Num(); ++Index)
        if (Collection->IsRigid(Index)) Selection.Add(Index);
    const FVector Size = Bounds.GetSize();
    int32 HeightAxis = Size.X >= Size.Y && Size.X >= Size.Z ? 0 : (Size.Y >= Size.Z ? 1 : 2);
    int32 WidthAxis = HeightAxis == 0 ? (Size.Y >= Size.Z ? 1 : 2) : (HeightAxis == 1 ? (Size.X >= Size.Z ? 0 : 2) : (Size.X >= Size.Y ? 0 : 1));
    FRandomStream Random(Seed);
    TArray<FPlane> Planes;
    auto AddCuts = [&](int32 Axis, int32 OtherAxis, int32 Count) {
        Count = FMath::Clamp(Count, 1, 8);
        for (int32 Index = 1; Index <= Count; ++Index)
        {
            FVector Point = Bounds.GetCenter();
            Point[Axis] = FMath::Lerp(Bounds.Min[Axis], Bounds.Max[Axis], double(Index) / double(Count + 1));
            FVector Normal = FVector::ZeroVector; Normal[Axis] = 1.; Normal[OtherAxis] = Random.FRandRange(-.06f, .06f);
            Planes.Emplace(Point, Normal.GetSafeNormal());
        }
    };
    AddCuts(WidthAxis, HeightAxis, WidthCuts);
    AddCuts(HeightAxis, WidthAxis, HeightCuts);
    FInternalSurfaceMaterials Internal;
    const int32 FirstNewGeometry = CutMultipleWithMultiplePlanes(Planes, Internal, *Collection, Selection, 0., 10., Seed);
    if (FirstNewGeometry == INDEX_NONE) return nullptr;

    FGeometryCollectionClusteringUtility::ClusterAllBonesUnderNewRoot(Collection.Get(), TEXT("BreachDoorRoot"), false);
    FGeometryCollectionClusteringUtility::UpdateHierarchyLevelOfChildren(Collection.Get(), INDEX_NONE);
    TArray<int32> Roots;
    FGeometryCollectionClusteringUtility::GetRootBones(Collection.Get(), Roots);
    if (Roots.Num() != 1) return nullptr;
    TArray<int32> Leaves;
    FGeometryCollectionClusteringUtility::GetLeafBones(Collection.Get(), Roots[0], true, Leaves);
    FGeometryCollectionClusteringUtility::ClusterBonesUnderExistingNode(Collection.Get(), Roots[0], Leaves);
    FGeometryCollectionClusteringUtility::RemoveDanglingClusters(Collection.Get());
    int32 RigidCount = 0;
    for (int32 Index = 0; Index < Collection->SimulationType.Num(); ++Index)
        if (Collection->IsRigid(Index)) ++RigidCount;
    if (RigidCount < 2) return nullptr;

    Collection->ReindexMaterials();
    Asset->InitializeMaterials();
    Asset->EnableClustering = true;
    Asset->DamageThreshold = {1.f};
    Asset->bMassAsDensity = false;
    Asset->Mass = 35.f;
    Asset->MinimumMassClamp = .01f;
    auto& CollisionData = Asset->GetDefaultSizeSpecificData();
    CollisionData.CollisionShapes.SetNum(1);
    CollisionData.CollisionShapes[0].CollisionType = ECollisionTypeEnum::Chaos_Volumetric;
    CollisionData.CollisionShapes[0].ImplicitType = EImplicitTypeEnum::Chaos_Implicit_Convex;
    CollisionData.CollisionShapes[0].CollisionObjectReductionPercentage = 0.f;
    CollisionData.CollisionShapes[0].CollisionMarginFraction = .01f;
    Asset->InvalidateCollection();
    Asset->UpdateGeometryDependentProperties();
    Asset->CreateSimulationData();
    Asset->RebuildRenderData();
    FAssetRegistryModule::AssetCreated(Asset);
    Asset->MarkPackageDirty();
    return Asset;
#else
    return nullptr;
#endif
}
