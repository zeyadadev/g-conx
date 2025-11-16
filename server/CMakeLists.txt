add_executable(venus-server
    main.cpp
    server_state.cpp
    renderer_decoder.c
)

target_include_directories(venus-server PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(venus-server PRIVATE
    venus_common
    Vulkan::Vulkan
)
