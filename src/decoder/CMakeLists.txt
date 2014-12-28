set(DECODER_SRC
	pcm.c
	dsdiff.c
	dsf.c
	dsdlib.c
	decoder_buffer.c
	flac_metadata.c
	flac_pcm.c
	flac_common.c
)

set(ALL_DECODER
	flac
	fluidsynth
	asap
	audiofile
	ffmpeg
	gme
	mad
	mikmod
	modplug
	mp4
	mpcdec
	mpg123
	sidplay
	sndfile
	vorbis
	wavpack
	wildmidi
)

set(list_var DECODER_LIST)
set(src_var DECODER_SRC)
check_enabled(ALL_DECODER ENABLE_DECODER)

if(_ENABLE_ffmpeg)
	if(NOT FFMPEG_FOUND)
		report_no_support(ffmpeg)
	else()
		set(ENABLE_FFMPEG true)
		list(APPEND DECODER_SRC ffmpeg.c ffmpeg_metadata.c)
		set_source_files_properties(ffmpeg.c ffmpeg_metadata.c
			PROPERTIES COMPILER_FLAGS "${FFMPEG_CFLAGS}")
		mpd_add_lib(${FFMPEG_LIBRARIES})
	endif()
endif()

if(_ENABLE_vorbis)
	if(USE_TREMOR)
		pkg_check_modules(VORBIS vorbisidec)
	else()
		pkg_check_modules(VORBIS vorbis vorbisfile ogg)
	endif()
	if(NOT VORBIS_FOUND)
		report_no_support(vorbis)
	else()
		set(ENABLE_VORBIS true)
		list(APPEND DECODER_SRC vorbis.c vorbis_comments.c)
		set_source_files_properties(vorbis.c vorbis_comments.c
			PROPERTIES COMPILER_FLAGS "${VORBIS_CFLAGS}")
		mpd_add_lib(${VORBIS_LIBRARIES})
	endif()
endif()

mpd_pc_single(flac flac>=1.1 flac.c)
mpd_pc_single(gme libgme gme.c)
mpd_pc_single(mikmod libmikmod mikmod.c)
mpd_pc_single(mad mad mad.c)
mpd_pc_single(modplug libmodplug modplug.c)
mpd_pc_single(mpg123 libmpg123 mpg123.c)
mpd_pc_single(sndfile sndfile sndfile.c)
mpd_pc_single(wavpack wavpack wavpack.c)
mpd_pc_single(fluidsynth fluidsynth>=1.1 fluidsynth.c)
mpd_pc_single(audiofile audiofile>=0.1.7 audiofile.c)
#sidplay
#faad and mp4ff
#mpc
#wildmidi


add_library(decoder STATIC ${DECODER_SRC})
configure_file(decoder_conf.h.in decoder_conf.h)
set(DECODER_LIST ${DECODER_LIST} PARENT_SCOPE)
unset(list_var)
unset(src_var)