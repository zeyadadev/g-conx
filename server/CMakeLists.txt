add_executable(venus-server
    main.cpp
    server_state.cpp
    renderer_decoder.c
    state/fake_gpu_data.cpp
    state/fake_gpu_data_bridge.cpp
    state/resource_tracker.cpp
    state/memory_requirements.cpp
    state/binding_validator.cpp
    state/command_buffer_state.cpp
    state/command_validator.cpp
)

target_include_directories(venus-server PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(venus-server PRIVATE
    venus_common
    Vulkan::Vulkan
)
