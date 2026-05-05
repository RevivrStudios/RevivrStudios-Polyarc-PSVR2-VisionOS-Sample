// GamepadInputSetup.cpp
// Controller-tracked grab system for PolyArc VRTemplate on visionOS.
//
// LEFT STICK   -> smooth locomotion (camera-relative, 200cm/s)
// RIGHT STICK  -> snap turn 30deg (always active, even while holding)
// R1 (press)   -> grab nearest GrabbableActor in 80cm / press again = release
// L1 (press)   -> same for left hand
// HELD OBJECTS -> attached to MotionControllerComponent, track with hands

#include "GamepadInputSetup.h"

#include "Components/SkeletalMeshComponent.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Kismet/GameplayStatics.h"
#include "Animation/AnimInstance.h"
#include "TimerManager.h"
#include "UObject/FieldIterator.h"
#include "UObject/UnrealType.h"

// On visionOS, PSVR2 controllers use SpatialGamepad (no extendedGamepad), so
// Unreal's native AppleControllerInterface never sees the thumbsticks. We read
// them directly via our Swift bridge and use the values for movement.
#if PLATFORM_VISIONOS
extern "C" {
  void SpatialAccessory_GetThumbstickValues(float *outLeftX, float *outLeftY,
                                             float *outRightX, float *outRightY,
                                             int *outControllerCount,
                                             int *outHasGamepad);
  void SpatialAccessory_GetButtonValues(float *outLeftTrigger, float *outLeftShoulder,
                                         float *outRightTrigger, float *outRightShoulder,
                                         int *outHasButtons);
  void SpatialAccessory_GetButtonDiagnostics(int *outButtonCount, int *outElementCount,
                                              int *outStrategy);
}
#endif

// Fixed hand offsets in pawn-local space (cm): forward, side (+right/-left), up
static const FVector RightHandOffset(60.f,  35.f, 120.f);
static const FVector LeftHandOffset (60.f, -35.f, 120.f);

// Gun rotation offset CVars — adjusts the grabbed weapon's rotation relative
// to the grip component after SnapToTarget attachment.  Aligns the barrel
// with the finger direction.  Tweak at runtime with console commands.
static TAutoConsoleVariable<float> CVarGunOffsetPitch(
    TEXT("SpatialAccessory.GunOffsetPitch"), 90.f,
    TEXT("Pitch offset for grabbed gun (degrees)"), ECVF_Default);
static TAutoConsoleVariable<float> CVarGunOffsetYaw(
    TEXT("SpatialAccessory.GunOffsetYaw"), 0.f,
    TEXT("Yaw offset for grabbed gun (degrees)"), ECVF_Default);
static TAutoConsoleVariable<float> CVarGunOffsetRoll(
    TEXT("SpatialAccessory.GunOffsetRoll"), 0.f,
    TEXT("Roll offset for grabbed gun (degrees)"), ECVF_Default);

// Debug logging — writes to UE_LOG only (HUD display disabled).
static void ScreenMsg(const FString &M) {
  UE_LOG(LogTemp, Warning, TEXT("[GamepadSetup] %s"), *M);
}

// ---------------------------------------------------------------------------
void UGamepadInputSetup::Initialize(FSubsystemCollectionBase &Collection) {
  Super::Initialize(Collection);
  GamepadIMC     = nullptr;
  HeldActorRight = nullptr;
  HeldActorLeft  = nullptr;
  bSetupDone     = false;
  bSnapTurnReady = true;
  bR1WasPressed  = false;
  bL1WasPressed  = false;
  bR2DirectFired = false;
  bL2DirectFired = false;
  bLoggedFuncsR  = false;
  bLoggedFuncsL  = false;

  ScreenMsg(TEXT("=== SUBSYSTEM INITIALIZED ==="));

  if (UWorld *W = GetGameInstance()->GetWorld()) {
    FTimerHandle T;
    W->GetTimerManager().SetTimer(
        T, FTimerDelegate::CreateUObject(this, &UGamepadInputSetup::SetupGamepadMappings),
        3.0f, false);
  }
  // 60hz for smooth movement and aim tracking
  TickHandle = FTSTicker::GetCoreTicker().AddTicker(
      FTickerDelegate::CreateUObject(this, &UGamepadInputSetup::Tick), 0.016f);
}

void UGamepadInputSetup::Deinitialize() {
  FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
  Super::Deinitialize();
}

// ---------------------------------------------------------------------------
bool UGamepadInputSetup::IsGrabbableActor(AActor *A) {
  if (!A) return false;
  for (UActorComponent *C : A->GetComponents())
    if (C && C->GetClass()->GetName().Contains(TEXT("Grab")))
      return true;
  return false;
}

// ---------------------------------------------------------------------------
// Find the MotionController grip component on the pawn for the given hand.
// VRPawn has "MotionControllerRightGrip" and "MotionControllerLeftGrip"
// components that track via SpatialAccessoryMotionController on visionOS.
static USceneComponent *FindGripComponent(APawn *Pawn, bool bRight) {
  if (!Pawn || !Pawn->GetRootComponent()) return nullptr;
  const FString Target = bRight ? TEXT("MotionControllerRightGrip")
                                : TEXT("MotionControllerLeftGrip");
  TArray<USceneComponent*> Children;
  Pawn->GetRootComponent()->GetChildrenComponents(true, Children);
  for (USceneComponent *C : Children)
    if (C->GetName() == Target)
      return C;
  return nullptr;
}

// ---------------------------------------------------------------------------
void UGamepadInputSetup::TryGrab(bool bRight, APlayerController *PC, APawn *Pawn) {
  AActor *&Slot = bRight ? HeldActorRight : HeldActorLeft;

  // Second press on same button = release
  if (Slot) { ReleaseGrab(bRight, Pawn); return; }

  // Find the motion controller grip component for this hand
  USceneComponent *GripComp = FindGripComponent(Pawn, bRight);

  // Search near the hand (grip component) if available, otherwise near the pawn
  FVector Center = GripComp ? GripComp->GetComponentLocation()
                            : Pawn->GetActorLocation();

  UWorld *World = Pawn->GetWorld();

  // Sphere overlap for nearby grabbable actors
  TArray<FOverlapResult> Hits;
  FCollisionShape Sphere; Sphere.SetSphere(80.f); // 80cm reach
  FCollisionQueryParams Params;
  Params.AddIgnoredActor(Pawn);
  if (HeldActorLeft)  Params.AddIgnoredActor(HeldActorLeft);
  if (HeldActorRight) Params.AddIgnoredActor(HeldActorRight);

  World->OverlapMultiByObjectType(
      Hits, Center, FQuat::Identity,
      FCollisionObjectQueryParams(FCollisionObjectQueryParams::AllDynamicObjects),
      Sphere, Params);

  // Find nearest actor that has a GrabComponent
  float   BestDSq = FLT_MAX;
  AActor *Best    = nullptr;
  for (auto &H : Hits) {
    AActor *A = H.GetActor();
    if (!A || !IsGrabbableActor(A)) continue;
    float D = FVector::DistSquared(A->GetActorLocation(), Center);
    if (D < BestDSq) { BestDSq = D; Best = A; }
  }

  if (!Best) {
    ScreenMsg(FString::Printf(TEXT("%s: nothing in 80cm"), bRight ? TEXT("R1") : TEXT("L1")));
    return;
  }

  // Disable physics before attaching (required or UE ignores the attach)
  for (UActorComponent *C : Best->GetComponents())
    if (UPrimitiveComponent *P = Cast<UPrimitiveComponent>(C)) {
      P->SetSimulatePhysics(false);
      P->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    }

  if (GripComp) {
    // Attach to the motion controller grip component — object tracks the hand.
    // SnapToTarget places the actor root at the grip origin with matching
    // orientation. Then we offset by the actor's GrabComponent so the grip
    // point on the weapon (not the actor origin) lines up with the hand.
    FAttachmentTransformRules Rules(EAttachmentRule::SnapToTarget,
                                    EAttachmentRule::SnapToTarget,
                                    EAttachmentRule::KeepWorld, true);
    Best->AttachToComponent(GripComp, Rules);

    // Find the GrabComponent on the weapon — it defines where the hand holds
    // the weapon and which direction the barrel faces relative to the grip.
    USceneComponent *ActorGrabComp = nullptr;
    for (UActorComponent *C : Best->GetComponents()) {
      USceneComponent *SC = Cast<USceneComponent>(C);
      if (SC && SC->GetClass()->GetName().Contains(TEXT("Grab"))) {
        ActorGrabComp = SC;
        break;
      }
    }

    if (ActorGrabComp) {
      // Offset the actor so the GrabComponent aligns with the grip.
      // After SnapToTarget the actor root is at the grip with identity
      // relative transform. The GrabComponent sits at some offset within
      // the actor. Applying the inverse of that offset moves the actor
      // so the GrabComponent lands exactly on the grip point.
      FTransform GrabLocal = ActorGrabComp->GetRelativeTransform();
      FTransform Correction = GrabLocal.Inverse();
      Best->SetActorRelativeLocation(Correction.GetLocation());
      Best->SetActorRelativeRotation(Correction.GetRotation().Rotator());
      ScreenMsg(FString::Printf(TEXT("GrabComp '%s' → offset applied"),
                                *ActorGrabComp->GetName()));
    } else {
      // Fallback: use CVar offset if no GrabComponent found
      const FRotator GunOffset(
          CVarGunOffsetPitch.GetValueOnAnyThread(),
          CVarGunOffsetYaw.GetValueOnAnyThread(),
          CVarGunOffsetRoll.GetValueOnAnyThread());
      if (!GunOffset.IsNearlyZero())
        Best->SetActorRelativeRotation(GunOffset);
    }
  } else {
    // Fallback: attach to pawn with fixed hand offsets (no motion controllers)
    const FVector &HandOff = bRight ? RightHandOffset : LeftHandOffset;
    FAttachmentTransformRules Rules(EAttachmentRule::KeepWorld,
                                    EAttachmentRule::KeepWorld,
                                    EAttachmentRule::KeepWorld, false);
    Best->AttachToActor(Pawn, Rules);
    Best->SetActorRelativeLocation(HandOff);
    Best->SetActorRelativeRotation(FRotator::ZeroRotator);
  }

  Slot = Best;

  // Tell the grabbed actor which hand holds it — the Pistol blueprint uses
  // bRightHand to decide whether to listen for IA_Shoot_Right or IA_Shoot_Left.
  {
    UClass *Cls = Best->GetClass();
    FBoolProperty *RHP = CastField<FBoolProperty>(Cls->FindPropertyByName(FName(TEXT("bRightHand"))));
    if (!RHP) RHP = CastField<FBoolProperty>(Cls->FindPropertyByName(FName(TEXT("Right Hand"))));
    if (RHP) {
      RHP->SetPropertyValue_InContainer(Best, bRight);
      ScreenMsg(FString::Printf(TEXT("  bRightHand = %s"), bRight ? TEXT("true") : TEXT("false")));
    } else {
      ScreenMsg(TEXT("  bRightHand property NOT found"));
    }
  }

  // NOTE: We do NOT call EnableInput on grab. The firing code toggles
  // EnableInput per-frame based on which trigger is pressed, ensuring
  // InjectInputForAction only reaches the correct hand's pistol.

  // Reset function logging flag so we re-log the new actor's functions
  if (bRight) { bLoggedFuncsR = false; bR2DirectFired = false; }
  else        { bLoggedFuncsL = false; bL2DirectFired = false; }

  ScreenMsg(FString::Printf(TEXT("%s: grabbed %s%s"), bRight ? TEXT("R1") : TEXT("L1"),
                            *Best->GetName(),
                            GripComp ? TEXT(" → MC") : TEXT(" → Pawn")));
}

// ---------------------------------------------------------------------------
void UGamepadInputSetup::ReleaseGrab(bool bRight, APawn *Pawn) {
  AActor *&Slot = bRight ? HeldActorRight : HeldActorLeft;
  if (!Slot) return;

  // Disable input on the weapon so it stops receiving fire actions
  if (UWorld *W = Pawn->GetWorld()) {
    APlayerController *PC = UGameplayStatics::GetPlayerController(W, 0);
    if (PC) Slot->DisableInput(PC);
  }

  // Detach first, then re-enable physics so it falls naturally
  Slot->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
  for (UActorComponent *C : Slot->GetComponents())
    if (UPrimitiveComponent *P = Cast<UPrimitiveComponent>(C)) {
      P->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
      P->SetSimulatePhysics(true);
    }

  ScreenMsg(FString::Printf(TEXT("%s: released"), bRight ? TEXT("R1") : TEXT("L1")));
  Slot = nullptr;
}

// ---------------------------------------------------------------------------
bool UGamepadInputSetup::Tick(float DeltaTime) {
  UWorld *World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
  if (!World) return true;
  APlayerController *PC = UGameplayStatics::GetPlayerController(World, 0);
  if (!PC) return true;
  APawn *Pawn = PC->GetPawn();
  if (!Pawn) return true;

  // ── Read gamepad input — native first, Swift bridge fallback ───────────
  float LX = PC->GetInputAnalogKeyState(EKeys::Gamepad_LeftX);
  float LY = PC->GetInputAnalogKeyState(EKeys::Gamepad_LeftY);
  float RX = PC->GetInputAnalogKeyState(EKeys::Gamepad_RightX);
  float RY = PC->GetInputAnalogKeyState(EKeys::Gamepad_RightY);
  float R1 = PC->GetInputAnalogKeyState(EKeys::Gamepad_RightShoulder);
  float L1 = PC->GetInputAnalogKeyState(EKeys::Gamepad_LeftShoulder);
  float R2 = PC->GetInputAnalogKeyState(EKeys::Gamepad_RightTrigger);
  float L2 = PC->GetInputAnalogKeyState(EKeys::Gamepad_LeftTrigger);

#if PLATFORM_VISIONOS
  // PSVR2 Sense controllers use SpatialGamepad — Unreal's native input system
  // never sees their thumbsticks or buttons. Read via our Swift bridge instead.
  {
    float BLX = 0, BLY = 0, BRX = 0, BRY = 0;
    int BCount = 0, BHasGP = 0;
    SpatialAccessory_GetThumbstickValues(&BLX, &BLY, &BRX, &BRY, &BCount, &BHasGP);
    if (BHasGP > 0) {
      LX = BLX; LY = BLY; RX = BRX; RY = BRY;
    }
  }
  // Bridge shoulder/trigger buttons too — same SpatialGamepad issue
  {
    float BLT = 0, BLS = 0, BRT = 0, BRS = 0;
    int BHasBtn = 0;
    SpatialAccessory_GetButtonValues(&BLT, &BLS, &BRT, &BRS, &BHasBtn);
    if (BHasBtn > 0) {
      R1 = BRS;  // Right shoulder → right grab
      L1 = BLS;  // Left shoulder  → left grab
      R2 = BRT;  // Right trigger  → index curl anim
      L2 = BLT;  // Left trigger   → index curl anim

      // One-time: log the first nonzero button values we see from Swift bridge
      static bool bLoggedFirstBtn = false;
      if (!bLoggedFirstBtn) {
        bLoggedFirstBtn = true;
        ScreenMsg(FString::Printf(
            TEXT("BTN bridge strat=%d R1:%.2f L1:%.2f R2:%.2f L2:%.2f"),
            BHasBtn, BRS, BLS, BRT, BLT));
      }
    }
  }
#endif

  // ── LEFT STICK: Camera-relative locomotion ─────────────────────────────
  const float DZ = 0.15f;
  if (FMath::Abs(LX) < DZ) LX = 0.f;
  if (FMath::Abs(LY) < DZ) LY = 0.f;

  if (FMath::Abs(LX) > 0.f || FMath::Abs(LY) > 0.f) {
    FRotator Yaw(0.f, PC->GetControlRotation().Yaw, 0.f);
    FVector Fwd   = FRotationMatrix(Yaw).GetUnitAxis(EAxis::X);
    FVector Right = FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y);
    Pawn->SetActorLocation(
        Pawn->GetActorLocation() + (Fwd * LY + Right * LX) * 200.f * DeltaTime, true);
  }

  // ── RIGHT STICK: Snap turn (ALWAYS — even while holding) ───────────────
  if (FMath::Abs(RX) < 0.3f) {
    bSnapTurnReady = true;
  } else if (bSnapTurnReady && FMath::Abs(RX) >= 0.6f) {
    float Snap = FMath::Sign(RX) * 30.f;
    FRotator PR = Pawn->GetActorRotation(); PR.Yaw += Snap; Pawn->SetActorRotation(PR);
    FRotator CR = PC->GetControlRotation(); CR.Yaw += Snap; PC->SetControlRotation(CR);
    bSnapTurnReady = false;
  }

  // ── TRIGGER: Per-hand firing ──────────────────────────────────────────
  // The VR Template Pistol binds to IA_Shoot_Right only (not _Left).
  // We inject IA_Shoot_Right each frame a trigger is held, and use
  // EnableInput/DisableInput to gate which pistol(s) receive the action.
  //
  // Our FTSTicker runs before the Enhanced Input subsystem tick, so
  // the EnableInput state we set HERE is what's active when the injection
  // is processed later in the frame.
  //
  //  R2 only  → Right enabled, Left disabled  → Right fires
  //  L2 only  → Left enabled, Right disabled  → Left fires
  //  Both     → Both enabled, one inject      → Both fire
#if PLATFORM_VISIONOS
  {
    const float FireDZ = 0.3f;

    ULocalPlayer *LP = PC->GetLocalPlayer();
    auto *EI = LP ? ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(LP) : nullptr;

    // Cache IA_Shoot_Right (the only action the Pistol Blueprint binds to)
    static TWeakObjectPtr<UInputAction> CachedIA_SR;
    if (EI && !CachedIA_SR.IsValid()) {
      CachedIA_SR = Cast<UInputAction>(StaticLoadObject(UInputAction::StaticClass(), nullptr,
          TEXT("/Game/VRTemplate/Input/Actions/IA_Shoot_Right.IA_Shoot_Right")));
      if (CachedIA_SR.IsValid()) ScreenMsg(TEXT("Cached IA_Shoot_Right"));
    }

    bool bR2Active = R2 > FireDZ;
    bool bL2Active = L2 > FireDZ;

    // Step 1: Set EnableInput state based on which triggers are active.
    // EnableInput/DisableInput are idempotent (push if absent / pop if present).
    if (HeldActorRight) {
      if (bR2Active) HeldActorRight->EnableInput(PC);
      else           HeldActorRight->DisableInput(PC);
    }
    if (HeldActorLeft) {
      if (bL2Active) HeldActorLeft->EnableInput(PC);
      else           HeldActorLeft->DisableInput(PC);
    }

    // Step 2: Inject IA_Shoot_Right if any trigger is held.
    // Only pistols with EnableInput will receive it.
    if (EI && CachedIA_SR.IsValid() && (bR2Active || bL2Active)) {
      EI->InjectInputForAction(CachedIA_SR.Get(), FInputActionValue(true), {}, {});
    }

    // Debug messages on state transitions
    if (bR2Active && !bR2DirectFired) {
      ScreenMsg(FString::Printf(TEXT(">>> R2 FIRE (%.2f)"), R2));
      bR2DirectFired = true;
    } else if (!bR2Active && bR2DirectFired) {
      bR2DirectFired = false;
    }
    if (bL2Active && !bL2DirectFired) {
      ScreenMsg(FString::Printf(TEXT(">>> L2 FIRE (%.2f)"), L2));
      bL2DirectFired = true;
    } else if (!bL2Active && bL2DirectFired) {
      bL2DirectFired = false;
    }

    // ── One-time: log all functions on held actors for diagnostics ──
    auto LogActorFuncs = [](AActor *A, bool &bLogged) {
      if (!A || bLogged) return;
      bLogged = true;
      for (TFieldIterator<UFunction> It(A->GetClass()); It; ++It) {
        UE_LOG(LogTemp, Log, TEXT("[FUNC] %s::%s (params=%d)"),
               *A->GetName(), *It->GetName(), (int)It->NumParms);
      }
    };
    LogActorFuncs(HeldActorRight, bLoggedFuncsR);
    LogActorFuncs(HeldActorLeft, bLoggedFuncsL);
  }
#endif

  // ── SHOULDER BUTTONS: Grab / Release ───────────────────────────────────
  const float BtnDZ = 0.5f;

  if (R1 >= BtnDZ && !bR1WasPressed) { TryGrab(true, PC, Pawn);  bR1WasPressed = true;  }
  else if (R1 < BtnDZ)                                           { bR1WasPressed = false; }
  if (L1 >= BtnDZ && !bL1WasPressed) { TryGrab(false, PC, Pawn); bL1WasPressed = true;  }
  else if (L1 < BtnDZ)                                           { bL1WasPressed = false; }

  // (DEBUG AXES disabled — re-enable by uncommenting in source)

  // ── HAND ANIMATIONS: Drive ABP_MannequinsXR pose alphas from buttons ──
  // On visionOS the Enhanced Input hand actions (IA_Hand_Grasp etc.) never
  // fire because SpatialGamepad buttons don't route through Unreal's input
  // system. We set the animation blueprint properties directly via reflection.
  {
    // Compute per-hand animation values from button/stick state
    float RightGrasp   = R1;                       // Shoulder grip
    float RightCurl    = R2;                       // Trigger pull → curl index
    float RightPoint   = 1.0f - R2;                // Extend index when not pulling trigger
    float RightThumbUp = (FMath::Abs(RX) > DZ || FMath::Abs(RY) > DZ) ? 0.f : 1.f;

    float LeftGrasp    = L1;
    float LeftCurl     = L2;
    float LeftPoint    = 1.0f - L2;
    float LeftThumbUp  = (FMath::Abs(LX) > DZ || FMath::Abs(LY) > DZ) ? 0.f : 1.f;

    // ABP_MannequinsXR stores these as Blueprint variables.  The compiled
    // FName can be either "PoseAlphaGrasp" or "Pose Alpha Grasp" depending
    // on the UE version, so we try the no-space variant first.
    struct FPoseProp {
      FName Primary;   // no-space (compiled script name)
      FName Fallback;  // with spaces (editor display name)
    };
    static const FPoseProp GraspProp  = { FName(TEXT("PoseAlphaGrasp")),    FName(TEXT("Pose Alpha Grasp")) };
    static const FPoseProp CurlProp   = { FName(TEXT("PoseAlphaIndexCurl")),FName(TEXT("Pose Alpha Index Curl")) };
    static const FPoseProp PointProp  = { FName(TEXT("PoseAlphaPoint")),    FName(TEXT("Pose Alpha Point")) };
    static const FPoseProp ThumbProp  = { FName(TEXT("PoseAlphaThumbUp")),  FName(TEXT("Pose Alpha Thumb Up")) };

    // Set a float property on a UObject by name, handling float/double
    // (UE5 Blueprints may store float vars as doubles internally).
    auto SetPoseAlpha = [](UObject *Obj, UClass *Cls, const FPoseProp &Prop, float Val) {
      FProperty *P = Cls->FindPropertyByName(Prop.Primary);
      if (!P) P = Cls->FindPropertyByName(Prop.Fallback);
      if (!P) return;
      if (FFloatProperty *FP = CastField<FFloatProperty>(P))
        FP->SetPropertyValue_InContainer(Obj, Val);
      else if (FDoubleProperty *DP = CastField<FDoubleProperty>(P))
        DP->SetPropertyValue_InContainer(Obj, (double)Val);
    };

    TArray<USkeletalMeshComponent*> HandMeshes;
    Pawn->GetComponents<USkeletalMeshComponent>(HandMeshes);

    for (USkeletalMeshComponent *Mesh : HandMeshes) {
      if (!Mesh->GetName().Contains(TEXT("Hand"))) continue;

      UAnimInstance *Anim = Mesh->GetAnimInstance();
      if (!Anim) continue;

      bool  bIsRight = Mesh->GetName().Contains(TEXT("Right"));
      float Grasp    = bIsRight ? RightGrasp   : LeftGrasp;
      float Curl     = bIsRight ? RightCurl    : LeftCurl;
      float Pt       = bIsRight ? RightPoint   : LeftPoint;
      float ThumbUp  = bIsRight ? RightThumbUp : LeftThumbUp;

      UClass *AC = Anim->GetClass();
      SetPoseAlpha(Anim, AC, GraspProp, Grasp);
      SetPoseAlpha(Anim, AC, CurlProp,  Curl);
      SetPoseAlpha(Anim, AC, PointProp, Pt);
      SetPoseAlpha(Anim, AC, ThumbProp, ThumbUp);
    }
  }

  // (DEBUG HUD disabled — re-enable by uncommenting in source)
  return true;
}

// ---------------------------------------------------------------------------
void UGamepadInputSetup::SetupGamepadMappings() {
  UWorld *World = GetGameInstance()->GetWorld();
  if (!World) { ScreenMsg(TEXT("ERROR: No World")); return; }

  APlayerController *PC = UGameplayStatics::GetPlayerController(World, 0);
  if (!PC) {
    FTimerHandle R;
    World->GetTimerManager().SetTimer(
        R, FTimerDelegate::CreateUObject(this, &UGamepadInputSetup::SetupGamepadMappings),
        3.0f, false);
    return;
  }

  APawn *Pawn = PC->GetPawn();

  // Show hand meshes (may be hidden if Blueprint BeginPlay was modified)
  if (Pawn) {
    TArray<USkeletalMeshComponent*> Meshes;
    Pawn->GetComponents<USkeletalMeshComponent>(Meshes);
    for (USkeletalMeshComponent *Mesh : Meshes) {
      if (Mesh->GetName().Contains(TEXT("Hand"))) {
        Mesh->SetVisibility(true);
        Mesh->SetHiddenInGame(false);
        ScreenMsg(FString::Printf(TEXT("Showed: %s"), *Mesh->GetName()));
      }
    }
  }

  // Enhanced Input subsystem
  ULocalPlayer *LP = PC->GetLocalPlayer();
  if (!LP) { ScreenMsg(TEXT("ERROR: No LocalPlayer")); return; }
  auto *EI = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(LP);
  if (!EI) { ScreenMsg(TEXT("ERROR: No EnhancedInput")); return; }

  // Ensure IMC_Default is active (needed for key state routing)
  auto *IMC_Def = Cast<UInputMappingContext>(StaticLoadObject(
      UInputMappingContext::StaticClass(), nullptr,
      TEXT("/Game/VRTemplate/Input/IMC_Default.IMC_Default")));
  if (IMC_Def && !EI->HasMappingContext(IMC_Def))
    EI->AddMappingContext(IMC_Def, 0);

  // Map L1/R1 to IA_Grab actions so shoulder buttons route through Enhanced Input
  // (Our Tick reads the grab directly, but routing ensures key states are valid)
  auto *IA_GR = Cast<UInputAction>(StaticLoadObject(UInputAction::StaticClass(), nullptr,
      TEXT("/Game/VRTemplate/Input/Actions/IA_Grab_Right.IA_Grab_Right")));
  auto *IA_GL = Cast<UInputAction>(StaticLoadObject(UInputAction::StaticClass(), nullptr,
      TEXT("/Game/VRTemplate/Input/Actions/IA_Grab_Left.IA_Grab_Left")));

  // Map R2/L2 triggers to IA_Shoot actions — the Pistol blueprint listens for
  // these Enhanced Input actions to fire. On visionOS the PSVR2 triggers come
  // through as Gamepad_RightTrigger/LeftTrigger (SpatialGamepad).
  auto *IA_SR = Cast<UInputAction>(StaticLoadObject(UInputAction::StaticClass(), nullptr,
      TEXT("/Game/VRTemplate/Input/Actions/IA_Shoot_Right.IA_Shoot_Right")));
  auto *IA_SL = Cast<UInputAction>(StaticLoadObject(UInputAction::StaticClass(), nullptr,
      TEXT("/Game/VRTemplate/Input/Actions/IA_Shoot_Left.IA_Shoot_Left")));

  GamepadIMC = NewObject<UInputMappingContext>(this, TEXT("IMC_GamepadLookAim"));
  bool bMapped = false;
  if (IA_GR) { GamepadIMC->MapKey(IA_GR, EKeys::Gamepad_RightShoulder); bMapped = true; }
  if (IA_GL) { GamepadIMC->MapKey(IA_GL, EKeys::Gamepad_LeftShoulder);  bMapped = true; }
  if (IA_SR) { GamepadIMC->MapKey(IA_SR, EKeys::Gamepad_RightTrigger);  bMapped = true; }
  if (IA_SL) { GamepadIMC->MapKey(IA_SL, EKeys::Gamepad_LeftTrigger);   bMapped = true; }
  if (bMapped) EI->AddMappingContext(GamepadIMC, 10);

  ScreenMsg(TEXT("R1=grab  L1=grab (controller-tracked)"));
  ScreenMsg(TEXT("R2=fire right  L2=fire left"));
  ScreenMsg(TEXT("READY"));
  bSetupDone = true;
}
