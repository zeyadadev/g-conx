#include "binding_validator.h"

#include <sstream>

namespace venus_plus {

namespace {

bool validate_common(const VkMemoryRequirements& requirements,
                     VkDeviceSize memory_size,
                     VkDeviceSize offset,
                     std::string* error_message) {
    if (requirements.alignment && (offset % requirements.alignment) != 0) {
        if (error_message) {
            std::ostringstream oss;
            oss << "Offset " << offset << " is not aligned to " << requirements.alignment;
            *error_message = oss.str();
        }
        return false;
    }

    if (memory_size && offset + requirements.size > memory_size) {
        if (error_message) {
            std::ostringstream oss;
            oss << "Binding exceeds allocation (offset=" << offset
                << ", size=" << requirements.size
                << ", allocation=" << memory_size << ")";
            *error_message = oss.str();
        }
        return false;
    }

    return true;
}

} // namespace

bool validate_buffer_binding(const VkMemoryRequirements& requirements,
                             VkDeviceSize memory_size,
                             VkDeviceSize offset,
                             std::string* error_message) {
    return validate_common(requirements, memory_size, offset, error_message);
}

bool validate_image_binding(const VkMemoryRequirements& requirements,
                            VkDeviceSize memory_size,
                            VkDeviceSize offset,
                            std::string* error_message) {
    return validate_common(requirements, memory_size, offset, error_message);
}

} // namespace venus_plus
