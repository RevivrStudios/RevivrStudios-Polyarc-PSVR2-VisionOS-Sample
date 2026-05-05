// SpatialAccessoryBridge.swift
// ARKit AccessoryTrackingProvider bridge for spatial controllers on visionOS 26+.
//
// Follows Apple's sample pattern: listen for GCControllerDidConnect, create
// Accessory(device:) for spatial controllers, then run AccessoryTrackingProvider.

import ARKit
import GameController
import RealityKit
import os
import QuartzCore

// MARK: - Thread-safe shared transform storage

private struct ControllerTransform: @unchecked Sendable {
    var position: SIMD3<Float> = .zero   // Unreal coords: cm, left-handed Z-up
    var rotation: simd_quatf = simd_quatf(ix: 0, iy: 0, iz: 0, r: 1)
    var isValid: Bool = false
}

private struct HeadTransform: @unchecked Sendable {
    var position: SIMD3<Float> = .zero   // Unreal coords: cm, left-handed Z-up
    var rotation: simd_quatf = simd_quatf(ix: 0, iy: 0, iz: 0, r: 1)
    var isValid: Bool = false
}

private struct HandTransformData: @unchecked Sendable {
    var position: SIMD3<Float> = .zero   // Unreal coords: cm, left-handed Z-up
    var rotation: simd_quatf = simd_quatf(ix: 0, iy: 0, iz: 0, r: 1)
    var isValid: Bool = false
}

// Status info for debugging — readable from C++ side
private struct TrackingStatus: @unchecked Sendable {
    var moduleLoaded: Bool = false
    var isSupported: Bool = false
    var controllersFound: Int32 = 0
    var spatialControllersFound: Int32 = 0
    var arkitSessionRunning: Bool = false
    var authorizationStatus: Int32 = 0  // 0=unknown, 1=allowed, 2=denied
    var lastError: String = ""
    var rightTracked: Bool = false
    var leftTracked: Bool = false
    var headTracked: Bool = false
    var handSessionStatus: Int32 = 0  // 0=not started, 1=running (combined), 2=failed (world-only fallback), 3=world-only also failed
    var handAnchorUpdateCount: Int64 = 0  // Total hand anchor updates received
}

private let lock = NSLock()
private var rightController = ControllerTransform()
private var leftController  = ControllerTransform()
private var rightHand       = HandTransformData()
private var leftHand        = HandTransformData()
private var trackingStatus  = TrackingStatus()
private var headTransform   = HeadTransform()
private var worldProvider: Any? = nil  // Type-erased WorldTrackingProvider

private let logger = Logger(subsystem: "com.example.SpatialAccessory", category: "Bridge")

// MARK: - Coordinate conversion

private func appleToUnrealPosition(_ p: SIMD3<Float>) -> SIMD3<Float> {
    return SIMD3<Float>(
        -p.z * 100.0,   // Apple -Z → Unreal +X (forward)
         p.x * 100.0,   // Apple +X → Unreal +Y (right)
         p.y * 100.0    // Apple +Y → Unreal +Z (up)
    )
}

// Coordinate-system conversion: Apple (right-handed, Y-up, -Z forward)
// → Unreal (left-handed, Z-up, X-forward).
//
// Position mapping:  Apple X → UE +Y,  Apple Y → UE +Z,  Apple Z → UE -X
// Transform matrix M has det = -1 (handedness flip), so the quaternion
// imaginary parts must be axis-remapped AND conjugated (sign-flipped).
//
// Before fix the signs were wrong, producing the INVERSE rotation and
// causing controllers to rotate unnaturally (mirrored pitch/yaw).
private func appleToUnrealRotation(_ q: simd_quatf) -> simd_quatf {
    return simd_quatf(ix: q.imag.z, iy: -q.imag.x, iz: -q.imag.y, r: q.real)
}

// MARK: - ARKit session management

private var arkitSession: ARKitSession?
private var trackingTask: Task<Void, Never>?
private var eventMonitorTask: Task<Void, Never>?
private var spatialAccessories: [Any] = []  // Type-erased [Accessory] for availability
private var connectObserver: NSObjectProtocol?
private var disconnectObserver: NSObjectProtocol?

// GCController → chirality mapping.  Populated in addSpatialController() when
// Accessory(device:) reveals inherentChirality.  Used by getThumbstickValues()
// and getButtonValues() to correctly assign left/right inputs.
private enum ControllerHand: CustomStringConvertible {
    case left, right, unknown
    var description: String {
        switch self {
        case .left: return "left"
        case .right: return "right"
        case .unknown: return "unknown"
        }
    }
}
private var controllerChirality: [ObjectIdentifier: ControllerHand] = [:]

@available(visionOS 26.0, *)
private func startARKitTracking() async {
    logger.info(">>> startARKitTracking() called")
    
    // Check support first
    let supported = AccessoryTrackingProvider.isSupported
    lock.lock()
    trackingStatus.isSupported = supported
    lock.unlock()
    
    if !supported {
        logger.error("AccessoryTrackingProvider is NOT supported on this device")
        lock.lock()
        trackingStatus.lastError = "Not supported"
        lock.unlock()
        return
    }
    logger.info("AccessoryTrackingProvider IS supported")
    
    // Register for controller connect/disconnect notifications
    connectObserver = NotificationCenter.default.addObserver(
        forName: NSNotification.Name.GCControllerDidConnect,
        object: nil, queue: nil) { notification in
        if let controller = notification.object as? GCController {
            logger.info("GCController connected: cat=\(controller.productCategory) vendor=\(controller.vendorName ?? "nil")")
            // Try ALL controllers — not just spatial — since without
            // SpatialGamepad in the plist, PSVR2 Sense controllers
            // register as ExtendedGamepad but may still be trackable.
            Task {
                await addSpatialController(controller)
            }
        }
    }
    
    disconnectObserver = NotificationCenter.default.addObserver(
        forName: NSNotification.Name.GCControllerDidDisconnect,
        object: nil, queue: nil) { notification in
        if let controller = notification.object as? GCController {
            logger.info("GCController disconnected: \(controller.productCategory)")
        }
    }
    
    // Check already-connected controllers
    let controllers = GCController.controllers()
    lock.lock()
    trackingStatus.controllersFound = Int32(controllers.count)
    lock.unlock()
    logger.info("Found \(controllers.count) already-connected controllers")
    
    for controller in controllers {
        logger.info("  Controller: cat=\(controller.productCategory), vendor=\(controller.vendorName ?? "nil")")
        // Try every controller for accessory tracking
        await addSpatialController(controller)
    }
    
    // Start tracking even if no spatial controllers yet
    // (they might connect later)
    await startTrackingSession()
}

@available(visionOS 26.0, *)
private func addSpatialController(_ controller: GCController) async {
    do {
        let accessory = try await Accessory(device: controller)
        spatialAccessories.append(accessory as Any)

        // Store GCController → chirality mapping so thumbstick/button readers
        // can correctly assign left vs right controller inputs.
        let hand: ControllerHand
        switch accessory.inherentChirality {
        case .left:  hand = .left
        case .right: hand = .right
        default:     hand = .unknown
        }
        lock.lock()
        controllerChirality[ObjectIdentifier(controller)] = hand
        trackingStatus.spatialControllersFound = Int32(spatialAccessories.count)
        lock.unlock()
        logger.info("Created Accessory: \(accessory.name), chirality: \(String(describing: accessory.inherentChirality)) → \(hand)")
    } catch {
        logger.error("Failed to create Accessory: \(error.localizedDescription)")
        lock.lock()
        trackingStatus.lastError = "Accessory init: \(error.localizedDescription)"
        lock.unlock()
    }
}

@available(visionOS 26.0, *)
private func startTrackingSession() async {
    let accessories = spatialAccessories.compactMap { $0 as? Accessory }
    let accessoryProvider = AccessoryTrackingProvider(accessories: accessories)

    let session = ARKitSession()
    arkitSession = session

    // Monitor ARKit session events on a separate task
    eventMonitorTask = Task {
        await monitorSessionEvents(session)
    }

    // Run AccessoryTrackingProvider in its own session (controllers only)
    do {
        try await session.run([accessoryProvider])
        lock.lock()
        trackingStatus.arkitSessionRunning = true
        lock.unlock()
        logger.info("ARKitSession running with AccessoryTracking (\(spatialAccessories.count) accessories)")
    } catch {
        logger.error("Failed to start ARKitSession: \(error.localizedDescription)")
        lock.lock()
        trackingStatus.lastError = "Session run: \(error.localizedDescription)"
        lock.unlock()
        return
    }

    // Run WorldTrackingProvider in a SEPARATE session so it doesn't
    // conflict with the OpenXR compositor managing the immersive space.
    Task {
        await startWorldTrackingSession()
    }

    // HandTrackingProvider is now combined into the WorldTrackingProvider
    // session (see startWorldTrackingSession) to avoid resource contention.
    // Running it in a separate ARKitSession caused it to get starved by the
    // OpenXR compositor's own hand tracking hold on visionOS.

    // Process accessory anchor updates (controllers)
    trackingTask = Task {
        for await update in accessoryProvider.anchorUpdates {
            let anchor = update.anchor
            
            let transform = anchor.originFromAnchorTransform
            let applePos = SIMD3<Float>(transform.columns.3.x,
                                         transform.columns.3.y,
                                         transform.columns.3.z)
            let rotMatrix = simd_float3x3(
                SIMD3<Float>(transform.columns.0.x, transform.columns.0.y, transform.columns.0.z),
                SIMD3<Float>(transform.columns.1.x, transform.columns.1.y, transform.columns.1.z),
                SIMD3<Float>(transform.columns.2.x, transform.columns.2.y, transform.columns.2.z)
            )
            let appleQuat = simd_quatf(rotMatrix)
            let uePos = appleToUnrealPosition(applePos)
            let ueRot = appleToUnrealRotation(appleQuat)
            
            let chirality = anchor.heldChirality
            let tracked = anchor.isTracked
            
            lock.lock()
            switch chirality {
            case .right:
                rightController.position = uePos
                rightController.rotation = ueRot
                rightController.isValid = tracked
                trackingStatus.rightTracked = tracked
            case .left:
                leftController.position = uePos
                leftController.rotation = ueRot
                leftController.isValid = tracked
                trackingStatus.leftTracked = tracked
            default:
                // Unknown chirality — assign to right by default
                rightController.position = uePos
                rightController.rotation = ueRot
                rightController.isValid = tracked
                trackingStatus.rightTracked = tracked
            }
            lock.unlock()
        }
        logger.info("Anchor update stream ended")
    }
}

private var worldTrackingSession: ARKitSession?  // Shared session for WorldTracking + HandTracking
private var handTrackingTask: Task<Void, Never>?

@available(visionOS 26.0, *)
private func startWorldTrackingSession() async {
    // WorldTrackingProvider + HandTrackingProvider in one shared ARKitSession.
    // Combining them avoids resource contention — running HandTrackingProvider
    // in a separate session caused it to get starved by the OpenXR compositor's
    // hand tracking on visionOS.
    let worldTracker = WorldTrackingProvider()
    worldProvider = worldTracker

    let handTracker = HandTrackingProvider()

    let session = ARKitSession()
    worldTrackingSession = session

    // Request both world sensing and hand tracking authorization.
    // World sensing is required for DeviceAnchor (head) queries.
    // Hand tracking is required for HandAnchor updates.
    logger.info("[6DOF] Requesting .worldSensing + .handTracking authorization...")
    let authResults = await session.requestAuthorization(for: [.worldSensing, .handTracking])
    for (authType, authStatus) in authResults {
        logger.info("[6DOF] Auth result: type=\(String(describing: authType)), status=\(String(describing: authStatus))")
        if authType == .worldSensing {
            lock.lock()
            if authStatus == .allowed {
                trackingStatus.authorizationStatus = 1
                logger.info("[6DOF-HEAD] World sensing ALLOWED")
            } else if authStatus == .denied {
                trackingStatus.authorizationStatus = 2
                trackingStatus.lastError = "World sensing denied by user"
                logger.error("[6DOF-HEAD] World sensing DENIED — head tracking will not work")
            } else {
                trackingStatus.authorizationStatus = 0
                logger.warning("[6DOF-HEAD] World sensing auth status: \(String(describing: authStatus))")
            }
            lock.unlock()
        }
        if authType == .handTracking {
            if authStatus == .allowed {
                logger.info("[6DOF-HAND] Hand tracking ALLOWED")
            } else {
                logger.error("[6DOF-HAND] Hand tracking auth status: \(String(describing: authStatus))")
            }
        }
    }

    // Monitor world tracking session events for auth changes and errors
    Task {
        await monitorWorldTrackingEvents(session)
    }

    // Run BOTH providers in the same session
    do {
        try await session.run([worldTracker, handTracker])
        lock.lock()
        trackingStatus.headTracked = true
        trackingStatus.handSessionStatus = 1  // Combined session running
        lock.unlock()
        logger.info("[6DOF] WorldTracking + HandTracking running in shared session")
    } catch {
        // If combined run fails, try world-only as fallback
        logger.error("[6DOF] Failed to start combined session: \(error.localizedDescription) — trying world-only")
        lock.lock()
        trackingStatus.handSessionStatus = 2  // Combined failed, trying world-only
        trackingStatus.lastError = "Combined: \(error.localizedDescription)"
        lock.unlock()
        do {
            try await session.run([worldTracker])
            lock.lock()
            trackingStatus.headTracked = true
            lock.unlock()
            logger.info("[6DOF-HEAD] WorldTrackingProvider running (hand tracking unavailable)")
        } catch {
            logger.error("[6DOF-HEAD] Failed to start WorldTrackingProvider: \(error.localizedDescription)")
            lock.lock()
            trackingStatus.headTracked = false
            trackingStatus.handSessionStatus = 3  // Both failed
            trackingStatus.lastError = "WorldTracking: \(error.localizedDescription)"
            lock.unlock()
        }
        return
    }

    // Process hand anchor updates from the shared session
    handTrackingTask = Task {
        for await update in handTracker.anchorUpdates {
            let anchor = update.anchor
            let tracked = anchor.isTracked

            lock.lock()
            trackingStatus.handAnchorUpdateCount += 1
            lock.unlock()

            let transform = anchor.originFromAnchorTransform
            let applePos = SIMD3<Float>(transform.columns.3.x,
                                         transform.columns.3.y,
                                         transform.columns.3.z)
            let rotMatrix = simd_float3x3(
                SIMD3<Float>(transform.columns.0.x, transform.columns.0.y, transform.columns.0.z),
                SIMD3<Float>(transform.columns.1.x, transform.columns.1.y, transform.columns.1.z),
                SIMD3<Float>(transform.columns.2.x, transform.columns.2.y, transform.columns.2.z)
            )
            let appleQuat = simd_quatf(rotMatrix)
            let uePos = appleToUnrealPosition(applePos)
            let ueRot = appleToUnrealRotation(appleQuat)

            lock.lock()
            switch anchor.chirality {
            case .left:
                leftHand.position = uePos
                leftHand.rotation = ueRot
                leftHand.isValid = tracked
            case .right:
                rightHand.position = uePos
                rightHand.rotation = ueRot
                rightHand.isValid = tracked
            @unknown default:
                break
            }
            lock.unlock()
        }
        logger.info("[6DOF-HAND] Hand anchor update stream ended")
    }
}

// startHandTrackingSession() has been removed — HandTrackingProvider is now
// combined into startWorldTrackingSession() to share a single ARKitSession.
// This avoids resource contention with the OpenXR compositor on visionOS.

@available(visionOS 26.0, *)
private func monitorWorldTrackingEvents(_ session: ARKitSession) async {
    for await event in session.events {
        switch event {
        case .dataProviderStateChanged(_, let newState, let error):
            logger.info("[6DOF-HEAD] WorldTracking provider state: \(String(describing: newState))")
            if let error {
                logger.error("[6DOF-HEAD] WorldTracking provider error: \(error)")
                lock.lock()
                trackingStatus.lastError = "WorldTracking provider: \(error)"
                lock.unlock()
            }
        case .authorizationChanged(let type, let status):
            logger.info("[6DOF-HEAD] WorldTracking auth changed: type=\(String(describing: type)), status=\(String(describing: status))")
            if type == .worldSensing {
                lock.lock()
                if status == .denied {
                    trackingStatus.authorizationStatus = 2
                    trackingStatus.lastError = "World sensing authorization denied"
                } else if status == .allowed {
                    trackingStatus.authorizationStatus = 1
                }
                lock.unlock()
            }
        default:
            break
        }
    }
}

@available(visionOS 26.0, *)
private func monitorSessionEvents(_ session: ARKitSession) async {
    for await event in session.events {
        switch event {
        case .dataProviderStateChanged(_, let newState, let error):
            logger.info("DataProvider state: \(String(describing: newState))")
            if newState == .stopped {
                if let error {
                    logger.error("DataProvider stopped with error: \(error)")
                    lock.lock()
                    trackingStatus.lastError = "Provider stopped: \(error)"
                    lock.unlock()
                }
            }
        case .authorizationChanged(let type, let status):
            logger.info("Authorization changed: type=\(String(describing: type)), status=\(String(describing: status))")
            if type == .accessoryTracking {
                lock.lock()
                if status == .denied {
                    trackingStatus.authorizationStatus = 2
                    trackingStatus.lastError = "Authorization denied by user"
                } else if status == .allowed {
                    trackingStatus.authorizationStatus = 1
                }
                lock.unlock()
            }
        default:
            break
        }
    }
}

// MARK: - @_cdecl exports (callable from C++)

@_cdecl("SpatialAccessory_StartTracking")
public func startTracking() {
    logger.info(">>> SpatialAccessory_StartTracking called")
    lock.lock()
    trackingStatus.moduleLoaded = true
    lock.unlock()
    
    if #available(visionOS 26.0, *) {
        Task { @MainActor in
            await startARKitTracking()
        }
    } else {
        logger.error("visionOS 26+ required")
        lock.lock()
        trackingStatus.lastError = "visionOS 26+ required"
        lock.unlock()
    }
}

@_cdecl("SpatialAccessory_StopTracking")
public func stopTracking() {
    logger.info("SpatialAccessory_StopTracking called")
    trackingTask?.cancel()
    trackingTask = nil
    eventMonitorTask?.cancel()
    eventMonitorTask = nil
    handTrackingTask?.cancel()
    handTrackingTask = nil
    arkitSession?.stop()
    arkitSession = nil
    worldTrackingSession?.stop()   // Also stops HandTrackingProvider (shared session)
    worldTrackingSession = nil

    if let obs = connectObserver {
        NotificationCenter.default.removeObserver(obs)
    }
    if let obs = disconnectObserver {
        NotificationCenter.default.removeObserver(obs)
    }

    worldProvider = nil

    lock.lock()
    rightController.isValid = false
    leftController.isValid = false
    rightHand.isValid = false
    leftHand.isValid = false
    headTransform.isValid = false
    trackingStatus.arkitSessionRunning = false
    trackingStatus.headTracked = false
    lock.unlock()
}

@_cdecl("SpatialAccessory_GetRightControllerTransform")
public func getRightControllerTransform(_ outPos: UnsafeMutablePointer<Float>,
                                         _ outRot: UnsafeMutablePointer<Float>) -> Int32 {
    lock.lock()
    defer { lock.unlock() }
    
    guard rightController.isValid else { return 0 }
    
    outPos[0] = rightController.position.x
    outPos[1] = rightController.position.y
    outPos[2] = rightController.position.z
    
    outRot[0] = rightController.rotation.imag.x
    outRot[1] = rightController.rotation.imag.y
    outRot[2] = rightController.rotation.imag.z
    outRot[3] = rightController.rotation.real
    
    return 1
}

@_cdecl("SpatialAccessory_GetLeftControllerTransform")
public func getLeftControllerTransform(_ outPos: UnsafeMutablePointer<Float>,
                                        _ outRot: UnsafeMutablePointer<Float>) -> Int32 {
    lock.lock()
    defer { lock.unlock() }
    
    guard leftController.isValid else { return 0 }
    
    outPos[0] = leftController.position.x
    outPos[1] = leftController.position.y
    outPos[2] = leftController.position.z
    
    outRot[0] = leftController.rotation.imag.x
    outRot[1] = leftController.rotation.imag.y
    outRot[2] = leftController.rotation.imag.z
    outRot[3] = leftController.rotation.real
    
    return 1
}

// MARK: - Head tracking (6DOF via WorldTrackingProvider → DeviceAnchor)

private var headDiagCounter: Int = 0

@_cdecl("SpatialHead_GetDeviceTransform")
public func getHeadTransform(_ outPos: UnsafeMutablePointer<Float>,
                              _ outRot: UnsafeMutablePointer<Float>) -> Int32 {
    // WorldTrackingProvider.queryDeviceAnchor is synchronous and thread-safe.
    // C++ calls this each frame from the game thread.
    guard let provider = worldProvider as? WorldTrackingProvider else {
        headDiagCounter += 1
        if headDiagCounter % 300 == 1 {
            logger.warning("[6DOF-HEAD] getHeadTransform: worldProvider is nil or wrong type")
        }
        lock.lock()
        trackingStatus.headTracked = false
        lock.unlock()
        return 0
    }

    guard let anchor = provider.queryDeviceAnchor(atTimestamp: CACurrentMediaTime()) else {
        headDiagCounter += 1
        if headDiagCounter % 300 == 1 {
            logger.warning("[6DOF-HEAD] getHeadTransform: queryDeviceAnchor returned nil (auth denied or not ready)")
        }
        lock.lock()
        trackingStatus.headTracked = false
        lock.unlock()
        return 0
    }

    guard anchor.isTracked else {
        headDiagCounter += 1
        if headDiagCounter % 300 == 1 {
            logger.warning("[6DOF-HEAD] getHeadTransform: anchor exists but isTracked=false")
        }
        lock.lock()
        trackingStatus.headTracked = false
        lock.unlock()
        return 0
    }
    headDiagCounter = 0  // Reset on success

    let transform = anchor.originFromAnchorTransform
    let applePos = SIMD3<Float>(transform.columns.3.x,
                                 transform.columns.3.y,
                                 transform.columns.3.z)
    let rotMatrix = simd_float3x3(
        SIMD3<Float>(transform.columns.0.x, transform.columns.0.y, transform.columns.0.z),
        SIMD3<Float>(transform.columns.1.x, transform.columns.1.y, transform.columns.1.z),
        SIMD3<Float>(transform.columns.2.x, transform.columns.2.y, transform.columns.2.z)
    )
    let appleQuat = simd_quatf(rotMatrix)
    let uePos = appleToUnrealPosition(applePos)
    let ueRot = appleToUnrealRotation(appleQuat)

    outPos[0] = uePos.x
    outPos[1] = uePos.y
    outPos[2] = uePos.z

    outRot[0] = ueRot.imag.x
    outRot[1] = ueRot.imag.y
    outRot[2] = ueRot.imag.z
    outRot[3] = ueRot.real

    lock.lock()
    trackingStatus.headTracked = true
    lock.unlock()

    return 1
}

// MARK: - Hand tracking (6DOF via HandTrackingProvider → HandAnchor)

@_cdecl("SpatialHand_GetRightHandTransform")
public func getRightHandTransform(_ outPos: UnsafeMutablePointer<Float>,
                                   _ outRot: UnsafeMutablePointer<Float>) -> Int32 {
    lock.lock()
    defer { lock.unlock() }

    guard rightHand.isValid else { return 0 }

    outPos[0] = rightHand.position.x
    outPos[1] = rightHand.position.y
    outPos[2] = rightHand.position.z

    outRot[0] = rightHand.rotation.imag.x
    outRot[1] = rightHand.rotation.imag.y
    outRot[2] = rightHand.rotation.imag.z
    outRot[3] = rightHand.rotation.real

    return 1
}

@_cdecl("SpatialHand_GetLeftHandTransform")
public func getLeftHandTransform(_ outPos: UnsafeMutablePointer<Float>,
                                  _ outRot: UnsafeMutablePointer<Float>) -> Int32 {
    lock.lock()
    defer { lock.unlock() }

    guard leftHand.isValid else { return 0 }

    outPos[0] = leftHand.position.x
    outPos[1] = leftHand.position.y
    outPos[2] = leftHand.position.z

    outRot[0] = leftHand.rotation.imag.x
    outRot[1] = leftHand.rotation.imag.y
    outRot[2] = leftHand.rotation.imag.z
    outRot[3] = leftHand.rotation.real

    return 1
}

// MARK: - Debug status exports

@_cdecl("SpatialAccessory_GetDebugStatus")
public func getDebugStatus(_ outModuleLoaded: UnsafeMutablePointer<Int32>,
                            _ outIsSupported: UnsafeMutablePointer<Int32>,
                            _ outControllersFound: UnsafeMutablePointer<Int32>,
                            _ outSpatialFound: UnsafeMutablePointer<Int32>,
                            _ outSessionRunning: UnsafeMutablePointer<Int32>,
                            _ outAuthStatus: UnsafeMutablePointer<Int32>,
                            _ outRightTracked: UnsafeMutablePointer<Int32>,
                            _ outLeftTracked: UnsafeMutablePointer<Int32>,
                            _ outHeadTracked: UnsafeMutablePointer<Int32>) {
    lock.lock()
    defer { lock.unlock() }

    outModuleLoaded.pointee = trackingStatus.moduleLoaded ? 1 : 0
    outIsSupported.pointee = trackingStatus.isSupported ? 1 : 0
    outControllersFound.pointee = trackingStatus.controllersFound
    outSpatialFound.pointee = trackingStatus.spatialControllersFound
    outSessionRunning.pointee = trackingStatus.arkitSessionRunning ? 1 : 0
    outAuthStatus.pointee = trackingStatus.authorizationStatus
    outRightTracked.pointee = trackingStatus.rightTracked ? 1 : 0
    outLeftTracked.pointee = trackingStatus.leftTracked ? 1 : 0
    outHeadTracked.pointee = trackingStatus.headTracked ? 1 : 0
}

@_cdecl("SpatialAccessory_GetHandTrackingStatus")
public func getHandTrackingStatus(_ outHandSessionStatus: UnsafeMutablePointer<Int32>,
                                   _ outHandAnchorUpdates: UnsafeMutablePointer<Int64>) {
    lock.lock()
    defer { lock.unlock() }

    outHandSessionStatus.pointee = trackingStatus.handSessionStatus
    outHandAnchorUpdates.pointee = trackingStatus.handAnchorUpdateCount
}

// MARK: - GCController thumbstick reading
// Reads thumbstick values from Apple's GameController framework.
// PSVR2 Sense controllers on visionOS use SpatialGamepad profile (no
// extendedGamepad), so we must try physicalInputProfile as fallback.
//
// outHasGamepad values:
//   0 = no thumbstick data found
//   1 = using extendedGamepad (standard controller)
//   2 = using physicalInputProfile dpads (SpatialGamepad controllers)
//   3 = using physicalInputProfile axes (raw axis fallback)

private var hasLoggedInputElements = false

@_cdecl("SpatialAccessory_GetThumbstickValues")
public func getThumbstickValues(
    _ outLeftX: UnsafeMutablePointer<Float>,
    _ outLeftY: UnsafeMutablePointer<Float>,
    _ outRightX: UnsafeMutablePointer<Float>,
    _ outRightY: UnsafeMutablePointer<Float>,
    _ outControllerCount: UnsafeMutablePointer<Int32>,
    _ outHasGamepad: UnsafeMutablePointer<Int32>
) {
    outLeftX.pointee = 0
    outLeftY.pointee = 0
    outRightX.pointee = 0
    outRightY.pointee = 0
    outHasGamepad.pointee = 0

    let controllers = GCController.controllers()
    outControllerCount.pointee = Int32(controllers.count)

    // One-time: log every element on every connected controller so we can
    // see exactly what the PSVR2 Sense controllers expose.
    if !hasLoggedInputElements && !controllers.isEmpty {
        hasLoggedInputElements = true
        for (idx, ctrl) in controllers.enumerated() {
            let p = ctrl.physicalInputProfile
            lock.lock()
            let hand = controllerChirality[ObjectIdentifier(ctrl)] ?? .unknown
            lock.unlock()
            logger.info("[INPUT] GCController[\(idx)] cat='\(ctrl.productCategory)' vendor='\(ctrl.vendorName ?? "?")' chirality=\(hand)")
            logger.info("[INPUT]   extendedGamepad=\(ctrl.extendedGamepad != nil)")
            logger.info("[INPUT]   elements[\(p.elements.count)]: \(Array(p.elements.keys).sorted())")
            logger.info("[INPUT]   dpads[\(p.dpads.count)]: \(Array(p.dpads.keys).sorted())")
            logger.info("[INPUT]   buttons[\(p.buttons.count)]: \(Array(p.buttons.keys).sorted())")
            logger.info("[INPUT]   axes[\(p.axes.count)]: \(Array(p.axes.keys).sorted())")
        }
    }

    // Strategy 1: extendedGamepad (standard controllers — DualSense, Xbox, etc.)
    for ctrl in controllers {
        if let gp = ctrl.extendedGamepad {
            outHasGamepad.pointee = 1
            outLeftX.pointee = gp.leftThumbstick.xAxis.value
            outLeftY.pointee = gp.leftThumbstick.yAxis.value
            outRightX.pointee = gp.rightThumbstick.xAxis.value
            outRightY.pointee = gp.rightThumbstick.yAxis.value
            return
        }
    }

    // Strategy 2: physicalInputProfile dpads — aggregate across controllers.
    // PSVR2 Sense controllers appear as 2 separate GCControllers (one per hand).
    // Each exposes its single thumbstick as a dpad in physicalInputProfile.
    // Use the chirality mapping (populated from Accessory.inherentChirality)
    // to correctly assign left controller → left stick, right → right stick.
    var leftFound = false
    var rightFound = false

    for ctrl in controllers {
        let dpads = Array(ctrl.physicalInputProfile.dpads.values)
        if dpads.isEmpty { continue }
        let stick = dpads[0]

        // Look up chirality from the mapping built when Accessory was created
        lock.lock()
        let hand = controllerChirality[ObjectIdentifier(ctrl)] ?? .unknown
        lock.unlock()

        switch hand {
        case .left:
            outLeftX.pointee = stick.xAxis.value
            outLeftY.pointee = stick.yAxis.value
            leftFound = true
        case .right:
            outRightX.pointee = stick.xAxis.value
            outRightY.pointee = stick.yAxis.value
            rightFound = true
        case .unknown:
            // Fallback: assign to whichever slot is empty (left first)
            if !leftFound {
                outLeftX.pointee = stick.xAxis.value
                outLeftY.pointee = stick.yAxis.value
                leftFound = true
            } else if !rightFound {
                outRightX.pointee = stick.xAxis.value
                outRightY.pointee = stick.yAxis.value
                rightFound = true
            }
        }
        if leftFound || rightFound { outHasGamepad.pointee = 2 }
    }
    if leftFound || rightFound { return }

    // Strategy 3: raw axes from physicalInputProfile — aggregate across controllers.
    // If thumbsticks are exposed as individual axes rather than dpads.
    for ctrl in controllers {
        let axisArray = Array(ctrl.physicalInputProfile.axes.values)
        if axisArray.count < 2 { continue }

        lock.lock()
        let hand = controllerChirality[ObjectIdentifier(ctrl)] ?? .unknown
        lock.unlock()

        switch hand {
        case .left:
            outLeftX.pointee = axisArray[0].value
            outLeftY.pointee = axisArray[1].value
            leftFound = true
        case .right:
            outRightX.pointee = axisArray[0].value
            outRightY.pointee = axisArray[1].value
            rightFound = true
        case .unknown:
            if !leftFound {
                outLeftX.pointee = axisArray[0].value
                outLeftY.pointee = axisArray[1].value
                leftFound = true
            } else if !rightFound {
                outRightX.pointee = axisArray[0].value
                if axisArray.count > 1 { outRightY.pointee = axisArray[1].value }
                rightFound = true
            }
        }
        if leftFound || rightFound { outHasGamepad.pointee = 3 }
    }
}


// MARK: - GCController button reading
// Reads trigger (L2/R2) and shoulder (L1/R1) button values from GameController.
// Same multi-strategy approach as thumbstick reading above.
// PSVR2 Sense controllers appear as 2 separate GCControllers — we aggregate
// across controllers: first controller → left hand, second → right hand.
//
// outHasButtons values:
//   0 = no button data found
//   1 = using extendedGamepad
//   2 = using physicalInputProfile buttons (name-matched)
//   3 = using physicalInputProfile elements (broadest search)
//   4 = using all-buttons fallback (positional)

private var hasLoggedButtonElements = false

@_cdecl("SpatialAccessory_GetButtonValues")
public func getButtonValues(
    _ outLeftTrigger: UnsafeMutablePointer<Float>,
    _ outLeftShoulder: UnsafeMutablePointer<Float>,
    _ outRightTrigger: UnsafeMutablePointer<Float>,
    _ outRightShoulder: UnsafeMutablePointer<Float>,
    _ outHasButtons: UnsafeMutablePointer<Int32>
) {
    outLeftTrigger.pointee = 0
    outLeftShoulder.pointee = 0
    outRightTrigger.pointee = 0
    outRightShoulder.pointee = 0
    outHasButtons.pointee = 0

    let controllers = GCController.controllers()
    if controllers.isEmpty { return }

    // One-time: log every button/element on every connected controller
    if !hasLoggedButtonElements {
        hasLoggedButtonElements = true
        for (idx, ctrl) in controllers.enumerated() {
            let p = ctrl.physicalInputProfile
            lock.lock()
            let hand = controllerChirality[ObjectIdentifier(ctrl)] ?? .unknown
            lock.unlock()
            logger.info("[BTN] GCController[\(idx)] chirality=\(hand) cat='\(ctrl.productCategory)'")
            logger.info("[BTN]   buttons[\(p.buttons.count)]: \(Array(p.buttons.keys).sorted())")
            logger.info("[BTN]   axes[\(p.axes.count)]: \(Array(p.axes.keys).sorted())")
            logger.info("[BTN]   elements[\(p.elements.count)]: \(Array(p.elements.keys).sorted())")
            // Also log each button's value type and current value
            for (name, button) in p.buttons.sorted(by: { $0.key < $1.key }) {
                logger.info("[BTN]   button '\(name)' value=\(button.value) pressed=\(button.isPressed)")
            }
            // Log all elements to see if triggers hide here
            for (name, element) in p.elements.sorted(by: { $0.key < $1.key }) {
                if let btn = element as? GCControllerButtonInput {
                    logger.info("[BTN]   element '\(name)' [button] value=\(btn.value) pressed=\(btn.isPressed)")
                } else if let axis = element as? GCControllerAxisInput {
                    logger.info("[BTN]   element '\(name)' [axis] value=\(axis.value)")
                } else {
                    logger.info("[BTN]   element '\(name)' [other] type=\(type(of: element))")
                }
            }
        }
    }

    // Strategy 1: extendedGamepad (standard controllers — DualSense, Xbox, etc.)
    for ctrl in controllers {
        if let gp = ctrl.extendedGamepad {
            outHasButtons.pointee = 1
            outLeftTrigger.pointee = gp.leftTrigger.value
            outLeftShoulder.pointee = gp.leftShoulder.value
            outRightTrigger.pointee = gp.rightTrigger.value
            outRightShoulder.pointee = gp.rightShoulder.value
            return
        }
    }

    // Strategy 2: physicalInputProfile buttons aggregated across controllers.
    // Each PSVR2 Sense controller is its own GCController with its own buttons.
    // Use chirality mapping to correctly assign left vs right hand buttons.
    var leftBtnDone = false
    var rightBtnDone = false

    for ctrl in controllers {
        let buttons = ctrl.physicalInputProfile.buttons
        if buttons.isEmpty { continue }

        var triggerVal: Float = 0
        var shoulderVal: Float = 0
        var foundTrigger = false
        var foundShoulder = false

        for (name, button) in buttons {
            let lower = name.lowercased()
            // Trigger: analog index-finger pull
            if !foundTrigger && (lower.contains("trigger") || lower.contains("adaptive")) {
                triggerVal = button.value
                foundTrigger = true
            }
            // Shoulder/bumper/grip: digital button above trigger
            if !foundShoulder && (lower.contains("shoulder") || lower.contains("bumper") ||
                lower.contains("grip") || lower.contains("button l1") || lower.contains("button r1")) {
                shoulderVal = button.value
                foundShoulder = true
            }
        }

        // Fallback: if no named matches, take first two buttons alphabetically
        if !foundTrigger && !foundShoulder {
            let sorted = buttons.sorted { $0.key < $1.key }
            if sorted.count >= 1 { triggerVal = sorted[0].value.value }
            if sorted.count >= 2 { shoulderVal = sorted[1].value.value }
        } else if foundTrigger && !foundShoulder {
            // Found trigger but not shoulder — take any other non-trigger button
            for (name, button) in buttons {
                if !name.lowercased().contains("trigger") && !name.lowercased().contains("adaptive") {
                    shoulderVal = button.value
                    foundShoulder = true
                    break
                }
            }
        }

        // Assign to correct hand based on chirality mapping
        lock.lock()
        let hand = controllerChirality[ObjectIdentifier(ctrl)] ?? .unknown
        lock.unlock()

        switch hand {
        case .left:
            outLeftTrigger.pointee = triggerVal
            outLeftShoulder.pointee = shoulderVal
            leftBtnDone = true
        case .right:
            outRightTrigger.pointee = triggerVal
            outRightShoulder.pointee = shoulderVal
            rightBtnDone = true
        case .unknown:
            if !leftBtnDone {
                outLeftTrigger.pointee = triggerVal
                outLeftShoulder.pointee = shoulderVal
                leftBtnDone = true
            } else if !rightBtnDone {
                outRightTrigger.pointee = triggerVal
                outRightShoulder.pointee = shoulderVal
                rightBtnDone = true
            }
        }
        if leftBtnDone || rightBtnDone { outHasButtons.pointee = 2 }
    }

    // Strategy 3: physicalInputProfile.elements — broadest search.
    // elements is a superset of buttons+axes+dpads. If the PSVR2 triggers
    // aren't in .buttons (e.g. classified as axes or have unusual names),
    // search all elements for anything that looks like a trigger/shoulder.
    if !leftBtnDone && !rightBtnDone {
        for ctrl in controllers {
            let elements = ctrl.physicalInputProfile.elements
            if elements.isEmpty { continue }

            var triggerVal: Float = 0
            var shoulderVal: Float = 0
            var foundTrigger = false
            var foundShoulder = false

            for (name, element) in elements {
                let lower = name.lowercased()
                // Skip dpads/thumbsticks — we only want buttons/axes here
                if element is GCControllerDirectionPad { continue }

                if let btn = element as? GCControllerButtonInput {
                    if !foundTrigger && (lower.contains("trigger") || lower.contains("adaptive") ||
                        lower.contains("r2") || lower.contains("l2")) {
                        triggerVal = btn.value
                        foundTrigger = true
                    } else if !foundShoulder && (lower.contains("shoulder") || lower.contains("bumper") ||
                        lower.contains("grip") || lower.contains("r1") || lower.contains("l1")) {
                        shoulderVal = btn.value
                        foundShoulder = true
                    }
                } else if let axis = element as? GCControllerAxisInput {
                    // Triggers may show as raw axes
                    if !foundTrigger && (lower.contains("trigger") || lower.contains("adaptive") ||
                        lower.contains("r2") || lower.contains("l2")) {
                        triggerVal = axis.value
                        foundTrigger = true
                    }
                }
            }

            // Ultimate fallback: take any button inputs we can find
            if !foundTrigger && !foundShoulder {
                var btnValues: [Float] = []
                for (_, element) in elements.sorted(by: { $0.key < $1.key }) {
                    if element is GCControllerDirectionPad { continue }
                    if let btn = element as? GCControllerButtonInput {
                        btnValues.append(btn.value)
                    }
                }
                if btnValues.count >= 1 { triggerVal = btnValues[0] }
                if btnValues.count >= 2 { shoulderVal = btnValues[1] }
                if !btnValues.isEmpty { foundTrigger = true }
            }

            if !foundTrigger && !foundShoulder { continue }

            lock.lock()
            let hand = controllerChirality[ObjectIdentifier(ctrl)] ?? .unknown
            lock.unlock()

            switch hand {
            case .left:
                outLeftTrigger.pointee = triggerVal
                outLeftShoulder.pointee = shoulderVal
                leftBtnDone = true
            case .right:
                outRightTrigger.pointee = triggerVal
                outRightShoulder.pointee = shoulderVal
                rightBtnDone = true
            case .unknown:
                if !leftBtnDone {
                    outLeftTrigger.pointee = triggerVal
                    outLeftShoulder.pointee = shoulderVal
                    leftBtnDone = true
                } else if !rightBtnDone {
                    outRightTrigger.pointee = triggerVal
                    outRightShoulder.pointee = shoulderVal
                    rightBtnDone = true
                }
            }
            if leftBtnDone || rightBtnDone { outHasButtons.pointee = 3 }
        }
    }
}

// MARK: - Button diagnostics bridge
// Returns counts/info about what the Swift bridge sees so C++ can display on HUD.
// outButtonCount:  total buttons across all controllers
// outElementCount: total elements across all controllers
// outStrategy:     which strategy is currently providing button data (0-3)

@_cdecl("SpatialAccessory_GetButtonDiagnostics")
public func getButtonDiagnostics(
    _ outButtonCount: UnsafeMutablePointer<Int32>,
    _ outElementCount: UnsafeMutablePointer<Int32>,
    _ outStrategy: UnsafeMutablePointer<Int32>
) {
    outButtonCount.pointee = 0
    outElementCount.pointee = 0
    outStrategy.pointee = 0

    let controllers = GCController.controllers()
    for ctrl in controllers {
        outButtonCount.pointee += Int32(ctrl.physicalInputProfile.buttons.count)
        outElementCount.pointee += Int32(ctrl.physicalInputProfile.elements.count)
        if ctrl.extendedGamepad != nil {
            outStrategy.pointee = 1
        }
    }
    // If no extendedGamepad, check which strategy would fire
    if outStrategy.pointee == 0 && outButtonCount.pointee > 0 {
        outStrategy.pointee = 2
    } else if outStrategy.pointee == 0 && outElementCount.pointee > 0 {
        outStrategy.pointee = 3
    }
}
