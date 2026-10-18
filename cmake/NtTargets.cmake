# id | NTDDI_VERSION | _WIN32_WINNT | subsystem version | display name
set(_NT_TARGET_TABLE
    "win8|0x06020000|0x0602|6.02|Windows 8"
    "winblue|0x06030000|0x0603|6.03|Windows 8.1"
    "win10|0x0A000000|0x0A00|10.00|Windows 10 (RTM)"
    "win10_ni|0x0A00000C|0x0A00|10.00|Windows 10 (NI)"
    "win11|0x0A000010|0x0A00|10.00|Windows 11"
    CACHE INTERNAL "NTDDI target table")

function(nt_target_list out_var)
    set(_ids "")
    foreach(_row IN LISTS _NT_TARGET_TABLE)
        string(REPLACE "|" ";" _cols "${_row}")
        list(GET _cols 0 _id)
        list(APPEND _ids "${_id}")
    endforeach()
    set(${out_var} "${_ids}" PARENT_SCOPE)
endfunction()

function(nt_target_info id)
    foreach(_row IN LISTS _NT_TARGET_TABLE)
        string(REPLACE "|" ";" _cols "${_row}")
        list(GET _cols 0 _id)
        if(NOT _id STREQUAL id)
            continue()
        endif()
        list(GET _cols 1 _ntddi)
        list(GET _cols 2 _winnt)
        list(GET _cols 3 _subsys)
        list(GET _cols 4 _display)

        # arm64 images cannot claim a subsystem older than 10.00
        if(NT_ARCH STREQUAL "arm64" AND _subsys VERSION_LESS "10.00")
            set(_subsys "10.00")
        endif()

        set(NT_TARGET_ID                 "${_id}"      PARENT_SCOPE)
        set(NT_TARGET_NTDDI              "${_ntddi}"   PARENT_SCOPE)
        set(NT_TARGET_WIN32_WINNT        "${_winnt}"   PARENT_SCOPE)
        set(NT_TARGET_SUBSYSTEM_VERSION  "${_subsys}"  PARENT_SCOPE)
        set(NT_TARGET_DISPLAY_NAME       "${_display}" PARENT_SCOPE)
        return()
    endforeach()

    nt_target_list(_known)
    string(REPLACE ";" ", " _known "${_known}")
    message(FATAL_ERROR
        "Unknown NT_TARGET '${id}'.\n"
        "Known targets: ${_known}")
endfunction()
