// SpatialXRTrackingWrapper.h
// Decorator around the engine's IXRTrackingSystem that injects 6DOF head pose
// from our Swift bridge (WorldTrackingProvider → DeviceAnchor) while delegating
// everything else — stereo rendering, controllers, etc. — to the original system.
//
// We do NOT override GetXRCamera() — the inner system's camera handles
// CompositorServices device anchors and drawable presentation on visionOS.
// Our GetCurrentPose() override feeds head pose to game logic only.

#pragma once

#include "CoreMinimal.h"
#include "IXRTrackingSystem.h"

/**
 * Wraps the engine's existing IXRTrackingSystem (typically FOpenXRHMD) and
 * overrides head-tracking methods to read from SpatialAccessoryBridge's
 * WorldTrackingProvider.  Everything else delegates unchanged.
 */
class FSpatialXRTrackingWrapper : public IXRTrackingSystem
{
public:
	FSpatialXRTrackingWrapper(TSharedPtr<IXRTrackingSystem, ESPMode::ThreadSafe> InInner);

	// ---- IXRSystemIdentifier -----------------------------------------------
	virtual FName GetSystemName() const override
	{ return Inner->GetSystemName(); }

	// ---- IXRTrackingSystem — KEY OVERRIDES ----------------------------------

	/** Inject head pose from Swift bridge for HMD; delegate others. */
	virtual bool GetCurrentPose(int32 DeviceId, FQuat& OutOrientation,
	                            FVector& OutPosition) override;

	// GetXRCamera() delegates to the inner system — the OpenXR VisionOS camera
	// manages CompositorServices device anchors and drawable presentation.
	// We must NOT replace it with FDefaultXRCamera or rendering breaks.
	virtual TSharedPtr<class IXRCamera, ESPMode::ThreadSafe> GetXRCamera(int32 DeviceId = HMDDeviceId) override
	{ return Inner->GetXRCamera(DeviceId); }

	virtual bool DoesSupportPositionalTracking() const override;
	virtual bool HasValidTrackingPosition() override;
	virtual bool IsTracking(int32 DeviceId) override;
	virtual bool IsHeadTrackingAllowed() const override;

	// ---- IXRTrackingSystem — PURE VIRTUALS DELEGATED TO INNER ---------------

	virtual FString GetVersionString() const override
	{ return Inner->GetVersionString(); }

	virtual int32 GetXRSystemFlags() const override
	{ return Inner->GetXRSystemFlags(); }

	virtual bool EnumerateTrackedDevices(TArray<int32>& OutDevices,
	                                     EXRTrackedDeviceType Type = EXRTrackedDeviceType::Any) override
	{ return Inner->EnumerateTrackedDevices(OutDevices, Type); }

	virtual uint32 CountTrackedDevices(EXRTrackedDeviceType Type = EXRTrackedDeviceType::Any) override
	{ return Inner->CountTrackedDevices(Type); }

	virtual bool GetRelativeEyePose(int32 DeviceId, int32 ViewIndex,
	                                FQuat& OutOrientation, FVector& OutPosition) override
	{ return Inner->GetRelativeEyePose(DeviceId, ViewIndex, OutOrientation, OutPosition); }

	virtual bool GetTrackingSensorProperties(int32 DeviceId, FQuat& OutOrientation,
	                                         FVector& OutPosition,
	                                         FXRSensorProperties& OutSensorProperties) override
	{ return Inner->GetTrackingSensorProperties(DeviceId, OutOrientation, OutPosition, OutSensorProperties); }

	virtual EXRTrackedDeviceType GetTrackedDeviceType(int32 DeviceId) const override
	{ return Inner->GetTrackedDeviceType(DeviceId); }

	virtual FString GetTrackedDevicePropertySerialNumber(int32 DeviceId) override
	{ return Inner->GetTrackedDevicePropertySerialNumber(DeviceId); }

	virtual void SetTrackingOrigin(EHMDTrackingOrigin::Type NewOrigin) override
	{ Inner->SetTrackingOrigin(NewOrigin); }

	virtual EHMDTrackingOrigin::Type GetTrackingOrigin() const override
	{ return Inner->GetTrackingOrigin(); }

	virtual FTransform GetTrackingToWorldTransform() const override
	{ return Inner->GetTrackingToWorldTransform(); }

	virtual float GetWorldToMetersScale() const override
	{ return Inner->GetWorldToMetersScale(); }

	virtual bool GetFloorToEyeTrackingTransform(FTransform& OutFloorToEye) const override
	{ return Inner->GetFloorToEyeTrackingTransform(OutFloorToEye); }

	virtual void UpdateTrackingToWorldTransform(const FTransform& TrackingToWorldOverride) override
	{ Inner->UpdateTrackingToWorldTransform(TrackingToWorldOverride); }

	virtual void ResetOrientationAndPosition(float Yaw = 0.f) override
	{ Inner->ResetOrientationAndPosition(Yaw); }

PRAGMA_DISABLE_DEPRECATION_WARNINGS
	virtual void GetMotionControllerData(UObject* WorldContext,
	                                     const EControllerHand Hand,
	                                     FXRMotionControllerData& MotionControllerData) override
	{ Inner->GetMotionControllerData(WorldContext, Hand, MotionControllerData); }
PRAGMA_ENABLE_DEPRECATION_WARNINGS

	virtual void GetMotionControllerState(UObject* WorldContext,
	                                      const EXRSpaceType XRSpaceType,
	                                      const EControllerHand Hand,
	                                      const EXRControllerPoseType XRControllerPoseType,
	                                      FXRMotionControllerState& MotionControllerState) override
	{ Inner->GetMotionControllerState(WorldContext, XRSpaceType, Hand, XRControllerPoseType, MotionControllerState); }

	virtual void GetHandTrackingState(UObject* WorldContext,
	                                  const EXRSpaceType XRSpaceType,
	                                  const EControllerHand Hand,
	                                  FXRHandTrackingState& HandTrackingState) override
	{ Inner->GetHandTrackingState(WorldContext, XRSpaceType, Hand, HandTrackingState); }

	virtual bool GetCurrentInteractionProfile(const EControllerHand Hand,
	                                          FString& InteractionProfile) override
	{ return Inner->GetCurrentInteractionProfile(Hand, InteractionProfile); }

	// ---- IXRTrackingSystem — VIRTUALS WITH DEFAULTS DELEGATED ---------------

	virtual bool DoesSupportLateUpdate() const override
	{ return Inner->DoesSupportLateUpdate(); }

	virtual bool DoesSupportLateProjectionUpdate() const override
	{ return Inner->DoesSupportLateProjectionUpdate(); }

	virtual void RebaseObjectOrientationAndPosition(FVector& Position, FQuat& Orientation) const override
	{ Inner->RebaseObjectOrientationAndPosition(Position, Orientation); }

	virtual FVector GetAudioListenerOffset(int32 DeviceId = HMDDeviceId) const override
	{ return Inner->GetAudioListenerOffset(DeviceId); }

	virtual void ResetOrientation(float Yaw = 0.f) override
	{ Inner->ResetOrientation(Yaw); }

	virtual void ResetPosition() override
	{ Inner->ResetPosition(); }

	virtual void SetBaseRotation(const FRotator& BaseRot) override
	{ Inner->SetBaseRotation(BaseRot); }

	virtual FRotator GetBaseRotation() const override
	{ return Inner->GetBaseRotation(); }

	virtual void SetBaseOrientation(const FQuat& BaseOrient) override
	{ Inner->SetBaseOrientation(BaseOrient); }

	virtual FQuat GetBaseOrientation() const override
	{ return Inner->GetBaseOrientation(); }

	virtual void SetBasePosition(const FVector& BasePosition) override
	{ Inner->SetBasePosition(BasePosition); }

	virtual FVector GetBasePosition() const override
	{ return Inner->GetBasePosition(); }

	virtual void CalibrateExternalTrackingSource(const FTransform& ExternalTrackingTransform) override
	{ Inner->CalibrateExternalTrackingSource(ExternalTrackingTransform); }

	virtual void UpdateExternalTrackingPosition(const FTransform& ExternalTrackingTransform) override
	{ Inner->UpdateExternalTrackingPosition(ExternalTrackingTransform); }

	virtual IHeadMountedDisplay* GetHMDDevice() override
	{ return Inner->GetHMDDevice(); }

	virtual TSharedPtr<IStereoRendering, ESPMode::ThreadSafe> GetStereoRenderingDevice() override
	{ return Inner->GetStereoRenderingDevice(); }

	virtual TSharedPtr<FARSupportInterface, ESPMode::ThreadSafe> GetARCompositionComponent() override
	{ return Inner->GetARCompositionComponent(); }

	virtual const TSharedPtr<const FARSupportInterface, ESPMode::ThreadSafe> GetARCompositionComponent() const override
	{ return Inner->GetARCompositionComponent(); }

	virtual IXRLoadingScreen* GetLoadingScreen() override
	{ return Inner->GetLoadingScreen(); }

	virtual bool IsHeadTrackingAllowedForWorld(UWorld& World) const override
	{ return Inner->IsHeadTrackingAllowedForWorld(World); }

	virtual bool IsHeadTrackingEnforced() const override
	{ return Inner->IsHeadTrackingEnforced(); }

	virtual void SetHeadTrackingEnforced(bool bEnabled) override
	{ Inner->SetHeadTrackingEnforced(bEnabled); }

	// ---- Lifecycle ----------------------------------------------------------

	virtual void OnBeginPlay(FWorldContext& InWorldContext) override
	{ Inner->OnBeginPlay(InWorldContext); }

	virtual void OnEndPlay(FWorldContext& InWorldContext) override
	{ Inner->OnEndPlay(InWorldContext); }

	virtual bool OnStartGameFrame(FWorldContext& WorldContext) override
	{ return Inner->OnStartGameFrame(WorldContext); }

	virtual bool OnEndGameFrame(FWorldContext& WorldContext) override
	{ return Inner->OnEndGameFrame(WorldContext); }

	virtual void OnBeginRendering_RenderThread(FRDGBuilder& GraphBuilder,
	                                           FSceneViewFamily& ViewFamily) override
	{ Inner->OnBeginRendering_RenderThread(GraphBuilder, ViewFamily); }

	virtual void OnBeginRendering_GameThread(FSceneViewFamily& InViewFamily) override
	{ Inner->OnBeginRendering_GameThread(InViewFamily); }

	virtual void OnLateUpdateApplied_RenderThread(FRDGBuilder& GraphBuilder,
	                                              const FTransform& NewRelativeTransform) override
	{ Inner->OnLateUpdateApplied_RenderThread(GraphBuilder, NewRelativeTransform); }

	virtual void GetHMDData(UObject* WorldContext, FXRHMDData& HMDData) override
	{ Inner->GetHMDData(WorldContext, HMDData); }

	virtual EXRDeviceConnectionResult::Type ConnectRemoteXRDevice(const FString& IpAddress, const int32 BitRate) override
	{ return Inner->ConnectRemoteXRDevice(IpAddress, BitRate); }

	virtual void DisconnectRemoteXRDevice() override
	{ Inner->DisconnectRemoteXRDevice(); }

	virtual FVector2D GetPlayAreaBounds(EHMDTrackingOrigin::Type Origin) const override
	{ return Inner->GetPlayAreaBounds(Origin); }

	virtual bool GetTrackingOriginTransform(TEnumAsByte<EHMDTrackingOrigin::Type> Origin, FTransform& OutTransform) const override
	{ return Inner->GetTrackingOriginTransform(Origin, OutTransform); }

	virtual bool GetPlayAreaRect(FTransform& OutTransform, FVector2D& OutRect) const override
	{ return Inner->GetPlayAreaRect(OutTransform, OutRect); }

	virtual IOpenXRHMD* GetIOpenXRHMD() override
	{ return Inner->GetIOpenXRHMD(); }

	/** Accessor so the module can restore the original system on shutdown. */
	TSharedPtr<IXRTrackingSystem, ESPMode::ThreadSafe> GetInner() const
	{ return Inner; }

private:
	TSharedPtr<IXRTrackingSystem, ESPMode::ThreadSafe> Inner;
};
