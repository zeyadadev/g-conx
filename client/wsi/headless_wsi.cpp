#include "wsi/platform_wsi.h"

#include "utils/logging.h"
#include <fstream>
#include <sstream>
#include <vector>

namespace venus_plus {

namespace {

bool decompress_rle(const VenusFrameHeader& frame,
                    const uint8_t* data,
                    std::vector<uint8_t>* output) {
    if (!data || !output) {
        return false;
    }
    output->clear();
    size_t offset = 0;
    while (offset < frame.payload_size) {
        if (offset + 2 > frame.payload_size) {
            return false;
        }
        uint8_t tag = data[offset++];
        uint8_t count = data[offset++];
        if (tag == 1) {
            if (offset >= frame.payload_size) {
                return false;
            }
            uint8_t value = data[offset++];
            output->insert(output->end(), count, value);
        } else {
            if (offset + count > frame.payload_size) {
                return false;
            }
            output->insert(output->end(), data + offset, data + offset + count);
            offset += count;
        }
    }
    if (frame.uncompressed_size != 0 && output->size() != frame.uncompressed_size) {
        return false;
    }
    return true;
}

class HeadlessWSI : public PlatformWSI {
public:
    bool init(const VkSwapchainCreateInfoKHR& info, uint32_t image_count) override {
        width_ = info.imageExtent.width;
        height_ = info.imageExtent.height;
        format_ = info.imageFormat;
        image_count_ = image_count;
        VP_LOG_STREAM_INFO(CLIENT) << "[WSI] Headless WSI initialized (" << width_ << "x"
                                    << height_ << ", images=" << image_count_ << ")";
        return true;
    }

    void handle_frame(const VenusFrameHeader& frame, const uint8_t* data) override {
        if (!data || frame.payload_size == 0) {
            return;
        }
        const uint8_t* write_ptr = data;
        size_t write_size = frame.payload_size;
        std::vector<uint8_t> decoded;
        if (frame.compression == FrameCompressionType::RLE) {
            if (!decompress_rle(frame, data, &decoded)) {
                VP_LOG_STREAM_ERROR(CLIENT) << "[WSI] Failed to decompress frame";
                return;
            }
            write_ptr = decoded.data();
            write_size = decoded.size();
        } else if (frame.compression != FrameCompressionType::NONE) {
            VP_LOG_STREAM_ERROR(CLIENT) << "[WSI] Unknown compression format";
            return;
        }
        std::ostringstream path;
        path << "swapchain_" << frame.swapchain_id << "_image_" << frame.image_index << ".rgba";
        std::ofstream file(path.str(), std::ios::binary);
        if (!file.is_open()) {
            VP_LOG_STREAM_ERROR(CLIENT) << "[WSI] Failed to open " << path.str();
            return;
        }
        file.write(reinterpret_cast<const char*>(write_ptr),
                   static_cast<std::streamsize>(write_size));
        VP_LOG_STREAM_INFO(CLIENT) << "[WSI] Wrote frame to " << path.str();
    }

    void shutdown() override {
        VP_LOG_STREAM_INFO(CLIENT) << "[WSI] Headless WSI shutdown";
    }

private:
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    uint32_t image_count_ = 0;
    VkFormat format_ = VK_FORMAT_UNDEFINED;
};

} // namespace

std::shared_ptr<PlatformWSI> create_platform_wsi() {
    return std::make_shared<HeadlessWSI>();
}

} // namespace venus_plus
