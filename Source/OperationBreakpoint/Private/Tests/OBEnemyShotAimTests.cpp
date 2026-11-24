#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Character/OBEnemyCharacter.h"
#include "Character/OBPlayerCharacter.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOBEnemyShotAimTest,
    "OperationBreakpoint.Combat.EnemyShotAim",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOBEnemyShotAimTest::RunTest(const FString& Parameters)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT
    {
        World->DestroyWorld(false);
        GEngine->DestroyWorldContext(World);
    };
    AOBEnemyCharacter* Enemy = World->SpawnActor<AOBEnemyCharacter>();
    AOBPlayerCharacter* Player = World->SpawnActor<AOBPlayerCharacter>(FVector(1000, 500, 200), FRotator::ZeroRotator);
    if (!TestNotNull(TEXT("Enemy"), Enemy) || !TestNotNull(TEXT("Player"), Player)) return false;
    TestEqual(TEXT("Default integer minimum aim chance is two percent"), Enemy->DirectAimMinPercent, 2);
    TestEqual(TEXT("Default integer maximum aim chance is two percent"), Enemy->DirectAimMaxPercent, 2);
    TestNotNull(TEXT("Minimum chance is an integer property"),FindFProperty<FIntProperty>(AOBEnemyCharacter::StaticClass(),TEXT("DirectAimMinPercent")));
    TestNotNull(TEXT("Maximum chance is an integer property"),FindFProperty<FIntProperty>(AOBEnemyCharacter::StaticClass(),TEXT("DirectAimMaxPercent")));
    TestEqual(TEXT("Six configured surrounding offsets"), Enemy->SurroundingAimOffsets.Num(), 6);
    const FVector OriginalAim = Player->GetActorLocation() + FVector::UpVector * Enemy->AimHeightOffset;
    Enemy->DirectAimMinPercent = Enemy->DirectAimMaxPercent = 100;
    TestTrue(TEXT("100 percent retains exact old aim even at upper random boundary"),
        Enemy->SelectShotAimLocation(*Player, 100, 0).Equals(OriginalAim));
    Enemy->DirectAimMinPercent = Enemy->DirectAimMaxPercent = 0;
    for (int32 Index = 0; Index < 6; ++Index)
    {
        const FVector Aim = Enemy->SelectShotAimLocation(*Player, 1, Index);
        TestTrue(TEXT("0 percent selects each requested surrounding point"),
            Aim.Equals(OriginalAim + Enemy->SurroundingAimOffsets[Index]));
        TestFalse(TEXT("Surrounding shot is not marked direct"), Enemy->bLastShotAimedAtPlayer);
        TestTrue(TEXT("Default surrounding target is laterally outside player"), FMath::Abs(Aim.Y - OriginalAim.Y) >= 150.0f);
    }
    TestEqual(TEXT("Six world points cached"), Enemy->SurroundingAimPoints.Num(), 6);
    for (int32 Percent : {2,25})
    {
        Enemy->DirectAimMinPercent=Enemy->DirectAimMaxPercent=Percent;
        TestTrue(TEXT("Integer threshold includes its final successful roll"),Enemy->SelectShotAimLocation(*Player,Percent,0).Equals(OriginalAim));
        TestFalse(TEXT("The next integer roll misses"),Enemy->SelectShotAimLocation(*Player,Percent+1,0).Equals(OriginalAim));
        int32 DirectSamples=0,SurroundingSamples=0;
        for (int32 Roll=1;Roll<=100;++Roll)
        {
            const FVector Aim=Enemy->SelectShotAimLocation(*Player,Roll,(Roll-1)%6);
            if (Aim.Equals(OriginalAim)) ++DirectSamples;
            else if (Aim.Equals(OriginalAim+Enemy->SurroundingAimOffsets[(Roll-1)%6])) ++SurroundingSamples;
            TestEqual(TEXT("Each selected chance retains its exact integer percent"),Enemy->GetLastShotAimChancePercent(),Percent);
        }
        TestEqual(TEXT("Every configured integer percent has exactly that many successes out of rolls 1..100"),DirectSamples,Percent);
        TestEqual(TEXT("All other rolls use actual miss coordinates"),SurroundingSamples,100-Percent);
    }
    Enemy->ConfigureEnemyArchetype(EOBEnemyArchetype::Rifleman);
    TSet<int32> ObservedChances;
    for (int32 Index=0;Index<2048;++Index)
    {
        Enemy->SelectShotAimLocation(*Player,100,0);
        const int32 Percent=Enemy->GetLastShotAimChancePercent();
        TestEqual(TEXT("Every fresh rifle shot uses exactly two percent"),Percent,2);
        ObservedChances.Add(Percent);
    }
    TestEqual(TEXT("The live rifle chance never varies by shot"),ObservedChances.Num(),1);

    Player->SetActorLocation(FVector(2000, -500, 350));
    Player->SetActorRotation(FRotator(0, 90, 0));
    Enemy->DirectAimMinPercent = Enemy->DirectAimMaxPercent = 0;
    const FVector MovedAim = Player->GetActorLocation() + FVector::UpVector * Enemy->AimHeightOffset;
    TestTrue(TEXT("Surrounding point follows player movement and yaw"),
        Enemy->SelectShotAimLocation(*Player, 1, 0).Equals(
            MovedAim + FRotator(0, 90, 0).RotateVector(Enemy->SurroundingAimOffsets[0])));
    TestTrue(TEXT("World array refreshes instead of accumulating"), Enemy->SurroundingAimPoints.Num() == 6);
    Enemy->SurroundingAimOffsets.Empty();
    TestFalse(TEXT("Empty miss configuration does not turn a zero-percent shot into a guaranteed direct aim"),Enemy->SelectShotAimLocation(*Player,1,0).Equals(MovedAim));
    return true;
}

#endif
