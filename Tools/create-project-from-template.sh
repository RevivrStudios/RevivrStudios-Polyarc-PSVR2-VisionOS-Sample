#!/bin/zsh
set -euo pipefail
export PATH="/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin:$PATH"
path=(/opt/homebrew/bin /usr/local/bin /usr/bin /bin /usr/sbin /sbin $path)

usage() {
	cat <<'USAGE'
Usage:
  create-project-from-template.sh /absolute/path/to/NewProjectFolder [ProjectName] [BundleIdentifier]

Examples:
  create-project-from-template.sh "/path/to/UE_Projects/PinkField" PinkField com.example.PinkField
  create-project-from-template.sh "/path/to/UE_Projects/MyNewVisionProject"

ProjectName defaults to the destination folder name.
BundleIdentifier is optional. When provided, CodeSigningPrefix and BundleName are set so Unreal produces that identifier.
USAGE
}

if [[ $# -lt 1 || $# -gt 3 ]]; then
	usage
	exit 64
fi

SOURCE_DIR="$(cd "$(dirname "$0")/.." && pwd)"
DEST_DIR="$1"
PROJECT_NAME="${2:-$(basename "$DEST_DIR")}"
BUNDLE_ID="${3:-}"
UPROJECT_NAME="${PROJECT_NAME}.uproject"

if [[ "$DEST_DIR" != /* ]]; then
	echo "Destination must be an absolute path: $DEST_DIR"
	exit 64
fi

if [[ -e "$DEST_DIR" ]]; then
	echo "Destination already exists: $DEST_DIR"
	exit 73
fi

MODULE_NAME="$(print -r -- "$PROJECT_NAME" | perl -pe 's/[^A-Za-z0-9_]+//g; s/^([0-9])/_$1/')"
if [[ -z "$MODULE_NAME" || ! "$MODULE_NAME" =~ '^[A-Za-z_][A-Za-z0-9_]*$' ]]; then
	echo "ProjectName must contain at least one letter or underscore after sanitizing: $PROJECT_NAME"
	exit 64
fi

if [[ -n "$BUNDLE_ID" && ! "$BUNDLE_ID" =~ '^[A-Za-z0-9][A-Za-z0-9.-]*\.[A-Za-z0-9-]+$' ]]; then
	echo "BundleIdentifier must look like a reverse-DNS identifier: $BUNDLE_ID"
	exit 64
fi

mkdir -p "$(dirname "$DEST_DIR")"

rsync -a \
	--exclude='.git/' \
	--exclude='Binaries/' \
	--exclude='Intermediate/' \
	--exclude='Saved/' \
	--exclude='DerivedDataCache/' \
	--exclude='ArchivedBuilds/' \
	--exclude='dist/' \
	--exclude='*.xcworkspace/' \
	--exclude='*.xcodeproj/' \
	--exclude='*.ipa' \
	--exclude='*.app' \
	--exclude='*.dSYM' \
	--exclude='.DS_Store' \
	"$SOURCE_DIR/" "$DEST_DIR/"

mv "$DEST_DIR/VisionPro.uproject" "$DEST_DIR/$UPROJECT_NAME"
mv "$DEST_DIR/Source/PolyarcSample" "$DEST_DIR/Source/$MODULE_NAME"
mv "$DEST_DIR/Source/PolyarcSample.Target.cs" "$DEST_DIR/Source/$MODULE_NAME.Target.cs"
mv "$DEST_DIR/Source/PolyarcSampleEditor.Target.cs" "$DEST_DIR/Source/${MODULE_NAME}Editor.Target.cs"
mv "$DEST_DIR/Source/$MODULE_NAME/PolyarcSample.Build.cs" "$DEST_DIR/Source/$MODULE_NAME/$MODULE_NAME.Build.cs"
mv "$DEST_DIR/Source/$MODULE_NAME/PolyarcSample.cpp" "$DEST_DIR/Source/$MODULE_NAME/$MODULE_NAME.cpp"
mv "$DEST_DIR/Source/$MODULE_NAME/PolyarcSample.h" "$DEST_DIR/Source/$MODULE_NAME/$MODULE_NAME.h"

rename_files=(
	"$DEST_DIR/$UPROJECT_NAME"
	"$DEST_DIR/Source/$MODULE_NAME.Target.cs"
	"$DEST_DIR/Source/${MODULE_NAME}Editor.Target.cs"
	"$DEST_DIR/Source/$MODULE_NAME/$MODULE_NAME.Build.cs"
	"$DEST_DIR/Source/$MODULE_NAME/$MODULE_NAME.cpp"
	"$DEST_DIR/Config/DefaultEngine.ini"
	"$DEST_DIR/Build/IOS/UBTGenerated/Info.Template.plist"
)

for file in "${rename_files[@]}"; do
	[[ -f "$file" ]] || continue
	perl -0pi -e "s/PolyarcSample/$MODULE_NAME/g; s/VisionPro\\.uproject/$UPROJECT_NAME/g" "$file"
done

if [[ -f "$DEST_DIR/Source/$MODULE_NAME/$MODULE_NAME.cpp" ]]; then
	perl -0pi -e "s/#include \"$MODULE_NAME\\.h\"/#include \"$MODULE_NAME.h\"/g" "$DEST_DIR/Source/$MODULE_NAME/$MODULE_NAME.cpp"
fi

if [[ -n "$BUNDLE_ID" ]]; then
	CODE_SIGNING_PREFIX="${BUNDLE_ID%.*}"
	BUNDLE_NAME="${BUNDLE_ID##*.}"
	perl -0pi -e "s/^CodeSigningPrefix=.*/CodeSigningPrefix=$CODE_SIGNING_PREFIX/m" "$DEST_DIR/Config/DefaultEngine.ini"
	perl -0pi -e "s/^BundleName=.*/BundleName=$BUNDLE_NAME/m" "$DEST_DIR/Config/DefaultEngine.ini"
fi

if ! /usr/bin/grep -Eq '"Name"[[:space:]]*:[[:space:]]*"SpatialAccessoryTracking"' "$DEST_DIR/$UPROJECT_NAME"; then
	echo "Template copy is missing required SpatialAccessoryTracking plugin entry."
	exit 65
fi

git -C "$DEST_DIR" init
git -C "$DEST_DIR" add .
git -C "$DEST_DIR" commit -m "Initial project from PSVR2 visionOS template"

echo "Created new project:"
echo "$DEST_DIR/$UPROJECT_NAME"
echo "Module: $MODULE_NAME"
if [[ -n "$BUNDLE_ID" ]]; then
	echo "Bundle identifier: $BUNDLE_ID"
fi
