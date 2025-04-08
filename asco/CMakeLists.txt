set(ASCO_SOURCES
    runtime.cpp
    sched.cpp
)

add_library(asco ${ASCO_SOURCES})
target_include_directories(asco PUBLIC ../)
# target_link_libraries(asco atomic)

add_library(asco-shared SHARED ${ASCO_SOURCES})
target_include_directories(asco-shared PUBLIC ../)
set_target_properties(asco-shared PROPERTIES OUTPUT_NAME asco)
