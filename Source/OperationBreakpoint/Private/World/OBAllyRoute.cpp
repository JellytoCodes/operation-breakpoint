#include "World/OBAllyRoute.h"
#include "Components/SplineComponent.h"
AOBAllyRoute::AOBAllyRoute()
{
    PrimaryActorTick.bCanEverTick=false;
    Spline=CreateDefaultSubobject<USplineComponent>(TEXT("Spline")); SetRootComponent(Spline);
    Spline->ClearSplinePoints(false);
    Spline->AddSplinePoint(FVector::ZeroVector,ESplineCoordinateSpace::Local,false);
    Spline->AddSplinePoint(FVector(500,0,0),ESplineCoordinateSpace::Local,true);
    Spline->SetClosedLoop(false);
}
