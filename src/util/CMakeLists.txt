add_library(util STATIC
	bit_reverse.c
	byte_reverse.c
	file_utils.c
	list_sort.c
)

target_include_directories(util PUBLIC .)
