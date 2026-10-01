# Example with multiple files and a clean structure
idf_component_register(
    SRCS "pqc_esp32_v4.c" "pqc_esp32.c" "pqc_esp32_v2.c" "pqc_esp32_v3.c" "pqc_benchmark.c"
    INCLUDE_DIRS "."
)

# Forward the -DBENCH argument to C preprocessor
if(DEFINED BENCH)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE "BENCH_${BENCH}=1")
endif()
