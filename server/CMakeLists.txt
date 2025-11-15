add_executable(venus-server
    main.cpp
)

target_link_libraries(venus-server PRIVATE
    venus_common
    Vulkan::Vulkan
)
