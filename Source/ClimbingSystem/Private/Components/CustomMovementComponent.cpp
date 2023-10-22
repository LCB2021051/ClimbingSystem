// Fill out your copyright notice in the Description page of Project Settings.


#include "Components/CustomMovementComponent.h"
#include "Kismet/KismetSystemLibrary.h"
#include "ClimbingSystem/ClimbingSystemCharacter.h"
#include "ClimbingSystem/DebugHelper.h"
#include "Components/CapsuleComponent.h"
#include "Kismet/KismetMathLibrary.h"




void UCustomMovementComponent::BeginPlay()
{
    Super::BeginPlay();

    OwningPlayerAnimInstance = CharacterOwner->GetMesh()->GetAnimInstance();
    if(OwningPlayerAnimInstance)
    {
        OwningPlayerAnimInstance->OnMontageEnded.AddDynamic(this,&UCustomMovementComponent::OnClimbMontageEnded);
        OwningPlayerAnimInstance->OnMontageBlendingOut.AddDynamic(this,&UCustomMovementComponent::OnClimbMontageEnded);
    }
}

void UCustomMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
    Super::TickComponent(DeltaTime,  TickType, ThisTickFunction);
    // TraceClimableSurfaces();
    // TraceFromEyeHeight(100.f);


}
void UCustomMovementComponent::OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode)
{   
    if(IsClimbing()){
        bOrientRotationToMovement = false;
        CharacterOwner->GetCapsuleComponent()->SetCapsuleHalfHeight(48.f);
    }
    if(PreviousMovementMode == MOVE_Custom && PreviousCustomMode == ECustomMovementMode::MOVE_Climb){
        bOrientRotationToMovement = true;
        CharacterOwner->GetCapsuleComponent()->SetCapsuleHalfHeight(96.f);

        const FRotator DirtyRotation = UpdatedComponent->GetComponentRotation();
        const FRotator CleanStandRotation = FRotator(0.f,DirtyRotation.Yaw,0.f);
        UpdatedComponent->SetRelativeRotation(CleanStandRotation);

        StopMovementImmediately();
    }

    Super::OnMovementModeChanged( PreviousMovementMode, PreviousCustomMode);
}

void UCustomMovementComponent::PhysCustom(float deltaTime, int32 Iterations)
{   
    if(IsClimbing()){
        PhysClimb(deltaTime,Iterations);
    }

    Super::PhysCustom(deltaTime,Iterations);
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

#pragma region ClimbTraces

TArray<FHitResult> UCustomMovementComponent::DoCapsuleTraceMultiByObject(const FVector &Start, const FVector &End, bool bShowDebugShape, bool bDrawPresistantShapes)
{   
    TArray<FHitResult> OutCapsuleTraceHitResults; 
    EDrawDebugTrace::Type DebugTraceType = EDrawDebugTrace::None;
    if(bShowDebugShape){
        DebugTraceType = EDrawDebugTrace::ForOneFrame;
        if(bDrawPresistantShapes){
            DebugTraceType = EDrawDebugTrace::Persistent;
        }
    }
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
    return OutCapsuleTraceHitResults;
}

FHitResult UCustomMovementComponent::DoLineTraceSingleByObject(const FVector &Start, const FVector &End, bool bShowDebugShape, bool bDrawPresistantShapes)
{       
    FHitResult OutHit;
    EDrawDebugTrace::Type DebugTraceType = EDrawDebugTrace::None;
    if(bShowDebugShape){
        DebugTraceType = EDrawDebugTrace::ForOneFrame;
        if(bDrawPresistantShapes){
            DebugTraceType = EDrawDebugTrace::Persistent;
        }
    }
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
            PlayClimbMontage(IdleToClimbontage);
        }
    }
    else{
        StopClimbing();
    }

}

bool UCustomMovementComponent::CanStartClimbing()
{
    if(IsFalling())return false;
    if(!TraceClimableSurfaces()) return false;
    if(!TraceFromEyeHeight(100.f).bBlockingHit) return false;

    return true;
}

void UCustomMovementComponent::StartClimbing()
{   
    SetMovementMode(MOVE_Custom,ECustomMovementMode::MOVE_Climb);
}
void UCustomMovementComponent::StopClimbing()
{   
    SetMovementMode(MOVE_Falling);
}

void UCustomMovementComponent::PhysClimb(float deltaTime, int32 Iterations)
{   
    if (deltaTime < MIN_TICK_TIME)
	{
		return;
	}

	/*Process all the climbable surfaces info*/
    TraceClimableSurfaces();
    ProcessClimbableSurfaceInfo();

	/*Check if we should stop climbing*/
    if(CheckShouldStopClimbing() || CheckHasReahedFloor())
    {
        StopClimbing();
    }

    if(CheckHasReachedLedge())
    {
        Debug::Print(TEXT("Has Reached Ledge"),FColor::Green,1);
    }
    else
    {
        Debug::Print(TEXT("Has NOT Reached Ledge"),FColor::Green,1);
    }

	RestorePreAdditiveRootMotionVelocity();

	if( !HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity() )
	{	//Define the max climb speed and acceleration
		CalcVelocity(deltaTime, 0.f, true, MaxBreakClimbDeceleration);
	}

	ApplyRootMotionToVelocity(deltaTime);

	FVector OldLocation = UpdatedComponent->GetComponentLocation();
	const FVector Adjusted = Velocity * deltaTime;
	FHitResult Hit(1.f);

	//Handle climb rotation
	SafeMoveUpdatedComponent(Adjusted, GetClimbRotation(deltaTime), true, Hit);

	if (Hit.Time < 1.f)
	{
		//adjust and try again
		HandleImpact(Hit, deltaTime, Adjusted);
		SlideAlongSurface(Adjusted, (1.f-Hit.Time), Hit.Normal, Hit, true);
	}

	if(!HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity() )
	{
		Velocity = (UpdatedComponent->GetComponentLocation() - OldLocation) / deltaTime;
	}

	/*Snap movement to climbable surfaces*/
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
        // Check if the floor is reached based on certain conditions
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
    // Get the location of the component
    const FVector ComponentLocation = UpdatedComponent->GetComponentLocation();

    // Calculate the eye height offset based on the up vector and base eye height
    const FVector EyeHeightOffset = UpdatedComponent->GetUpVector() * CharacterOwner->BaseEyeHeight * 1.5f;

    // Define the start position for the line trace
    const FVector Start = ComponentLocation + EyeHeightOffset;

    // Calculate the end position for the line trace
    const FVector End = Start + UpdatedComponent->GetForwardVector() * 100.f;

    FHitResult EyeHit = DoLineTraceSingleByObject(Start, End, true);

    if(!EyeHit.bBlockingHit)
    {
        // Extend the end position downward to check for a ledge
        const FVector End2 = End + -UpdatedComponent->GetUpVector() * 50.f;

        // Perform a line trace to detect a ledge hit
        FHitResult LedgeHit = DoLineTraceSingleByObject(End, End2, true);

        // Check if a ledge hit occurred
        if (!LedgeHit.bBlockingHit) return false;

        // Check if the surface normal is parallel to the up vector and the climb velocity is sufficient
        return FVector::Parallel(-LedgeHit.ImpactNormal, FVector::UpVector) && GetUnrotatedClimbVelocity().Z > 10.f;
    
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
    if(Montage == IdleToClimbontage)
    {
        StartClimbing();
    }
}

FVector UCustomMovementComponent::GetUnrotatedClimbVelocity() const
{
    // Unrotate the velocity vector using the component's quaternion
    // This is done to get the velocity in the component's local space without rotation
    return UKismetMathLibrary::Quat_UnrotateVector(UpdatedComponent->GetComponentQuat(), Velocity);
}

#pragma endregion