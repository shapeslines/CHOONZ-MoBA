# Structural contract for the authoritative simulation target. This is evaluated
# during configure so an unexpected source, include, or direct link never reaches
# a compiler invocation.

function(moba_collect_sim_contract_errors out_variable sim_root sources links interface_links includes)
    set(errors "")

    file(TO_CMAKE_PATH "${sim_root}" normalized_root)
    string(REGEX REPLACE "/$" "" normalized_root "${normalized_root}")

    foreach(source IN LISTS sources)
        file(TO_CMAKE_PATH "${source}" normalized_source)
        if(IS_ABSOLUTE "${normalized_source}")
            file(RELATIVE_PATH relative_source "${normalized_root}" "${normalized_source}")
        else()
            set(relative_source "${normalized_source}")
        endif()
        if(NOT relative_source MATCHES "^src/[A-Za-z0-9_./-]+\\.cpp$")
            list(APPEND errors "forbidden eng_sim implementation source '${source}'")
        endif()
    endforeach()

    set(allowed_links
        eng::core
        eng::math
        eng::serialize
        moba_warnings
        moba_options
        moba::sim_determinism)
    foreach(link IN LISTS links)
        list(FIND allowed_links "${link}" allowed_at)
        if(allowed_at EQUAL -1)
            list(APPEND errors "forbidden direct link dependency '${link}'")
        endif()
    endforeach()
    foreach(required IN LISTS allowed_links)
        set(required_count 0)
        foreach(link IN LISTS links)
            if(link STREQUAL required)
                math(EXPR required_count "${required_count} + 1")
            endif()
        endforeach()
        if(NOT required_count EQUAL 1)
            list(APPEND errors
                "required direct link '${required}' appears ${required_count} times")
        endif()
    endforeach()

    # Static-library PRIVATE dependencies appear in INTERFACE_LINK_LIBRARIES as
    # LINK_ONLY entries so the final executable can resolve the archive without
    # inheriting their usage requirements. Permit only the three named policies.
    set(allowed_interface_links
        eng::core
        eng::math
        eng::serialize
        "$<LINK_ONLY:moba_warnings>"
        "$<LINK_ONLY:moba_options>"
        "$<LINK_ONLY:moba::sim_determinism>")
    foreach(link IN LISTS interface_links)
        list(FIND allowed_interface_links "${link}" allowed_at)
        if(allowed_at EQUAL -1)
            list(APPEND errors "forbidden exported link dependency '${link}'")
        endif()
    endforeach()
    foreach(required IN LISTS allowed_interface_links)
        set(required_count 0)
        foreach(link IN LISTS interface_links)
            if(link STREQUAL required)
                math(EXPR required_count "${required_count} + 1")
            endif()
        endforeach()
        if(NOT required_count EQUAL 1)
            list(APPEND errors
                "required exported link '${required}' appears ${required_count} times")
        endif()
    endforeach()

    set(allowed_includes "${normalized_root}/include" "${normalized_root}/src")
    set(normalized_includes "")
    foreach(include IN LISTS includes)
        file(TO_CMAKE_PATH "${include}" normalized_include)
        string(REGEX REPLACE "/$" "" normalized_include "${normalized_include}")
        list(APPEND normalized_includes "${normalized_include}")
        list(FIND allowed_includes "${normalized_include}" allowed_at)
        if(allowed_at EQUAL -1)
            list(APPEND errors "forbidden direct include directory '${include}'")
        endif()
    endforeach()
    foreach(required IN LISTS allowed_includes)
        set(required_count 0)
        foreach(include IN LISTS normalized_includes)
            if(include STREQUAL required)
                math(EXPR required_count "${required_count} + 1")
            endif()
        endforeach()
        if(NOT required_count EQUAL 1)
            list(APPEND errors
                "required direct include '${required}' appears ${required_count} times")
        endif()
    endforeach()

    set(${out_variable} "${errors}" PARENT_SCOPE)
endfunction()

function(moba_enforce_sim_target target sim_root)
    if(NOT TARGET "${target}")
        message(FATAL_ERROR "sim isolation target does not exist: ${target}")
    endif()

    get_target_property(target_type "${target}" TYPE)
    if(NOT target_type STREQUAL "STATIC_LIBRARY")
        message(FATAL_ERROR "${target} must remain a STATIC_LIBRARY; found ${target_type}")
    endif()

    get_target_property(sources "${target}" SOURCES)
    get_target_property(links "${target}" LINK_LIBRARIES)
    get_target_property(interface_links "${target}" INTERFACE_LINK_LIBRARIES)
    get_target_property(includes "${target}" INCLUDE_DIRECTORIES)
    foreach(property IN ITEMS sources links interface_links includes)
        if("${${property}}" MATCHES "-NOTFOUND$")
            set(${property} "")
        endif()
    endforeach()

    moba_collect_sim_contract_errors(errors "${sim_root}"
        "${sources}" "${links}" "${interface_links}" "${includes}")
    if(errors)
        list(JOIN errors "\n  " formatted)
        message(FATAL_ERROR "${target} structural isolation violated:\n  ${formatted}")
    endif()
endfunction()
