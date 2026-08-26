# Configure-time mirror of the portable asset-path contract in asset_id.h.
# This gate runs before any discovered path is used as generated build input.
function(moba_asset_path_is_canonical asset_path out_valid)
    set(_valid TRUE)
    string(LENGTH "${asset_path}" _length)
    if(_length EQUAL 0 OR _length GREATER_EQUAL 512)
        set(_valid FALSE)
    elseif(NOT "${asset_path}" MATCHES "^[a-z0-9._/-]+$")
        set(_valid FALSE)
    elseif("${asset_path}" MATCHES "(^|/)(\\.|\\.\\.)(/|$)" OR
           "${asset_path}" MATCHES "//" OR
           NOT "${asset_path}" MATCHES "\\.(tga|wav)$")
        set(_valid FALSE)
    endif()

    if(_valid)
        string(REPLACE "/" ";" _segments "${asset_path}")
        foreach(_segment IN LISTS _segments)
            if(_segment STREQUAL "" OR _segment MATCHES "\\.$" OR
               _segment MATCHES "^(con|prn|aux|nul|com[1-9]|lpt[1-9])(\\..*)?$")
                set(_valid FALSE)
                break()
            endif()
        endforeach()
    endif()
    set(${out_valid} ${_valid} PARENT_SCOPE)
endfunction()

function(moba_require_canonical_asset_path asset_path)
    moba_asset_path_is_canonical("${asset_path}" _valid)
    if(NOT _valid)
        message(FATAL_ERROR
            "Asset source path is not canonical and portable: '${asset_path}'")
    endif()
endfunction()
