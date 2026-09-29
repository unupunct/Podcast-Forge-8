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

# All JUCE modules compiled once into a static library shared by the app, UI and tests.
add_library(pf8_juce STATIC)
target_link_libraries(pf8_juce
    PRIVATE
        juce::juce_core juce::juce_events juce::juce_data_structures juce::juce_graphics
        juce::juce_gui_basics juce::juce_audio_basics juce::juce_audio_formats
    PUBLIC
        juce::juce_recommended_config_flags)
target_compile_definitions(pf8_juce
    PUBLIC
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_LOAD_CURL_SYMBOLS_LAZILY=0
        JUCE_STANDALONE_APPLICATION=1 JUCE_USE_FLAC=1 JUCE_USE_OGGVORBIS=0 JUCE_USE_MP3AUDIOFORMAT=1
        JUCE_USE_WINDOWS_MEDIA_FORMAT=0 JUCE_REPORT_APP_USAGE=0 JUCE_STRICT_REFCOUNTEDPOINTER=1
        JUCE_APPLICATION_NAME_STRING="Podcast Forge 8"
        JUCE_APPLICATION_VERSION_STRING="${PROJECT_VERSION}"
    INTERFACE
        $<TARGET_PROPERTY:pf8_juce,COMPILE_DEFINITIONS>)
target_include_directories(pf8_juce INTERFACE $<TARGET_PROPERTY:pf8_juce,INCLUDE_DIRECTORIES>)
