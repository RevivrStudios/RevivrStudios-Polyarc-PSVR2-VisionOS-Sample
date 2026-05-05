// SpatialXRTrackingWrapper.cpp
// Implementation of the IXRTrackingSystem decorator that injects 6DOF head
// pose from the Swift WorldTrackingProvider bridge while delegating everything
// else to the original (inner) tracking system.

#include "SpatialXRTrackingWrapper.h"

#if WITH_SPATIAL_ACCESSORY_TRACKING
#include "SpatialAccessoryBridge.h"
#endif

// DefaultXRCamera.h / SceneViewExtension.h no longer needed — we delegate
// GetXRCamera() to the inner system instead of creating our own camera.

DEFINE_LOG_CATEGORY_STATIC(LogSpatialXRWrapper, Log, All);

// -------------------------------------------------------------------------- //

FSpatialXRTrackingWrapper::FSpatialXRTrackingWrapper(
    TSharedPtr<IXRTrackingSystem, ESPMode::ThreadSafe> InInner)
    : Inner(InInner)
{
	UE_LOG(LogSpatialXRWrapper, Log,
	       TEXT("SpatialXRTrackingWrapper created, wrapping system: %s"),
	       *Inner->GetSystemName().ToString());
}

// -------------------------------------------------------------------------- //
// GetCurrentPose — the heart of the wrapper.
// For HMD (DeviceId 0) we query the Swift bridge; for everything else we
// delegate to the inner system.
// -------------------------------------------------------------------------- //

bool FSpatialXRTrackingWrapper::GetCurrentPose(int32 DeviceId,
                                               FQuat& OutOrientation,
                                               FVector& OutPosition)
{
#if WITH_SPATIAL_ACCESSORY_TRACKING
	if (DeviceId == HMDDeviceId)
	{
		float Pos[3] = {0.f, 0.f, 0.f};
		float Rot[4] = {0.f, 0.f, 0.f, 1.f};

		const int32 BridgeResult = SpatialHead_GetDeviceTransform(Pos, Rot);
		if (BridgeResult)
		{
			// The Swift bridge outputs centimetres.  Apply the engine's
			// WorldToMetersScale so the position matches whatever the game
			// has configured (default 100 → Scale = 1.0).
			const float Scale = Inner->GetWorldToMetersScale() / 100.f;
			OutPosition = FVector(
			    Pos[0] * Scale,
			    Pos[1] * Scale,
			    Pos[2] * Scale);
			OutOrientation = FQuat(Rot[0], Rot[1], Rot[2], Rot[3]);

			// Diagnostic: log bridge-provided head pose periodically
			static int32 BridgeLogCount = 0;
			if (++BridgeLogCount % 300 == 1)
			{
				UE_LOG(LogSpatialXRWrapper, Warning,
				       TEXT("[6DOF-DIAG] BRIDGE head: Pos=(%.1f, %.1f, %.1f) Rot=(%.3f, %.3f, %.3f, %.3f)"),
				       OutPosition.X, OutPosition.Y, OutPosition.Z,
				       OutOrientation.X, OutOrientation.Y, OutOrientation.Z, OutOrientation.W);
			}
			return true;
		}
		// If the bridge has no data yet, fall through to the inner system
		// so the engine still gets *some* pose (even if it's identity).

		// Diagnostic: log inner system head pose to check if it updates
		FQuat InnerOri;
		FVector InnerPos;
		bool bInnerOk = Inner->GetCurrentPose(DeviceId, InnerOri, InnerPos);

		static int32 DiagLogCount = 0;
		if (++DiagLogCount % 300 == 1)
		{
			UE_LOG(LogSpatialXRWrapper, Warning,
			       TEXT("[6DOF-DIAG] Bridge=FAIL(%d) | Inner=%s Pos=(%.1f, %.1f, %.1f) Rot=(%.3f, %.3f, %.3f, %.3f)"),
			       BridgeResult,
			       bInnerOk ? TEXT("OK") : TEXT("FAIL"),
			       InnerPos.X, InnerPos.Y, InnerPos.Z,
			       InnerOri.X, InnerOri.Y, InnerOri.Z, InnerOri.W);
		}

		// Use the inner system's result
		OutOrientation = InnerOri;
		OutPosition = InnerPos;
		return bInnerOk;
	}
#endif

	return Inner->GetCurrentPose(DeviceId, OutOrientation, OutPosition);
}

// -------------------------------------------------------------------------- //
// GetXRCamera — delegate to inner system.
// On visionOS the OpenXR camera manages CompositorServices device anchors and
// drawable presentation.  Replacing it with FDefaultXRCamera breaks rendering
// ("Presenting a drawable without a device anchor").  The compositor already
// tracks the head for RENDERING; our GetCurrentPose override feeds the pose
// to GAME LOGIC (player pawn position, interactions, etc.).
// -------------------------------------------------------------------------- //

// -------------------------------------------------------------------------- //
// Tracking status helpers
// -------------------------------------------------------------------------- //

bool FSpatialXRTrackingWrapper::DoesSupportPositionalTracking() const
{
	// We always support positional tracking — that's the whole point.
	return true;
}

bool FSpatialXRTrackingWrapper::HasValidTrackingPosition()
{
#if WITH_SPATIAL_ACCESSORY_TRACKING
	float Pos[3], Rot[4];
	if (SpatialHead_GetDeviceTransform(Pos, Rot))
	{
		return true;
	}
#endif
	return Inner->HasValidTrackingPosition();
}

bool FSpatialXRTrackingWrapper::IsTracking(int32 DeviceId)
{
#if WITH_SPATIAL_ACCESSORY_TRACKING
	if (DeviceId == HMDDeviceId)
	{
		float Pos[3], Rot[4];
		return SpatialHead_GetDeviceTransform(Pos, Rot) != 0;
	}
#endif
	return Inner->IsTracking(DeviceId);
}

bool FSpatialXRTrackingWrapper::IsHeadTrackingAllowed() const
{
	return true;
}
