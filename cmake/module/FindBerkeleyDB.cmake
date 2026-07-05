# FindBerkeleyDB.cmake
#
# Find BerkeleyDB 6.2 headers and libraries.
#
# This module defines:
#  BerkeleyDB_INCLUDE_DIRS, where to find db_cxx.h
#  BerkeleyDB_LIBRARIES, the libraries to link against to use BerkeleyDB.
#  BerkeleyDB_FOUND, If false, do not try to use BerkeleyDB.
#  BerkeleyDB_VERSION, the version of BerkeleyDB found.

set(BerkeleyDB_INCLUDE_DIRS "")
set(BerkeleyDB_LIBRARIES "")
set(BerkeleyDB_FOUND FALSE)
set(BerkeleyDB_VERSION "")

# Search paths for headers
set(BerkeleyDB_HEADER_SEARCH_PATHS
    /usr/local/include
    /usr/local/opt/berkeley-db/include
    /opt/local/include
    /usr/include
)

# Search for the header
find_path(BerkeleyDB_INCLUDE_DIR
    NAMES db_cxx.h
    PATHS ${BerkeleyDB_HEADER_SEARCH_PATHS}
    PATH_SUFFIXES db62 db6.2 libdb62 libdb6.2 db6 db5.3 db4.8
)

if(BerkeleyDB_INCLUDE_DIR)
    # Extract version from db_cxx.h (similar to BITCOIN_FIND_BDB62)
    file(READ "${BerkeleyDB_INCLUDE_DIR}/db_cxx.h" _BDB_HEADER_CONTENT)
    
    string(REGEX MATCH "#define[ \t]+DB_VERSION_MAJOR[ \t]+([0-9]+)" _BDB_MAJOR_MATCH "${_BDB_HEADER_CONTENT}")
    set(BerkeleyDB_VERSION_MAJOR "${CMAKE_MATCH_1}")
    
    string(REGEX MATCH "#define[ \t]+DB_VERSION_MINOR[ \t]+([0-9]+)" _BDB_MINOR_MATCH "${_BDB_HEADER_CONTENT}")
    set(BerkeleyDB_VERSION_MINOR "${CMAKE_MATCH_1}")
    
    set(BerkeleyDB_VERSION "${BerkeleyDB_VERSION_MAJOR}.${BerkeleyDB_VERSION_MINOR}")

    # Blackcoin More REQUIREMENT: BDB 6.2
    if(NOT "${BerkeleyDB_VERSION}" VERSION_EQUAL "6.2")
        message(WARNING "Found BerkeleyDB ${BerkeleyDB_VERSION} at ${BerkeleyDB_INCLUDE_DIR}, but Blackcoin More requires 6.2 for portable wallet support.")
        if(NOT BerkeleyDB_FIND_QUIETLY)
            message(STATUS "If this is intended, use -DWITH_INCOMPATIBLE_BDB=ON")
        endif()
        
        if(NOT WITH_INCOMPATIBLE_BDB)
            set(BerkeleyDB_FOUND FALSE)
        else()
             set(BerkeleyDB_FOUND TRUE)
        endif()
    else()
        set(BerkeleyDB_FOUND TRUE)
    endif()
endif()

if(BerkeleyDB_FOUND)
    # Search for the library
    set(BerkeleyDB_LIBRARY_SEARCH_NAMES db_cxx-6.2 db_cxx db6_cxx)
    
    find_library(BerkeleyDB_LIBRARY
        NAMES ${BerkeleyDB_LIBRARY_SEARCH_NAMES}
        PATHS /usr/local/lib /usr/local/opt/berkeley-db/lib /opt/local/lib /usr/lib
    )
    
    if(BerkeleyDB_LIBRARY)
        set(BerkeleyDB_LIBRARIES ${BerkeleyDB_LIBRARY})
        set(BerkeleyDB_INCLUDE_DIRS ${BerkeleyDB_INCLUDE_DIR})
    else()
        set(BerkeleyDB_FOUND FALSE)
    endif()
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(BerkeleyDB
    REQUIRED_VARS BerkeleyDB_LIBRARIES BerkeleyDB_INCLUDE_DIRS
    VERSION_VAR BerkeleyDB_VERSION
)

mark_as_advanced(BerkeleyDB_INCLUDE_DIR BerkeleyDB_LIBRARY)
