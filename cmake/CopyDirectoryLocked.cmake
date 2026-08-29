if(NOT DEFINED CNA_COPY_SOURCE OR NOT IS_DIRECTORY "${CNA_COPY_SOURCE}")
    message(FATAL_ERROR "CNA_COPY_SOURCE must name an existing directory")
endif()
if(NOT DEFINED CNA_COPY_DESTINATION OR CNA_COPY_DESTINATION STREQUAL "")
    message(FATAL_ERROR "CNA_COPY_DESTINATION must not be empty")
endif()
if(NOT DEFINED CNA_COPY_LOCK OR CNA_COPY_LOCK STREQUAL "")
    message(FATAL_ERROR "CNA_COPY_LOCK must not be empty")
endif()

get_filename_component(_cna_copy_destination_parent "${CNA_COPY_DESTINATION}" DIRECTORY)
file(MAKE_DIRECTORY "${_cna_copy_destination_parent}")
file(LOCK "${CNA_COPY_LOCK}" GUARD PROCESS TIMEOUT 300 RESULT_VARIABLE _cna_copy_lock_result)
if(NOT _cna_copy_lock_result EQUAL 0)
    message(FATAL_ERROR "Could not lock ${CNA_COPY_LOCK}: ${_cna_copy_lock_result}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${CNA_COPY_SOURCE}" "${CNA_COPY_DESTINATION}"
    RESULT_VARIABLE _cna_copy_result)
if(NOT _cna_copy_result EQUAL 0)
    message(FATAL_ERROR
        "Could not copy ${CNA_COPY_SOURCE} to ${CNA_COPY_DESTINATION}")
endif()
