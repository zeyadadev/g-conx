add_library(venus_icd SHARED
    icd/icd_entrypoints.cpp
    state/handle_allocator.cpp
    state/instance_state.cpp
    state/device_state.cpp
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

# Generate ICD manifest
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/venus_icd.json.in
    ${CMAKE_CURRENT_BINARY_DIR}/venus_icd.x86_64.json
    @ONLY
)
