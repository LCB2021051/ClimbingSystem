// Fill out your copyright notice in the Description page of Project Settings.


#include "AnimInstance/CharacterAnimInstance.h"
#include "ClimbingSystem/Public/AnimInstance/CharacterAnimInstance.h"
#include "Components/CustomMovementComponent.h"
#include "ClimbingSystem/ClimbingSystemCharacter.h"
#include "Kismet/KismetMathLibrary.h"


void UCharacterAnimInstance::NativeInitializeAnimation()
{
    Super::NativeInitializeAnimation();
    ClimbingSystemCharacter = Cast<AClimbingSystemCharacter>(TryGetPawnOwner());

    if(ClimbingSystemCharacter)
    {
        CustomMovementComponent = ClimbingSystemCharacter->GetCustomeMovementComponent();
    }
}

void UCharacterAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
    Super::NativeUpdateAnimation(DeltaSeconds);

    if(!ClimbingSystemCharacter || !CustomMovementComponent) return;

    GetGroundSpeed();
    GetAirSpeed();
    GetShouldMove();
    GetIsFalling();

}
// Calculates the ground speed by taking the 2D magnitude of the character's velocity.
void UCharacterAnimInstance::GetGroundSpeed()
{
    GroundSpeed = UKismetMathLibrary::VSizeXY(ClimbingSystemCharacter->GetVelocity());
}

// Retrieves the vertical component of the character's velocity as the air speed.
void UCharacterAnimInstance::GetAirSpeed()
{
    AirSpeed = ClimbingSystemCharacter->GetVelocity().Z;
}

// Determines if the character should move based on acceleration, ground speed, and falling status.
void UCharacterAnimInstance::GetShouldMove()
{
    bShouldMove =  CustomMovementComponent->GetCurrentAcceleration().Size() > 0 &&
                   GroundSpeed > 5.f &&
                   !bIsFalling;
}

// Checks if the character is currently in a falling state.
void UCharacterAnimInstance::GetIsFalling()
{
    bIsFalling = CustomMovementComponent->IsFalling();
}
