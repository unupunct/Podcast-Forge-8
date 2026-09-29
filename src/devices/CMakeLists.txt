add_library(pf8_devices STATIC
    DeviceInfo.cpp
    DeviceRegistry.cpp
    WinEndpointEnumerator.cpp
)
target_link_libraries(pf8_devices PUBLIC pf8_core PRIVATE setupapi ole32 propsys)
pf8_set_warnings(pf8_devices)
