# Custom FindNDI.cmake file to locate NDI SDK on macOS
set(NDI_SDK_PATH "/Library/NDI SDK for Apple")

# Locate the NDI library
find_library(NDI_LIBRARIES
    NAMES ndi
    PATHS ${NDI_SDK_PATH}/lib/macOS
    REQUIRED
)

# Locate the NDI headers
find_path(NDI_INCLUDE_DIR
    NAMES Processing.NDI.Lib.h
    PATHS ${NDI_SDK_PATH}/include
    REQUIRED
)

# Export the found paths for external use
if(NDI_LIBRARIES AND NDI_INCLUDE_DIR)
    set(NDI_FOUND TRUE)
    set(NDI_LIBRARIES ${NDI_LIBRARIES})
    set(NDI_INCLUDE_DIR ${NDI_INCLUDE_DIR})
else()
    set(NDI_FOUND FALSE)
endif()

mark_as_advanced(NDI_LIBRARIES NDI_INCLUDE_DIR)
