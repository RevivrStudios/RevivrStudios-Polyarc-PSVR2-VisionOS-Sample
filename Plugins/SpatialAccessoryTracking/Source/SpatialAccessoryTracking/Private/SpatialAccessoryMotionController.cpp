// SpatialAccessoryMotionController.cpp
// IMotionController implementation reading 6DOF from the Swift bridge,
// with fallback to IHandTracker (OpenXR hand tracking) and IXRTrackingSystem.

#include "SpatialAccessoryMotionController.h"

#if WITH_SPATIAL_ACCESSORY_TRACKING
#include "SpatialAccessoryBridge.h"
#endif

#include "IMotionController.h"
#include "IXRTrackingSystem.h"
#include "IHandTracker.h"
#include "HeadMountedDisplayTypes.h"
#include "Engine/Engine.h"
#include "Features/IModularFeatures.h"

DEFINE_LOG_CATEGORY_STATIC(LogSpatialMotionCtrl, Log, All);

// -------------------------------------------------------------------------- //
// Per-hand grip rotation offset — compensates for difference between the
// controller's tracking reference frame and Unreal's hand mesh orientation.
//
// Left and right hands have separate offsets since the PSVR2 Sense
// controllers are not perfectly symmetric in their tracking frames.
//
// Tweak at runtime via console:
//   SpatialAccessory.Left.GripOffsetPitch -20
//   SpatialAccessory.Left.GripOffsetYaw 17
//   SpatialAccessory.Right.GripOffsetPitch -20
//
// Positive Pitch = nose up, Positive Yaw = turn right, Positive Roll = CW.
// -------------------------------------------------------------------------- //

// Left hand grip offset (calibrated from PSVR2 Sense controller readings)
static TAutoConsoleVariable<float> CVarLeftGripPitch(
    TEXT("SpatialAccessory.Left.GripOffsetPitch"), 75.f,
    TEXT("Left hand pitch offset (degrees). Tunable at runtime."),
    ECVF_Default);
static TAutoConsoleVariable<float> CVarLeftGripYaw(
    TEXT("SpatialAccessory.Left.GripOffsetYaw"), 17.f,
    TEXT("Left hand yaw offset (degrees). Tunable at runtime."),
    ECVF_Default);
static TAutoConsoleVariable<float> CVarLeftGripRoll(
    TEXT("SpatialAccessory.Left.GripOffsetRoll"), 4.f,
    TEXT("Left hand roll offset (degrees). Tunable at runtime."),
    ECVF_Default);

// Right hand grip offset (mirrored from left hand calibration)
static TAutoConsoleVariable<float> CVarRightGripPitch(
    TEXT("SpatialAccessory.Right.GripOffsetPitch"), 75.f,
    TEXT("Right hand pitch offset (degrees). Tunable at runtime."),
    ECVF_Default);
static TAutoConsoleVariable<float> CVarRightGripYaw(
    TEXT("SpatialAccessory.Right.GripOffsetYaw"), -17.f,
    TEXT("Right hand yaw offset (degrees). Tunable at runtime."),
    ECVF_Default);
static TAutoConsoleVariable<float> CVarRightGripRoll(
    TEXT("SpatialAccessory.Right.GripOffsetRoll"), -4.f,
    TEXT("Right hand roll offset (degrees). Tunable at runtime."),
    ECVF_Default);

// -------------------------------------------------------------------------- //
// Helpers: identify hand from motion source name
// -------------------------------------------------------------------------- //

static bool IsRightHand(const FName &MotionSource) {
  return MotionSource == FName(TEXT("Right")) ||
         MotionSource == FName(TEXT("RightGrip")) ||
         MotionSource == FName(TEXT("RightAim"));
}

static bool IsLeftHand(const FName &MotionSource) {
  return MotionSource == FName(TEXT("Left")) ||
         MotionSource == FName(TEXT("LeftGrip")) ||
         MotionSource == FName(TEXT("LeftAim"));
}

static bool IsAimSource(const FName &MotionSource) {
  return MotionSource == FName(TEXT("RightAim")) ||
         MotionSource == FName(TEXT("LeftAim"));
}

static EControllerHand GetHandFromSource(const FName &MotionSource) {
  if (IsRightHand(MotionSource))
    return EControllerHand::Right;
  return EControllerHand::Left;
}

// -------------------------------------------------------------------------- //
// Fallback 2: Query IHandTracker modular feature DIRECTLY.
// OpenXRHandTracking registers as IHandTracker and populates data via
// xrLocateHandJointsEXT each frame.
//
// IMPORTANT: GetKeypointState() returns WORLD SPACE (it applies
// TrackingToWorldTransform internally). But IMotionController must return
// TRACKING SPACE because UMotionControllerComponent uses
// SetRelativeLocationAndRotation() directly. So we undo the world transform.
// -------------------------------------------------------------------------- //

static bool TryHandTrackerDirect(const FName &MotionSource,
                                 FRotator &OutOrientation,
                                 FVector &OutPosition) {
  const EControllerHand Hand = GetHandFromSource(MotionSource);

  TArray<IHandTracker *> HandTrackers =
      IModularFeatures::Get().GetModularFeatureImplementations<IHandTracker>(
          IHandTracker::GetModularFeatureName());

  for (IHandTracker *Tracker : HandTrackers) {
    if (!Tracker || !Tracker->IsHandTrackingStateValid()) {
      continue;
    }

    FTransform PalmTransform;
    float PalmRadius = 0.f;
    if (Tracker->GetKeypointState(Hand, EHandKeypoint::Palm, PalmTransform,
                                  PalmRadius)) {
      // GetKeypointState returns WORLD space — convert back to TRACKING space
      // by applying the inverse TrackingToWorldTransform.
      if (GEngine && GEngine->XRSystem.IsValid()) {
        const FTransform TrackingToWorld =
            GEngine->XRSystem->GetTrackingToWorldTransform();
        PalmTransform = PalmTransform * TrackingToWorld.Inverse();
      }

      OutPosition = PalmTransform.GetLocation();
      OutOrientation = PalmTransform.GetRotation().Rotator();

      static int32 DirectLogCount = 0;
      if (++DirectLogCount % 300 == 1) {
        UE_LOG(LogSpatialMotionCtrl, Warning,
               TEXT("[MC-HT] IHandTracker direct (tracking space): %s(%s) Palm "
                    "Pos=(%.1f, %.1f, %.1f)"),
               *MotionSource.ToString(),
               Hand == EControllerHand::Right ? TEXT("R") : TEXT("L"),
               OutPosition.X, OutPosition.Y, OutPosition.Z);
      }
      return true;
    }
  }

  return false;
}

static ETrackingStatus TryHandTrackerDirectStatus(const FName &MotionSource) {
  const EControllerHand Hand = GetHandFromSource(MotionSource);

  TArray<IHandTracker *> HandTrackers =
      IModularFeatures::Get().GetModularFeatureImplementations<IHandTracker>(
          IHandTracker::GetModularFeatureName());

  for (IHandTracker *Tracker : HandTrackers) {
    if (!Tracker || !Tracker->IsHandTrackingStateValid()) {
      continue;
    }

    FTransform PalmTransform;
    float PalmRadius = 0.f;
    if (Tracker->GetKeypointState(Hand, EHandKeypoint::Palm, PalmTransform,
                                  PalmRadius)) {
      return ETrackingStatus::Tracked;
    }
  }

  return ETrackingStatus::NotTracked;
}

// -------------------------------------------------------------------------- //
// Fallback 3: Query IXRTrackingSystem for hand data.
// This goes through FOpenXRHMD which internally queries IHandTracker /
// IMotionController, so it may fail if those haven't updated yet.
//
// Uses Tracking space (not UnrealWorldSpace) because IMotionController must
// return tracking-relative positions.
// -------------------------------------------------------------------------- //

static bool TryXRSystemFallback(const FName &MotionSource,
                                FRotator &OutOrientation,
                                FVector &OutPosition) {
  if (!GEngine || !GEngine->XRSystem.IsValid()) {
    return false;
  }

  IXRTrackingSystem *XRSystem = GEngine->XRSystem.Get();
  const EControllerHand Hand = GetHandFromSource(MotionSource);
  const EXRControllerPoseType PoseType =
      IsAimSource(MotionSource) ? EXRControllerPoseType::Aim
                                : EXRControllerPoseType::Grip;

  // 3a) Try FXRMotionControllerState (grip/aim pose from XR system)
  {
    FXRMotionControllerState ControllerState;
    XRSystem->GetMotionControllerState(nullptr,
                                       EXRSpaceType::XRTrackingSpace, Hand,
                                       PoseType, ControllerState);

    if (ControllerState.bValid &&
        ControllerState.TrackingStatus == ETrackingStatus::Tracked) {
      OutPosition = ControllerState.ControllerLocation;
      OutOrientation = ControllerState.ControllerRotation.Rotator();

      static int32 CtrlStateLogCount = 0;
      if (++CtrlStateLogCount % 300 == 1) {
        UE_LOG(LogSpatialMotionCtrl, Log,
               TEXT("[MC-XR] ControllerState %s(%s) tracking: "
                    "Pos=(%.1f, %.1f, %.1f)"),
               *MotionSource.ToString(),
               Hand == EControllerHand::Right ? TEXT("R") : TEXT("L"),
               OutPosition.X, OutPosition.Y, OutPosition.Z);
      }
      return true;
    }
  }

  // 3b) Try FXRHandTrackingState (hand joint data — Palm as controller pos)
  {
    FXRHandTrackingState HandState;
    XRSystem->GetHandTrackingState(nullptr, EXRSpaceType::XRTrackingSpace,
                                   Hand, HandState);

    const int32 PalmIdx = static_cast<int32>(EHandKeypoint::Palm);
    if (HandState.bValid &&
        HandState.TrackingStatus == ETrackingStatus::Tracked &&
        HandState.HandKeyLocations.Num() > PalmIdx &&
        HandState.HandKeyRotations.Num() > PalmIdx) {
      OutPosition = HandState.HandKeyLocations[PalmIdx];
      OutOrientation = HandState.HandKeyRotations[PalmIdx].Rotator();

      static int32 HandStateLogCount = 0;
      if (++HandStateLogCount % 300 == 1) {
        UE_LOG(LogSpatialMotionCtrl, Log,
               TEXT("[MC-XR] HandTracking %s(%s) tracking: "
                    "Palm Pos=(%.1f, %.1f, %.1f)"),
               *MotionSource.ToString(),
               Hand == EControllerHand::Right ? TEXT("R") : TEXT("L"),
               OutPosition.X, OutPosition.Y, OutPosition.Z);
      }
      return true;
    }
  }

  return false;
}

static ETrackingStatus
TryXRSystemTrackingStatus(const FName &MotionSource) {
  if (!GEngine || !GEngine->XRSystem.IsValid()) {
    return ETrackingStatus::NotTracked;
  }

  IXRTrackingSystem *XRSystem = GEngine->XRSystem.Get();
  const EControllerHand Hand = GetHandFromSource(MotionSource);
  const EXRControllerPoseType PoseType =
      IsAimSource(MotionSource) ? EXRControllerPoseType::Aim
                                : EXRControllerPoseType::Grip;

  {
    FXRMotionControllerState ControllerState;
    XRSystem->GetMotionControllerState(nullptr,
                                       EXRSpaceType::XRTrackingSpace, Hand,
                                       PoseType, ControllerState);
    if (ControllerState.bValid &&
        ControllerState.TrackingStatus == ETrackingStatus::Tracked) {
      return ETrackingStatus::Tracked;
    }
  }

  {
    FXRHandTrackingState HandState;
    XRSystem->GetHandTrackingState(nullptr, EXRSpaceType::XRTrackingSpace,
                                   Hand, HandState);
    if (HandState.bValid &&
        HandState.TrackingStatus == ETrackingStatus::Tracked) {
      return ETrackingStatus::Tracked;
    }
  }

  return ETrackingStatus::NotTracked;
}

// -------------------------------------------------------------------------- //
// IMotionController implementation
// -------------------------------------------------------------------------- //

FName FSpatialAccessoryMotionController::GetMotionControllerDeviceTypeName()
    const {
  return GetSourceName();
}

bool FSpatialAccessoryMotionController::GetControllerOrientationAndPosition(
    const int32 ControllerIndex, const FName MotionSource,
    FRotator &OutOrientation, FVector &OutPosition,
    float WorldToMetersScale) const {

  // Only handle Left/Right hand sources
  if (!IsRightHand(MotionSource) && !IsLeftHand(MotionSource)) {
    return false;
  }

  // Periodic diagnostic: confirm this method IS being polled
  static int32 PollLogCount = 0;
  if (++PollLogCount % 300 == 1) {
    UE_LOG(LogSpatialMotionCtrl, Warning,
           TEXT("[MC-POLL] GetControllerOrientationAndPosition called: "
                "Idx=%d Src=%s WtM=%.1f"),
           ControllerIndex, *MotionSource.ToString(), WorldToMetersScale);
  }

  // Tier 0: Try Swift bridge spatial controllers (PSVR2 Sense controllers)
#if WITH_SPATIAL_ACCESSORY_TRACKING
  {
    float Pos[3] = {0.f, 0.f, 0.f};
    float Rot[4] = {0.f, 0.f, 0.f, 1.f};
    int32 Result = 0;

    if (IsRightHand(MotionSource)) {
      Result = SpatialAccessory_GetRightControllerTransform(Pos, Rot);
    } else if (IsLeftHand(MotionSource)) {
      Result = SpatialAccessory_GetLeftControllerTransform(Pos, Rot);
    }

    if (Result != 0) {
      float Scale = WorldToMetersScale / 100.f;
      OutPosition = FVector(Pos[0] * Scale, Pos[1] * Scale, Pos[2] * Scale);

      // Build raw orientation from bridge quaternion
      FQuat RawQuat(Rot[0], Rot[1], Rot[2], Rot[3]);

      // Apply per-hand configurable grip offset to correct for controller-to-hand
      // reference frame mismatch. The offset is applied in the controller's
      // local space (post-multiply) so it rotates the hand mesh relative to
      // the controller body regardless of world orientation.
      const bool bRight = IsRightHand(MotionSource);
      const FRotator GripOffset(
          bRight ? CVarRightGripPitch.GetValueOnAnyThread()
                 : CVarLeftGripPitch.GetValueOnAnyThread(),
          bRight ? CVarRightGripYaw.GetValueOnAnyThread()
                 : CVarLeftGripYaw.GetValueOnAnyThread(),
          bRight ? CVarRightGripRoll.GetValueOnAnyThread()
                 : CVarLeftGripRoll.GetValueOnAnyThread());
      FQuat FinalQuat = RawQuat * GripOffset.Quaternion();
      OutOrientation = FinalQuat.Rotator();

      static int32 BridgeLogCount = 0;
      if (++BridgeLogCount % 300 == 1) {
        FRotator RawRot = RawQuat.Rotator();
        UE_LOG(LogSpatialMotionCtrl, Warning,
               TEXT("[MC-T0] Swift bridge CONTROLLER for %s "
                    "Pos=(%.1f,%.1f,%.1f) "
                    "RawRot=(P:%.1f Y:%.1f R:%.1f) "
                    "Offset=(P:%.1f Y:%.1f R:%.1f)"),
               *MotionSource.ToString(), OutPosition.X, OutPosition.Y,
               OutPosition.Z,
               RawRot.Pitch, RawRot.Yaw, RawRot.Roll,
               GripOffset.Pitch, GripOffset.Yaw, GripOffset.Roll);
      }
      return true;
    }
  }

  // Tier 1: Try Swift bridge HAND tracking (ARKit HandTrackingProvider).
  // This provides real-time hand anchor data directly from ARKit, bypassing
  // the stale OpenXR xrLocateHandJointsEXT path on visionOS. Same proven
  // coordinate conversion as our head tracking bridge.
  {
    float Pos[3] = {0.f, 0.f, 0.f};
    float Rot[4] = {0.f, 0.f, 0.f, 1.f};
    int32 Result = 0;

    if (IsRightHand(MotionSource)) {
      Result = SpatialHand_GetRightHandTransform(Pos, Rot);
    } else if (IsLeftHand(MotionSource)) {
      Result = SpatialHand_GetLeftHandTransform(Pos, Rot);
    }

    if (Result != 0) {
      float Scale = WorldToMetersScale / 100.f;
      OutPosition = FVector(Pos[0] * Scale, Pos[1] * Scale, Pos[2] * Scale);
      OutOrientation = FQuat(Rot[0], Rot[1], Rot[2], Rot[3]).Rotator();

      static int32 HandBridgeLogCount = 0;
      if (++HandBridgeLogCount % 300 == 1) {
        UE_LOG(LogSpatialMotionCtrl, Warning,
               TEXT("[MC-T1] Swift bridge HAND for %s "
                    "Pos=(%.1f,%.1f,%.1f)"),
               *MotionSource.ToString(), OutPosition.X, OutPosition.Y,
               OutPosition.Z);
      }
      return true;
    }
  }
#endif

  // Tier 2: Fallback — Query IHandTracker DIRECTLY (bypasses FOpenXRHMD).
  //    On visionOS, OpenXR hand data may be stale. This is a safety net.
  //    Coordinate space is converted from world → tracking in the helper.
  {
    bool bResult =
        TryHandTrackerDirect(MotionSource, OutOrientation, OutPosition);
    if (bResult) {
      static int32 HTLogCount = 0;
      if (++HTLogCount % 300 == 1) {
        UE_LOG(LogSpatialMotionCtrl, Warning,
               TEXT("[MC-T2] IHandTracker direct for %s "
                    "Pos=(%.1f,%.1f,%.1f)"),
               *MotionSource.ToString(), OutPosition.X, OutPosition.Y,
               OutPosition.Z);
      }
      return true;
    }
  }

  // Tier 3: Fallback — Query XR system (tracking space)
  {
    bool bResult =
        TryXRSystemFallback(MotionSource, OutOrientation, OutPosition);
    if (bResult) {
      static int32 XRLogCount = 0;
      if (++XRLogCount % 300 == 1) {
        UE_LOG(LogSpatialMotionCtrl, Warning,
               TEXT("[MC-T3] XR system fallback for %s "
                    "Pos=(%.1f,%.1f,%.1f)"),
               *MotionSource.ToString(), OutPosition.X, OutPosition.Y,
               OutPosition.Z);
      }
      return true;
    }
  }

  // All fallbacks failed
  static int32 FailLogCount = 0;
  if (++FailLogCount % 60 == 1) { // Log every ~1 second at 60fps
    // Check IHandTracker availability for diagnostic
    TArray<IHandTracker *> HandTrackers =
        IModularFeatures::Get().GetModularFeatureImplementations<IHandTracker>(
            IHandTracker::GetModularFeatureName());
    int32 NumTrackers = HandTrackers.Num();
    bool bAnyValid = false;
    for (IHandTracker *HT : HandTrackers) {
      if (HT && HT->IsHandTrackingStateValid()) {
        bAnyValid = true;
        break;
      }
    }

    UE_LOG(LogSpatialMotionCtrl, Warning,
           TEXT("[MC-FAIL] ALL fallbacks failed for %s — "
                "HandTrackers:%d AnyValid:%d XRSys:%d"),
           *MotionSource.ToString(), NumTrackers, bAnyValid ? 1 : 0,
           (GEngine && GEngine->XRSystem.IsValid()) ? 1 : 0);
  }

  return false;
}

bool FSpatialAccessoryMotionController::GetControllerOrientationAndPosition(
    const int32 ControllerIndex, const FName MotionSource,
    FRotator &OutOrientation, FVector &OutPosition,
    bool &OutbProvidedLinearVelocity, FVector &OutLinearVelocity,
    bool &OutbProvidedAngularVelocity,
    FVector &OutAngularVelocityAsAxisAndLength,
    bool &OutbProvidedLinearAcceleration, FVector &OutLinearAcceleration,
    float WorldToMetersScale) const {
  OutbProvidedLinearVelocity = false;
  OutbProvidedAngularVelocity = false;
  OutbProvidedLinearAcceleration = false;
  return GetControllerOrientationAndPosition(ControllerIndex, MotionSource,
                                             OutOrientation, OutPosition,
                                             WorldToMetersScale);
}

ETrackingStatus FSpatialAccessoryMotionController::GetControllerTrackingStatus(
    const int32 ControllerIndex, const FName MotionSource) const {

  if (!IsRightHand(MotionSource) && !IsLeftHand(MotionSource)) {
    return ETrackingStatus::NotTracked;
  }

  // Tier 0: Try Swift bridge controllers
#if WITH_SPATIAL_ACCESSORY_TRACKING
  {
    float Pos[3], Rot[4];
    int32 Result = 0;

    if (IsRightHand(MotionSource)) {
      Result = SpatialAccessory_GetRightControllerTransform(Pos, Rot);
    } else if (IsLeftHand(MotionSource)) {
      Result = SpatialAccessory_GetLeftControllerTransform(Pos, Rot);
    }

    if (Result != 0) {
      return ETrackingStatus::Tracked;
    }
  }

  // Tier 1: Try Swift bridge hands
  {
    float Pos[3], Rot[4];
    int32 Result = 0;

    if (IsRightHand(MotionSource)) {
      Result = SpatialHand_GetRightHandTransform(Pos, Rot);
    } else if (IsLeftHand(MotionSource)) {
      Result = SpatialHand_GetLeftHandTransform(Pos, Rot);
    }

    if (Result != 0) {
      return ETrackingStatus::Tracked;
    }
  }
#endif

  // Tier 2: Try IHandTracker direct
  {
    ETrackingStatus Status = TryHandTrackerDirectStatus(MotionSource);
    if (Status == ETrackingStatus::Tracked) {
      return Status;
    }
  }

  // Tier 3: Try XR system
  return TryXRSystemTrackingStatus(MotionSource);
}

bool FSpatialAccessoryMotionController::GetControllerOrientationAndPosition(
    const int32 ControllerIndex, const EControllerHand DeviceHand,
    FRotator &OutOrientation, FVector &OutPosition,
    float WorldToMetersScale) const {
  FName Source;
  switch (DeviceHand) {
  case EControllerHand::Right:
    Source = FName(TEXT("Right"));
    break;
  case EControllerHand::Left:
    Source = FName(TEXT("Left"));
    break;
  default:
    return false;
  }
  return GetControllerOrientationAndPosition(
      ControllerIndex, Source, OutOrientation, OutPosition, WorldToMetersScale);
}

ETrackingStatus FSpatialAccessoryMotionController::GetControllerTrackingStatus(
    const int32 ControllerIndex, const EControllerHand DeviceHand) const {
  FName Source;
  switch (DeviceHand) {
  case EControllerHand::Right:
    Source = FName(TEXT("Right"));
    break;
  case EControllerHand::Left:
    Source = FName(TEXT("Left"));
    break;
  default:
    return ETrackingStatus::NotTracked;
  }
  return GetControllerTrackingStatus(ControllerIndex, Source);
}

void FSpatialAccessoryMotionController::EnumerateSources(
    TArray<FMotionControllerSource> &SourcesOut) const {
  SourcesOut.Add(FMotionControllerSource(FName(TEXT("Right"))));
  SourcesOut.Add(FMotionControllerSource(FName(TEXT("Left"))));
  SourcesOut.Add(FMotionControllerSource(FName(TEXT("RightGrip"))));
  SourcesOut.Add(FMotionControllerSource(FName(TEXT("LeftGrip"))));
  SourcesOut.Add(FMotionControllerSource(FName(TEXT("RightAim"))));
  SourcesOut.Add(FMotionControllerSource(FName(TEXT("LeftAim"))));
}

bool FSpatialAccessoryMotionController::
    GetControllerOrientationAndPositionForTime(
        const int32 ControllerIndex, const FName MotionSource, FTimespan Time,
        bool &OutTimeWasUsed, FRotator &OutOrientation, FVector &OutPosition,
        bool &OutbProvidedLinearVelocity, FVector &OutLinearVelocity,
        bool &OutbProvidedAngularVelocity,
        FVector &OutAngularVelocityAsAxisAndLength,
        bool &OutbProvidedLinearAcceleration, FVector &OutLinearAcceleration,
        float WorldToMetersScale) const {
  OutTimeWasUsed = false;
  OutbProvidedLinearVelocity = false;
  OutbProvidedAngularVelocity = false;
  OutbProvidedLinearAcceleration = false;
  return GetControllerOrientationAndPosition(ControllerIndex, MotionSource,
                                             OutOrientation, OutPosition,
                                             WorldToMetersScale);
}

float FSpatialAccessoryMotionController::GetCustomParameterValue(
    const FName MotionSource, FName ParameterName, bool &bOutValueFound) const {
  bOutValueFound = false;
  return 0.f;
}

bool FSpatialAccessoryMotionController::GetHandJointPosition(
    const FName MotionSource, int jointIndex, FVector &OutPosition) const {

  if (!IsRightHand(MotionSource) && !IsLeftHand(MotionSource)) {
    return false;
  }

  const EControllerHand Hand = GetHandFromSource(MotionSource);
  const EHandKeypoint Keypoint = static_cast<EHandKeypoint>(jointIndex);

  // Try IHandTracker direct first
  TArray<IHandTracker *> HandTrackers =
      IModularFeatures::Get().GetModularFeatureImplementations<IHandTracker>(
          IHandTracker::GetModularFeatureName());

  for (IHandTracker *Tracker : HandTrackers) {
    if (!Tracker || !Tracker->IsHandTrackingStateValid()) {
      continue;
    }

    FTransform JointTransform;
    float JointRadius = 0.f;
    if (Tracker->GetKeypointState(Hand, Keypoint, JointTransform,
                                  JointRadius)) {
      OutPosition = JointTransform.GetLocation();
      return true;
    }
  }

  // Fallback: XR system
  if (!GEngine || !GEngine->XRSystem.IsValid()) {
    return false;
  }

  FXRHandTrackingState HandState;
  GEngine->XRSystem->GetHandTrackingState(
      nullptr, EXRSpaceType::UnrealWorldSpace, Hand, HandState);

  if (HandState.bValid &&
      HandState.TrackingStatus == ETrackingStatus::Tracked &&
      HandState.HandKeyLocations.IsValidIndex(jointIndex)) {
    OutPosition = HandState.HandKeyLocations[jointIndex];
    return true;
  }

  return false;
}
