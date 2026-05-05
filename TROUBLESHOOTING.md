# Troubleshooting

Start with `SETUP.md` before using this file. Most failures come from one of four areas:

- Missing or incompatible Polyarc UE 5.6 engine build.
- Generated Unreal/Xcode files that are stale.
- Local Apple signing/provisioning differences.
- PSVR2 controllers not paired or connected to visionOS.

## App Installs But Quits After About One Second

Use `devicectl` with console attached to capture the device log:

```bash
xcrun devicectl device process launch --device <DEVICE_ID> --terminate-existing --console --timeout 20 <BUNDLE_IDENTIFIER>
```

If the log says:

```text
Failed to open descriptor file ../../../<ProjectName>/<ProjectName>.uproject
```

the staged/package output is stale or incomplete. Re-cook and package from Unreal Editor so `Saved/StagedBuilds/VisionOS` and the generated Xcode project are refreshed.

Use the bundle identifier shown in Xcode for the generated project. If you created the project with `Tools/create-project-from-template.sh`, this should match the optional `BundleIdentifier` argument you provided.

## Missing Module: OpenXREyeTracker

`OpenXREyeTracker` should be disabled for this project. Check `My_Project.uproject` and confirm:

```json
{
  "Name": "OpenXREyeTracker",
  "Enabled": false
}
```

## Packaging Fails Building UnrealPak On Xcode 26.4

If packaging fails while building `UnrealPak-Mac-Development` with warning-as-error conversion diagnostics, apply:

```bash
git apply /path/to/Polyarc-PSVR2-VisionOS-Sample/patches/polyarc-ue56-xcode-26.4-unrealpak.patch
```

Run that command from the root of your Polyarc Unreal Engine checkout.

This patch is for the engine checkout, not this sample project. It does not switch Xcode versions and it does not add any signing credentials.

## App Does Not Enter Immersive Mode

Collect device logs with `devicectl` and look for the first fatal error or missing-file message. If the app closes after about one second, treat it as a launch/runtime failure rather than a controller failure until logs prove otherwise.

Common checks:

- Re-cook and package from Unreal Editor.
- Open the newly generated VisionOS Xcode project, not an old one.
- Confirm Xcode installed the newest build on the headset.
- Confirm the bundle identifier in Xcode matches any `devicectl` launch command.
- Confirm the headset has Developer Mode enabled.

## PSVR2 Controllers Do Not Work

First confirm the app itself launches into immersive mode. Controller debugging should happen only after the app launch is stable.

Then check:

- PSVR2 Sense controllers are paired to visionOS.
- Controllers show as connected before launching the app.
- `SpatialAccessoryTracking` is enabled in `My_Project.uproject`.
- The generated Xcode project includes the current plugin output after a fresh Unreal package step.

## Swift NSLock Warnings

Xcode may warn that `NSLock.lock()` and `unlock()` are unavailable from asynchronous contexts in Swift 6 language mode. In the tested setup these were warnings, not build failures. They are a future cleanup item for the `SpatialAccessoryTracking` Swift bridge.
