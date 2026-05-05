// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "HMDStatusSubsystem.generated.h"

/**
 * 
 */
UCLASS()
class MY_PROJECT_API UHMDStatusSubsystem : public UGameInstanceSubsystem
{
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	GENERATED_BODY()
	
private:
	void HmdReconnected();
};
