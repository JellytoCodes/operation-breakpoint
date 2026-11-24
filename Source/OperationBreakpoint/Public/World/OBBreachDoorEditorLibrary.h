#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "OBBreachDoorEditorLibrary.generated.h"

class UStaticMesh;
class UMaterialInterface;
class UGeometryCollection;

UCLASS()
class OPERATIONBREAKPOINT_API UOBBreachDoorEditorLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    /** Editor only: converts the original leaf, then cuts real geometry and builds Chaos data. Does not overwrite an asset. */
    UFUNCTION(BlueprintCallable, Category="OB|Breach|Editor")
    static UGeometryCollection* CreateFracturedDoor(UStaticMesh* SourceMesh, const FString& PackagePath,
        const TArray<UMaterialInterface*>& OverrideMaterials, int32 WidthCuts = 2, int32 HeightCuts = 4, int32 Seed = 260630);
    UFUNCTION(BlueprintCallable, Category="OB|Breach|Editor")
    static UGeometryCollection* CreateFracturedWall(UStaticMesh* SourceMesh,const FString& PackagePath,
        const TArray<UMaterialInterface*>& OverrideMaterials,FVector OpeningCenter,FVector OpeningExtent,float DamageMargin=35.f);
    UFUNCTION(BlueprintCallable, Category="OB|Breach|Editor")
    static bool ConfigureWallAnchors(UGeometryCollection* Asset);
};
