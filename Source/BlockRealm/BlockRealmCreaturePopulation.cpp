#include "BlockRealmCreaturePopulation.h"

#include "BlockRealmCharacter.h"
#include "BlockRealmDayNight.h"
#include "BlockRealmGameMode.h"
#include "BlockRealmTorch.h"
#include "BlockWorld.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "Kismet/GameplayStatics.h"
#include "VoxelData.h"

namespace
{
    bool IsLivingCreature(const ABlockRealmMob* Creature)
    {
        return IsValid(Creature) && !Creature->IsActorBeingDestroyed() && !Creature->IsDead();
    }
}

ABlockRealmCreaturePopulation::ABlockRealmCreaturePopulation()
{
    PrimaryActorTick.bCanEverTick = true;
    Tags.Add(TEXT("BlockRealmCreaturePopulation"));
}

void ABlockRealmCreaturePopulation::Initialize(ABlockWorld* InWorld, APawn* InPlayer, ABlockRealmDayNight* InClock)
{
    if (BlockWorld != InWorld || Player != InPlayer) ClearPopulation();
    BlockWorld = InWorld;
    Player = InPlayer;
    Clock = InClock;
    if (IsValid(BlockWorld))
    {
        PopulationRandom.Initialize(BlockWorld->GetSeed() ^ 0x16A37B1);
        SafeSpawnAnchor = BlockWorld->GetSpawnLocation();
    }
    LastPlayerLocation = IsValid(Player) ? Player->GetActorLocation() : FVector::ZeroVector;
    bPopulationStarted = false;
    AnimalRefillRemaining = HostileRefillRemaining = RetryRemaining = 0.f;
}

void ABlockRealmCreaturePopulation::SetPopulationEnabled(bool bInEnabled)
{
    bEnabled = bInEnabled;
    if (!bEnabled) ClearPopulation();
}

bool ABlockRealmCreaturePopulation::CanPopulate() const
{
    if (!bEnabled || !GetWorld() || GetWorld()->bIsTearingDown || GetWorld()->IsPaused()
        || !IsValid(BlockWorld) || BlockWorld->IsActorBeingDestroyed() || BlockWorld->GetWorld() != GetWorld()
        || !IsValid(Player) || Player->IsActorBeingDestroyed() || Player->GetWorld() != GetWorld()
        || Player->GetActorLocation().ContainsNaN() || !BlockWorld->IsInitialTerrainReady()) return false;
    const ABlockRealmGameMode* Game = Cast<ABlockRealmGameMode>(UGameplayStatics::GetGameMode(this));
    if (Game && !Game->IsAdventureActive()) return false;
    const ABlockRealmCharacter* Character = Cast<ABlockRealmCharacter>(Player.Get());
    return !Character || (!Character->IsInventoryOpen() && Character->GetHealth() > 0.f);
}

bool ABlockRealmCreaturePopulation::IsNight() const
{
    if (IsValid(Clock)) return Clock->IsNight();
    const ABlockRealmGameMode* Game = Cast<ABlockRealmGameMode>(UGameplayStatics::GetGameMode(this));
    return Game && Game->IsNight();
}

void ABlockRealmCreaturePopulation::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (!CanPopulate()) return;
    const float SafeDelta = FMath::IsFinite(DeltaSeconds) ? FMath::Clamp(DeltaSeconds, 0.f, .25f) : 0.f;
    if (FVector::DistSquared2D(Player->GetActorLocation(), LastPlayerLocation) > FMath::Square(4000.f))
    {
        PrunePopulation();
        if (GetAnimalCount() == 0) bPopulationStarted = false;
    }
    LastPlayerLocation = Player->GetActorLocation();
    AnimalRefillRemaining = FMath::Max(0.f, AnimalRefillRemaining - SafeDelta);
    HostileRefillRemaining = FMath::Max(0.f, HostileRefillRemaining - SafeDelta);
    RetryRemaining = FMath::Max(0.f, RetryRemaining - SafeDelta);
    if (RetryRemaining > 0.f) return;
    PopulateNow();
}

void ABlockRealmCreaturePopulation::PopulateNow()
{
    if (!CanPopulate()) return;
    PrunePopulation();
    RetryRemaining = 2.f;
    if (AnimalRefillRemaining <= 0.f)
    {
        const int32 TargetAnimals = FMath::Clamp(DesiredAnimals, 2, MaximumAnimals);
        // Both species are attempted independently, with a hard per-pass budget.
        for (int32 Attempt = 0; Attempt < TargetAnimals && GetAnimalCount() < TargetAnimals; ++Attempt)
        {
            const int32 Pigs = GetSpeciesCount(EBlockRealmAnimalSpecies::Pig);
            const int32 Sheep = GetSpeciesCount(EBlockRealmAnimalSpecies::Sheep);
            const EBlockRealmAnimalSpecies Species = Pigs <= Sheep ? EBlockRealmAnimalSpecies::Pig : EBlockRealmAnimalSpecies::Sheep;
            if (!SpawnAnimal(Species, !bPopulationStarted && GetAnimalCount() < 2))
            {
                // A hillside may support one intended side of the opening view only.
                const EBlockRealmAnimalSpecies Other = Species == EBlockRealmAnimalSpecies::Pig
                    ? EBlockRealmAnimalSpecies::Sheep : EBlockRealmAnimalSpecies::Pig;
                SpawnAnimal(Other, !bPopulationStarted && GetAnimalCount() < 2);
            }
        }
        bPopulationStarted = GetSpeciesCount(EBlockRealmAnimalSpecies::Pig) > 0
            && GetSpeciesCount(EBlockRealmAnimalSpecies::Sheep) > 0;
        AnimalRefillRemaining = bPopulationStarted ? 25.f : 3.f;
    }
    const int32 TargetHostiles = IsNight()
        ? FMath::Clamp(DesiredNightHostiles, 0, MaximumHostiles) : FMath::Clamp(DesiredDayHostiles, 0, 1);
    if (HostileRefillRemaining <= 0.f && GetHostileCount() < TargetHostiles)
    {
        const bool bSpawned = SpawnHostile();
        HostileRefillRemaining = bSpawned ? 18.f : 5.f;
    }
}

void ABlockRealmCreaturePopulation::PrunePopulation()
{
    for (int32 Index = Creatures.Num() - 1; Index >= 0; --Index)
    {
        ABlockRealmMob* Creature = Creatures[Index];
        if (!IsLivingCreature(Creature)) { Creatures.RemoveAtSwap(Index); continue; }
        if (IsValid(Player) && (Creature->GetActorLocation().ContainsNaN()
            || FVector::DistSquared(Creature->GetActorLocation(), Player->GetActorLocation()) > FMath::Square(6500.f)))
        {
            Creature->Destroy();
            Creatures.RemoveAtSwap(Index);
        }
    }
}

void ABlockRealmCreaturePopulation::ClearPopulation()
{
    for (ABlockRealmMob* Creature : Creatures)
        if (IsValid(Creature) && !Creature->IsActorBeingDestroyed()) Creature->Destroy();
    Creatures.Empty();
    bPopulationStarted = false;
    AnimalRefillRemaining = HostileRefillRemaining = RetryRemaining = 0.f;
}

void ABlockRealmCreaturePopulation::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    ClearPopulation();
    Super::EndPlay(EndPlayReason);
}

int32 ABlockRealmCreaturePopulation::GetAnimalCount() const
{
    int32 Count = 0;
    for (const ABlockRealmMob* Creature : Creatures)
        if (IsLivingCreature(Creature) && !Creature->IsHostile()) ++Count;
    return Count;
}

int32 ABlockRealmCreaturePopulation::GetHostileCount() const
{
    int32 Count = 0;
    for (const ABlockRealmMob* Creature : Creatures)
        if (IsLivingCreature(Creature) && Creature->IsHostile()) ++Count;
    return Count;
}

int32 ABlockRealmCreaturePopulation::GetSpeciesCount(EBlockRealmAnimalSpecies Species) const
{
    int32 Count = 0;
    for (const ABlockRealmMob* Creature : Creatures)
    {
        const ABlockRealmAnimal* Animal = Cast<ABlockRealmAnimal>(Creature);
        if (IsLivingCreature(Animal) && Animal->GetSpecies() == Species) ++Count;
    }
    return Count;
}

FString ABlockRealmCreaturePopulation::GetStatusText() const
{
    return FString::Printf(TEXT("栗斑猪 %d · 绒角羊 %d · 岩脊守兽 %d"),
        GetSpeciesCount(EBlockRealmAnimalSpecies::Pig), GetSpeciesCount(EBlockRealmAnimalSpecies::Sheep), GetHostileCount());
}

ABlockRealmMob* ABlockRealmCreaturePopulation::GetNearestCreature(bool bHostile) const
{
    ABlockRealmMob* Closest = nullptr;
    double ClosestDistance = TNumericLimits<double>::Max();
    const FVector Origin = IsValid(Player) ? Player->GetActorLocation() : GetActorLocation();
    for (ABlockRealmMob* Creature : Creatures)
    {
        if (!IsLivingCreature(Creature) || Creature->IsHostile() != bHostile) continue;
        const double Distance = FVector::DistSquared(Origin, Creature->GetActorLocation());
        if (Distance < ClosestDistance) { Closest = Creature; ClosestDistance = Distance; }
    }
    return Closest;
}

bool ABlockRealmCreaturePopulation::IsSpawnSpaceFree(const FVector& Location, float Radius, float HalfHeight) const
{
    if (!GetWorld() || !IsValid(Player) || Location.ContainsNaN()) return false;
    for (const ABlockRealmMob* Creature : Creatures)
        if (IsLivingCreature(Creature) && FVector::DistSquared2D(Location, Creature->GetActorLocation()) < FMath::Square(205.f)) return false;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(BlockRealmPopulationSpawn), false, this);
    return !GetWorld()->OverlapAnyTestByObjectType(Location, FQuat::Identity, FCollisionObjectQueryParams(ECC_Pawn),
        FCollisionShape::MakeCapsule(Radius, HalfHeight), Query);
}

bool ABlockRealmCreaturePopulation::HasRoof(const FVector& Location) const
{
    if (!IsValid(BlockWorld)) return false;
    FIntVector Cell = ABlockWorld::WorldToBlock(Location + FVector(0.f, 0.f, 100.f));
    for (int32 Z = FMath::Max(Cell.Z, 0); Z < FVoxelData::WorldHeight; ++Z)
        if (FVoxelData::IsSolid(BlockWorld->GetBlock(FIntVector(Cell.X, Cell.Y, Z)))) return true;
    return false;
}

bool ABlockRealmCreaturePopulation::IsOpeningViewClear(const FVector& Location) const
{
    FVector Eye;
    FRotator Rotation;
    Player->GetActorEyesViewPoint(Eye, Rotation);
    const FVector Ray = Location + FVector(0.f, 0.f, 15.f) - Eye;
    FIntVector HitBlock, Adjacent;
    return !BlockWorld->TraceBlocks(Eye, Ray, Ray.Size(), HitBlock, Adjacent);
}

bool ABlockRealmCreaturePopulation::FindSpawnLocation(bool bHostile, bool bInOpeningView, FVector& OutLocation)
{
    if (!IsValid(BlockWorld) || !IsValid(Player)) return false;
    const FVector PlayerLocation = Player->GetActorLocation();
    const float HalfHeight = bHostile ? 86.f : 64.f;
    const float Radius = bHostile ? 42.f : 48.f;
    const ACharacter* Character = Cast<ACharacter>(Player.Get());
    const float PlayerHalfHeight = Character ? Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 90.f;
    const FVector Forward = FRotator(0.f, Player->GetViewRotation().Yaw, 0.f).Vector();
    const FVector Right(-Forward.Y, Forward.X, 0.f);
    const bool bDay = !IsNight();
    const int32 Attempts = bHostile ? 28 : 20;
    for (int32 Attempt = 0; Attempt < Attempts; ++Attempt)
    {
        float Angle;
        float Distance;
        if (bInOpeningView)
        {
            const float Side = GetSpeciesCount(EBlockRealmAnimalSpecies::Pig) == 0 ? -1.f : 1.f;
            Angle = Side * (19.f + (Attempt % 5) * 5.f);
            Distance = (Side < 0.f ? 570.f : 690.f) + (Attempt / 5) * 100.f;
        }
        else if (bHostile)
        {
            Angle = Attempt == 0 ? 42.f : PopulationRandom.FRandRange(-165.f, 165.f);
            Distance = PopulationRandom.FRandRange(1700.f, 2400.f);
        }
        else
        {
            Angle = PopulationRandom.FRandRange(-150.f, 150.f);
            Distance = PopulationRandom.FRandRange(760.f, 1450.f);
        }
        const float Radians = FMath::DegreesToRadians(Angle);
        FVector Desired = PlayerLocation + Forward * FMath::Cos(Radians) * Distance + Right * FMath::Sin(Radians) * Distance;
        Desired.Z += HalfHeight - PlayerHalfHeight;
        FVector Ground;
        const float VerticalSearch = bInOpeningView ? 305.f : 605.f;
        if (!ABlockRealmMob::FindWalkableGround(BlockWorld, Desired, Radius, HalfHeight,
            VerticalSearch, VerticalSearch, Ground, !bHostile)) continue;
        if (bInOpeningView && !IsOpeningViewClear(Ground)) continue;
        if (!IsSpawnSpaceFree(Ground, Radius, HalfHeight)) continue;
        if (bHostile)
        {
            if (FVector::DistSquared2D(Ground, SafeSpawnAnchor) < FMath::Square(1350.f)
                || FVector::DistSquared2D(Ground, PlayerLocation) < FMath::Square(1500.f)) continue;
            if (bDay && Attempt < 12 && !HasRoof(Ground)) continue;
            bool bNearTorch = false;
            for (TActorIterator<ABlockRealmTorch> It(GetWorld()); It; ++It)
                if (!It->IsActorBeingDestroyed() && FVector::DistSquared(It->GetActorLocation(), Ground) < FMath::Square(650.f))
                { bNearTorch = true; break; }
            if (bNearTorch) continue;
        }
        OutLocation = Ground;
        return true;
    }
    return false;
}

bool ABlockRealmCreaturePopulation::SpawnAnimal(EBlockRealmAnimalSpecies Species, bool bInOpeningView)
{
    if (GetAnimalCount() >= FMath::Clamp(DesiredAnimals, 2, MaximumAnimals)) return false;
    FVector Location;
    if (!FindSpawnLocation(false, bInOpeningView, Location)
        && (!bInOpeningView || !FindSpawnLocation(false, false, Location))) return false;
    const FTransform Transform(FRotator(0.f, PopulationRandom.FRandRange(-180.f, 180.f), 0.f), Location);
    ABlockRealmAnimal* Animal = GetWorld()->SpawnActorDeferred<ABlockRealmAnimal>(ABlockRealmAnimal::StaticClass(),
        Transform, this, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if (!Animal) return false;
    Animal->SetSpecies(Species);
    Animal->SetWorld(BlockWorld);
    Animal->SetTarget(Player);
    Animal->FinishSpawning(Transform);
    if (!IsLivingCreature(Animal)) return false;
    Creatures.Add(Animal);
    return true;
}

bool ABlockRealmCreaturePopulation::SpawnHostile()
{
    if (GetHostileCount() >= MaximumHostiles) return false;
    FVector Location;
    if (!FindSpawnLocation(true, false, Location)) return false;
    const FVector Facing = Player->GetActorLocation() - Location;
    const FTransform Transform(FRotator(0.f, Facing.Rotation().Yaw, 0.f), Location);
    ABlockRealmMob* Mob = GetWorld()->SpawnActorDeferred<ABlockRealmMob>(ABlockRealmMob::StaticClass(),
        Transform, this, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if (!Mob) return false;
    Mob->SetWorld(BlockWorld);
    Mob->SetTarget(Player);
    Mob->ConfigureEncounter(IsNight() ? 1850.f : 1050.f, IsNight() ? 6.f : 12.f);
    Mob->Tags.Add(IsNight() ? TEXT("BlockRealmNightEncounter") : TEXT("BlockRealmExplorationEncounter"));
    Mob->FinishSpawning(Transform);
    if (!IsLivingCreature(Mob)) return false;
    Creatures.Add(Mob);
    return true;
}

bool ABlockRealmCreaturePopulation::ValidatePopulation(TArray<FString>& Failures) const
{
    const int32 PreviousFailures = Failures.Num();
    if (GetAnimalCount() > MaximumAnimals || GetHostileCount() > MaximumHostiles)
        Failures.Add(TEXT("Local creature population exceeds its hard cap"));
    for (const ABlockRealmMob* Creature : Creatures)
    {
        if (!IsLivingCreature(Creature)) continue;
        if (Creature->GetBlockWorld() != BlockWorld || Creature->GetWorld() != GetWorld()
            || Creature->GetActorLocation().ContainsNaN())
        {
            Failures.Add(TEXT("Creature has invalid world association or position"));
            continue;
        }
        FVector Ground;
        const UCapsuleComponent* Capsule = Creature->GetCapsuleComponent();
        if (!ABlockRealmMob::FindWalkableGround(BlockWorld, Creature->GetActorLocation(),
            Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight(), 3.f, 3.f, Ground))
            Failures.Add(FString::Printf(TEXT("%s lacks close, dry authoritative support"), *Creature->GetCreatureDisplayName()));
    }
    return Failures.Num() == PreviousFailures;
}
