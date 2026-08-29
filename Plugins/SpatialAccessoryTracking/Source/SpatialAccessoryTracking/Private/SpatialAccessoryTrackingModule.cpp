// SpatialAccessoryTrackingModule.cpp
// Module startup/shutdown for the SpatialAccessoryTracking plugin.
//
// Registers the IMotionController for PSVR2 controllers AND wraps the
// engine's IXRTrackingSystem so our head-tracking bridge feeds HMD pose.

#include "Containers/Ticker.h"
#include "DrawDebugHelpers.h"
#include "HAL/IConsoleManager.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Features/IModularFeatures.h"
#include "GameFramework/PlayerController.h"
#include "HeadMountedDisplayTypes.h"
#include "IHandTracker.h"
#include "IMotionController.h"
#include "IXRTrackingSystem.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CoreDelegates.h"
#include "Modules/ModuleManager.h"
#include "SpatialAccessoryMotionController.h"
#include "SpatialXRTrackingWrapper.h"

#if WITH_SPATIAL_ACCESSORY_TRACKING
#include "SpatialAccessoryBridge.h"
#endif

#define LOCTEXT_NAMESPACE "SpatialAccessoryTracking"

class FSpatialAccessoryTrackingModule : public IModuleInterface {
public:
  virtual void StartupModule() override {
    // This log fires unconditionally — proves the module loaded
    UE_LOG(
        LogTemp, Warning,
        TEXT("########## [6DOF] MODULE STARTUP (WITH_SPATIAL=%d) ##########"),
        WITH_SPATIAL_ACCESSORY_TRACKING);

#if WITH_SPATIAL_ACCESSORY_TRACKING
    // Register our motion controller as a modular feature.
    IModularFeatures::Get().RegisterModularFeature(
        IMotionController::GetModularFeatureName(), &MotionController);

    // Start the ARKit AccessoryTrackingProvider + WorldTrackingProvider
    SpatialAccessory_StartTracking();

    UE_LOG(LogTemp, Warning,
           TEXT("[6DOF] IMotionController registered, tracking started"));

    // Defer XR system wrapping until after the engine is fully initialised
    // (GEngine->XRSystem is not populated yet during StartupModule).
    // UE 5.8: the OnPostEngineInit member delegate is deprecated in favour of the
    // GetOnPostEngineInit() accessor, and the deprecation warns it will stop compiling in the
    // next release. Same delegate, same semantics.
    PostEngineInitHandle = FCoreDelegates::GetOnPostEngineInit().AddRaw(
        this, &FSpatialAccessoryTrackingModule::OnPostEngineInit);
#endif

    // Register a ticker for on-screen debug display
    // This is OUTSIDE the #if so it always fires
    TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
        FTickerDelegate::CreateRaw(this,
                                   &FSpatialAccessoryTrackingModule::Tick),
        1.0f);
  }

  virtual void ShutdownModule() override {
    UE_LOG(LogTemp, Warning, TEXT("[6DOF] MODULE SHUTDOWN"));

    if (TickerHandle.IsValid()) {
      FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
    }

#if WITH_SPATIAL_ACCESSORY_TRACKING
    // Restore the original XR system before we destroy the wrapper.
    if (XRWrapper.IsValid() && GEngine) {
      UE_LOG(LogTemp, Warning,
             TEXT("[6DOF] Restoring original XRSystem on shutdown"));
      GEngine->XRSystem = XRWrapper->GetInner();
      XRWrapper.Reset();
    }

    FCoreDelegates::GetOnPostEngineInit().Remove(PostEngineInitHandle);

    IModularFeatures::Get().UnregisterModularFeature(
        IMotionController::GetModularFeatureName(), &MotionController);
    SpatialAccessory_StopTracking();
#endif
  }

private:
#if WITH_SPATIAL_ACCESSORY_TRACKING
  FSpatialAccessoryMotionController MotionController;
  TSharedPtr<FSpatialXRTrackingWrapper, ESPMode::ThreadSafe> XRWrapper;
  FDelegateHandle PostEngineInitHandle;
#endif
  FTSTicker::FDelegateHandle TickerHandle;

#if WITH_SPATIAL_ACCESSORY_TRACKING
  void OnPostEngineInit() {
    if (!GEngine) {
      UE_LOG(LogTemp, Error,
             TEXT("[6DOF] OnPostEngineInit: GEngine is null!"));
      return;
    }

    if (!GEngine->XRSystem.IsValid()) {
      UE_LOG(LogTemp, Warning,
             TEXT("[6DOF] OnPostEngineInit: GEngine->XRSystem is null — "
                  "no XR system to wrap."));
      return;
    }

    UE_LOG(LogTemp, Warning,
           TEXT("[6DOF] Wrapping XRSystem '%s' with "
                "SpatialXRTrackingWrapper for head tracking"),
           *GEngine->XRSystem->GetSystemName().ToString());

    XRWrapper = MakeShared<FSpatialXRTrackingWrapper, ESPMode::ThreadSafe>(
        GEngine->XRSystem);
    GEngine->XRSystem = XRWrapper;

    UE_LOG(LogTemp, Warning,
           TEXT("[6DOF] XRSystem successfully wrapped — head tracking active"));
  }
#endif

  bool Tick(float DeltaTime) {
    // Find the game world
    UWorld *World = nullptr;
    if (GEngine) {
      for (const FWorldContext &Ctx : GEngine->GetWorldContexts()) {
        if (Ctx.World() && Ctx.WorldType == EWorldType::Game) {
          World = Ctx.World();
          break;
        }
      }
    }
    if (!World)
      return true;

    // ── Debug HUD disabled — all DrawDebugString calls below are skipped.
    //    Set to false to re-enable on-screen diagnostics.
    constexpr bool bShowDebugHUD = false;
    if (!bShowDebugHUD)
      return true;

    // Use position BELOW the game's debug text (game uses 170 Z, we start at
    // 50)
    FVector Base(200.f, 0.f, 50.f);
    float LineGap = 12.f;

#if WITH_SPATIAL_ACCESSORY_TRACKING
    // Query debug status from Swift bridge
    int32 ModuleLoaded = 0, IsSupported = 0, ControllersFound = 0;
    int32 SpatialFound = 0, SessionRunning = 0, AuthStatus = 0;
    int32 RightTracked = 0, LeftTracked = 0, HeadTracked = 0;

    SpatialAccessory_GetDebugStatus(
        &ModuleLoaded, &IsSupported, &ControllersFound, &SpatialFound,
        &SessionRunning, &AuthStatus, &RightTracked, &LeftTracked,
        &HeadTracked);

    FString AuthStr;
    switch (AuthStatus) {
    case 0:
      AuthStr = TEXT("?");
      break;
    case 1:
      AuthStr = TEXT("OK");
      break;
    case 2:
      AuthStr = TEXT("DENIED");
      break;
    default:
      AuthStr = TEXT("?");
      break;
    }

    DrawDebugString(
        World, Base,
        FString::Printf(
            TEXT("6DOF: Mod:%d Sup:%d GC:%d Sp:%d ARK:%d Auth:%s R:%d L:%d "
                 "H:%d W:%d"),
            ModuleLoaded, IsSupported, ControllersFound, SpatialFound,
            SessionRunning, *AuthStr, RightTracked, LeftTracked, HeadTracked,
            XRWrapper.IsValid() ? 1 : 0),
        nullptr, FColor::Yellow, 1.5f, true, 1.5f);

    // Show controller positions AND rotations if tracked (from Swift bridge)
    int32 DebugLine = 1;
    if (RightTracked) {
      float Pos[3], Rot[4];
      if (SpatialAccessory_GetRightControllerTransform(Pos, Rot)) {
        FRotator EulerRot = FQuat(Rot[0], Rot[1], Rot[2], Rot[3]).Rotator();
        DrawDebugString(
            World, Base - FVector(0, 0, LineGap * DebugLine),
            FString::Printf(TEXT("R:(%.0f,%.0f,%.0f) P:%.0f Y:%.0f R:%.0f"),
                            Pos[0], Pos[1], Pos[2],
                            EulerRot.Pitch, EulerRot.Yaw, EulerRot.Roll),
            nullptr, FColor::Green, 1.5f, true, 1.5f);
        DebugLine++;
      }
    }
    if (LeftTracked) {
      float Pos[3], Rot[4];
      if (SpatialAccessory_GetLeftControllerTransform(Pos, Rot)) {
        FRotator EulerRot = FQuat(Rot[0], Rot[1], Rot[2], Rot[3]).Rotator();
        DrawDebugString(
            World, Base - FVector(0, 0, LineGap * DebugLine),
            FString::Printf(TEXT("L:(%.0f,%.0f,%.0f) P:%.0f Y:%.0f R:%.0f"),
                            Pos[0], Pos[1], Pos[2],
                            EulerRot.Pitch, EulerRot.Yaw, EulerRot.Roll),
            nullptr, FColor::Green, 1.5f, true, 1.5f);
        DebugLine++;
      }
    }

    // Show per-hand grip offset CVar values
    {
      auto GetCVar = [](const TCHAR *Name) -> float {
        IConsoleVariable *CV = IConsoleManager::Get().FindConsoleVariable(Name);
        return CV ? CV->GetFloat() : 0.f;
      };
      float LP = GetCVar(TEXT("SpatialAccessory.Left.GripOffsetPitch"));
      float LY = GetCVar(TEXT("SpatialAccessory.Left.GripOffsetYaw"));
      float LR = GetCVar(TEXT("SpatialAccessory.Left.GripOffsetRoll"));
      float RP = GetCVar(TEXT("SpatialAccessory.Right.GripOffsetPitch"));
      float RY = GetCVar(TEXT("SpatialAccessory.Right.GripOffsetYaw"));
      float RR = GetCVar(TEXT("SpatialAccessory.Right.GripOffsetRoll"));
      float GP = GetCVar(TEXT("SpatialAccessory.GunOffsetPitch"));
      float GY = GetCVar(TEXT("SpatialAccessory.GunOffsetYaw"));
      float GR = GetCVar(TEXT("SpatialAccessory.GunOffsetRoll"));
      DrawDebugString(
          World, Base - FVector(0, 0, LineGap * DebugLine),
          FString::Printf(TEXT("GripL: P:%.0f Y:%.0f R:%.0f  GripR: P:%.0f Y:%.0f R:%.0f"),
                          LP, LY, LR, RP, RY, RR),
          nullptr, FColor::Silver, 1.5f, true, 1.5f);
      DebugLine++;
      DrawDebugString(
          World, Base - FVector(0, 0, LineGap * DebugLine),
          FString::Printf(TEXT("Gun: P:%.0f Y:%.0f R:%.0f"), GP, GY, GR),
          nullptr, FColor::Silver, 1.5f, true, 1.5f);
      DebugLine++;
    }

    // Show raw GCController thumbstick values (direct from Apple framework)
    // GP: 0=none, 1=extGamepad, 2=physProfile dpads, 3=physProfile axes
    {
      float StickLX = 0, StickLY = 0, StickRX = 0, StickRY = 0;
      int32 StickGCCount = 0, StickHasGamepad = 0;
      SpatialAccessory_GetThumbstickValues(&StickLX, &StickLY, &StickRX,
                                            &StickRY, &StickGCCount,
                                            &StickHasGamepad);
      const TCHAR *GPSrc = TEXT("none");
      FColor StickColor = FColor::Red;
      switch (StickHasGamepad) {
      case 1: GPSrc = TEXT("ext");  StickColor = FColor::Green;  break;
      case 2: GPSrc = TEXT("dpad"); StickColor = FColor::Yellow; break;
      case 3: GPSrc = TEXT("axis"); StickColor = FColor::Yellow; break;
      }
      DrawDebugString(
          World, Base - FVector(0, 0, LineGap * DebugLine),
          FString::Printf(
              TEXT("Stick(%s): L(%.2f,%.2f) R(%.2f,%.2f)"),
              GPSrc, StickLX, StickLY, StickRX, StickRY),
          nullptr, StickColor, 1.5f, true, 1.5f);
      DebugLine++;
    }

    // Show raw GCController button values (trigger + shoulder per hand)
    {
      float BtnLT = 0, BtnLS = 0, BtnRT = 0, BtnRS = 0;
      int32 BtnHas = 0;
      SpatialAccessory_GetButtonValues(&BtnLT, &BtnLS, &BtnRT, &BtnRS, &BtnHas);
      FColor BtnColor = BtnHas > 0 ? FColor::Yellow : FColor::Red;
      DrawDebugString(
          World, Base - FVector(0, 0, LineGap * DebugLine),
          FString::Printf(
              TEXT("Btn: L1:%.1f L2:%.1f R1:%.1f R2:%.1f (%s)"),
              BtnLS, BtnLT, BtnRS, BtnRT,
              BtnHas == 1 ? TEXT("ext") : BtnHas == 2 ? TEXT("phys") : TEXT("none")),
          nullptr, BtnColor, 1.5f, true, 1.5f);
      DebugLine++;
    }

    // Show Swift bridge HAND tracking status (ARKit HandTrackingProvider)
    {
      float RPos[3], RRot[4], LPos[3], LRot[4];
      bool bBridgeHandR = (SpatialHand_GetRightHandTransform(RPos, RRot) != 0);
      bool bBridgeHandL = (SpatialHand_GetLeftHandTransform(LPos, LRot) != 0);

      // Query hand tracking session diagnostic
      int32 HandSessionStatus = 0;
      int64 HandAnchorUpdates = 0;
      SpatialAccessory_GetHandTrackingStatus(&HandSessionStatus,
                                              &HandAnchorUpdates);

      // Session status: 0=not started, 1=combined running, 2=combined failed, 3=both failed
      const TCHAR *HandSessionStr = TEXT("?");
      FColor HandColor = FColor::Emerald;
      switch (HandSessionStatus) {
      case 0:
        HandSessionStr = TEXT("INIT");
        break;
      case 1:
        HandSessionStr = TEXT("OK");
        break;
      case 2:
        HandSessionStr = TEXT("FAIL>W");
        HandColor = FColor::Red;
        break;
      case 3:
        HandSessionStr = TEXT("FAIL");
        HandColor = FColor::Red;
        break;
      }

      DrawDebugString(
          World, Base - FVector(0, 0, LineGap * DebugLine),
          FString::Printf(TEXT("BridgeHand: R:%d L:%d Sess:%s Upd:%lld"),
                          bBridgeHandR ? 1 : 0, bBridgeHandL ? 1 : 0,
                          HandSessionStr, HandAnchorUpdates),
          nullptr, HandColor, 1.5f, true, 1.5f);
      DebugLine++;

      if (bBridgeHandR) {
        DrawDebugString(
            World, Base - FVector(0, 0, LineGap * DebugLine),
            FString::Printf(TEXT("BH-R:(%.0f,%.0f,%.0f)"), RPos[0], RPos[1],
                            RPos[2]),
            nullptr, FColor::Emerald, 1.5f, true, 1.5f);
        DebugLine++;
      }
      if (bBridgeHandL) {
        DrawDebugString(
            World, Base - FVector(0, 0, LineGap * DebugLine),
            FString::Printf(TEXT("BH-L:(%.0f,%.0f,%.0f)"), LPos[0], LPos[1],
                            LPos[2]),
            nullptr, FColor::Emerald, 1.5f, true, 1.5f);
        DebugLine++;
      }
    }

    // Enumerate ALL registered IMotionControllers and show which ones respond
    {
      TArray<IMotionController *> AllMCs =
          IModularFeatures::Get()
              .GetModularFeatureImplementations<IMotionController>(
                  IMotionController::GetModularFeatureName());

      static int32 EnumLogCount = 0;
      bool bLogThisTick = (++EnumLogCount % 5 == 1); // Log every ~5 seconds

      FString MCInfo = FString::Printf(TEXT("MCs:%d"), AllMCs.Num());

      for (int32 i = 0; i < AllMCs.Num(); ++i) {
        IMotionController *MC = AllMCs[i];
        if (!MC)
          continue;

        FRotator TestOriR, TestOriL;
        FVector TestPosR = FVector::ZeroVector;
        FVector TestPosL = FVector::ZeroVector;
        bool bRightOK = MC->GetControllerOrientationAndPosition(
            0, FName(TEXT("Right")), TestOriR, TestPosR, 100.f);
        bool bLeftOK = MC->GetControllerOrientationAndPosition(
            0, FName(TEXT("Left")), TestOriL, TestPosL, 100.f);

        FName TypeName = MC->GetMotionControllerDeviceTypeName();

        MCInfo += FString::Printf(TEXT(" [%d:%s R:%d L:%d]"), i,
                                  *TypeName.ToString(), bRightOK ? 1 : 0,
                                  bLeftOK ? 1 : 0);

        if (bLogThisTick) {
          UE_LOG(
              LogTemp, Warning,
              TEXT("[MC-ENUM] Controller[%d] Type='%s' Right=%s(%.0f,%.0f,%.0f)"
                   " Left=%s(%.0f,%.0f,%.0f)"),
              i, *TypeName.ToString(),
              bRightOK ? TEXT("OK") : TEXT("--"), TestPosR.X, TestPosR.Y,
              TestPosR.Z, bLeftOK ? TEXT("OK") : TEXT("--"), TestPosL.X,
              TestPosL.Y, TestPosL.Z);
        }
      }

      DrawDebugString(World, Base - FVector(0, 0, LineGap * DebugLine), MCInfo,
                      nullptr, FColor::Magenta, 1.5f, true, 1.5f);
      DebugLine++;
    }

    // Show XR system hand tracking fallback status
    if (GEngine && GEngine->XRSystem.IsValid()) {
      IXRTrackingSystem *XRSys = GEngine->XRSystem.Get();

      // Check right hand via XR system
      bool bXRRight = false;
      FVector XRRightPos = FVector::ZeroVector;
      {
        FXRMotionControllerState RState;
        XRSys->GetMotionControllerState(nullptr,
                                        EXRSpaceType::UnrealWorldSpace,
                                        EControllerHand::Right,
                                        EXRControllerPoseType::Grip, RState);
        if (RState.bValid &&
            RState.TrackingStatus == ETrackingStatus::Tracked) {
          bXRRight = true;
          XRRightPos = RState.ControllerLocation;
        } else {
          // Try hand tracking
          FXRHandTrackingState RHand;
          XRSys->GetHandTrackingState(nullptr,
                                      EXRSpaceType::UnrealWorldSpace,
                                      EControllerHand::Right, RHand);
          if (RHand.bValid &&
              RHand.TrackingStatus == ETrackingStatus::Tracked &&
              RHand.HandKeyLocations.Num() > 0) {
            bXRRight = true;
            XRRightPos = RHand.HandKeyLocations[0]; // Palm
          }
        }
      }

      // Check left hand via XR system
      bool bXRLeft = false;
      FVector XRLeftPos = FVector::ZeroVector;
      {
        FXRMotionControllerState LState;
        XRSys->GetMotionControllerState(nullptr,
                                        EXRSpaceType::UnrealWorldSpace,
                                        EControllerHand::Left,
                                        EXRControllerPoseType::Grip, LState);
        if (LState.bValid &&
            LState.TrackingStatus == ETrackingStatus::Tracked) {
          bXRLeft = true;
          XRLeftPos = LState.ControllerLocation;
        } else {
          FXRHandTrackingState LHand;
          XRSys->GetHandTrackingState(nullptr,
                                      EXRSpaceType::UnrealWorldSpace,
                                      EControllerHand::Left, LHand);
          if (LHand.bValid &&
              LHand.TrackingStatus == ETrackingStatus::Tracked &&
              LHand.HandKeyLocations.Num() > 0) {
            bXRLeft = true;
            XRLeftPos = LHand.HandKeyLocations[0]; // Palm
          }
        }
      }

      DrawDebugString(
          World, Base - FVector(0, 0, LineGap * DebugLine),
          FString::Printf(TEXT("XR: R:%d L:%d"), bXRRight ? 1 : 0,
                          bXRLeft ? 1 : 0),
          nullptr, FColor::Orange, 1.5f, true, 1.5f);
      DebugLine++;

      if (bXRRight) {
        DrawDebugString(
            World, Base - FVector(0, 0, LineGap * DebugLine),
            FString::Printf(TEXT("XR-R:(%.0f,%.0f,%.0f)"), XRRightPos.X,
                            XRRightPos.Y, XRRightPos.Z),
            nullptr, FColor::Orange, 1.5f, true, 1.5f);
        DebugLine++;
      }
      if (bXRLeft) {
        DrawDebugString(
            World, Base - FVector(0, 0, LineGap * DebugLine),
            FString::Printf(TEXT("XR-L:(%.0f,%.0f,%.0f)"), XRLeftPos.X,
                            XRLeftPos.Y, XRLeftPos.Z),
            nullptr, FColor::Orange, 1.5f, true, 1.5f);
        DebugLine++;
      }

      // Show actual head pose from GetCurrentPose (what the game uses)
      FQuat HeadOri;
      FVector HeadPos;
      if (XRSys->GetCurrentPose(IXRTrackingSystem::HMDDeviceId, HeadOri,
                                HeadPos)) {
        DrawDebugString(
            World, Base - FVector(0, 0, LineGap * DebugLine),
            FString::Printf(TEXT("Head:(%.0f,%.0f,%.0f)"), HeadPos.X,
                            HeadPos.Y, HeadPos.Z),
            nullptr, FColor::Cyan, 1.5f, true, 1.5f);
        DebugLine++;
      }
    }

    // ---- IHandTracker direct query (same path as our IMotionController) ----
    // This is OUTSIDE the XRSystem check — IHandTracker is an independent
    // modular feature that doesn't require GEngine->XRSystem.
    {
      TArray<IHandTracker *> HandTrackers =
          IModularFeatures::Get()
              .GetModularFeatureImplementations<IHandTracker>(
                  IHandTracker::GetModularFeatureName());

      bool bHTRight = false, bHTLeft = false;
      FVector HTRightPos = FVector::ZeroVector;
      FVector HTLeftPos = FVector::ZeroVector;
      int32 NumValid = 0;

      for (IHandTracker *HT : HandTrackers) {
        if (!HT)
          continue;
        bool bValid = HT->IsHandTrackingStateValid();
        if (bValid)
          NumValid++;

        if (bValid && !bHTRight) {
          FTransform PalmT;
          float PalmR = 0.f;
          if (HT->GetKeypointState(EControllerHand::Right,
                                   EHandKeypoint::Palm, PalmT, PalmR)) {
            bHTRight = true;
            HTRightPos = PalmT.GetLocation();
          }
        }
        if (bValid && !bHTLeft) {
          FTransform PalmT;
          float PalmR = 0.f;
          if (HT->GetKeypointState(EControllerHand::Left,
                                   EHandKeypoint::Palm, PalmT, PalmR)) {
            bHTLeft = true;
            HTLeftPos = PalmT.GetLocation();
          }
        }
      }

      DrawDebugString(
          World, Base - FVector(0, 0, LineGap * DebugLine),
          FString::Printf(TEXT("HT:%d/%d R:%d L:%d"), NumValid,
                          HandTrackers.Num(), bHTRight ? 1 : 0,
                          bHTLeft ? 1 : 0),
          nullptr, FColor::White, 1.5f, true, 1.5f);
      DebugLine++;

      if (bHTRight) {
        DrawDebugString(
            World, Base - FVector(0, 0, LineGap * DebugLine),
            FString::Printf(TEXT("HT-R:(%.0f,%.0f,%.0f)"), HTRightPos.X,
                            HTRightPos.Y, HTRightPos.Z),
            nullptr, FColor::White, 1.5f, true, 1.5f);
        DebugLine++;
      }
      if (bHTLeft) {
        DrawDebugString(
            World, Base - FVector(0, 0, LineGap * DebugLine),
            FString::Printf(TEXT("HT-L:(%.0f,%.0f,%.0f)"), HTLeftPos.X,
                            HTLeftPos.Y, HTLeftPos.Z),
            nullptr, FColor::White, 1.5f, true, 1.5f);
        DebugLine++;
      }
    }
#else
    // If not on VisionOS, still show that the module loaded
    DrawDebugString(World, Base, TEXT("6DOF: NOT VISIONOS BUILD"), nullptr,
                    FColor::Red, 1.5f, true, 1.5f);
#endif

    return true; // Keep ticking
  }
};

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FSpatialAccessoryTrackingModule, SpatialAccessoryTracking)
