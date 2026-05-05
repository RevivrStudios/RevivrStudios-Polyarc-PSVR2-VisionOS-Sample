// SpatialAccessoryBridge.h
// C-linkage declarations for the Swift @_cdecl functions.
// These bridge ARKit AccessoryTrackingProvider data into C++ land.

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Start the ARKit AccessoryTrackingProvider session.
// Call once at module startup. Safe to call multiple times.
void SpatialAccessory_StartTracking(void);

// Stop the ARKit session. Call on module shutdown.
void SpatialAccessory_StopTracking(void);

// Query the latest 6DOF transform for the right-hand controller.
// outPos: float[3] receiving (x, y, z) in Unreal coordinates (cm, left-handed
// Z-up). outRot: float[4] receiving quaternion (x, y, z, w) in Unreal
// coordinates. Returns 1 if valid tracking data is available, 0 otherwise.
int SpatialAccessory_GetRightControllerTransform(float *outPos, float *outRot);

// Query the latest 6DOF transform for the left-hand controller.
// Same semantics as the right-hand version.
int SpatialAccessory_GetLeftControllerTransform(float *outPos, float *outRot);

// Query the latest 6DOF head/device transform from WorldTrackingProvider.
// outPos: float[3] receiving (x, y, z) in Unreal coordinates (cm, left-handed
// Z-up). outRot: float[4] receiving quaternion (x, y, z, w) in Unreal
// coordinates. Returns 1 if valid tracking data is available, 0 otherwise.
// This is called each frame from the game thread; the underlying
// WorldTrackingProvider.queryDeviceAnchor is synchronous and thread-safe.
int SpatialHead_GetDeviceTransform(float *outPos, float *outRot);

// Query the latest 6DOF right-hand transform from HandTrackingProvider.
// Returns real-time hand anchor position from ARKit (unlike the stale
// OpenXR xrLocateHandJointsEXT data on visionOS).
// outPos: float[3], outRot: float[4] — same format as controller transforms.
// Returns 1 if valid tracking data is available, 0 otherwise.
int SpatialHand_GetRightHandTransform(float *outPos, float *outRot);

// Query the latest 6DOF left-hand transform from HandTrackingProvider.
// Same semantics as the right-hand version.
int SpatialHand_GetLeftHandTransform(float *outPos, float *outRot);

// Get tracking debug status. All outputs are int32 (0/1 boolean except counts).
void SpatialAccessory_GetDebugStatus(int *outModuleLoaded, int *outIsSupported,
                                     int *outControllersFound,
                                     int *outSpatialFound,
                                     int *outSessionRunning, int *outAuthStatus,
                                     int *outRightTracked, int *outLeftTracked,
                                     int *outHeadTracked);

// Get hand tracking session diagnostic status.
// outHandSessionStatus: 0=not started, 1=running (combined), 2=combined failed (world-only), 3=both failed
// outHandAnchorUpdates: total number of hand anchor updates received (0 = never fired)
void SpatialAccessory_GetHandTrackingStatus(int *outHandSessionStatus,
                                             long long *outHandAnchorUpdates);

// Read raw GCController thumbstick values directly from Apple's GameController
// framework. For diagnosing whether hardware input is reaching the OS level.
// outLeftX/Y, outRightX/Y: thumbstick axis values (-1 to +1).
// outControllerCount: number of GCControllers connected.
// outHasGamepad: 0=none, 1=extendedGamepad, 2=physProfile dpads, 3=physProfile axes.
void SpatialAccessory_GetThumbstickValues(float *outLeftX, float *outLeftY,
                                           float *outRightX, float *outRightY,
                                           int *outControllerCount,
                                           int *outHasGamepad);

// Read trigger (L2/R2) and shoulder (L1/R1) button values from GCController.
// PSVR2 Sense controllers use SpatialGamepad profile, so Unreal's native input
// never sees these buttons. This bridges them via physicalInputProfile.
// Values are 0.0–1.0 (analog triggers) or 0/1 (digital buttons).
// outHasButtons: 0=none, 1=extendedGamepad, 2=physicalInputProfile buttons.
void SpatialAccessory_GetButtonValues(float *outLeftTrigger, float *outLeftShoulder,
                                       float *outRightTrigger, float *outRightShoulder,
                                       int *outHasButtons);

#ifdef __cplusplus
}
#endif
