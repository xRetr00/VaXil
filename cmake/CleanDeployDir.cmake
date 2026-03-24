if(NOT DEFINED DEPLOY_DIR OR DEPLOY_DIR STREQUAL "")
    message(FATAL_ERROR "DEPLOY_DIR is required")
endif()

if(NOT EXISTS "${DEPLOY_DIR}")
    file(MAKE_DIRECTORY "${DEPLOY_DIR}")
    return()
endif()

file(GLOB children RELATIVE "${DEPLOY_DIR}" "${DEPLOY_DIR}/*")
foreach(child IN LISTS children)
    if(child STREQUAL "logs")
        continue()
    endif()

    file(REMOVE_RECURSE "${DEPLOY_DIR}/${child}")
endforeach()
