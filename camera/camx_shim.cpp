#include <dlfcn.h>
#include <log/log.h>
#include <system/camera_metadata.h>
#include <unordered_map>
#include <vector>
#include <mutex>

static std::mutex gMutex;
static std::unordered_map<const camera_metadata_t*, std::vector<int32_t>> gModifiedMap;

extern "C" int find_camera_metadata_ro_entry(const camera_metadata_t *metadata,
                                             uint32_t tag,
                                             camera_metadata_ro_entry_t *entry) {
    static auto orig_find = reinterpret_cast<decltype(&find_camera_metadata_ro_entry)>(
            dlsym(RTLD_NEXT, "find_camera_metadata_ro_entry"));

    int ret = orig_find(metadata, tag, entry);
    if (ret != 0 || tag != ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS || !entry || !entry->data.i32) {
        return ret;
    }

    std::lock_guard<std::mutex> lock(gMutex);

    auto it = gModifiedMap.find(metadata);
    if (it != gModifiedMap.end()) {
        entry->data.i32 = it->second.data();
        return ret;
    }

    size_t count = entry->count;
    std::vector<int32_t> configs(entry->data.i32, entry->data.i32 + count);

    bool modified = false;
    for (size_t i = 0; i + 3 < count; i += 4) {
        int32_t format   = configs[i];
        int32_t width    = configs[i + 1];
        int32_t height   = configs[i + 2];
        int32_t is_input = configs[i + 3];

        if (is_input == ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT &&
            (format == HAL_PIXEL_FORMAT_YCbCr_420_888 ||
             format == HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED)) {
            if (width < 1280 && height < 720) {
                configs[i + 1] = 1280;
                configs[i + 2] = 960;
                modified = true;
            }
        }
    }

    if (modified) {
        auto inserted = gModifiedMap.emplace(metadata, std::move(configs));
        entry->data.i32 = inserted.first->second.data();
        ALOGI("veux_camx_shim: Replaced <720p stream configurations with 1280x960");
    }

    return ret;
}
