// Fill out your copyright notice in the Description page of Project Settings.


#include "Components/CustomMovementComponent.h"
#include "Kismet/KismetSystemLibrary.h"
#include "ClimbingSystem/ClimbingSystemCharacter.h"
#include "ClimbingSystem/DebugHelper.h"
#include "Components/CapsuleComponent.h"
#include "Kismet/KismetMathLibrary.h"




// Called when the game starts or when spawned
void UCustomMovementComponent::BeginPlay()
{
    // Call the parent class's BeginPlay function
    Super::BeginPlay();

    // Get the animation instance associated with the owning player's mesh
    OwningPlayerAnimInstance = CharacterOwner->GetMesh()->GetAnimInstance();

    // Check if the animation instance is valid
    if (OwningPlayerAnimInstance)
    {
        // Bind functions to animation montage events
        // This function will be called when a montage ends
        OwningPlayerAnimInstance->OnMontageEnded.AddDynamic(this, &UCustomMovementComponent::OnClimbMontageEnded);
        
        // This function will be called when a montage is blending out
        OwningPlayerAnimInstance->OnMontageBlendingOut.AddDynamic(this, &UCustomMovementComponent::OnClimbMontageEnded);
    }
}


void UCustomMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
    Super::TickComponent(DeltaTime,  TickType, ThisTickFunction);
    // TraceClimableSurfaces();
    // TraceFromEyeHeight(100.f);
    CanClimbDownLedge();


}

// Called when the movement mode of the character changes
void UCustomMovementComponent::OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode)
{
    // Check if the character is in climbing mode
    if (IsClimbing())
    {
        // Cannot rotate now
        bOrientRotationToMovement = false;
        // Half the Capsule Height
        CharacterOwner->GetCapsuleComponent()->SetCapsuleHalfHeight(48.f);
    }

    // Check if the previous movement mode was custom climbing mode
    if (PreviousMovementMode == MOVE_Custom && PreviousCustomMode == ECustomMovementMode::MOVE_Climb)
    {
        // Restore properties when exiting climbing mode
        bOrientRotationToMovement = true;
        CharacterOwner->GetCapsuleComponent()->SetCapsuleHalfHeight(96.f); // reset capsule size 

        // Reset rotation to a clean standing position
        const FRotator DirtyRotation = UpdatedComponent->GetComponentRotation();
        const FRotator CleanStandRotation = FRotator(0.f, DirtyRotation.Yaw, 0.f);
        UpdatedComponent->SetRelativeRotation(CleanStandRotation);

        // Stop movement immediately when exiting climbing mode
        StopMovementImmediately();
    }

    // Call the parent class's OnMovementModeChanged function
    Super::OnMovementModeChanged(PreviousMovementMode, PreviousCustomMode);
}


// Custom physics update for the character
void UCustomMovementComponent::PhysCustom(float deltaTime, int32 Iterations)
{   
    // Check if the character is in climbing mode
    if(IsClimbing()){
        // Perform custom climbing physics
        PhysClimb(deltaTime, Iterations);
    }

    // Call the parent class's PhysCustom function
    Super::PhysCustom(deltaTime, Iterations);
}


float UCustomMovementComponent::GetMaxSpeed() const
{   
    if(IsClimbing()){
        return MaxClimbSpeed;
    }
    else{
        return Super::GetMaxSpeed();
    }
}

float UCustomMovementComponent::GetMaxAcceleration() const
{
    if(IsClimbing()){
        return MaxClimbAcceleration;
    }
    else{
        return Super::GetMaxAcceleration();
    }
}

// Constrain the animation root motion velocity based on current conditions
FVector UCustomMovementComponent::ConstrainAnimRootMotionVelocity(const FVector &RootMotionVelocity, const FVector &CurrentVelocity) const
{
    // Check if the character is falling and playing any animation montage
    const bool bIsPlayingRMMontrage = (IsFalling() && OwningPlayerAnimInstance && OwningPlayerAnimInstance->IsAnyMontagePlaying());

    // If playing a root motion montage during a fall, allow the animation root motion velocity
    if(bIsPlayingRMMontrage)
    {
        return RootMotionVelocity;
    }
    else
    {
        // If not playing a root motion montage during a fall, call the parent class's ConstrainAnimRootMotionVelocity
        return Super::ConstrainAnimRootMotionVelocity(RootMotionVelocity, CurrentVelocity);
    }
}


#pragma region ClimbTraces

// Perform a capsule trace for multiple objects and return the hit results
TArray<FHitResult> UCustomMovementComponent::DoCapsuleTraceMultiByObject(const FVector &Start, const FVector &End, bool bShowDebugShape, bool bDrawPresistantShapes)
{   
    // Array to store the hit results from the capsule trace
    TArray<FHitResult> OutCapsuleTraceHitResults; 

    // Set the default debug trace type to none
    EDrawDebugTrace::Type DebugTraceType = EDrawDebugTrace::None;

    // Check if debug shapes should be shown
    if(bShowDebugShape){
        // Set debug trace type to one frame or persistent based on the flag
        DebugTraceType = EDrawDebugTrace::ForOneFrame;
        if(bDrawPresistantShapes){
            DebugTraceType = EDrawDebugTrace::Persistent;
        }
    }

    // Perform capsule trace for multiple objects
    UKismetSystemLibrary::CapsuleTraceMultiForObjects(
        this,
        Start,
        End,
        ClimbCapsuleTraceRadius,
        ClimbCapsuleTraceHalfHeight,
        ClimableSurfaceTraceTypes,
        false,
        TArray<AActor*>(),
        DebugTraceType,
        OutCapsuleTraceHitResults,
        false
    );

    // Return the array of hit results
    return OutCapsuleTraceHitResults;
}


// Perform a line trace for a single object and return the hit result
FHitResult UCustomMovementComponent::DoLineTraceSingleByObject(const FVector &Start, const FVector &End, bool bShowDebugShape, bool bDrawPresistantShapes)
{       
    // Hit result structure to store information about the hit
    FHitResult OutHit;

    // Set the default debug trace type to none
    EDrawDebugTrace::Type DebugTraceType = EDrawDebugTrace::None;

    // Check if debug shapes should be shown
    if(bShowDebugShape){
        // Set debug trace type to one frame or persistent based on the flag
        DebugTraceType = EDrawDebugTrace::ForOneFrame;
        if(bDrawPresistantShapes){
            DebugTraceType = EDrawDebugTrace::Persistent;
        }
    }

    // Perform line trace for a single object
    UKismetSystemLibrary::LineTraceSingleForObjects(
        this,
        Start,
        End,
        ClimableSurfaceTraceTypes,
        false,
        TArray<AActor*>(),
        DebugTraceType,
        OutHit,
        false
    );

    // Return the hit result structure
    return OutHit;
}

#pragma endregion

#pragma region ClimbCore
void UCustomMovementComponent::ToggleClimbing(bool bEnableClimb)
{
    if(bEnableClimb)
    {   
        if(CanStartClimbing()){
            //enter climb state   
            // Debug::Print(TEXT("Can Start Climbing"));
            PlayClimbMontage(IdleToClimbMontage);
        }
        else if(CanClimbDownLedge())
        {
            PlayClimbMontage(ClimbDownLedgeMontage);
        }
    }
    else{
        StopClimbing();
    }

}

bool UCustomMovementComponent::CanStartClimbing()
{
    // if(IsFalling())return false;
    if(!TraceClimableSurfaces()) return false;
    if(!TraceFromEyeHeight(100.f).bBlockingHit) return false;

    return true;
}

bool UCustomMovementComponent::CanClimbDownLedge()
{   
    // Check if the character is currently falling
    if(IsFalling()) return false;

    // Get the current location, forward vector, and downward vector of the character
    const FVector ComponentLocation = UpdatedComponent->GetComponentLocation();
    const FVector ComponentForward = UpdatedComponent->GetForwardVector();
    const FVector DownVector = -UpdatedComponent->GetUpVector();

    // Define the starting point for the walkable surface trace
    const FVector WalkableSurfaceTraceStart = ComponentLocation +  ComponentForward * ClimbDownWalkableSurfaceTraceOffset;
    // Define the ending point for the walkable surface trace
    const FVector WalkableSurfaceTraceEnd = WalkableSurfaceTraceStart + DownVector * 100.f;

    // Perform a line trace to detect a walkable surface below the character
    FHitResult WalkableSurfaceHit = DoLineTraceSingleByObject(WalkableSurfaceTraceStart, WalkableSurfaceTraceEnd, true);

    // Define the starting point for the ledge trace after finding a walkable surface
    const FVector LedgeTraceStart = WalkableSurfaceHit.TraceStart + ComponentForward * ClimbDownLedgeTraceOffset;
    // Define the ending point for the ledge trace
    const FVector LedgeTraceEnd = LedgeTraceStart + DownVector * 300.f ;

    // Perform another line trace to check for a ledge below the walkable surface
    FHitResult LedgeTraceHit = DoLineTraceSingleByObject(LedgeTraceStart, LedgeTraceEnd, true);

    // If there is a walkable surface hit but no ledge hit, return true (indicating the ability to climb down)
    if(WalkableSurfaceHit.bBlockingHit && !LedgeTraceHit.bBlockingHit)
    {
        return true;
    }
    
    // Otherwise, return false
    return false;
}


void UCustomMovementComponent::StartClimbing()
{   
    SetMovementMode(MOVE_Custom,ECustomMovementMode::MOVE_Climb);
}
void UCustomMovementComponent::StopClimbing()
{   
    SetMovementMode(MOVE_Falling);
}

// Custom physics handling for climbing movement mode
void UCustomMovementComponent::PhysClimb(float deltaTime, int32 Iterations)
{   
    // Ensure deltaTime is above a minimum threshold to avoid division by zero
    if (deltaTime < MIN_TICK_TIME)
	{
		return;
	}

	/* Process all climbable surfaces information */
    TraceClimableSurfaces();
    ProcessClimbableSurfaceInfo();

	/* Check if we should stop climbing */
    if(CheckShouldStopClimbing() || CheckHasReahedFloor())
    {
        StopClimbing();
    }

    // Check if the character has reached a ledge during climbing
    if(CheckHasReachedLedge())
    {
        // Play the climb to top montage
        PlayClimbMontage(ClimbToTopMontage);
    }

    /*
        This line of code is essentially instructing the system to bring back
        the original velocity of the character before any additional motion (root motion) is applied.
    */
    RestorePreAdditiveRootMotionVelocity();

    // If there is no animation root motion or override velocity
    if( !HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity() )
    {
        // Calculate velocity based on max climb speed and acceleration
        CalcVelocity(deltaTime, 0.f, true, MaxBreakClimbDeceleration);
    }

    // Apply root motion to velocity
    ApplyRootMotionToVelocity(deltaTime);

    // Save the current location
    FVector OldLocation = UpdatedComponent->GetComponentLocation();
    // Calculate adjusted movement based on velocity and time
    const FVector Adjusted = Velocity * deltaTime;
    FHitResult Hit(1.f);

    // Handle climb rotation
    SafeMoveUpdatedComponent(Adjusted, GetClimbRotation(deltaTime), true, Hit);

    // If there was a hit during movement
    if (Hit.Time < 1.f)
    {
        // Adjust and try again to handle surface interactions
        HandleImpact(Hit, deltaTime, Adjusted);
        SlideAlongSurface(Adjusted, (1.f-Hit.Time), Hit.Normal, Hit, true);
    }

    // If there is no animation root motion or override velocity
    if(!HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity() )
    {
        // Calculate velocity based on the updated location
        Velocity = (UpdatedComponent->GetComponentLocation() - OldLocation) / deltaTime;
    }

    /* Snap movement to climbable surfaces */
    SnapMovementToClimableSurfaces(deltaTime);
}


void UCustomMovementComponent::ProcessClimbableSurfaceInfo()
{
    CurrentClimbableSurfaceLocation = FVector::ZeroVector;

    CurrentClimbableSurfaceNormal = FVector::ZeroVector;

    if(ClimbableSurfacesTracedResults.IsEmpty()) return;

    for(const FHitResult& TracedHitResult : ClimbableSurfacesTracedResults){
        CurrentClimbableSurfaceLocation += TracedHitResult.ImpactPoint;
        CurrentClimbableSurfaceNormal += TracedHitResult.ImpactNormal;
    }

    CurrentClimbableSurfaceLocation /= ClimbableSurfacesTracedResults.Num();
    CurrentClimbableSurfaceNormal = CurrentClimbableSurfaceNormal.GetSafeNormal();

}

bool UCustomMovementComponent::CheckShouldStopClimbing()
{   
    if(ClimbableSurfacesTracedResults.IsEmpty()) return true;
    const float DotResult = FVector::DotProduct(CurrentClimbableSurfaceNormal,FVector::UpVector);
    const float DegreeDiff = FMath::RadiansToDegrees(FMath::Acos(DotResult));

    if(DegreeDiff<=60.f)
    {
        return true;
    }
    
    Debug::Print(TEXT("Degree Diff: ") + FString::SanitizeFloat(DegreeDiff),FColor::Cyan,1);

    return false;
}

bool UCustomMovementComponent::CheckHasReahedFloor()
{
    // Define a vector pointing downwards based on the component's up vector
    const FVector DownVector = -UpdatedComponent->GetUpVector();

    // Offset the start position by moving down 50 units
    const FVector StartOffset = DownVector * 50.f;

    // Calculate the start and end positions for the capsule trace
    const FVector Start = UpdatedComponent->GetComponentLocation() + StartOffset;
    const FVector End = Start + DownVector;

    // Perform a capsule trace to detect the floor hits
    TArray<FHitResult> PossibleFloorHits = DoCapsuleTraceMultiByObject(Start, End);

    // If no floor hits, return false
    if (PossibleFloorHits.IsEmpty()) return false;

    // Iterate through the possible floor hits
    for (const FHitResult& PossibleFloorHit : PossibleFloorHits)
    {
        // Check if the floor is walkable based on certain conditions
        const bool bFloorReached = FVector::Parallel(-PossibleFloorHit.ImpactNormal, FVector::UpVector) &&
            GetUnrotatedClimbVelocity().Z < -10.f;

        // If the floor is reached, return true
        if (bFloorReached) return true;
    }

    // If no matching floor conditions found, return false
    return false;
}

bool UCustomMovementComponent::CheckHasReachedLedge()
{
    FHitResult LedgeHitResult = TraceFromEyeHeight(100.f,50.f);
    if(!LedgeHitResult.bBlockingHit)
    {
        const FVector WalkableSurfaceTraceStart = LedgeHitResult.TraceEnd;
        const FVector DownVector = -UpdatedComponent->GetUpVector();
        const FVector WalkableSurfaceTraceEnd = WalkableSurfaceTraceStart + DownVector * 100.f;

        FHitResult WalkableSurfaceHitResult = DoLineTraceSingleByObject(WalkableSurfaceTraceStart,WalkableSurfaceTraceEnd,true);

        if(WalkableSurfaceHitResult.bBlockingHit)
        {
            const bool bLedgeReached = FVector::Parallel(-WalkableSurfaceHitResult.ImpactNormal, FVector::UpVector) &&
            GetUnrotatedClimbVelocity().Z > 10.f;

            return bLedgeReached;
        }
    }
    return false;
}


FQuat UCustomMovementComponent::GetClimbRotation(float DeltaTime)
{   
    // Get the current rotation of the movement component
    const FQuat CurrentQuat = UpdatedComponent->GetComponentQuat();

    // Check if there is animation root motion or if there's an override velocity
    if (HasAnimRootMotion() || CurrentRootMotion.HasOverrideVelocity()) {
        // If yes, return the current rotation without any changes
        return CurrentQuat;
    }

    // If there's no animation root motion or override velocity:
    // Create a rotation based on the negative of the current climbable surface normal
    const FQuat TargetQuat = FRotationMatrix::MakeFromX(-CurrentClimbableSurfaceNormal).ToQuat();

    // Interpolate (blend) between the current rotation and the target rotation over time (DeltaTime)
    // The 5.f is a speed factor, controlling how fast the interpolation happens
    return FMath::QInterpTo(CurrentQuat, TargetQuat, DeltaTime, 5.f);
}


void UCustomMovementComponent::SnapMovementToClimableSurfaces(float DeltaTime)
{   
    // Get the forward direction of the movement component
    const FVector ComponentForward = UpdatedComponent->GetForwardVector();

    // Get the current location of the movement component
    const FVector ComponentLocation = UpdatedComponent->GetComponentLocation();

    // Project the vector from the character to the climbable surface onto the forward direction
    const FVector ProjectedCharacterToSurface = (CurrentClimbableSurfaceLocation - ComponentLocation).ProjectOnTo(ComponentForward);

    // Calculate a vector that "snaps" the character to the climbable surface
    const FVector SnapVector = -CurrentClimbableSurfaceNormal * ProjectedCharacterToSurface.Length();

    // Move the component based on the snap vector, time, and maximum climb speed
    UpdatedComponent->MoveComponent(
        SnapVector * DeltaTime * MaxClimbSpeed,
        UpdatedComponent->GetComponentQuat(),
        true);
}



bool UCustomMovementComponent::IsClimbing() const
{   
    return MovementMode == MOVE_Custom && CustomMovementMode == ECustomMovementMode::MOVE_Climb;
}


// trace for climable surfaces, reteun true if there are indeed vali surfaces otherwise false
bool UCustomMovementComponent::TraceClimableSurfaces()
{   
    const FVector StartOffset = UpdatedComponent->GetForwardVector() * 30.f;
    const FVector Start = UpdatedComponent->GetComponentLocation() + StartOffset;
    const FVector End = Start + UpdatedComponent->GetForwardVector();

    ClimbableSurfacesTracedResults = DoCapsuleTraceMultiByObject(Start, End);
    
    return !ClimbableSurfacesTracedResults.IsEmpty();
}

FHitResult UCustomMovementComponent::TraceFromEyeHeight(float TraceDistance, float TraceStartOffset)
{
    const FVector ComponentLocation = UpdatedComponent->GetComponentLocation();
    const FVector EyeHeightOffset = UpdatedComponent->GetUpVector() * (CharacterOwner->BaseEyeHeight + TraceStartOffset);
    
    const FVector Start = ComponentLocation + EyeHeightOffset;
    const FVector End = Start + UpdatedComponent->GetForwardVector() * TraceDistance;

    return DoLineTraceSingleByObject(Start,End);
}

void UCustomMovementComponent::PlayClimbMontage(UAnimMontage *MontageToPlay)
{
    if(!MontageToPlay) return;
    if(!OwningPlayerAnimInstance) return;
    if(OwningPlayerAnimInstance->IsAnyMontagePlaying()) return;

    OwningPlayerAnimInstance->Montage_Play(MontageToPlay);

}

void UCustomMovementComponent::OnClimbMontageEnded(UAnimMontage *Montage, bool bInterrupted)
{
    if(Montage == IdleToClimbMontage || Montage == ClimbDownLedgeMontage)
    {
        StartClimbing();
        StopMovementImmediately();
    }
    if(Montage == ClimbToTopMontage)
    {
        SetMovementMode(MOVE_Walking);
    }
}

FVector UCustomMovementComponent::GetUnrotatedClimbVelocity() const
{
    // Unrotate the velocity vector using the component's quaternion
    // This is done to get the velocity in the component's local space without rotation
    return UKismetMathLibrary::Quat_UnrotateVector(UpdatedComponent->GetComponentQuat(), Velocity);
}

#pragma endregion