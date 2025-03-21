set(ASCO_SOURCES
    runtime.cpp
    sched.cpp
)

add_library(asco ${ASCO_SOURCES})
target_include_directories(asco PUBLIC ../)
