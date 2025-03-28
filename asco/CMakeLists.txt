set(ASCO_SOURCES
    runtime.cpp
    sched.cpp
)

add_library(asco ${ASCO_SOURCES})
target_include_directories(asco PUBLIC ../)
target_compile_options(asco PUBLIC -fuse-ld=mold -fsanitize=undefined)
target_link_options(asco PUBLIC -lasan -lubsan -lstdc++)

add_library(asco-shared SHARED ${ASCO_SOURCES})
target_include_directories(asco-shared PUBLIC ../)
set_target_properties(asco-shared PROPERTIES OUTPUT_NAME asco)
