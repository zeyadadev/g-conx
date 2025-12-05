add_executable(venus-test-app
    main.cpp
    phase01/phase01_test.cpp
    phase02/phase02_test.cpp
    phase03/phase03_test.cpp
    phase04/phase04_test.cpp
    phase05/phase05_test.cpp
    phase06/phase06_test.cpp
    phase07/phase07_test.cpp
    phase08/phase08_test.cpp
    phase09/phase09_test.cpp
    phase10/phase10_test.cpp
    phase09_1/phase09_1_test.cpp
    phase11/phase11_test.cpp
    phase12/phase12_test.cpp
    phase13/phase13_test.cpp
    phase14/phase14_test.cpp
)

target_include_directories(venus-test-app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/common
)

target_link_libraries(venus-test-app PRIVATE
    venus_common
    Vulkan::Vulkan
)

if(UNIX AND NOT APPLE)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(TESTAPP_XCB xcb)
    if(TESTAPP_XCB_FOUND)
        target_include_directories(venus-test-app PRIVATE ${TESTAPP_XCB_INCLUDE_DIRS})
        target_link_libraries(venus-test-app PRIVATE ${TESTAPP_XCB_LIBRARIES})
        target_compile_definitions(venus-test-app PRIVATE VENUS_TESTAPP_HAS_XCB VK_USE_PLATFORM_XCB_KHR)
    endif()
    pkg_check_modules(TESTAPP_WAYLAND wayland-client)
    if(TESTAPP_WAYLAND_FOUND)
        target_include_directories(venus-test-app PRIVATE ${TESTAPP_WAYLAND_INCLUDE_DIRS})
        target_link_libraries(venus-test-app PRIVATE ${TESTAPP_WAYLAND_LIBRARIES})
        target_compile_definitions(venus-test-app PRIVATE VENUS_TESTAPP_HAS_WAYLAND VK_USE_PLATFORM_WAYLAND_KHR)
    endif()
endif()

# Direct network test (bypasses Vulkan loader)
add_executable(venus-network-test
    phase01/network_test.cpp
)

target_link_libraries(venus-network-test PRIVATE
    venus_common
    Vulkan::Vulkan
)

target_include_directories(venus-network-test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/common
)
