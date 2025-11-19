add_library(venus_icd SHARED
    icd/icd_entrypoints.cpp
    state/handle_allocator.cpp
    state/instance_state.cpp
    state/device_state.cpp
    state/resource_state.cpp
    state/shadow_buffer.cpp
    state/command_buffer_state.cpp
    state/sync_state.cpp
    state/pipeline_state.cpp
    state/swapchain_state.cpp
    wsi/headless_wsi.cpp
    wsi/linux_surface.cpp
    wsi/linux_wsi.cpp
)

# Set default symbol visibility to hidden - only VP_PUBLIC symbols should be exported
set_target_properties(venus_icd PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    C_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
)

target_include_directories(venus_icd PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Important: ICD must NOT link against Vulkan loader (Vulkan::Vulkan)
# This would create circular dependency and symbol conflicts
# We only need Vulkan headers, which are provided by venus_common
target_link_libraries(venus_icd PRIVATE
    venus_common
)

if(UNIX AND NOT APPLE)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(XCB REQUIRED xcb)
    pkg_check_modules(X11_XCB REQUIRED x11-xcb)
    pkg_check_modules(XCB_DRI3 REQUIRED xcb-dri3)
    pkg_check_modules(XCB_PRESENT REQUIRED xcb-present)
    pkg_check_modules(GBM REQUIRED gbm)
    pkg_check_modules(DRM REQUIRED libdrm)
    find_package(X11 REQUIRED)
    target_include_directories(venus_icd PRIVATE
        ${XCB_INCLUDE_DIRS}
        ${X11_INCLUDE_DIR}
        ${XCB_DRI3_INCLUDE_DIRS}
        ${XCB_PRESENT_INCLUDE_DIRS}
        ${GBM_INCLUDE_DIRS}
        ${DRM_INCLUDE_DIRS})
    target_link_libraries(venus_icd PRIVATE
        ${XCB_LIBRARIES}
        ${X11_LIBRARIES}
        ${X11_XCB_LIBRARIES}
        ${XCB_DRI3_LIBRARIES}
        ${XCB_PRESENT_LIBRARIES}
        ${GBM_LIBRARIES}
        ${DRM_LIBRARIES})
    target_compile_definitions(venus_icd PRIVATE
        VK_USE_PLATFORM_XCB_KHR
        VK_USE_PLATFORM_XLIB_KHR)
    pkg_check_modules(WAYLAND wayland-client)
    if(WAYLAND_FOUND)
        target_include_directories(venus_icd PRIVATE ${WAYLAND_INCLUDE_DIRS})
        target_link_libraries(venus_icd PRIVATE ${WAYLAND_LIBRARIES})
        target_compile_definitions(venus_icd PRIVATE VK_USE_PLATFORM_WAYLAND_KHR VENUS_PLUS_HAS_WAYLAND)
    endif()
endif()

# Generate ICD manifest
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/venus_icd.json.in
    ${CMAKE_CURRENT_BINARY_DIR}/venus_icd.x86_64.json
    @ONLY
)
