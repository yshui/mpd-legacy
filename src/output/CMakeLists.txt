set(OUTPUT_SRC 
	null.c
	pipe.c
	httpd.c
	httpd_client.c
	icy_server.c
	recorder.c
	mvp.c
)
set(ALL_OUTPUT
	alsa
	pulse
	shout
	roar
	ffado
	ao
	jack
	openal
)

set(list_var OUTPUT_LIST)
set(src_var OUTPUT_SRC)

check_enabled(ALL_OUTPUT ENABLE_OUTPUT)

check_function_exists(mkfifo HAVE_FIFO)
if(HAVE_FIFO)
	list(APPEND OUTPUT_SRC "fifo.c")
endif()

mpd_pc_single(alsa alsa alsa.c)
set(ENABLE_ALSA ${ENABLE_ALSA} PARENT_SCOPE)

mpd_pc_single(pulse libpulse pulse.c)
set(ENABLE_PULSE ${ENABLE_PULSE} PARENT_SCOPE)

mpd_pc_single(shout shout shout.c)
mpd_pc_single(roar roar roar.c)
set(ENABLE_ROAR ${ENABLE_ROAR} PARENT_SCOPE)

mpd_pc_single(ffado libffado ffado.c)
mpd_pc_single(ao ao ao.c)
mpd_pc_single(alsa alsa alsa.c)
mpd_pc_single(jack jack>=0.100 jack.c)
if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
	if(_ENABLE_openal)
		find_library(OPENAL_LIBRARIES OpenAL)
		mark_as_advanced(OPENAL_LIBRARIES)
		check_include_file(AL/al.h AL_H)
		check_include_file(AL/alc.h ALC_H)
		set(HAVE_OPENAL true)
		if(NOT OPENAL_LIBRARIES)
			set(HAVE_OPENAL false)
		endif()
		if(NOT AL_H OR NOT ALC_H)
			set(HAVE_OPENAL false)
		endif()
		if(HAVE_OPENAL)
			set(ENABLE_OPENAL true)
			list(APPEND OUTPUT_SRC openal.c)
			mpd_add_lib(${OPENAL_LIBRARIES})
		endif()
	endif()
	find_library(AU AudioUnit)
	find_library(COREAUDIO CoreAudio)
	find_library(CORESERVICE CoreService)
	mark_as_advanced(AU COREAUDIO CORESERVICE)
	list(APPEND ${OUTPUT_SRC} osx.c)
	mpd_add_lib(${AU} ${CORESERVICE} ${COREAUDIO})
	set(ENABLE_OSX true)
else()
	mpd_pc_single(openal openal openal.c)
endif()

if(WIN32)
	set(ENABLE_WINMM true)
	set(ENABLE_WINMM true PARENT_SCOPE)
	list(APPEND ${OUTPUT_SRC} winmm.c)
	mpd_add_lib(winmm)
endif()

check_include_file(sys/soundcard.h HAVE_OSS)
check_include_file(soundcard.h HAVE_OSS_BSD)
if(HAVE_OSS OR HAVE_OSS_BSD)
	set(ENABLE_OSS true)
	list(APPEND OUTPUT_SRC oss.c)
endif()

if(${CMAKE_SYSTEM_NAME} MATCHES SunOS)
	set(ENABLE_SOLARIES true)
	list(APPEND OUTPUT_SRC solaris.c)
endif()

configure_file(output_conf.h.in output_conf.h)
add_library(output STATIC ${OUTPUT_SRC})
target_link_libraries(output util)

set(OUTPUT_LIST ${OUTPUT_LIST} PARENT_SCOPE)
unset(list_var)
unset(src_var)
