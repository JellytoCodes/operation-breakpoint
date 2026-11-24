#include "World/OBRailPath.h"

#include "Components/SplineComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogOBRailPath, Log, All);

AOBRailPath::AOBRailPath()
{
    PrimaryActorTick.bCanEverTick = false;

    RailSpline = CreateDefaultSubobject<USplineComponent>(TEXT("RailSpline"));
    SetRootComponent(RailSpline);

    StopPointSettings.Add(1);
}

int32 AOBRailPath::GetNextStopPointIndex(int32 AfterPointIndex) const
{
    const int32 LastPointIndex = RailSpline->GetNumberOfSplinePoints() - 1;
    if (AfterPointIndex >= LastPointIndex) return INDEX_NONE;

    int32 NextPointIndex = LastPointIndex;

    for (const TPair<int32, FOBRailStopSettings>& Entry : StopPointSettings)
    {
        const int32 PointIndex = Entry.Key;
        if (PointIndex <= 0 || PointIndex > LastPointIndex)
        {
            UE_LOG(LogOBRailPath, Warning,
                TEXT("Invalid stop point ignored | Rail=%s | Point=%d | ValidRange=1..%d"),
                *GetName(), PointIndex, LastPointIndex);
            continue;
        }
        if (PointIndex > AfterPointIndex) NextPointIndex = FMath::Min(NextPointIndex, PointIndex);
    }

    return NextPointIndex;
}
