# Copyright 2011,2012 Free Software Foundation, Inc.
#
# This file is part of GNU Radio
#
# GNU Radio is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3, or (at your option)
# any later version.
#
# GNU Radio is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with GNU Radio; see the file COPYING.  If not, write to
# the Free Software Foundation, Inc., 51 Franklin Street,
# Boston, MA 02110-1301, USA.

########################################################################
# Find HDF5
########################################################################
find_package(HDF5)

if(NOT HDF5_FOUND)
    message(FATAL_ERROR "HDF5 required to compile drf")
endif()

########################################################################
# Setup library
########################################################################
include(GrPlatform) #define LIB_SUFFIX

include_directories(${Boost_INCLUDE_DIRS} ${HDF5_INCLUDE_DIRS})
link_directories(${Boost_LIBRARY_DIRS} ${HDF5_LIBRARY_DIRS})
list(APPEND drf_sources
    digital_rf_sink_impl.cc
    )

set(drf_sources "${drf_sources}" PARENT_SCOPE)
if(NOT drf_sources)
	MESSAGE(STATUS "No C++ sources... skipping lib/")
	return()
endif(NOT drf_sources)

add_library(gnuradio-drf SHARED ${drf_sources})
target_link_libraries(gnuradio-drf ${Boost_LIBRARIES} ${HDF5_LIBRARIES} ${GNURADIO_ALL_LIBRARIES} -ldigital_rf -lm)
set_target_properties(gnuradio-drf PROPERTIES DEFINE_SYMBOL "gnuradio_drf_EXPORTS")

if(APPLE)
    set_target_properties(gnuradio-drf PROPERTIES
        INSTALL_NAME_DIR "${CMAKE_INSTALL_PREFIX}/lib"
    )
endif(APPLE)

########################################################################
# Install built library files
########################################################################
install(TARGETS gnuradio-drf
    LIBRARY DESTINATION lib${LIB_SUFFIX} # .so/.dylib file
    ARCHIVE DESTINATION lib${LIB_SUFFIX} # .lib file
    RUNTIME DESTINATION bin              # .dll file
)

########################################################################
# Build and register unit test
########################################################################
include(GrTest)

include_directories(${CPPUNIT_INCLUDE_DIRS})

list(APPEND test_drf_sources
    ${CMAKE_CURRENT_SOURCE_DIR}/test_drf.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/qa_drf.cc
)

add_executable(test-drf ${test_drf_sources})

target_link_libraries(
  test-drf
  ${GNURADIO_RUNTIME_LIBRARIES}
  ${Boost_LIBRARIES}
  ${CPPUNIT_LIBRARIES}
  gnuradio-drf
)

GR_ADD_TEST(test_drf test-drf)
