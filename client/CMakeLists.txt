add_library(venus_icd SHARED
    icd/icd_entrypoints.cpp
)

target_link_libraries(venus_icd PRIVATE
    venus_common
    Vulkan::Vulkan
)

# Generate ICD manifest
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/venus_icd.json.in
    ${CMAKE_CURRENT_BINARY_DIR}/venus_icd.x86_64.json
    @ONLY
)
