add_executable(venus-test-app
    main.cpp
    phase01/phase01_test.cpp
)

target_include_directories(venus-test-app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(venus-test-app PRIVATE
    Vulkan::Vulkan
)

# Direct network test (bypasses Vulkan loader)
add_executable(venus-network-test
    phase01/network_test.cpp
)

target_link_libraries(venus-network-test PRIVATE
    venus_common
    Vulkan::Vulkan
)
