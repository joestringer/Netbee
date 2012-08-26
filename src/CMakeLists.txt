# Set requirement for minimum version.
CMAKE_MINIMUM_REQUIRED(VERSION 2.6)

PROJECT(makeallsources)

IF(CMAKE_COMPILER_IS_GNUCXX)
        ADD_DEFINITIONS(-Wall -Wno-write-strings)
ENDIF(CMAKE_COMPILER_IS_GNUCXX)
IF(CMAKE_COMPILER_IS_GNUCC)
        ADD_DEFINITIONS(-Wall -Wno-write-strings)
ENDIF(CMAKE_COMPILER_IS_GNUCC)

## Code profiling
OPTION(
  ENABLE_PROFILING
  "Enable profiling symbols generation by the C++ compiler. This option DOES NOT enable any NetBee-specific profiling code and WILL impact on performance."
  OFF
)

IF(ENABLE_PROFILING)
  ADD_DEFINITIONS(
    -pg
  )
  SET (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -pg")
ENDIF(ENABLE_PROFILING)
#################

ADD_SUBDIRECTORY(nbee)
ADD_SUBDIRECTORY(nbnetvm)
ADD_SUBDIRECTORY(nbpflcompiler)
ADD_SUBDIRECTORY(nbprotodb)
ADD_SUBDIRECTORY(nbsockutils)


# Set dependencies among project files
ADD_DEPENDENCIES(nbpflcompiler nbprotodb)
ADD_DEPENDENCIES(nbee nbprotodb nbpflcompiler nbnetvm nbsockutils)

