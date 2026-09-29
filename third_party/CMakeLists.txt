# SQLite amalgamation
add_library(sqlite3 STATIC sqlite/sqlite3.c)
target_include_directories(sqlite3 PUBLIC sqlite)
target_compile_definitions(sqlite3 PRIVATE SQLITE_THREADSAFE=1 SQLITE_OMIT_LOAD_EXTENSION
                                           SQLITE_DEFAULT_WAL_SYNCHRONOUS=1 SQLITE_DQS=0)

# JUCE (AGPLv3)
add_subdirectory(JUCE EXCLUDE_FROM_ALL)

if(PF8_BUILD_TESTS)
    add_subdirectory(Catch2 EXCLUDE_FROM_ALL)
endif()
