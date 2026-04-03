# retry_copy.cmake – copy a file with retries to work around transient locks
# (e.g. Windows Defender scanning or anti-cheat software locking executables).
#
# Usage:
#   cmake -DSRC=<source> -DDST=<destination> -P retry_copy.cmake
#
# Optional:
#   -DMAX_RETRIES=<n>   (default 5)
#   -DDELAY_MS=<ms>     (default 500)

if(NOT DEFINED SRC OR NOT DEFINED DST)
    message(FATAL_ERROR "retry_copy.cmake: SRC and DST must be defined.")
endif()

if(NOT DEFINED MAX_RETRIES)
    set(MAX_RETRIES 5)
endif()
if(NOT DEFINED DELAY_MS)
    set(DELAY_MS 500)
endif()

# Convert delay from milliseconds to seconds for cmake sleep
math(EXPR DELAY_SEC "${DELAY_MS} / 1000")
if(DELAY_SEC LESS 1)
    set(DELAY_SEC 1)
endif()

foreach(attempt RANGE 1 ${MAX_RETRIES})
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${SRC}" "${DST}"
        RESULT_VARIABLE result
        ERROR_QUIET
    )
    if(result EQUAL 0)
        return()
    endif()
    if(attempt LESS ${MAX_RETRIES})
        message(STATUS "retry_copy: attempt ${attempt}/${MAX_RETRIES} failed for ${SRC}, retrying in ${DELAY_SEC}s...")
        execute_process(COMMAND ${CMAKE_COMMAND} -E sleep ${DELAY_SEC})
    endif()
endforeach()

# Don't fail the build – the compilation succeeded, only the deploy copy failed.
# This commonly happens when anti-cheat software (e.g. Vanguard) locks .exe files.
message(WARNING "retry_copy: could not copy ${SRC} -> ${DST} after ${MAX_RETRIES} attempts. "
    "The file is likely locked by another process (anti-cheat, antivirus). "
    "Stop the locking process or change ENGINE_DEPLOY_DIR.")
