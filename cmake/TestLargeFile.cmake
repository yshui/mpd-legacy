
# - Define macro to check large file support
#
#  This macro will also add necessary definitions to enable large file support, for instance
#  _LARGE_FILES
#  _LARGEFILE_SOURCE
#  _FILE_OFFSET_BITS 64
#
#  However, it is YOUR job to make sure these defines are set in a #cmakedefine so they
#  end up in a config.h file that is included in your source if necessary!
#
#  Adapted from Music Player Daemon
#  by Yuxuan Shui
#

include(CheckTypeSize)

unset(CMAKE_REQUIRED_DEFINITIONS)
if(NOT HAVE_LARGEFILE)
	check_type_size(off_t OFF_T)
	# First check without any special flags
	if(OFF_T EQUAL 8)
		set(HAVE_LARGEFILE true)
		message(STATUS "Checking for 64-bit off_t - present")
	endif()
endif()

if(NOT HAVE_LARGEFILE)
	set(CMAKE_REQUIRED_DEFINITIONS "-D_FILE_OFFSET_BITS=64")
	# Test with _FILE_OFFSET_BITS=64
	check_type_size(off_t OFF_T_2)
	if(OFF_T_2 EQUAL 8)
		set(HAVE_LARGEFILE true)
		message(STATUS "Checking for 64-bit off_t - present with _FILE_OFFSET_BITS=64")
		add_definitions(-D_FILE_OFFSET_BITS=64)
	endif()
endif()

if(NOT HAVE_LARGEFILE)
	# Test with _LARGE_FILES
	set(CMAKE_REQUIRED_DEFINITIONS "-D_LARGE_FILES")
	check_type_size(off_t OFF_T_3)
	if(OFF_T_3 EQUAL 8)
		set(HAVE_LARGEFILE true)
		message(STATUS "Checking for 64-bit off_t - present with _LARGE_FILES")
		add_definitions(-D_LARGE_FILES)
	endif()
endif()

if(NOT HAVE_LARGEFILE)
	# Test with _LARGE_FILES
	set(CMAKE_REQUIRED_DEFINITIONS "-D_LARGEFILE_SOURCE")
	check_type_size(off_t OFF_T_4)
	if(OFF_T_4 EQUAL 8)
		set(HAVE_LARGEFILE true)
		message(STATUS "Checking for 64-bit off_t - present with _LARGEFILE_SOURCE")
		add_definitions(-D_LARGEFILE_SOURCE)
	endif()
endif()

if(NOT HAVE_LARGEFILE)
	message("Checking for 64-bit off_t - not present, no large file support")
endif()
