add_library(venus_common STATIC
    network/socket_utils.cpp
    network/network_client.cpp
    network/network_server.cpp
)

# Enable position-independent code for static library
set_target_properties(venus_common PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

target_include_directories(venus_common PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/venus-protocol
)

target_link_libraries(venus_common PUBLIC
    Vulkan::Vulkan
)
