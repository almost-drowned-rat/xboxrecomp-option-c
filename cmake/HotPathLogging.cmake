# HotPathLogging.cmake - console output of the hot-path libraries goes through the logger thread.
#
# Standing rule (maintainer, 2026-10-27): no blocking file or log I/O in kernel-call, DPC, ISR or
# one-CPU-gate-holding paths. xboxrecomp_hot_path_logging(<target>) force-includes src/kernel/log_async.h
# into every source of <target> (fprintf/printf/fputs/fwrite/fflush on stderr/stdout are queued, see the
# header) and links the logger. tools/test_hot_path_io.py checks that the kernel, USB and APU libraries
# call it and that their sources do not get around it.

get_filename_component(XBOXRECOMP_LOG_DIR "${CMAKE_CURRENT_LIST_DIR}/../src/kernel" ABSOLUTE)

if(NOT TARGET xbox_log)
    add_library(xbox_log STATIC "${XBOXRECOMP_LOG_DIR}/log_async.c")
    get_filename_component(_xbox_log_src "${XBOXRECOMP_LOG_DIR}/.." ABSOLUTE)
    target_include_directories(xbox_log PUBLIC "${XBOXRECOMP_LOG_DIR}" "${_xbox_log_src}")
    if(WIN32)
        target_link_libraries(xbox_log PUBLIC platform kernel32)
    else()
        target_link_libraries(xbox_log PUBLIC platform)
    endif()
endif()

function(xboxrecomp_hot_path_logging target)
    if(MSVC)
        target_compile_options(${target} PRIVATE "/FI${XBOXRECOMP_LOG_DIR}/log_async.h")
    else()
        target_compile_options(${target} PRIVATE -include "${XBOXRECOMP_LOG_DIR}/log_async.h")
    endif()
    if(NOT WIN32)
        # The forced header includes <stdio.h> before any source line, so a source's own
        # #define _GNU_SOURCE (kernel_file.c: FNM_CASEFOLD) would come too late.
        target_compile_definitions(${target} PRIVATE _GNU_SOURCE)
    endif()
    target_link_libraries(${target} PUBLIC xbox_log)
endfunction()
