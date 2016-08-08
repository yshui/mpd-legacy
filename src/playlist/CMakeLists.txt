set(PLAYLIST_SRC
	extm3u.c
	m3u.c
	pls.c
	xspf.c
	asx.c
	rss.c
	cue.c
	embcue.c
)
set(ALL_PLAYLIST
	despotify
	lastfm
	soundcloud
)
set(list_var PLAYLIST_LIST)
check_enabled(ALL_PLAYLIST ENABLE_PLAYLIST)

if(_ENABLE_despotify)
	pkg_check_modules(DESPOTIFY "despotify")
	if(NOT DESPOTIFY_FOUND)
		report_no_support("despotify")
	else()
		set(ENABLE_DESPOTIFY true)
		list(APPEND PLAYLIST_SRC "despotify.c"
			"../despotify_utils.c")
		set_source_files_properties(despotify.c PROPERTIES COMPILE_FLAGS "${DESPOTIFY_CFLAGS}")
		set_source_files_properties(../despotify_utils.c PROPERTIES COMPILE_FLAGS "${DESPOTIFY_CFLAGS}")
		mpd_add_lib(${DESPOTIFY_LIBRARIES})
	endif()
endif()

if(_ENABLE_lastfm)
	if(NOT CURL_FOUND)
		report_no_support("lastfm")
	else()
		set(ENABLE_LASTFM true)
		list(APPEND PLAYLIST_SRC "lastfm.c")
		set_source_files_properties(lastfm.c PROPERTIES COMPILE_FLAGS "${CURL_CFLAGS}")
		mpd_add_lib(${CURL_LIBRARIES})
	endif()
endif()

if(_ENABLE_soundcloud)
	pkg_check_modules(YAJL "yajl")
	if(NOT YAJL_FOUND)
		report_no_support("soundcloud")
	else()
		set(ENABLE_SOUNDCLOUD true)
		list(APPEND PLAYLIST_SRC "soundcloud.c")
		set_source_files_properties(soundcloud.c PROPERTIES COMPILE_FLAGS "${YAJL_CFLAGS}")
		mpd_add_lib(${YAJL_LIBRARIES})
	endif()
endif()

set(PLAYLIST_LIST ${PLAYLIST_LIST} PARENT_SCOPE)
unset(list_var)
add_library(playlist ${PLAYLIST_SRC})
target_link_libraries(playlist util arch)
configure_file(playlist_conf.h.in playlist_conf.h)
