add_library(pf8_core STATIC
    RealtimeGuard.cpp
    Log.cpp
    Paths.cpp
    SystemStats.cpp
    Clock.cpp
    Json.cpp
    SettingsDb.cpp
)
target_include_directories(pf8_core PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(pf8_core PUBLIC shell32 ole32 PRIVATE sqlite3)
pf8_set_warnings(pf8_core)
