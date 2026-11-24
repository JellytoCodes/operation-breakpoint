#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "OBInfiltrationEditorLibrary.generated.h"
class UAnimBlueprint;
class UAnimSequence;
class ANavMeshBoundsVolume;
class USoundCue;
class USoundWave;
UCLASS()
class OPERATIONBREAKPOINT_API UOBInfiltrationEditorLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category="OB|Infiltration|Editor")
    static bool ConfigureEnemyShotCue(USoundCue* Cue,USoundWave* First,USoundWave* Second);
    UFUNCTION(BlueprintCallable, Category="OB|Infiltration|Editor")
    static void PrepareForAuthoring(UWorld* World);
    UFUNCTION(BlueprintPure, Category="OB|Infiltration|Editor")
    static FVector GetHitImpactPoint(const FHitResult& Hit);
    UFUNCTION(BlueprintCallable, Category="OB|Infiltration|Editor")
    static bool ConfigureAllyAnimation(UAnimBlueprint* Blueprint,UAnimSequence* Idle,UAnimSequence* Jog);
    UFUNCTION(BlueprintCallable, Category="OB|Infiltration|Editor")
    static ANavMeshBoundsVolume* CreateNavigationBounds(UWorld* World,FVector Center,FVector Extent,bool bRebuildNavigation=true);
    UFUNCTION(BlueprintCallable, Category="OB|Infiltration|Editor")
    static bool RebuildNavigation(UWorld* World);
    UFUNCTION(BlueprintCallable, Category="OB|Infiltration|Editor")
    static bool TickNavigationBuild(UWorld* World);
    UFUNCTION(BlueprintCallable, Category="OB|Infiltration|Editor")
    static FString DescribeEditableProperties(UObject* Object);
    UFUNCTION(BlueprintCallable, Category="OB|Infiltration|Editor")
    static TArray<FVector> FindReachableSpawnLocations(UWorld* World,FVector PreferredCenter,FVector Goal,float CapsuleRadius,float CapsuleHalfHeight,float SearchRadius=800.f);
    UFUNCTION(BlueprintCallable, Category="OB|Infiltration|Editor")
    static FString DescribeEncounterNavigation(UWorld* World);
    UFUNCTION(BlueprintCallable, Category="OB|Infiltration|Editor")
    static FString DescribeRailNavigationGaps(UWorld* World);
    UFUNCTION(BlueprintCallable, Category="OB|Infiltration|Editor")
    static bool SetNavigationAuthoringLock(UWorld* World,bool bLocked);
};
