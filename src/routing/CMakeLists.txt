add_library(pf8_routing STATIC
    RoutingEngine.cpp
)
target_link_libraries(pf8_routing PUBLIC pf8_core)
pf8_set_warnings(pf8_routing)
