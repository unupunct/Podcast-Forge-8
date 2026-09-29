add_library(pf8_ui STATIC
    LookAndFeel.cpp
    TopBar.cpp
    DeviceListView.cpp
    Widgets.cpp
    ChannelStripView.cpp
    MasterStripView.cpp
    MixerView.cpp
    RoutingGridView.cpp
    DeviceMatrixView.cpp
    VerifyUi.cpp
    MainWindow.cpp
)
target_link_libraries(pf8_ui PUBLIC pf8_juce pf8_engine pf8_devices pf8_core)
target_compile_options(pf8_ui PRIVATE /utf-8 /MP)
target_compile_definitions(pf8_ui PRIVATE NOMINMAX)
