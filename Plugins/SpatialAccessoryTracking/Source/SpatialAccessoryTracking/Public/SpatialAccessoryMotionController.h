// SpatialAccessoryMotionController.h
// IMotionController implementation that reads 6DOF data from the Swift bridge.

#pragma once

#include "CoreMinimal.h"
#include "IMotionController.h"

/**
 * Provides 6DOF positional tracking for PSVR2 Sense controllers on visionOS.
 * Registers as a modular feature so UMotionControllerComponent discovers it
 * without any compile-time coupling to game code.
 */
class FSpatialAccessoryMotionController : public IMotionController {
public:
  // Unique source name used by UMotionControllerComponent
  static FName GetSourceName() {
    return FName(TEXT("SpatialAccessoryTracking"));
  }

  // IMotionController interface — simple overload
  virtual bool GetControllerOrientationAndPosition(
      const int32 ControllerIndex, const FName MotionSource,
      FRotator &OutOrientation, FVector &OutPosition,
      float WorldToMetersScale) const override;

  // IMotionController interface — extended overload with velocity/acceleration
  virtual bool GetControllerOrientationAndPosition(
      const int32 ControllerIndex, const FName MotionSource,
      FRotator &OutOrientation, FVector &OutPosition,
      bool &OutbProvidedLinearVelocity, FVector &OutLinearVelocity,
      bool &OutbProvidedAngularVelocity,
      FVector &OutAngularVelocityAsAxisAndLength,
      bool &OutbProvidedLinearAcceleration, FVector &OutLinearAcceleration,
      float WorldToMetersScale) const override;

  virtual ETrackingStatus
  GetControllerTrackingStatus(const int32 ControllerIndex,
                              const FName MotionSource) const override;

  virtual FName GetMotionControllerDeviceTypeName() const override;

  bool GetControllerOrientationAndPosition(const int32 ControllerIndex,
                                           const EControllerHand DeviceHand,
                                           FRotator &OutOrientation,
                                           FVector &OutPosition,
                                           float WorldToMetersScale) const;

  ETrackingStatus
  GetControllerTrackingStatus(const int32 ControllerIndex,
                              const EControllerHand DeviceHand) const;

  // Enumerate available motion sources
  virtual void
  EnumerateSources(TArray<FMotionControllerSource> &SourcesOut) const override;

  // Additional pure virtual overrides (return defaults — we don't provide
  // these)
  virtual bool GetControllerOrientationAndPositionForTime(
      const int32 ControllerIndex, const FName MotionSource, FTimespan Time,
      bool &OutTimeWasUsed, FRotator &OutOrientation, FVector &OutPosition,
      bool &OutbProvidedLinearVelocity, FVector &OutLinearVelocity,
      bool &OutbProvidedAngularVelocity,
      FVector &OutAngularVelocityAsAxisAndLength,
      bool &OutbProvidedLinearAcceleration, FVector &OutLinearAcceleration,
      float WorldToMetersScale) const override;

  virtual float GetCustomParameterValue(const FName MotionSource,
                                        FName ParameterName,
                                        bool &bOutValueFound) const override;

  virtual bool GetHandJointPosition(const FName MotionSource, int jointIndex,
                                    FVector &OutPosition) const override;
};
