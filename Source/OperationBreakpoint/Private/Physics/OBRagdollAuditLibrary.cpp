#include "Physics/OBRagdollAuditLibrary.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
#include "PhysicsEngine/PhysicsConstraintTemplate.h"
#include "PhysicsEngine/BodyInstance.h"
#include "Engine/SkeletalMesh.h"
#include "Dom/JsonObject.h"
#include "JsonObjectConverter.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

FString UOBRagdollAuditLibrary::DescribePhysicsAsset(UPhysicsAsset* Asset, USkeletalMesh* Mesh)
{
    if (!Asset || !Mesh) return TEXT("{}");
    auto Root=MakeShared<FJsonObject>();
    Root->SetStringField(TEXT("asset"),Asset->GetPathName());
    Root->SetStringField(TEXT("mesh"),Mesh->GetPathName());
    Root->SetNumberField(TEXT("body_count"),Asset->SkeletalBodySetups.Num());
    Root->SetNumberField(TEXT("constraint_count"),Asset->ConstraintSetup.Num());
    Root->SetNumberField(TEXT("collision_disable_table_count"),Asset->CollisionDisableTable.Num());
    Root->SetStringField(TEXT("reference_overlap_note"),TEXT("Reference-pose AABB intersections are candidates, not exact shape overlap/contact tests."));
    const auto& Skeleton=Mesh->GetRefSkeleton();
    TArray<FTransform> ReferenceCS=Skeleton.GetRefBonePose();
    for (int32 Index=0;Index<ReferenceCS.Num();++Index)
        if (Skeleton.GetParentIndex(Index)!=INDEX_NONE) ReferenceCS[Index]*=ReferenceCS[Skeleton.GetParentIndex(Index)];
    TArray<TSharedPtr<FJsonValue>> Bodies,Constraints,Pairs;
    TArray<FBox> Boxes;
    for (int32 Index=0;Index<Asset->SkeletalBodySetups.Num();++Index)
    {
        const USkeletalBodySetup* Setup=Asset->SkeletalBodySetups[Index];
        auto Entry=MakeShared<FJsonObject>();
        Entry->SetNumberField(TEXT("index"),Index);
        if (!Setup) { Boxes.Add(FBox(ForceInit));Bodies.Add(MakeShared<FJsonValueObject>(Entry));continue; }
        Entry->SetStringField(TEXT("bone"),Setup->BoneName.ToString());
        Entry->SetNumberField(TEXT("shape_count"),Setup->AggGeom.GetElementCount());
        Entry->SetObjectField(TEXT("body_defaults"),FJsonObjectConverter::UStructToJsonObject(Setup->DefaultInstance));
        Entry->SetObjectField(TEXT("geometry"),FJsonObjectConverter::UStructToJsonObject(Setup->AggGeom));
        Entry->SetNumberField(TEXT("physics_type"),static_cast<int32>(Setup->PhysicsType));
        const int32 Bone=Skeleton.FindBoneIndex(Setup->BoneName);
        const FBox Bounds=Bone!=INDEX_NONE ? Setup->AggGeom.CalcAABB(ReferenceCS[Bone]) : FBox(ForceInit);
        Boxes.Add(Bounds);
        Entry->SetStringField(TEXT("reference_aabb_min"),Bounds.IsValid ? Bounds.Min.ToString() : TEXT("invalid"));
        Entry->SetStringField(TEXT("reference_aabb_max"),Bounds.IsValid ? Bounds.Max.ToString() : TEXT("invalid"));
        Bodies.Add(MakeShared<FJsonValueObject>(Entry));
    }
    for (const UPhysicsConstraintTemplate* Setup : Asset->ConstraintSetup)
    {
        if (!Setup) continue;
        auto Entry=MakeShared<FJsonObject>();
        const auto& Joint=Setup->DefaultInstance;
        Entry->SetStringField(TEXT("bone1"),Joint.ConstraintBone1.ToString());
        Entry->SetStringField(TEXT("bone2"),Joint.ConstraintBone2.ToString());
        Entry->SetObjectField(TEXT("profile"),FJsonObjectConverter::UStructToJsonObject(Joint.ProfileInstance));
        Entry->SetObjectField(TEXT("joint"),FJsonObjectConverter::UStructToJsonObject(Joint));
        Constraints.Add(MakeShared<FJsonValueObject>(Entry));
    }
    for (int32 A=0;A<Boxes.Num();++A)
        for (int32 B=A+1;B<Boxes.Num();++B)
        {
            auto Entry=MakeShared<FJsonObject>();
            Entry->SetNumberField(TEXT("a"),A);Entry->SetNumberField(TEXT("b"),B);
            Entry->SetBoolField(TEXT("enabled"),Asset->IsCollisionEnabled(A,B));
            Entry->SetBoolField(TEXT("reference_aabb_intersection"),Boxes[A].IsValid && Boxes[B].IsValid && Boxes[A].Intersect(Boxes[B]));
            Pairs.Add(MakeShared<FJsonValueObject>(Entry));
        }
    Root->SetArrayField(TEXT("bodies"),Bodies);
    Root->SetArrayField(TEXT("constraints"),Constraints);
    Root->SetArrayField(TEXT("body_pairs"),Pairs);
    // This asset setting is for RBAN; the world ragdoll uses body/world solver settings instead.
    Root->SetObjectField(TEXT("rban_solver_settings"),FJsonObjectConverter::UStructToJsonObject(Asset->SolverSettings));
    FString Json;FJsonSerializer::Serialize(Root,TJsonWriterFactory<>::Create(&Json));return Json;
}
