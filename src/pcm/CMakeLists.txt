set(PCM_SRC
    pcm_buffer.c
    pcm_export.c
    pcm_convert.c
    dsd2pcm.c
    pcm_dsd.c
    pcm_dsd_usb.c
    pcm_volume.c
    pcm_mix.c
    pcm_channels.c
    pcm_pack.c
    pcm_format.c
    pcm_resample.c
    pcm_resample_fallback.c
    pcm_dither.c
)
option(ENABLE_LIBSAMPLERATE "Enable libsamplerate resample support." ON)
if(ENABLE_LIBSAMPLERATE)
	pkg_check_modules(SRC "samplerate>=0.1.3")
	if(NOT SRC_FOUND)
		pkg_check_modules(SRC "samplerate")
		set(HAVE_LIBSAMPLERATE_NOINT true)
	endif()
	if(SRC_FOUND)
		set(ENABLE_SRC true)
		set(ENABLE_SRC true PARENT_SCOPE)
		list(APPEND PCM_SRC pcm_resample_libsamplerate.c)
		mpd_add_lib(${SRC_LIBRARIES})
	else()
		message("libsamplerate not found, not enabling libsamplerate support.")
	endif()
endif()
configure_file(pcm_conf.h.in pcm_conf.h)
add_library(pcm STATIC ${PCM_SRC})
target_link_libraries(pcm util)
