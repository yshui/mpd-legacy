set(MIXER_SRC
	software.c
	oss.c
)

if(ENABLE_ALSA)
	set(MIXER_SRC ${MIXER_SRC}
		alsa.c
	)
endif()

if(ENABLE_WINMM)
	set(MIXER_SRC ${MIXER_SRC}
		winmm.c
	)
endif()

if(ENABLE_ROAR)
	set(MIXER_SRC ${MIXER_SRC}
		roar.c
	)
endif()

if(ENABLE_PULSE)
	set(MIXER_SRC ${MIXER_SRC}
		pulse.c
	)
endif()

add_library(mixer
	${MIXER_SRC}
)

target_link_libraries(mixer util)
