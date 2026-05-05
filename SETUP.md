# Setup Guide

This guide is written for a technical user or an AI assistant helping that user reproduce the working PSVR2 Sense controller sample on Apple Vision Pro.

## Goal

Create, package, install, and launch the Unreal visionOS sample on an Apple Vision Pro so the app enters immersive mode and receives PSVR2 Sense controller input.

This repository is the Unreal project and plugin sample. It is not the Unreal Engine checkout and it is not a prebuilt app.

## Known Good Result

The validated local workflow was:

1. Create a renamed project from this template, or open this template directly for template maintenance.
2. Open the project in a built Polyarc Unreal Engine 5.6 editor.
3. Package the project from Unreal Editor for VisionOS.
4. Open the generated VisionOS Xcode project.
5. Build and deploy from Xcode to Apple Vision Pro.
6. Launch the installed app on the headset.
7. The app enters the immersive Vision Pro experience and PSVR2 Sense controller tracking/input works.

## Required Local Pieces

You need all of these outside this repo:

- Apple Vision Pro with Developer Mode enabled.
- PSVR2 Sense controllers paired to visionOS.
- macOS with Xcode and the visionOS SDK installed.
- Apple Developer account and signing team available in Xcode.
- Git LFS.
- A built Polyarc Unreal Engine 5.6 checkout that supports visionOS.

This repo intentionally does not include:

- Unreal Engine source or binaries.
- Generated `Binaries/`, `Intermediate/`, `Saved/`, or `DerivedDataCache/`.
- Generated Xcode project/workspace files.
- Packaged `.app`, `.ipa`, `.dSYM`, signing certificates, provisioning profiles, or local signing state.

## AI Assistant Instructions

If an AI assistant is helping, it should not assume generated files are missing by mistake. They are intentionally excluded.

First inspect:

```text
README.md
SETUP.md
TROUBLESHOOTING.md
My_Project.uproject
Plugins/SpatialAccessoryTracking/
Config/
Source/
patches/
```

Then confirm:

1. Git LFS files were downloaded.
2. The user has a separate built Polyarc UE 5.6 engine.
3. `OpenXREyeTracker` is disabled in the `.uproject`.
4. `SpatialAccessoryTracking` is enabled in the `.uproject`.
5. No MCP/editor-automation plugin is enabled in the `.uproject`.
6. The user can open the project in the Polyarc editor before attempting packaging.

## Create A Renamed Project

For normal iteration, do not duplicate the folder by hand. Use:

```bash
Tools/create-project-from-template.sh "/path/to/UE_Projects/MyProject" MyProject com.example.MyProject
```

The third argument is optional. If provided, it should be the Apple signing bundle identifier you intend to use. The helper renames the C++ module, `.uproject`, bundle display fields, and signing prefix while preserving PSVR2 controller support. It does not add or require MCP/editor-automation tooling.

Before creating a project, you can check whether the master template has generated local output:

```bash
Tools/validate-template-clean.sh
```

Use `Tools/validate-template-clean.sh --clean` to remove generated output from the master template folder.

Do not commit generated Unreal or Xcode output unless the project owner explicitly asks for it.

## Clone

Install Git LFS before cloning:

```bash
git lfs install
git clone https://github.com/RevivrStudios/RevivrStudios-Polyarc-PSVR2-VisionOS-Sample.git
cd RevivrStudios-Polyarc-PSVR2-VisionOS-Sample
git lfs pull
```

Check that LFS content is present:

```bash
git lfs status
```

## Engine Setup

Build or obtain the compatible Polyarc Unreal Engine 5.6 checkout separately.

Expected local shape is similar to:

```text
/path/to/UnrealEngine-PolyArc-5.6/
  Engine/
    Binaries/
    Source/
```

Open this project with that editor build:

```text
My_Project.uproject
```

On macOS, the editor app is usually under:

```text
Engine/Binaries/Mac/UnrealEditor.app
```

## Xcode 26.4 UnrealPak Patch

Some Polyarc UE 5.6 plus Xcode 26.4 environments fail during packaging while building `UnrealPak-Mac-Development`. The failure is caused by Mac compiler warnings being treated as errors, usually with diagnostics like:

```text
implicit conversion ... may lose precision [-Werror,-Wimplicit-int-float-conversion]
```

If this happens, apply the engine patch from the root of the Polyarc Unreal Engine checkout:

```bash
git apply /path/to/RevivrStudios-Polyarc-PSVR2-VisionOS-Sample/patches/polyarc-ue56-xcode-26.4-unrealpak.patch
```

This does not change the selected Xcode version. It only changes the engine `UnrealPak` target so the known conversion diagnostics remain warnings instead of stopping packaging.

After applying the patch, rebuild or allow Unreal packaging to rebuild `UnrealPak`.

## Open The Unreal Project

Open:

```text
My_Project.uproject
```

Use the Polyarc UE 5.6 editor, not a stock Epic launcher engine.

If Unreal shows a missing module dialog for `OpenXREyeTracker`, check `My_Project.uproject`. It should contain:

```json
{
  "Name": "OpenXREyeTracker",
  "Enabled": false
}
```

If Unreal asks to rebuild project modules, build through the IDE or the engine build tools. Do not rely on runtime module compilation for engine modules.

## Package For VisionOS

In Unreal Editor:

1. Select the VisionOS platform.
2. Confirm project settings for visionOS are valid for your local signing setup.
3. Cook/package the project for VisionOS.
4. Wait for packaging to complete successfully.

The generated Xcode project is expected to be under the generated Unreal output, commonly inside:

```text
Intermediate/ProjectFiles/
```

The exact path can vary by engine/project generation settings. It is intentionally not committed to this repo.

## Build And Install From Xcode

Open the generated VisionOS Xcode project.

In Xcode:

1. Select the VisionOS app target/scheme.
2. Select the connected Apple Vision Pro as the run destination.
3. Set your Apple Developer team.
4. Resolve bundle identifier conflicts if needed.
5. Ensure signing and provisioning succeed.
6. Build and run to the headset.

The bundle identifier used in commands in this repo may not match your local signing setup. If you change it in Xcode, use the changed bundle identifier when launching or collecting logs.

## Pair And Verify PSVR2 Controllers

Before launch:

1. Pair PSVR2 Sense controllers to visionOS.
2. Confirm they are connected in visionOS settings.
3. Launch the installed app on Apple Vision Pro.
4. Confirm the app enters immersive mode instead of closing back to the home view.
5. Confirm PSVR2 controller tracking/input works in the sample.

## If The App Installs But Quits

Use the commands and failure patterns in `TROUBLESHOOTING.md`.

The most important known failure is:

```text
Failed to open descriptor file ../../../<ProjectName>/<ProjectName>.uproject
```

That usually means stale or incomplete staged output. Re-cook and re-package from Unreal Editor, then regenerate/open the Xcode project again.

## What To Put In Git

Commit:

- `Config/`
- `Content/`
- `Source/`
- `Plugins/SpatialAccessoryTracking/`
- `Build/` resources that are hand-authored or required templates/assets.
- Documentation and patches.

Do not commit:

- `Binaries/`
- `Intermediate/`
- `Saved/`
- `DerivedDataCache/`
- Generated Xcode projects/workspaces.
- Packaged apps.
- Local signing credentials.
- Device-specific logs.
- Package version counters.
- File open order logs.

## Success Criteria

Consider the reproduction successful when:

1. The project opens in the Polyarc UE 5.6 editor.
2. The project packages for VisionOS without fatal errors.
3. The generated Xcode project builds and installs on Apple Vision Pro.
4. The app launches into immersive mode.
5. PSVR2 Sense controllers are detected and usable.
