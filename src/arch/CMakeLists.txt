add_library(arch STATIC
	c11thread.c
)

target_include_directories(arch PUBLIC .)
