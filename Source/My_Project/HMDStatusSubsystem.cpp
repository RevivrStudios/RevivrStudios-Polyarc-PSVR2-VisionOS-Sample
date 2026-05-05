// Fill out your copyright notice in the Description page of Project Settings.


#include "HMDStatusSubsystem.h"

#include "HeadMountedDisplayFunctionLibrary.h"

void UHMDStatusSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	
	FCoreDelegates::VRHeadsetReconnected.AddUObject(this, &UHMDStatusSubsystem::HmdReconnected);
}

void UHMDStatusSubsystem::HmdReconnected()
{
	// Re-enable stereo rendering
	UHeadMountedDisplayFunctionLibrary::EnableHMD(true);
}
