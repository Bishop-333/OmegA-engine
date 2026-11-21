#!/bin/bash

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

. ${SCRIPT_DIR}/lib-versions.sh

CODE="${SCRIPT_DIR}"

if [ "$(uname)" = "Darwin" ]; then
    FIND_CMD="find -E ."
else
    FIND_CMD="find . -regextype posix-extended"
fi

prepare()
{
    local DIR="$1"
    local URL="$2"
    local COMMAND="$3"
    local INCLUDE_PATTERN="$4"
    local EXCLUDE_PATTERN="$5"

    local FILENAME=$(basename ${URL})

    echo ${FILENAME}

    mkdir -p "${CODE}/${DIR}"
    curl -sL "${URL}" | tar -xv -C "${CODE}/${DIR}" --strip-components=1

    (
        cd ${CODE}/${DIR}
        [[ -n "$COMMAND" ]] && eval "$COMMAND"
        [[ -n "$INCLUDE_PATTERN" ]] && ${FIND_CMD} -type f -not -regex ${INCLUDE_PATTERN} -delete
        [[ -n "$EXCLUDE_PATTERN" ]] && ${FIND_CMD} -type f -regex ${EXCLUDE_PATTERN} -delete
        find . -type d -empty -delete
        find -L . -type l -delete
    )
}

prepare "libogg" \
    "https://downloads.xiph.org/releases/ogg/libogg-${OGG_VERSION}.tar.gz" \
    "./configure" \
    "\./((include|src)/.*\.[ch]|COPYING)"

prepare "libvorbis" \
    "https://downloads.xiph.org/releases/vorbis/libvorbis-${VORBIS_VERSION}.tar.gz" \
    "./configure" \
    "\./((include|lib)/.*\.[ch]|COPYING)" \
    "\./lib/(barkmel|psytune|tone|vorbisenc)\.c"

prepare "libflac" \
    "https://downloads.xiph.org/releases/flac/flac-${FLAC_VERSION}.tar.xz" \
    "mv src/libFLAC . && rm -rf src && mv libFLAC src" \
    "\./((include/(FLAC|share)|src)/.*\.[ch]|COPYING\.Xiph)"

prepare "libz-ng" \
    "https://github.com/zlib-ng/zlib-ng/archive/refs/tags/${ZLIB_VERSION}.tar.gz" \
    "" \
    "" \
    "\./(\..*|(doc|test|tools|win32)/.*)"

prepare "libjpeg-turbo" \
    "https://github.com/libjpeg-turbo/libjpeg-turbo/archive/refs/tags/${JPEG_VERSION}.tar.gz" \
    "" \
    "" \
    "\./(\..*|(doc/html|fuzz|jna|sharedlib|test|testimages)/.*)"

prepare "libcurl" \
    "https://curl.se/download/curl-${CURL_VERSION}.tar.gz" \
    "" \
    "" \
    "\./(\..*|(docs/[^C].*|m4|projects|src|tests)/.*)"

prepare "libopenal" \
    "https://github.com/kcat/openal-soft/archive/refs/tags/${OPENAL_VERSION}.tar.gz" \
    "" \
    "\./(include/AL/.*\.h|macosx/.*|windows/.*|COPYING)"

prepare "libsdl" \
    "https://github.com/libsdl-org/SDL/releases/download/release-${SDL_VERSION}/SDL3-${SDL_VERSION}.tar.gz" \
    "" \
    "\./(include/SDL3/.*\.h|macosx/.*|windows/.*|LICENSE\.txt)"

TMPDIR=$(mktemp -d)
cd ${TMPDIR}

mkdir -p "${CODE}/libsdl/windows/mingw/lib32" "${CODE}/libsdl/windows/mingw/lib64"

curl -sLO "https://github.com/libsdl-org/SDL/releases/download/release-${SDL_VERSION}/SDL3-${SDL_VERSION}-win32-x86.zip"
unzip SDL3-${SDL_VERSION}-win32-x86.zip -d x86
cp x86/SDL3.dll "${CODE}/libsdl/windows/mingw/lib32/"

curl -sLO "https://github.com/libsdl-org/SDL/releases/download/release-${SDL_VERSION}/SDL3-${SDL_VERSION}-win32-x64.zip"
unzip SDL3-${SDL_VERSION}-win32-x64.zip -d x64
cp x64/SDL3.dll "${CODE}/libsdl/windows/mingw/lib64/"

if [ "$(uname)" = "Darwin" ]; then
    curl -sL "https://github.com/libsdl-org/SDL/releases/download/release-${SDL_VERSION}/SDL3-${SDL_VERSION}.dmg" -o SDL3-${SDL_VERSION}.dmg
    hdiutil attach -nobrowse -mountpoint mnt SDL3-${SDL_VERSION}.dmg
    mkdir -p "${CODE}/libsdl/macosx"
    cp mnt/SDL3.xcframework/macos-arm64_x86_64/SDL3.framework/Versions/A/SDL3 ${CODE}/libsdl/macosx/libSDL3.0.dylib
    hdiutil detach mnt
    install_name_tool -id @executable_path/libSDL3.0.dylib ${CODE}/libsdl/macosx/libSDL3.0.dylib
    codesign --force --sign - ${CODE}/libsdl/macosx/libSDL3.0.dylib
fi

cd
rm -r ${TMPDIR}
