# Local Template Notes

This folder is the local reusable template for new Polyarc UE 5.6 visionOS projects that need PSVR2 Sense controller support on Apple Vision Pro.

It was created from the public clean sample:

```text
Polyarc-PSVR2-VisionOS-Sample
```

## How To Start A New Project

Use the template helper so generated Unreal files are not copied into the new project and the new project is not left with the `PolyarcSample` module name:

```zsh
"/path/to/Polyarc-PSVR2-VisionOS-Template/Tools/create-project-from-template.sh" "/path/to/UE_Projects/MyNewVisionProject" MyNewVisionProject com.example.MyNewVisionProject
```

The destination folder must not already exist. `ProjectName` and `BundleIdentifier` are optional, but provide them when you already know the Apple signing bundle ID. If omitted, `ProjectName` defaults to the destination folder name.

The helper copies the durable project files, skips generated build/editor output, renames the Unreal C++ module, renames the `.uproject`, updates display/signing metadata, initializes a new Git repo, and creates the first commit.

Generated projects will keep:

- `Plugins/SpatialAccessoryTracking/` enabled for PSVR2 Sense controller input on Apple Vision Pro.

If you duplicate the folder manually, remove these folders from the duplicate before opening it:

- `Binaries/`
- `Intermediate/`
- `Saved/`
- `DerivedDataCache/`

Before making creative changes in a generated project:

1. Open the generated `<ProjectName>.uproject` in the Polyarc UE 5.6 editor.
2. Let Unreal rebuild project/editor modules if requested.
3. Package once for VisionOS.
4. Open the generated VisionOS Xcode project.
5. Build and install to Apple Vision Pro.
6. Confirm the app enters immersive mode and PSVR2 Sense controllers work.
7. Commit that clean working baseline.

Only after that baseline works should you rename modules, change maps, import large assets, or redesign gameplay.

## Template Validation

Before creating a new project, check the master template:

```zsh
"/path/to/Polyarc-PSVR2-VisionOS-Template/Tools/validate-template-clean.sh"
```

If generated local Unreal/Xcode output is present and you want to remove it from the master folder:

```zsh
"/path/to/Polyarc-PSVR2-VisionOS-Template/Tools/validate-template-clean.sh" --clean
```

This does not remove durable source, config, content, plugins, docs, or patches.

## Development Tooling

This template intentionally excludes MCP/editor-automation plugins. Testers preferred not to receive that tooling, and omitting it avoids exposing an editor bridge or local automation surface in redistributed projects.

## Keep Generated Files Out

Do not keep these in the template or in projects created from it:

- `Binaries/`
- `Intermediate/`
- `Saved/`
- `DerivedDataCache/`
- generated Xcode projects/workspaces
- packaged `.app`, `.ipa`, or `.dSYM`
- local signing/provisioning files
- device logs
- package version counters
- file open order logs

## Best Use

Use this folder as a stable starting point. For every new project, duplicate it first and make changes in the duplicate, not in this master template.
