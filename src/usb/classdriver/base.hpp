#pragma once
#include "../../error.hpp"
#include "../endpoint.hpp"
#include "../setupdata.hpp"

namespace usb {
class Device;

class ClassDriver {
  private:
    Device* owner;

  public:
    auto get_owner_device() const -> Device* {
        return owner;
    }

    virtual auto initialize() -> Error                                                                               = 0;
    virtual auto set_endpoint(const EndpointConfig& config) -> Error                                                 = 0;
    virtual auto on_endpoint_configured() -> Error                                                                   = 0;
    virtual auto on_control_completed(EndpointID id, const SetupData& setup_data, const void* buf, int len) -> Error = 0;
    virtual auto on_interrupt_completed(EndpointID id, const void* buf, int len) -> Error                            = 0;

    ClassDriver(Device* const owner) : owner(owner) {}
    virtual ~ClassDriver() = default;
};
} // namespace usb
