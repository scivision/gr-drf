INCLUDE(FindPkgConfig)
PKG_CHECK_MODULES(PC_DRF drf)

FIND_PATH(
    DRF_INCLUDE_DIRS
    NAMES drf/api.h
    HINTS $ENV{DRF_DIR}/include
        ${PC_DRF_INCLUDEDIR}
    PATHS ${CMAKE_INSTALL_PREFIX}/include
          /usr/local/include
          /usr/include
)

FIND_LIBRARY(
    DRF_LIBRARIES
    NAMES gnuradio-drf
    HINTS $ENV{DRF_DIR}/lib
        ${PC_DRF_LIBDIR}
    PATHS ${CMAKE_INSTALL_PREFIX}/lib
          ${CMAKE_INSTALL_PREFIX}/lib64
          /usr/local/lib
          /usr/local/lib64
          /usr/lib
          /usr/lib64
)

INCLUDE(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(DRF DEFAULT_MSG DRF_LIBRARIES DRF_INCLUDE_DIRS)
MARK_AS_ADVANCED(DRF_LIBRARIES DRF_INCLUDE_DIRS)

