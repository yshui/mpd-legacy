add_library(filter
	    audio_compress.c
	    null.c
	    chain.c
	    autoconvert.c
	    convert.c
	    route.c
	    normalize.c
	    replay_gain.c
	    volume.c
)
target_link_libraries(filter util)
