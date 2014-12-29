set(ENABLE_DECODER ffmpeg
	vorbis
	flac
	gme
	mikmod
	mad
	modplug
	mpg123
	sndfile
	wavpack
	fluidsynth
	audiofile
	mpcdec
	asap
	wildmidi
	CACHE STRING
"A semicolon seperated list of enabled decoders.
For a whole list of available decoders, see the DECODER file."
)

set(ENABLE_ENCODER "lame;twolame;vorbis;flac" CACHE STRING
"A semicolon seperated list of enabled encoders.
Available encoders are lame, twolame, flac and vorbis."
)

set(ENABLE_OUTPUT "alsa;pulse;shout;roar;jack;ffado;ao;openal" CACHE STRING
"A semicolon seperated list of enabled output plugins."
)

set(ENABLE_INPUT "ffmpeg;curl;despotify" CACHE STRING
"A semicolon seperated list of enabled input plugins.
Available input plugins are cdio, curl, despotify,
ffmpeg, mms"
)

set(ENABLE_PLAYLIST "lastfm;despotify;soundcloud" CACHE STRING
"A semicolon seperated list of enabled playlist plugins.
Available playlist plugins are lastfm, despotify, soundcloud"
)

option(ENABLE_STICKER "Enable sticker database support." ON)
option(ENABLE_ID3TAG "Enable id3tag support." ON)
option(ENABLE_SYSTEMD_ACTIVATION "Systemd activation support." ON)
set(ZEROCONF "avahi" CACHE STRING "Zeroconf service provider, can either be avahi
or bonjour. Other value will disable zeroconf")
option(COMPRESS_DB "Compress the database." ON)
option(USE_TREMOR "Use tremor fixed point vorbis decoder instead." OFF)

include(CheckLibraryExists)
include(CheckFunctionExists)
include(CheckSymbolExists)
include(CheckStructHasMember)
include(CheckIncludeFile)
include(CheckCCompilerFlag)
include(TestLargeFile)
include(FindPkgConfig)

pkg_check_modules(GLIB REQUIRED glib-2.0 gthread-2.0)
include_directories(${GLIB_INCLUDE_DIRS})
add_definitions(-std=gnu11 -D_GNU_SOURCE)

if("${CMAKE_SYSTEM_NAME}" MATCHES "Darwin")
	add_definitions(-D_DARWIN_USE_64_BIT_INODE=1)
endif()

include_directories(.)
set(MPD_SRC
	output_list.c
	output_all.c
	output_thread.c
	output_control.c
	output_state.c
	output_print.c
	output_command.c
	output_plugin.c
	output_finish.c
	output_init.c
	input_init.c
	input_stream.c
	input_list.c
	input_internal.c
	mixer_control.c
	mixer_type.c
	mixer_all.c
	mixer_api.c
	clock.c
	notify.c
	audio_config.c
	audio_check.c
	audio_format.c
	audio_parser.c
	protocol/argparser.c
	protocol/result.c
	command.c
	idle.c
	cmdline.c
	conf.c
	crossfade.c
	cue/cue_parser.c
	dbUtils.c
	decoder_list.c
	decoder_plugin.c
	decoder_thread.c
	decoder_control.c
	decoder_api.c
	decoder_internal.c
	decoder_print.c
	encoder_list.c
	directory.c
	database.c
	db_lock.c
	db_print.c
	db/simple_db_plugin.c
	exclude.c
	fd_util.c
	fifo_buffer.c
	growing_fifo.c
	filter_config.c
	filter_plugin.c
	filter_registry.c
	update.c
	update_queue.c
	update_io.c
	update_db.c
	update_walk.c
	update_song.c
	update_container.c
	update_remove.c
	client.c
	client_event.c
	client_expire.c
	client_global.c
	client_idle.c
	client_list.c
	client_new.c
	client_process.c
	client_read.c
	client_write.c
	client_message.c
	client_subscribe.c
	client_file.c
	server_socket.c
	listen.c
	log.c
	ls.c
	io_thread.c
	main.c
	main_win32.c
	event_pipe.c
	daemon.c
	buffer.c
	pipe.c
	chunk.c
	path.c
	mapper.c
	page.c
	permission.c
	player_thread.c
	player_control.c
	playlist.c
	playlist_list.c
	playlist_global.c
	playlist_control.c
	playlist_edit.c
	playlist_print.c
	playlist_save.c
	playlist_mapper.c
	playlist_any.c
	playlist_song.c
	playlist_state.c
	playlist_queue.c
	playlist_vector.c
	queue.c
	queue_print.c
	queue_save.c
	replay_gain_config.c
	replay_gain_info.c
	sig_handlers.c
	song.c
	song_update.c
	song_print.c
	song_sort.c
	resolver.c
	socket_util.c
	state_file.c
	stats.c
	tag.c
	tag_pool.c
	tag_print.c
	tag_handler.c
	tag_file.c
	tokenizer.c
	text_file.c
	text_input_stream.c
	strset.c
	uri.c
	utils.c
	string_util.c
	volume.c
	locate.c
	stored_playlist.c
	timer.c
	util/list_sort.c
	util/byte_reverse.c
	util/bit_reverse.c
	util/file_utils.c
	fd_util.c
	fifo_buffer.c
	inotify_source.c
	inotify_queue.c
	inotify_update.c

	replay_gain_ape.c
	tag_ape.c
	ape.c
)

macro(cflag_check_and_apply flag)
	string(REPLACE "-" "_" TMP "${flag}")
	string(REPLACE "=" "_" TMP "${TMP}")
	check_c_compiler_flag(${flag} is_${TMP}_supported)
	if(is_${TMP}_supported)
		message(STATUS "Compiler flag ${flag} is supported, add to CFLAGS")
		add_compile_options(${flag})
		get_directory_property(TMP COMPILE_OPTIONS)
	endif()
endmacro()

cflag_check_and_apply(-Wall)
cflag_check_and_apply(-pedantic)
cflag_check_and_apply(-Wall)
cflag_check_and_apply(-Wextra)
cflag_check_and_apply(-Wmissing-prototypes)
cflag_check_and_apply(-Wshadow)
cflag_check_and_apply(-Wpointer-arith)
cflag_check_and_apply(-Wstrict-prototypes)
cflag_check_and_apply(-Wcast-qual)
cflag_check_and_apply(-Wformat=2)
cflag_check_and_apply(-Werror=format)
cflag_check_and_apply(-Wwrite-strings)
cflag_check_and_apply(-Wno-unused-parameter)
cflag_check_and_apply(-Werror=incompatible-pointer-types)
cflag_check_and_apply(-Werror=implicit-function-declaration)
cflag_check_and_apply(-pedantic)

macro(report_no_support var1)
	set(var2 ${ARGN})
	if(NOT var2)
		set(var2 ${var1})
	endif()
	message(STATUS "${var2} doesn't exists, not enabling ${var1}.")
	list(REMOVE_ITEM ${list_var} ${var1})
	set(${var0} ${${var0}} PARENT_SCOPE)
endmacro()

macro(mpd_add_lib)
	list(APPEND MPD_LINK_LIBS ${ARGN})
	set(MPD_LINK_LIBS ${MPD_LINK_LIBS} PARENT_SCOPE)
endmacro()

macro(check_enabled input enable_list)
	set(tmp "")
	foreach(i ${${input}})
		list(FIND ${enable_list} "${i}" _ENABLE_${i})
		if(NOT _ENABLE_${i} EQUAL -1)
			set(_ENABLE_${i} true)
			list(APPEND tmp "${i}")
		else()
			set(_ENABLE_${i} false)
		endif()
	endforeach()
	set(${list_var} ${tmp})
endmacro()

macro(mpd_pc_single p_name pc_name)
	string(TOUPPER "enable_${p_name}" out_var)
	if(_ENABLE_${p_name})
		pkg_check_modules(${p_name} ${pc_name})
		if(NOT ${p_name}_FOUND)
			set(${out_var} false)
			report_no_support(${p_name} ${pc_name})
		else()
			set(${out_var} true)
			list(APPEND ${src_var} ${ARGN})
			set_source_files_properties(${ARGN} PROPERTIES COMPILER_FLAGS
				"${${p_name}_CFLAGS}")
			mpd_add_lib(${${p_name}_LIBRARIES})
		endif()
	else()
		set(${out_var} false)
	endif()
endmacro()

set(MPD_LINK_LIBS "${GLIB_LIBRARIES}")

list(APPEND MPD_SRC fd_util.c
		    fifo_buffer.c
		    inotify_source.c
		    inotify_queue.c
		    inotify_update.c
)
check_function_exists(inotify_init HAVE_INOTIFY_INIT)
check_function_exists(inotify_init1 HAVE_INOTIFY_INIT1)

if(ENABLE_STICKER)
	pkg_check_modules(SQLITE sqlite3)
	if(NOT SQLITE_FOUND)
		message(STATUS "sqlite support not found, not enabling sticker db")
		set(ENABLE_STICKER false)
	else()
		set(STICKER_SRC
			sticker.c
			song_sticker.c
			sticker_print.c
		)
		list(APPEND MPD_LINK_LIBS ${SQLITE_LIBRARIES})
		list(APPEND MPD_SRC ${STICKER_SRC})
		set_source_files_properties(${STICKER_SRC} PROPERTIES COMPILER_FLAGS
			"${SQLITE_CFLAGS}")
	endif()
endif()

if(ZEROCONF STREQUAL "avahi")
	pkg_check_modules(AVAHI avahi-glib avahi-client)
	if(NOT AVAHI_FOUND)
		message(STATUS "avahi is not found, not enabling avahi")
		set(ENABLE_AVAHI false)
	else()
		set(ENABLE_AVAHI true)
		set(ENABLE_ZEROCONF true)
		list(APPEND MPD_SRC zeroconf-avahi.c)
		list(APPEND MPD_LINK_LIBS ${AVAHI_LIBRARIES})
		set_source_files_properties(zeroconf-avahi.c PROPERTIES COMPILER_FLAGS
			"${AVAHI_CFLAGS}")
	endif()
endif()

if(ZEROCONF STREQUAL "bonjour")
	check_include_file(dns_sd.h DNS_SD_H)
	find_library(DNS_SD_LIB dns_sd)
	mark_as_advanced(DNS_SD_LIB)
	if(NOT DNS_SD_LIB OR NOT DNS_SD_H)
		set(HAVE_BONJOUR false)
	else()
		set(HAVE_BONJOUR true)
	endif()
	if(NOT HAVE_BONJOUR)
		message(STATUS "Bonjour support not found, not enabling")
		set(ENABLE_BONJOUR false)
	else()
		set(ENABLE_BONJOUR true)
		set(ENABLE_ZEROCONF true)
		list(APPEND MPD_SRC zeroconf-bonjour.c)
	endif()
endif()

if(ENABLE_ZEROCONF)
	list(APPEND MPD_SRC zeroconf.c)
endif()

if(ENABLE_SYSTEMD_ACTIVATION)
	pkg_check_modules(SYSTEMD_DAEMON libsystemd)
	if(NOT SYSTEMD_DAEMON_FOUND)
		report_no_support("systemd activation" "libsystemd")
		set(ENABLE_SYSTEMD_ACTIVATION false)
	else()
		list(APPEND MPD_LINK_LIBS ${SYSTEMD_DAEMON_LIBRARIES})
		set_source_files_properties(listen.c PROPERTIES COMPILER_FLAGS
			"${SYSTEMD_DAEMON_CFLAGS}")
	endif()
endif()

pkg_check_modules(ZLIB zlib)
if(NOT ZLIB_FOUND)
	set(COMPRESS_DB false)
else()
	set(HAVE_ZLIB true)
endif()
if(COMPRESS_DB)
	list(APPEND MPD_LINK_LIBS ${ZLIB_LIBRARIES})
endif()

pkg_check_modules(CURL libcurl)
pkg_check_modules(FFMPEG libavcodec libavformat libavutil)
pkg_check_modules(ID3TAG id3tag)

if(FFMPEG_FOUND)
	set(HAVE_FFMPEG true)
endif()

check_function_exists(accept4 HAVE_ACCEPT4)
check_function_exists(fork HAVE_FORK)
check_function_exists(pipe2 HAVE_PIPE2)
check_function_exists(daemon HAVE_DAEMON)

if(ID3TAG_FOUND AND ENABLE_ID3TAG)
	set(ID3TAG_SRC
		tag_id3.c
		tag_rva2.c
		aiff.c
		riff.c
	)
	list(APPEND MPD_LINK_LIBS ${ID3TAG_LIBRARIES})
	list(APPEND MPD_SRC ${ID3TAG_SRC})
	set_source_files_properties(${ID3TAG_SRC}
		PROPERTIES COMPILER_FLAGS
		"${ID3TAG_CFLAGS}")
else()
	set(ENABLE_ID3TAG false)
endif()

set(CMAKE_REQUIRED_LIBRARIES bsd)
check_function_exists(getpeereid HAVE_GETPEEREID)
unset(CMAKE_REQUIRED_LIBRARIES)
set(CMAKE_REQUIRED_DEFINITIONS -D_GNU_SOURCE)
check_struct_has_member("struct ucred" uid sys/socket.h HAVE_STRUCT_UCRED)

if(NOT HAVE_STRUCT_UCRED AND HAVE_GETPEEREID)
	list(APPEND MPD_LINK_LIBS bsd)
endif()

add_subdirectory(encoder)
add_subdirectory(decoder)
add_subdirectory(input)
add_subdirectory(playlist)
add_subdirectory(protocol)
add_subdirectory(output)
add_subdirectory(mixer)
add_subdirectory(filter)
add_subdirectory(pcm)

list(REMOVE_DUPLICATES MPD_LINK_LIBS)

add_executable(mpd ${MPD_SRC})
target_link_libraries(mpd
	encoder
	decoder
	playlist
	protocol
	input
	output
	mixer
	filter
	pcm
	${MPD_LINK_LIBS}
)
foreach(component OUTPUT INPUT ENCODER PLAYLIST DECODER)
	string(REPLACE ";" ", " TMP "${${component}_LIST}")
	message("Enabled ${component} plugins: ${TMP}")
endforeach()

configure_file(config.h.in config.h)
