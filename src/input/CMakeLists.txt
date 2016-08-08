set(INPUT_SRC
	file.c
	#rewind.c
	archive.c
)

set(ALL_INPUT
	curl
	mms
	ffmpeg
	despotify
	cdio
)

set(list_var INPUT_LIST)
check_enabled(ALL_INPUT ENABLE_INPUT)

if(_ENABLE_curl)
	if(NOT CURL_FOUND)
		report_no_support("curl")
	else()
		set(ENABLE_CURL true)
		list(APPEND INPUT_SRC curl.c icy_metadata.c)
		set_source_files_properties(curl.c PROPERTIES COMPILE_FLAGS "${CURL_CFLAGS}")
		mpd_add_lib(${CURL_LIBRARIES})
	endif()
endif()

if(_ENABLE_ffmpeg)
	if(NOT HAVE_FFMPEG)
		report_no_support("ffmpeg")
	else()
		set(ENABLE_FFMPEG true)
		list(APPEND INPUT_LIST "ffmpeg")
		list(APPEND INPUT_SRC "ffmpeg.c")
		set_source_files_properties(ffmpeg.c PROPERTIES COMPILE_FLAGS "${FFMPEG_CFLAGS}")
		mpd_add_lib(${FFMPEG_LIBRARIES})
	endif()
endif()

if(_ENABLE_mms)
	pkg_check_modules(MMS "mms")
	if(NOT MMS_FOUND)
		report_no_support("mms")
	else()
		set(ENABLE_MMS true)
		list(APPEND INPUT_SRC "mms.c")
		set_source_files_properties(mms.c PROPERTIES COMPILE_FLAGS "${MMS_CFLAGS}")
		mpd_add_lib(${MMS_LIBRARIES})
	endif()
endif()

if(_ENABLE_cdio)
	pkg_check_modules(CDIO "libcdio_paranoia")
	if(NOT CDIO_FOUND)
		report_no_support("cdio" "cdio_paranoia")
	else()
		set(ENABLE_CDIO true)
		list(APPEND INPUT_SRC "cdio_paranoia.c")
		set_source_files_properties(cdio_paranoia.c.c PROPERTIES COMPILE_FLAGS "${CDIO_CFLAGS}")
		mpd_add_lib(${CDIO_LIBRARIES})
	endif()
endif()

if(_ENABLE_despotify)
	pkg_check_modules(DESPOTIFY "despotify")
	if(NOT DESPOTIFY_FOUND)
		report_no_support("despotify")
	else()
		set(ENABLE_DESPOTIFY true)
		list(APPEND INPUT_SRC "despotify.c"
			"../despotify_utils.c")
		set_source_files_properties(despotify.c PROPERTIES COMPILE_FLAGS "${DESPOTIFY_CFLAGS}")
		set_source_files_properties(../despotify_utils.c PROPERTIES COMPILE_FLAGS "${DESPOTIFY_CFLAGS}")
		mpd_add_lib(${DESPOTIFY_LIBRARIES})
	endif()
endif()

configure_file(input_conf.h.in input_conf.h)
add_library(input STATIC ${INPUT_SRC})
target_link_libraries(input util arch)

set(INPUT_LIST ${INPUT_LIST} PARENT_SCOPE)
unset(list_var)
