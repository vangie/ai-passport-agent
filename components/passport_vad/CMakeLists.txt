file(GLOB vad_sources "src/vad/*.c" "src/signal_processing/*.c")
idf_component_register(SRCS "passport_vad.c" ${vad_sources} INCLUDE_DIRS ".")
target_compile_options(${COMPONENT_LIB} PRIVATE -Os)
