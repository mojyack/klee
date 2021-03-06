#pragma once
#include <array>
#include <variant>

class Error {
  public:
    enum class Code {
        Success = 0,
        Full,
        Empty,
        NoEnoughMemory,
        IndexOutOfRange,
        HostControllerNotHalted,
        InvalidSlotID,
        PortNotConnected,
        InvalidEndpointNumber,
        TransferRingNotSet,
        AlreadyAllocated,
        NotImplemented,
        InvalidDescriptor,
        BufferTooSmall,
        UnknownDevice,
        NoCorrespondingSetupStage,
        TransferFailed,
        InvalidPhase,
        UnknownXHCISpeedID,
        NoWaiter,
        NoPCIMSI,
        NoSuchTask,
        // virtio
        VirtIOLegacyDevice,
        VirtIODeviceNotReady,
        LastOfCode,
    };

  private:
    Code code;

  public:
    operator bool() const {
        return code != Code::Success;
    }

    Error(const Code code) : code(code) {}
};

template <class T>
class Result {
  private:
    std::variant<T, Error> data;

  public:
    auto as_value() -> T& {
        return std::get<T>(data);
    }

    auto as_value() const -> const T& {
        return std::get<T>(data);
    }

    auto as_error() const -> Error {
        return std::get<Error>(data);
    }

    operator bool() const {
        return std::holds_alternative<T>(data);
    }

    Result(T&& data) : data(std::move(data)) {}

    Result(const Error error) : data(error) {}
    Result(const Error::Code error) : data(error) {}
};
