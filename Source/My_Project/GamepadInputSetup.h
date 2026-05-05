// GamepadInputSetup.h
#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "GamepadInputSetup.generated.h"

class UInputMappingContext;
class APawn;

UCLASS()
class MY_PROJECT_API UGamepadInputSetup : public UGameInstanceSubsystem {
  GENERATED_BODY()

public:
  virtual void Initialize(FSubsystemCollectionBase &Collection) override;
  virtual void Deinitialize() override;

private:
  UPROPERTY()
  UInputMappingContext *GamepadIMC;

  // Held actors — C++ managed, not Blueprint
  UPROPERTY() AActor *HeldActorRight;
  UPROPERTY() AActor *HeldActorLeft;

  bool bSetupDone;
  bool bSnapTurnReady;
  bool bR1WasPressed;
  bool bL1WasPressed;

  // Per-hand fire state for direct ProcessEvent fire (no InjectInputForAction)
  bool bR2DirectFired;
  bool bL2DirectFired;
  bool bLoggedFuncsR;  // true once we've logged all functions on right-held actor
  bool bLoggedFuncsL;  // true once we've logged all functions on left-held actor

  void SetupGamepadMappings();
  void TryGrab(bool bRightHand, APlayerController *PC, APawn *Pawn);
  void ReleaseGrab(bool bRightHand, APawn *Pawn);
  static bool IsGrabbableActor(AActor *Actor);

  bool Tick(float DeltaTime);
  FTSTicker::FDelegateHandle TickHandle;
};
