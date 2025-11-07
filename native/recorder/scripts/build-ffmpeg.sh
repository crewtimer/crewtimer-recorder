#!/bin/bash
set -e

# Set the base build directory
BASE_BUILD_DIR="$PWD/lib-build"
if [[ "$OSTYPE" == "cygwin" ]]; then
  BASE_BUILD_DIR=`cygpath -m "${BASE_BUILD_DIR}"`
  echo BASE_BUILD_DIR=${BASE_BUILD_DIR}
fi

# Determine the platform (macOS or Windows via WSL or others)
if [[ "$OSTYPE" == "darwin"* ]]; then
  PLATFORM="mac"
  TARGET="darwin"
  ARCH_FLAGS="x86_64 arm64"
elif [[ "$OSTYPE" == "cygwin" || "$OSTYPE" == "msys" || "$OSTYPE" == "win32" || "$OS" == "Windows"* ]]; then
  PLATFORM="win"
  TARGET="win64"
  ARCH_FLAGS="x86_64"
  #PLATFORM_FLAGS="--toolchain=msvc --disable-x86asm "
  PLATFORM_FLAGS="--toolchain=msvc"
else
  PLATFORM="linux"
fi

# Variables
FFMPEG_VERSION="n7.1"
DOWNLOAD_DIR="${BASE_BUILD_DIR}/ffmpeg-${FFMPEG_VERSION}"
INSTALL_DIR="${BASE_BUILD_DIR}/ffmpeg-static-${PLATFORM}"
BUILD_DIR="${DOWNLOAD_DIR}/build-${PLATFORM}"
FFMPEG_URL="https://github.com/FFmpeg/FFmpeg/archive/refs/tags/${FFMPEG_VERSION}.zip"
CHECK_FILE="${INSTALL_DIR}/lib/libswscale.a"  # File to check for existing build
# libsrt
LIBSRT_VERSION="1.5.1"
SRT_URL="https://github.com/Haivision/srt/archive/refs/tags/v${LIBSRT_VERSION}.zip"
SRT_SRC_DIR="${BASE_BUILD_DIR}/srt-${LIBSRT_VERSION}"

if [[ "$OSTYPE" == "cygwin" ]]; then
  # INSTALL_DIR=`cygdrive "${INSTALL_DIR}"`
  echo INSTALL_DIR=${INSTALL_DIR}
fi

# Check if the library has already been built
if [ -f "$CHECK_FILE" ]; then
  echo "FFMPEG static library already built ($CHECK_FILE). Skipping build."
  exit 0
fi

# Create the base build directory if it doesn't exist
mkdir -p "$BASE_BUILD_DIR"

# Download FFMPEG if not already downloaded
if [ ! -f "${BASE_BUILD_DIR}/ffmpeg-${FFMPEG_VERSION}.zip" ]; then
  echo "Downloading FFMPEG version ${FFMPEG_VERSION}..."
  # wget -O "${BASE_BUILD_DIR}/${FFMPEG_VERSION}.zip" "${FFMPEG_URL}"
  curl -L "${FFMPEG_URL}" -o "${BASE_BUILD_DIR}/ffmpeg-${FFMPEG_VERSION}.zip"
fi

# Download SRT if not present
if [ ! -f "${BASE_BUILD_DIR}/srt-${LIBSRT_VERSION}.zip" ]; then
  echo "Downloading SRT ${LIBSRT_VERSION}..."
  curl -L "${SRT_URL}" -o "${BASE_BUILD_DIR}/srt-${LIBSRT_VERSION}.zip"
fi

# Extract FFMPEG
if [ ! -d "$DOWNLOAD_DIR" ]; then
  echo "Extracting FFMPEG..."
  unzip -q "${BASE_BUILD_DIR}/ffmpeg-${FFMPEG_VERSION}.zip" -d "$BASE_BUILD_DIR"
fi

# Extract SRT
if [ ! -d "$SRT_SRC_DIR" ]; then
  echo "Extracting SRT..."
  unzip -q "${BASE_BUILD_DIR}/srt-${LIBSRT_VERSION}.zip" -d "$BASE_BUILD_DIR"
fi

# Create build directory
# mkdir -p "$BUILD_DIR"
# cd "$BUILD_DIR"
# echo "Building in ${BUILD_DIR}"

# Configure FFMPEG build
cd "${BASE_BUILD_DIR}/FFmpeg-${FFMPEG_VERSION}"
pwd
for ARCH in $ARCH_FLAGS; do
    if [ -f "${INSTALL_DIR}-${ARCH}/lib/libswscale.a" ]; then
      continue;
    fi
    if [[ "$PLATFORM" == "mac" ]]; then
      CONFIGURE_OPTIONS=(
        --enable-cross-compile
        --arch="$ARCH"
        --extra-cflags="-arch $ARCH"
        --extra-ldflags="-arch $ARCH"
      )
    fi

    # Build and install SRT for this arch if needed
    if [ ! -f "${INSTALL_DIR}-${ARCH}/lib/libsrt.a" ]; then
      echo "Building SRT for $ARCH..."
      mkdir -p "${BASE_BUILD_DIR}/srt-build-${ARCH}"
      pushd "${BASE_BUILD_DIR}/srt-build-${ARCH}" > /dev/null
      cmake_args=(
        -DCMAKE_INSTALL_PREFIX=${INSTALL_DIR}-${ARCH}
        -DBUILD_SHARED_LIBS=OFF
        -DENABLE_STATIC_LIB=ON
        -DENABLE_APPS=OFF
        -DENABLE_SHARED=OFF
        -DENABLE_STATIC=ON
        -DCMAKE_BUILD_TYPE=Release
      )
      if [[ "$PLATFORM" == "win" ]]; then
        cmake_args+=(
          -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded
          -DCMAKE_C_FLAGS_RELEASE="/MT"
          -DCMAKE_CXX_FLAGS_RELEASE="/MT"
        )
      fi
      # allow platform-specific flags
      if [[ "$PLATFORM" == "mac" ]]; then
        cmake "${SRT_SRC_DIR}" -DENABLE_ENCRYPTION=OFF -DCMAKE_OSX_ARCHITECTURES=${ARCH} "${cmake_args[@]}"
      elif [[ "$PLATFORM" == "win" ]]; then
        cmake "${SRT_SRC_DIR}" -A x64 -DENABLE_ENCRYPTION=OFF "${cmake_args[@]}"
      else
        cmake "${SRT_SRC_DIR}" -DENABLE_ENCRYPTION=OFF "${cmake_args[@]}"
      fi

      cmake --build . --parallel --config Release
      echo "SRT Built. Installing SRT for $ARCH..."
      cmake --install . --config Release
      echo "SRT Installed for $ARCH."
      popd > /dev/null
    fi

    PKG_CONFIG_PATH="${INSTALL_DIR}-${ARCH}/lib/pkgconfig"
    if [[ "$OSTYPE" == "cygwin" ]]; then
      # if pkg-config is a cygwin binary, convert path to unix style
      PKG_CONFIG_PATH=`cygpath -u "${PKG_CONFIG_PATH}"`
    fi
    export PKG_CONFIG_PATH
    echo PKG_CONFIG_PATH="${PKG_CONFIG_PATH}"

    EXTRA_CFLAGS="-I${INSTALL_DIR}-${ARCH}/include"
    EXTRA_LDFLAGS="-L${INSTALL_DIR}-${ARCH}/lib"

    echo "Configuring FFMPEG for static linking and $ARCH..."
    ./configure --prefix=${INSTALL_DIR}-${ARCH} \
        ${PLATFORM_FLAGS} \
        "${CONFIGURE_OPTIONS[@]}" \
        --enable-static --enable-gpl --enable-libsrt --disable-doc \
        --disable-avdevice --disable-swresample --disable-postproc --disable-avfilter \
        --disable-doc \
        --extra-cflags="${EXTRA_CFLAGS}" \
        --extra-ldflags="${EXTRA_LDFLAGS}"

# --disable-network
                # --disable-programs \

    # Compile and install FFMPEG
    echo "Building FFMPEG..."
    echo "PKG_CONFIG_PATH=${PKG_CONFIG_PATH}"
    make -j4

    echo "Installing FFMPEG..."
    make install

    make clean && make distclean
done

echo "Final copy to ${INSTALL_DIR}..."

cp -a "${INSTALL_DIR}-x86_64/include" "${INSTALL_DIR}"
cp -a "${INSTALL_DIR}-x86_64/lib" "${INSTALL_DIR}"
cp -a "${INSTALL_DIR}-x86_64/share" "${INSTALL_DIR}"
# rm "${INSTALL_DIR}/lib/"*.a
for file in "${INSTALL_DIR}/lib/"*.a; do
    file=`basename "$file"`
    if [[ "$PLATFORM" == "mac" ]]; then
        lipo -create -arch arm64 "${INSTALL_DIR}-arm64/lib/$file" -arch x86_64 "${INSTALL_DIR}-x86_64/lib/$file" -output "${INSTALL_DIR}/lib/$file"
    fi
done

# Environment setup for Electron module
export FFMPEG_DIR="${INSTALL_DIR}"

echo "FFMPEG static library installed at ${INSTALL_DIR}"
