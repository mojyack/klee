#include "../fs/main.hpp"

namespace devfs {
inline auto create_device_file(const std::string_view name, const uintptr_t device_impl) -> Error {
    auto& manager     = fs::manager->unsafe_access();
    auto& root        = manager.get_fs_root();
    auto  open_result = root.open("/dev", fs::OpenMode::Write);
    if(!open_result) {
        logger(LogLevel::Error, "failed to open \"/dev\": ", open_result.as_error().as_int());
        return open_result.as_error();
    }
    auto& dev = open_result.as_value();
    dev.create_device(name, device_impl);
    root.close(std::move(dev));
    return Error();
}
} // namespace devfs
