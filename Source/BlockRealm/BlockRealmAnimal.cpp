#include "BlockRealmAnimal.h"

#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"

ABlockRealmAnimal::ABlockRealmAnimal(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    Tags.Remove(TEXT("BlockRealmMob"));
    Tags.Add(TEXT("BlockRealmAnimal"));
    Tags.Add(TEXT("BlockRealmPig"));
    GetCapsuleComponent()->InitCapsuleSize(48.f, 64.f);
    MaxHealth = 14.f;
    AttackDamage = 0.f;
    DetectionRadius = 0.f;
    MoveSpeed = 108.f;
    DespawnDistance = 6500.f;

    for (UStaticMeshComponent* Part : HostileParts) Part->SetVisibility(false, true);
    BodyMesh->SetRelativeLocation(FVector::ZeroVector);
    BodyMesh->SetRelativeScale3D(FVector(1.08f, .66f, .50f));
    AnimalHead = CreateCreaturePart(TEXT("AnimalHead"), FVector(57.f, 0.f, 15.f), FVector(46.f, 48.f, 44.f));
    Snout = CreateCreaturePart(TEXT("AnimalSnout"), FVector(84.f, 0.f, 9.f), FVector(18.f, 29.f, 22.f));
    Tail = CreateCreaturePart(TEXT("AnimalTail"), FVector(-60.f, 0.f, 7.f), FVector(15.f, 11.f, 12.f));

    const FName LegNames[] = { TEXT("AnimalFrontLeftLeg"), TEXT("AnimalFrontRightLeg"),
        TEXT("AnimalBackLeftLeg"), TEXT("AnimalBackRightLeg") };
    for (int32 Index = 0; Index < 4; ++Index)
        AnimalLegs.Add(CreateCreaturePart(LegNames[Index],
            FVector(Index < 2 ? 33.f : -33.f, Index % 2 == 0 ? -23.f : 23.f, -47.f), FVector(18.f, 18.f, 34.f)));

    Ears.Add(CreateCreaturePart(TEXT("AnimalEarLeft"), FVector(49.f, -22.f, 42.f), FVector(20.f, 12.f, 24.f)));
    Ears.Add(CreateCreaturePart(TEXT("AnimalEarRight"), FVector(49.f, 22.f, 42.f), FVector(20.f, 12.f, 24.f)));
    Eyes.Add(CreateCreaturePart(TEXT("AnimalEyeLeft"), FVector(80.5f, -17.f, 25.f), FVector(3.f, 8.f, 8.f)));
    Eyes.Add(CreateCreaturePart(TEXT("AnimalEyeRight"), FVector(80.5f, 17.f, 25.f), FVector(3.f, 8.f, 8.f)));

    CoatPieces.Add(CreateCreaturePart(TEXT("AnimalCoatLeft"), FVector(-17.f, -34.f, 3.f), FVector(45.f, 5.f, 32.f)));
    CoatPieces.Add(CreateCreaturePart(TEXT("AnimalCoatRight"), FVector(-6.f, 34.f, 0.f), FVector(36.f, 5.f, 25.f)));
    CoatPieces.Add(CreateCreaturePart(TEXT("AnimalCoatTop"), FVector(-9.f, 0.f, 30.f), FVector(76.f, 57.f, 17.f)));
    Horns.Add(CreateCreaturePart(TEXT("AnimalHornLeft"), FVector(40.f, -28.f, 43.f), FVector(20.f, 15.f, 24.f)));
    Horns.Add(CreateCreaturePart(TEXT("AnimalHornRight"), FVector(40.f, 28.f, 43.f), FVector(20.f, 15.f, 24.f)));
    Horns.Add(CreateCreaturePart(TEXT("AnimalHornTipLeft"), FVector(34.f, -28.f, 55.f), FVector(14.f, 15.f, 12.f)));
    Horns.Add(CreateCreaturePart(TEXT("AnimalHornTipRight"), FVector(34.f, 28.f, 55.f), FVector(14.f, 15.f, 12.f)));
    for (UStaticMeshComponent* Horn : Horns) Horn->SetVisibility(false);
    CoatPieces[2]->SetVisibility(false);
}

void ABlockRealmAnimal::BeginPlay()
{
    Super::BeginPlay();
    GrazingHome = GetActorLocation();
    const FIntVector Cell = FIntVector(FMath::RoundToInt(GrazingHome.X), FMath::RoundToInt(GrazingHome.Y), int32(Species));
    WanderRandom.Initialize(static_cast<int32>(GetTypeHash(Cell)) ^ 0x3C5A871);
    DecisionRemaining = WanderRandom.FRandRange(2.5f, 5.f);
}

void ABlockRealmAnimal::SetSpecies(EBlockRealmAnimalSpecies InSpecies)
{
    Species = InSpecies == EBlockRealmAnimalSpecies::Sheep ? InSpecies : EBlockRealmAnimalSpecies::Pig;
    Tags.Remove(TEXT("BlockRealmPig"));
    Tags.Remove(TEXT("BlockRealmSheep"));
    Tags.Add(Species == EBlockRealmAnimalSpecies::Pig ? TEXT("BlockRealmPig") : TEXT("BlockRealmSheep"));
    if (HasActorBegunPlay()) ConfigureAppearance();
}

FString ABlockRealmAnimal::GetCreatureDisplayName() const
{
    return Species == EBlockRealmAnimalSpecies::Pig ? TEXT("栗斑猪") : TEXT("绒角羊");
}

void ABlockRealmAnimal::ConfigureAppearance()
{
    Super::ConfigureAppearance();
    for (UStaticMeshComponent* Part : HostileParts) Part->SetVisibility(false, true);
    const bool bSheep = Species == EBlockRealmAnimalSpecies::Sheep;
    const FLinearColor Coat = bSheep ? FLinearColor(.79f, .76f, .62f) : FLinearColor(.74f, .29f, .24f);
    const FLinearColor Face = bSheep ? FLinearColor(.19f, .15f, .11f) : FLinearColor(.88f, .41f, .33f);
    const FLinearColor Hoof = bSheep ? FLinearColor(.15f, .13f, .11f) : FLinearColor(.39f, .18f, .13f);
    SetPartColor(BodyMesh, Coat);
    SetPartColor(AnimalHead, Face);
    SetPartColor(Snout, bSheep ? Face * .82f : FLinearColor(.60f, .20f, .19f));
    SetPartColor(Tail, Coat);
    BodyMesh->SetRelativeScale3D(bSheep ? FVector(1.12f, .72f, .58f) : FVector(1.08f, .66f, .50f));
    AnimalHead->SetRelativeScale3D(bSheep ? FVector(.43f, .43f, .46f) : FVector(.46f, .48f, .44f));
    Snout->SetRelativeScale3D(bSheep ? FVector(.15f, .28f, .26f) : FVector(.18f, .29f, .22f));
    for (int32 Index = 0; Index < AnimalLegs.Num(); ++Index) SetPartColor(AnimalLegs[Index], Hoof);
    for (int32 Index = 0; Index < Ears.Num(); ++Index)
    {
        const float Side = Index == 0 ? -1.f : 1.f;
        Ears[Index]->SetRelativeLocation(bSheep ? FVector(52.f, Side * 31.f, 28.f) : FVector(49.f, Side * 22.f, 42.f));
        Ears[Index]->SetRelativeScale3D(bSheep ? FVector(.20f, .22f, .10f) : FVector(.20f, .12f, .24f));
        Ears[Index]->SetRelativeRotation(FRotator(0.f, 0.f, Side * (bSheep ? 12.f : 15.f)));
        SetPartColor(Ears[Index], bSheep ? Face : FLinearColor(.57f, .18f, .16f));
    }
    for (UStaticMeshComponent* Eye : Eyes) SetPartColor(Eye, bSheep ? FLinearColor(.96f, .86f, .61f) : FLinearColor(.035f, .025f, .025f));
    for (int32 Index = 0; Index < CoatPieces.Num(); ++Index)
    {
        CoatPieces[Index]->SetVisibility(bSheep || Index < 2);
        SetPartColor(CoatPieces[Index], bSheep ? FLinearColor(.92f, .87f, .72f) : FLinearColor(.42f, .16f, .12f));
    }
    for (UStaticMeshComponent* Horn : Horns)
    {
        Horn->SetVisibility(bSheep);
        SetPartColor(Horn, FLinearColor(.51f, .36f, .19f));
    }
    // Visible heads and snouts extend beyond the movement capsule. Give each
    // visible piece an aiming query so a side-on hit on the model still lands.
    TInlineComponentArray<UStaticMeshComponent*> Parts;
    GetComponents(Parts);
    for (UStaticMeshComponent* Part : Parts)
    {
        Part->SetCollisionEnabled(Part->IsVisible() ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
        Part->SetCollisionResponseToAllChannels(ECR_Ignore);
        Part->SetCollisionResponseToChannel(ECC_Visibility, Part->IsVisible() ? ECR_Block : ECR_Ignore);
        Part->SetGenerateOverlapEvents(false);
    }
}

void ABlockRealmAnimal::TickBehavior(float DeltaSeconds)
{
    if (!IsGrounded()) return;
    FleeRemaining = FMath::Max(0.f, FleeRemaining - DeltaSeconds);
    if (FleeRemaining > 0.f)
    {
        FVector Away = GetActorLocation() - ThreatPosition;
        if (Away.SizeSquared2D() < 1.f) Away = -GetActorForwardVector();
        if (!MoveAlongGround(Away, 325.f, DeltaSeconds))
            ThreatPosition = GetActorLocation() - Away.RotateAngleAxis(95.f, FVector::UpVector);
        return;
    }
    DecisionRemaining -= DeltaSeconds;
    if (DecisionRemaining <= 0.f)
    {
        const float Angle = WanderRandom.FRandRange(-PI, PI);
        WanderDirection = FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f);
        if (FVector::DistSquared2D(GetActorLocation(), GrazingHome) > FMath::Square(850.f))
            WanderDirection = (GrazingHome - GetActorLocation()).GetSafeNormal2D();
        WalkRemaining = WanderRandom.FRandRange(1.6f, 3.7f);
        DecisionRemaining = WalkRemaining + WanderRandom.FRandRange(2.f, 5.f);
    }
    if (WalkRemaining > 0.f)
    {
        WalkRemaining = FMath::Max(0.f, WalkRemaining - DeltaSeconds);
        if (!MoveAlongGround(WanderDirection, MoveSpeed, DeltaSeconds))
        {
            WalkRemaining = 0.f;
            DecisionRemaining = FMath::Min(DecisionRemaining, 1.3f);
        }
    }
}

void ABlockRealmAnimal::TickCreatureVisuals(float DeltaSeconds)
{
    Super::TickCreatureVisuals(DeltaSeconds);
    const bool bWalking = GetMovedDistanceThisFrame() > .1f;
    GaitPhase += DeltaSeconds * (IsFleeing() ? 16.f : bWalking ? 9.f : 1.2f);
    for (int32 Index = 0; Index < AnimalLegs.Num(); ++Index)
    {
        const float Side = Index == 0 || Index == 3 ? 1.f : -1.f;
        AnimalLegs[Index]->SetRelativeRotation(FRotator(bWalking ? FMath::Sin(GaitPhase) * 19.f * Side : 0.f, 0.f, 0.f));
    }
    const float Grazing = !bWalking && !IsFleeing() ? (FMath::Sin(GaitPhase * .55f) + 1.f) * 4.f : 0.f;
    AnimalHead->SetRelativeRotation(FRotator(-Grazing, 0.f, 0.f));
    Tail->SetRelativeRotation(FRotator(0.f, FMath::Sin(GaitPhase) * 13.f, 0.f));
}

void ABlockRealmAnimal::OnDamaged(float DamageAmount, AActor* DamageCauser)
{
    const AActor* Threat = IsValid(DamageCauser) ? DamageCauser : GetTarget();
    ThreatPosition = Threat ? Threat->GetActorLocation() : GetActorLocation() + GetActorForwardVector() * 100.f;
    FleeRemaining = 7.f;
    WalkRemaining = 0.f;
    DecisionRemaining = 0.f;
}

void ABlockRealmAnimal::SpawnDeathDrops(const FVector& DropOrigin)
{
    SpawnFoodDrop(DropOrigin + FVector(-18.f, 0.f, 0.f));
    if (Species == EBlockRealmAnimalSpecies::Pig) SpawnFoodDrop(DropOrigin + FVector(18.f, 0.f, 0.f));
}
