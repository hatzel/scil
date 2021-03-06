cmake_minimum_required (VERSION 2.8)

# project name
set (PROJECT_NAME scil)
project (${PROJECT_NAME})

MESSAGE( STATUS "Preparing makefiles for SCIL")

# version numbers
set (VERSION_MAJOR 0)
set (VERSION_MINOR 1)

set(DEV_DIR "${CMAKE_SOURCE_DIR}/../dev")
set(DEPS_DIR "${CMAKE_SOURCE_DIR}/../deps")
set(DEPS_COMPILED_DIR "${CMAKE_BINARY_DIR}/deps")

set (CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_SOURCE_DIR}/cmake ${DEV_DIR}/CMakeModules)


find_package(PkgConfig REQUIRED)

include(CTest)
include(FeatureSummary)
include(CheckCSourceCompiles)
include(CheckCSourceRuns)

option(CODE_COVERAGE "CODE_COVERAGE" OFF)

IF (CODE_COVERAGE)
IF ( CMAKE_BUILD_TYPE STREQUAL "Debug")
	IF ( CMAKE_COMPILER_IS_GNUCC )
	    include(CodeCoverage)
	    setup_target_for_coverage(coverage ctest coverage)
	    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fprofile-arcs -ftest-coverage")
	    set(GCOV_LIBRARIES "-lgcov")
        else()
		MESSAGE (FATAL_ERROR "Error, you need to compile with gcc")
        endif()
else()
	MESSAGE (FATAL_ERROR "Error, you need to compile with Debug mode")
endif()
endif()


# compile flags
set (CMAKE_C_FLAGS  "${CMAKE_C_FLAGS} -std=gnu99 -pipe -Wall -Wno-unused-parameter -Wextra -Wfloat-equal -fdata-sections -ffunction-sections -Wl,--gc-sections")
# -fdiagnostics-color=always
# -fvisibility=hidden -fvisibility-inlines-hidden -Wl,--print-gc-sections
set ( CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -pedantic -pipe -Wall -Wextra -Wfloat-equal -fdata-sections -ffunction-sections -Wl,--gc-sections -Wl,--retain-symbols-file=${CMAKE_SOURCE_DIR}/symbols.txt")
# -Wl,--no-allow-shlib-undefined  does not work with Centos7 / older GCC

if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS "4.6")
else()
set (CMAKE_C_FLAGS  "${CMAKE_C_FLAGS} -Wdouble-promotion -flto -pedantic-errors -pedantic")
set ( CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -Wdouble-promotion -flto -pedantic-errors -pedantic")
endif()


set (CMAKE_C_FLAGS_DEBUG   "-O0 -g3 -fvar-tracking -DDEBUG")
set (CMAKE_CXX_FLAGS_DEBUG "-O0 -g3")

set (CMAKE_C_FLAGS_RELEASE "-O3")

set (SCIL_ALGO_SOURCE ${CMAKE_SOURCE_DIR}/algo)
set (SCIL_ALGO_BINARY ${CMAKE_BINARY_DIR}/algo)


find_path(LIBZ_INCLUDE_DIRS NAMES zlib.h PATHS ENV ADDITIONAL_INC_PATH)
find_library(LIBZ_LIBRARIES NAMES z PATHS ENV ADDITIONAL_LIB_PATH)

find_path(LIBGSL_INCLUDE_DIRS NAMES gsl/gsl_errno.h PATHS ENV ADDITIONAL_INC_PATH)
find_library(LIBGSL_LIBRARIES NAMES gsl PATHS ENV ADDITIONAL_LIB_PATH)

find_path(LIBLZ4_INCLUDE_DIRS NAMES lz4.h PATHS ENV ADDITIONAL_INC_PATH)
find_library(LIBLZ4_LIBRARIES NAMES lz4 PATHS ENV ADDITIONAL_LIB_PATH)

find_path(LIBNETCDF_INCLUDE_DIRS NAMES netcdf.h PATHS ENV ADDITIONAL_INC_PATH)
find_library(LIBNETCDF_LIBRARIES NAMES netcdf PATHS ENV ADDITIONAL_LIB_PATH)

##### ENDIANESS

set(ENDIANESS "undefined" CACHE STRING "Endianess")

CHECK_C_SOURCE_RUNS("
  #include <assert.h>
  int main(){
    int v = 1;
    char *p = (char*) & v;
    assert(*p == 1);
  }  " LITTLE_ENDIAN)

if( ${LITTLE_ENDIAN} )
  set (CMAKE_C_FLAGS  "${CMAKE_C_FLAGS} -D SCIL_LITTLE_ENDIAN")
  set (ENDIANESS "LITTLE" CACHE STRING "Endianess" FORCE)
endif()


CHECK_C_SOURCE_RUNS("
  #include <assert.h>
  int main(){
    int v = 1;
    char *p = (char*) & v;
    assert(*(p + sizeof(int) - 1) == 1);
  }  " BIG_ENDIAN)

if( ${BIG_ENDIAN} )
  set (CMAKE_C_FLAGS  "${CMAKE_C_FLAGS} -D SCIL_BIG_ENDIAN")
  set (ENDIANESS "BIG" CACHE STRING "Endianess" FORCE)
endif()

message(STATUS "Endianess: ${ENDIANESS}")

#########################


# run during configure:
execute_process(COMMAND ${DEPS_DIR}/build-dependencies.sh) # WORKING_DIRECTORY ${DEPS_DIR}
execute_process(COMMAND ${DEV_DIR}/create-datatype-variants.py ${CMAKE_SOURCE_DIR} ${CMAKE_BINARY_DIR}/)

#
# set(ENV{PKG_CONFIG_PATH} "$ENV{PKG_CONFIG_PATH}:${DEPS_DIR}/openjpeg/lib/pkgconfig/")
# pkg_search_module(OPENJPEG REQUIRED libopenjp2)


FILE(GLOB ALGO ${CMAKE_SOURCE_DIR}/algo/*.c ${CMAKE_BINARY_DIR}/algo/*.c)
FILE(GLOB REMOVE ${CMAKE_SOURCE_DIR}/algo/*.dtype.c)
list(REMOVE_ITEM ALGO ${REMOVE})

FILE(GLOB BINARY_DEPS ${CMAKE_BINARY_DIR}/scil-*.c)
FILE(GLOB SOURCE_DEPS ${CMAKE_SOURCE_DIR}/scil-*.c)

FILE(GLOB REMOVE ${CMAKE_SOURCE_DIR}/scil-*.dtype.c)
list(REMOVE_ITEM SOURCE_DEPS ${REMOVE})


FILE(GLOB SOURCE_COMPRESSION_DEPS ${CMAKE_SOURCE_DIR}/compression/*.c)
FILE(GLOB SOURCE_INTERNAL_DEPS ${CMAKE_SOURCE_DIR}/internal/*.c)
FILE(GLOB SOURCE_PREPARE_DEPS ${CMAKE_SOURCE_DIR}/prepare/*.c)
FILE(GLOB SOURCE_UTIL_DEPS ${CMAKE_SOURCE_DIR}/util/*.c)

# MESSAGE( STATUS "Available algorithms: " ${ALGO})

add_library(scil SHARED scil.c scil-dummy.cpp
    ${SOURCE_DEPS}
    ${SOURCE_COMPRESSION_DEPS}
    ${SOURCE_INTERNAL_DEPS}
    ${SOURCE_PREPARE_DEPS}
    ${SOURCE_UTIL_DEPS}
    ${BINARY_DEPS}
    ${CMAKE_BINARY_DIR}/scil-quantizer.c
    ${ALGO}
    ${DEPS_DIR}/open-simplex-noise-in-c/open-simplex-noise.c
    ${DEPS_COMPILED_DIR}/wavelet_code/alloc.c
    ${DEPS_COMPILED_DIR}/wavelet_code/wav_basic.c
    ${DEPS_COMPILED_DIR}/wavelet_code/wav_trf.c
)
target_link_libraries(scil m ${GCOV_LIBRARIES})

add_custom_target(
    trigger_datatype_variants
    ALL COMMAND ${DEV_DIR}/create-datatype-variants.py ${CMAKE_SOURCE_DIR} ${CMAKE_BINARY_DIR}/)

add_dependencies(scil trigger_datatype_variants)

# for specific algorithms
include_directories(
  ${LIBZ_INCLUDE_DIRS}
  ${CMAKE_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/tools
  ${CMAKE_SOURCE_DIR}/compression
  ${CMAKE_SOURCE_DIR}/internal
  ${CMAKE_SOURCE_DIR}/pattern
  ${CMAKE_SOURCE_DIR}/prepare
  ${CMAKE_SOURCE_DIR}/util
  ${CMAKE_BINARY_DIR}
  ${DEPS_DIR}/open-simplex-noise-in-c
  ${DEPS_COMPILED_DIR}/wavelet_code
  ${DEPS_COMPILED_DIR}/include/zfp
  ${DEPS_COMPILED_DIR}/include/fpzip
  ${DEPS_COMPILED_DIR}/include/cnoise
	${DEPS_COMPILED_DIR}/include/sz
)

target_link_libraries(scil ${LIBZ_LIBRARIES})
target_link_libraries(scil
  ${DEPS_COMPILED_DIR}/libfpzip.a
  ${DEPS_COMPILED_DIR}/libzfp.a
  ${DEPS_COMPILED_DIR}/libcnoise.a
	${DEPS_COMPILED_DIR}/libsz.a
  lz4
  rt
  )

configure_file("${CMAKE_SOURCE_DIR}/scil-config.h.in" "scil-config.h" @ONLY)


## Documentation
find_package(Doxygen)
if(DOXYGEN_FOUND)
    add_custom_target(doc
        ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "Generating API documentation with Doxygen" VERBATIM
    )

    find_package( Java )
    # add_feature_info( Java DESCRIPTION "Java is needed in the documentation to run Plantuml" TYPE OPTIONAL)
    if(JAVA_FOUND)
      find_path (PLANTUML_JAR_PATH plantuml.jar HINTS ${DEPS_DIR} DOC "PLANTUML directory" ENV PLANTUML_JAR_PATH)
      if(PLANTUML_JAR_PATH)
        set(PLANTUML_JAR_PATH "${PLANTUML_JAR_PATH}/plantuml.jar")
        set(PLANTUML_ON ON)
      else()
        message(STATUS "Warning: no plantuml.jar found.")
      endif()
    endif(JAVA_FOUND)
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile @ONLY)
endif(DOXYGEN_FOUND)

option(plantuml "Plantuml support" ${PLANTUML_ON})
add_feature_info(plantuml plantuml  "PlantUML for documentation" URL http://de.plantuml.com/ TYPE OPTIONAL)
##### END Documentation


SUBDIRS (test)
SUBDIRS (tools)
SUBDIRS (pattern)


## Installation
configure_file("${DEV_DIR}/scil.pc.in" "scil.pc" @ONLY)

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/scil.pc"  DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/pkgconfig")
install(TARGETS scil LIBRARY DESTINATION lib)
install(FILES scil.h DESTINATION include)
install(FILES util/scil-util.h util/scil-error.h util/scil-compressors.h DESTINATION include)

##
feature_summary(WHAT ALL)
