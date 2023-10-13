// Fill out your copyright notice in the Description page of Project Settings.


#include "Components/CustomMovementComponent.h"
#include "Kismet/KismetSystemLibrary.h"
#include "ClimbingSystem/ClimbingSystemCharacter.h"
#include "ClimbingSystem/DebugHelper.h"


void UCustomMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
    Super::TickComponent(DeltaTime,  TickType, ThisTickFunction);
    // TraceClimableSurfaces();
    // TraceFromEyeHeight(100.f);


}
#pragma region ClimbTraces
TArray<FHitResult> UCustomMovementComponent::DoCapsuleTraceMultiByObject(const FVector &Start, const FVector &End, bool bShowDebugShape , bool bDrawPresistantShapes)
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
            Debug::Print(TEXT("Can Start Climbing"));
        }
        else{
            //stop climbing
            Debug::Print(TEXT("Can Not Start Climbing"));
        }
    }

}

bool UCustomMovementComponent::CanStartClimbing()
{
    if(IsFalling())return false;
    if(!TraceClimableSurfaces()) return false;
    if(!TraceFromEyeHeight(100.f).bBlockingHit) return false;

    return true;
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

    ClimableSurfacesTracedResults = DoCapsuleTraceMultiByObject(Start, End, true,true);
    
    return !ClimableSurfacesTracedResults.IsEmpty();
}

FHitResult UCustomMovementComponent::TraceFromEyeHeight(float TraceDistance, float TraceStartOffset)
{
    const FVector ComponentLocation = UpdatedComponent->GetComponentLocation();
    const FVector EyeHeightOffset = UpdatedComponent->GetUpVector() * (CharacterOwner->BaseEyeHeight + TraceStartOffset);
    
    const FVector Start = ComponentLocation + EyeHeightOffset;
    const FVector End = Start + UpdatedComponent->GetForwardVector() * TraceDistance;

    return DoLineTraceSingleByObject(Start,End,true,true);
}



#pragma endregion