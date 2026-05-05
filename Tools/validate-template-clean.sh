#!/bin/zsh
set -euo pipefail
export PATH="/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin:$PATH"
path=(/opt/homebrew/bin /usr/local/bin /usr/bin /bin /usr/sbin /sbin $path)

usage() {
	cat <<'USAGE'
Usage:
  validate-template-clean.sh [--clean]

Checks that the master template has the required PSVR2/Vision Pro pieces and no generated Unreal/Xcode output.
Use --clean to remove generated local output from the template folder.
USAGE
}

CLEAN=false
if [[ $# -gt 1 ]]; then
	usage
	exit 64
fi

if [[ $# -eq 1 ]]; then
	if [[ "$1" == "--clean" ]]; then
		CLEAN=true
	else
		usage
		exit 64
	fi
fi

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT_DIR"

uproject_files=( *.uproject(N) )
module_build_files=( Source/*/*.Build.cs(N) )

required_paths=(
	"Config/VisionOS/VisionOSEngine.ini"
	"Plugins/SpatialAccessoryTracking/SpatialAccessoryTracking.uplugin"
)

missing=()
for path in "${required_paths[@]}"; do
	if [[ ! -e "$path" ]]; then
		missing+=("$path")
	fi
done

if (( ${#missing[@]} > 0 )); then
	echo "Missing required template paths:"
	printf '  %s\n' "${missing[@]}"
	exit 65
fi

if (( ${#uproject_files[@]} != 1 )); then
	echo "Expected exactly one .uproject file, found ${#uproject_files[@]}."
	printf '  %s\n' "${uproject_files[@]}"
	exit 65
fi

if (( ${#module_build_files[@]} < 1 )); then
	echo "Expected at least one Source/<Module>/<Module>.Build.cs file."
	exit 65
fi

UPROJECT_FILE="${uproject_files[1]}"

if ! /usr/bin/grep -Eq '"Name"[[:space:]]*:[[:space:]]*"SpatialAccessoryTracking"' "$UPROJECT_FILE"; then
	echo "$UPROJECT_FILE must keep SpatialAccessoryTracking enabled for PSVR2 controller input."
	exit 65
fi

generated=()
while IFS= read -r path; do
	generated+=("$path")
done < <(/usr/bin/find . -maxdepth 4 \( \
	-name Binaries -o \
	-name Intermediate -o \
	-name Saved -o \
	-name DerivedDataCache -o \
	-name ArchivedBuilds -o \
	-name dist -o \
	-name '*.xcodeproj' -o \
	-name '*.xcworkspace' -o \
	-name '*.ipa' -o \
	-name '*.app' -o \
	-name '*.dSYM' -o \
	-name '*.PackageVersionCounter' \
\) -print | /usr/bin/sort)

if (( ${#generated[@]} > 0 )); then
	if [[ "$CLEAN" == true ]]; then
		printf 'Removing generated output:\n'
		printf '  %s\n' "${generated[@]}"
		/bin/rm -rf -- "${generated[@]}"
	else
		echo "Generated local output found:"
		printf '  %s\n' "${generated[@]}"
		echo "Run Tools/validate-template-clean.sh --clean to remove it."
		exit 66
	fi
fi

echo "Template validation passed."
