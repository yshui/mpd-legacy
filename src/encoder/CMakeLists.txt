# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

# Copyright (C) 2013 Yuxuan Shui, yshuiv7@gmail.com

set(ENCODER_SRC null.c wave.c)
set(ALL_ENCODER
	lame
	twolame
	flac
	vorbis
)
set(list_var ENCODER_LIST)
set(src_var ENCODER_SRC)

check_enabled(ALL_ENCODER ENABLE_ENCODER)

if(_ENABLE_lame)
	set(CMAKE_REQUIRED_LIBRARIES m)
	check_library_exists(mp3lame lame_init "/usr/lib" ENABLE_LAME_ENCODER)
	if(ENABLE_LAME_ENCODER)
		list(APPEND ENCODER_SRC "lame.c")
		mpd_add_lib("mp3lame;m")
	else()
		report_no_support("lame" "libmp3lame")
	endif()
endif()

if(_ENABLE_twolame)
	pkg_check_modules(TWOLAME "twolame")
	if(NOT TWOLAME_FOUND)
		report_no_support("twolame")
	else()
		set(ENABLE_TWOLAME_ENCODER true)
		list(APPEND ENCODER_SRC "twolame.c")
		mpd_add_lib(${TWOLAME_LIBRARIES})
	endif()
endif()

if(_ENABLE_flac)
	pkg_check_modules(FLAC "flac")
	if(NOT FLAC_FOUND)
		report_no_support("flac")
	else()
		set(ENABLE_FLAC_ENCODER true)
		list(APPEND ENCODER_SRC "flac.c")
		mpd_add_lib(${FLAC_LIBRARIES})
	endif()
endif()

if(_ENABLE_vorbis)
	if(USE_TREMOR)
		message("Can't enable vorbis encoder because we are using tremor")
		list(REMOVE_ITEM ENCODER_LIST "vorbis")
	else()
		pkg_check_modules(VORBISENC vorbisenc vorbis ogg)
		if(NOT VORBISENC_FOUND)
			report_no_support("vorbis")
		else()
			set(ENABLE_VORBIS_ENCODER true)
			list(APPEND ENCODER_SRC "vorbis.c")
			mpd_add_lib(${VORBISENC_LIBRARIES})
		endif()
	endif()
endif()

set(ENCODER_LIST ${ENCODER_LIST} PARENT_SCOPE)
unset(list_var)
add_library(encoder STATIC ${ENCODER_SRC})
target_link_libraries(encoder util)
configure_file(encoder_conf.h.in encoder_conf.h)
