#include "Stage/OBStageScore.h"

double FOBStageScoring::HitWeight(FName Zone)
{
    if (Zone==TEXT("HitZone.Head")) return 1.;
    if (Zone==TEXT("HitZone.Body")) return .75;
    if (Zone==TEXT("HitZone.Arm") || Zone==TEXT("HitZone.Leg")) return .35;
    return 0.;
}
void FOBStageScoring::Evaluate(FOBStageZoneResult& Z)
{
    Z.KillScore=Z.TimeScore=Z.HeartScore=Z.Score=0;
    Z.TimeGrade.Empty();
    if (!Z.bStarted || Z.Zone<1 || Z.Zone>6) return;
    if (Z.Zone==6 && (!Z.bRescued || Z.RemainingSeconds<=0 || Z.HostageHits>0 || !Z.FailureReason.IsEmpty())) { Z.TimeGrade=TEXT("실패");return; }
    Z.KillScore=Z.PlannedEnemies>0 ? FMath::Clamp(Z.KillAssessmentSum/Z.PlannedEnemies,0.,1.)*70. : 0.;
    if (Z.SupportReason.IsEmpty())
    {
        if (Z.Zone==6)
        {
            const double R=Z.RemainingSeconds;
            Z.TimeScore=R>7 ? 30 : R>5 ? 22.5 : R>2 ? 15 : R>0 ? 7.5 : 0;
        }
        else
        {
            const double T=Z.ElapsedSeconds;
            Z.TimeScore=T<=25 ? 20 : T<=30 ? 15 : T<=34 ? 10 : T<=60 ? 5 : 0;
            Z.HeartScore=FMath::Clamp(Z.RemainingHearts,0,5)*2.;
        }
        const double A=Z.Zone==6 ? 30. : 20.;
        Z.TimeGrade=Z.TimeScore==A ? TEXT("A") : Z.TimeScore==A*.75 ? TEXT("B") : Z.TimeScore==A*.5 ? TEXT("C") : TEXT("D");
    }
    else Z.TimeGrade=TEXT("지원");
    Z.Score=Z.KillScore+Z.TimeScore+Z.HeartScore;
}
double FOBStageScoring::Average(const TArray<FOBStageZoneResult>& Zones)
{
    double Sum=0;
    for (const auto& Z:Zones) if (Z.Zone>=1 && Z.Zone<=6 && Z.bFinalized) Sum+=Z.Score;
    return Sum/6.;
}
FString FOBStageScoring::Rank(double Score)
{
    return Score>=90 ? TEXT("S") : Score>=80 ? TEXT("A") : Score>=70 ? TEXT("B") : Score>=60 ? TEXT("C") : TEXT("D");
}
