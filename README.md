# PSVR2 Sense Controller Sample for Polyarc UE 5.6 VisionOS

This repository is a public Unreal Engine sample for using paired PlayStation VR2 Sense controllers with Apple Vision Pro through Polyarc's Unreal Engine 5.6 visionOS work.

The goal is to give developers a clean, reproducible starting point for testing PSVR2 controller tracking and input in a visionOS immersive app. The repo includes the Unreal project, source module, Spatial Accessory Tracking plugin, setup notes, and the small engine patch used by this workflow. It does not include a built Unreal Engine, a packaged app, signing credentials, provisioning profiles, or generated Xcode build products.

This is a technical sample, not a turnkey binary release. You will need a compatible Polyarc UE 5.6 editor build, Xcode with visionOS support, an Apple Developer signing setup, an Apple Vision Pro, and paired PSVR2 Sense controllers.

## Requirements

- Apple Vision Pro with Developer Mode enabled.
- PSVR2 Sense controllers paired to visionOS.
- macOS with Xcode and the visionOS SDK installed.
- A built Polyarc Unreal Engine 5.6 checkout compatible with this sample. You can obtain Polyarc's Unreal Engine version at https://polyarcgames.github.io.
- Apple Developer account/team configured in Xcode for device deployment.
- Git LFS installed before cloning.

## Start Here

If you are using an AI assistant to reproduce this project, give it this repository and tell it to read these files first:

1. `README.md`
2. `SETUP.md`
3. `TROUBLESHOOTING.md`
4. `My_Project.uproject`
5. `Plugins/SpatialAccessoryTracking/`

The shortest successful path is:

1. Clone this repo with Git LFS.
2. For a new project, create a renamed copy with `Tools/create-project-from-template.sh`.
3. Open the generated `.uproject` in a compatible built Polyarc UE 5.6 editor.
4. Apply `patches/polyarc-ue56-xcode-26.4-unrealpak.patch` to the engine if Xcode 26.4 packaging fails while building `UnrealPak`.
5. Package the project for VisionOS from Unreal Editor.
6. Open the generated VisionOS Xcode project.
7. Set the local Apple Developer team/signing values.
8. Build and install to a Developer Mode Apple Vision Pro.
9. Pair PSVR2 Sense controllers to visionOS and verify input in the immersive app.

For the full reproduction guide, see `SETUP.md`.

## Clone

```bash
git lfs install
git clone https://github.com/RevivrStudios/RevivrStudios-Polyarc-PSVR2-VisionOS-Sample.git
```

## Open

Open `My_Project.uproject` with the Polyarc UE 5.6 editor build when working directly in the master template. For normal iteration, create a named copy first:

```bash
Tools/create-project-from-template.sh "/path/to/UE_Projects/MyProject" MyProject com.example.MyProject
```

The generated project keeps PSVR2 Sense controller support through `SpatialAccessoryTracking`. MCP/editor-automation plugins are intentionally excluded from the public template.

If Unreal shows a missing module dialog for `OpenXREyeTracker`, make sure `My_Project.uproject` has `OpenXREyeTracker` disabled. This sample intentionally disables it for this workflow.

## Cook And Package

In Unreal Editor:

1. Select the VisionOS platform.
2. Cook/package the project for VisionOS.
3. Open the generated VisionOS Xcode project.
4. Select your Apple Developer team/signing settings.
5. Build and deploy to the paired Apple Vision Pro.

The app should enter the immersive Vision Pro experience and receive PSVR2 controller input.

## Xcode 26.4 Engine Patch

Some Polyarc UE 5.6 + Xcode 26.4 setups fail while packaging because `UnrealPak` builds Mac engine code with warnings treated as errors. If packaging fails with errors like `implicit conversion ... may lose precision [-Werror,-Wimplicit-int-float-conversion]`, apply the patch in:

`patches/polyarc-ue56-xcode-26.4-unrealpak.patch`

This patch does not force use of Xcode 26.2. It only changes the engine `UnrealPak` target so those known compiler diagnostics remain warnings instead of stopping packaging.

## What Is Intentionally Not Included

- Unreal Engine source/build output.
- `Binaries/`, `Intermediate/`, `Saved/`, `DerivedDataCache/`.
- Generated Xcode projects/workspaces.
- Packaged `.app` or `.ipa` output.
- Provisioning profiles, certificates, or local signing data.
- Local file-open-order logs and package version counters.

## Repository Scope

This is a reproducible sample/lab project. It is meant to preserve a known-good working PSVR2-on-Vision-Pro setup without depending on one local machine's generated build state.

## Support

For questions about this public sample, contact support@revivrstudios.com.
