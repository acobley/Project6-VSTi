#!/bin/sh
#-----------------------------------------------------------------------------
# Generate the Xcode project (DXi porting template)
#
#   ./setup-xcode.sh                  check the toolchain, configure, open Xcode
#   ./setup-xcode.sh --no-open        configure only
#   ./setup-xcode.sh --makefiles      build without Xcode (Command Line Tools only)
#   ./setup-xcode.sh --no-validator   skip Steinberg's validator after each build
#   ./setup-xcode.sh --minimal        --no-validator plus no moduleinfo.json
#   ./setup-xcode.sh --no-au          skip the Audio Unit (.component) target
#   ./setup-xcode.sh --clean          throw away build/ first
#
#   VST3_SDK_ROOT=/path ./setup-xcode.sh    use an SDK you already have
#-----------------------------------------------------------------------------

set -e

cd "$(dirname "$0")"

# The plug-in's name lives in one place - the identity block in CMakeLists.txt
# - so this script reads it from there rather than having its own copy to
# forget about.
PLUGIN_NAME=$(sed -n 's/^[[:space:]]*set(PLUGIN_NAME[[:space:]]*"\([^"]*\)".*/\1/p' CMakeLists.txt | head -1)
if [ -z "$PLUGIN_NAME" ]; then
	echo "error: could not read PLUGIN_NAME from CMakeLists.txt."
	echo "       It should contain a line like:  set(PLUGIN_NAME \"MyPlugin\")"
	exit 1
fi

GENERATOR="Xcode"
BUILD_DIR="build"
OPEN_XCODE=1
CLEAN=0
VALIDATOR=ON
MODULEINFO=ON
BUILD_AU=ON

for arg in "$@"; do
	case "$arg" in
		--no-open)      OPEN_XCODE=0 ;;
		--makefiles)    GENERATOR="Unix Makefiles"; BUILD_DIR="build-make"; OPEN_XCODE=0 ;;
		--no-validator) VALIDATOR=OFF ;;
		--minimal)      VALIDATOR=OFF; MODULEINFO=OFF ;;
		--no-au)        BUILD_AU=OFF ;;
		--clean)        CLEAN=1 ;;
		-h|--help)      sed -n '3,15p' "$0"; exit 0 ;;
		*)              echo "unknown option: $arg"; exit 1 ;;
	esac
done

#-----------------------------------------------------------------------------
# Prerequisites
#-----------------------------------------------------------------------------
if ! command -v cmake >/dev/null 2>&1; then
	echo "error: cmake not found."
	echo "       brew install cmake        (or https://cmake.org/download/)"
	exit 1
fi

if ! command -v git >/dev/null 2>&1; then
	echo "error: git not found. Install the Xcode command line tools:"
	echo "       xcode-select --install"
	exit 1
fi

#-----------------------------------------------------------------------------
# Toolchain check
#
# "No CMAKE_C_COMPILER could be found" almost always means one of these three.
#-----------------------------------------------------------------------------
if [ "$(uname)" = "Darwin" ]; then

	DEVDIR=$(xcode-select -p 2>/dev/null || true)

	if [ -z "$DEVDIR" ]; then
		echo "error: no developer tools are selected."
		echo
		echo "  Install the command line tools:"
		echo "      xcode-select --install"
		echo
		echo "  Then, if you have Xcode installed, point at it:"
		echo "      sudo xcode-select -s /Applications/Xcode.app/Contents/Developer"
		exit 1
	fi

	echo "Developer directory: $DEVDIR"

	case "$DEVDIR" in
		*CommandLineTools*)
			if [ "$GENERATOR" = "Xcode" ]; then
				echo
				echo "error: only the Command Line Tools are selected, but the Xcode"
				echo "       generator needs full Xcode. That is what produces"
				echo "       \"No CMAKE_C_COMPILER could be found\"."
				echo
				if [ -d /Applications/Xcode.app ]; then
					echo "  Xcode IS installed. Point the tools at it and try again:"
					echo "      sudo xcode-select -s /Applications/Xcode.app/Contents/Developer"
					echo "      sudo xcodebuild -license accept"
					echo "      ./setup-xcode.sh --clean"
				else
					echo "  Xcode does not appear to be installed. Either:"
					echo "    a) install Xcode from the App Store, then:"
					echo "         sudo xcode-select -s /Applications/Xcode.app/Contents/Developer"
					echo "         sudo xcodebuild -license accept"
					echo "         ./setup-xcode.sh --clean"
					echo "    b) or build the plug-in without Xcode:"
					echo "         ./setup-xcode.sh --makefiles"
				fi
				exit 1
			fi
			;;
	esac

	if [ "$GENERATOR" = "Xcode" ] && ! command -v xcodebuild >/dev/null 2>&1; then
		echo "error: xcodebuild not on PATH even though $DEVDIR is selected."
		echo "       Try:  sudo xcode-select -s /Applications/Xcode.app/Contents/Developer"
		exit 1
	fi

	# An unaccepted licence makes every compiler probe fail, with the same
	# CMAKE_C_COMPILER message.
	if [ "$GENERATOR" = "Xcode" ]; then
		if ! xcodebuild -version >/dev/null 2>&1; then
			echo "error: xcodebuild cannot run. The Xcode licence is probably not accepted:"
			echo "       sudo xcodebuild -license accept"
			exit 1
		fi
		echo "Xcode: $(xcodebuild -version | head -1)"
	fi

	# Final sanity check: can clang actually compile?
	if ! printf 'int main(void){return 0;}\n' | xcrun clang -x c - -o /dev/null >/dev/null 2>&1; then
		echo "error: the C compiler does not work."
		echo "       xcode-select -p   ->  $DEVDIR"
		echo "       Try:  sudo xcode-select --reset"
		echo "             xcode-select --install"
		exit 1
	fi
fi

#-----------------------------------------------------------------------------
# Configure
#-----------------------------------------------------------------------------
# A failed configure leaves a poisoned CMakeCache.txt that keeps reporting the
# same error even after the toolchain is fixed, so drop it.
if [ "$CLEAN" = "1" ]; then
	rm -rf "$BUILD_DIR"
elif [ -f "$BUILD_DIR/CMakeCache.txt" ]; then
	if ! grep -q "^CMAKE_C_COMPILER:" "$BUILD_DIR/CMakeCache.txt"; then
		echo "Removing the incomplete cache left in $BUILD_DIR by an earlier failed run."
		rm -rf "$BUILD_DIR"
	fi
fi

CMAKE_ARGS="-DSMTG_RUN_VST_VALIDATOR=$VALIDATOR -DSMTG_CREATE_MODULE_INFO=$MODULEINFO"
CMAKE_ARGS="$CMAKE_ARGS -DPORT_BUILD_AU=$BUILD_AU"
if [ "$GENERATOR" = "Unix Makefiles" ]; then
	CMAKE_ARGS="$CMAKE_ARGS -DCMAKE_BUILD_TYPE=Release"
fi
if [ -n "$VST3_SDK_ROOT" ]; then
	CMAKE_ARGS="$CMAKE_ARGS -DVST3_SDK_ROOT=$VST3_SDK_ROOT"
fi

if [ "$VALIDATOR" = "OFF" ]; then
	echo "Validator: disabled"
fi

if [ ! -d external/vst3sdk ] && [ -z "$VST3_SDK_ROOT" ]; then
	echo
	echo "The Steinberg VST3 SDK (~250 MB) will be downloaded into external/vst3sdk."
	echo "This happens once and takes a few minutes."
	echo
fi

if [ "$BUILD_AU" = "ON" ] && [ "$GENERATOR" = "Xcode" ] && [ ! -d external/AudioUnitSDK ] \
   && [ "$(uname)" = "Darwin" ]; then
	echo "Apple's AudioUnitSDK will also be downloaded, for the Audio Unit wrapper."
	echo "Skip it with --no-au."
	echo
fi

# shellcheck disable=SC2086
cmake -B "$BUILD_DIR" -G "$GENERATOR" $CMAKE_ARGS .

echo
if [ "$GENERATOR" = "Xcode" ]; then
	echo "Xcode project written to $BUILD_DIR/$PLUGIN_NAME.xcodeproj"
	echo "Or build from the command line:"
	echo "    cmake --build $BUILD_DIR --config Release"
	if [ "$BUILD_AU" = "ON" ]; then
		echo
		echo "Building the $PLUGIN_NAME-au scheme also produces $PLUGIN_NAME.component and"
		echo "copies it to ~/Library/Audio/Plug-Ins/Components/."
	fi
	if [ "$OPEN_XCODE" = "1" ] && [ "$(uname)" = "Darwin" ]; then
		open "$BUILD_DIR/$PLUGIN_NAME.xcodeproj"
	fi
else
	echo "Configured with Makefiles. Build with:"
	echo "    cmake --build $BUILD_DIR -j"
fi
echo
