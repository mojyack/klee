#include "../../manager.hpp"
#include "driver.hpp"

namespace fs::fat {

// functions below are called by fs::manager, so it should already be locked

inline BlockDevice::~BlockDevice() {
    auto& manager = fs::manager->unsafe_access(); 
    auto& root           = manager.get_fs_root();
    if(const auto e = root.close(std::move(handle))) {
        logger(LogLevel::Error, "[fat] failed to close devie file\n");
    }
}

inline auto new_driver(const std::string_view path) -> Result<std::unique_ptr<Driver>> {
    auto& manager = fs::manager->unsafe_access(); 
    auto& root    = manager.get_fs_root();
    value_or(handle, root.open(path, fs::OpenMode::Write));
    auto driver = std::unique_ptr<Driver>(new Driver(std::move(handle)));
    error_or(driver->init());
    return driver;
}

} // namespace fs::fat
