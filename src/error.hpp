#pragma once
#include <array>
#include <variant>

class [[nodiscard]] Error {
  public:
    enum class Code : int {
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
        BadChecksum,
        EntryNotSet,
        // process manager
        InvalidNice,
        NoSuchEvent,
        UnFinishedEvent,
        AlivingThread,
        DeadThread,
        NoSuchProcess,
        NoSuchThread,
        // filesystem
        IOError,
        InvalidData,
        InvalidSize,
        InvalidSector,
        NotDirectory,
        NotFile,
        NoSuchFile,
        UnknownFilesystem,
        FileExists,
        FileOpened,
        FileNotOpened,
        VolumeMounted,
        VolumeBusy,
        NotMounted,
        AlreadyMounted,
        EndOfFile,
        // FAT
        NotFAT,
        // devfs
        InvalidDeviceType,
        InvalidDeviceOperation,
        // block
        NotMBR,
        NotGPT,
        UnsupportedGPT,
        // virtio
        VirtIOLegacyDevice,
        VirtIODeviceNotReady,
        LastOfCode,
        // elf
        NotELF,
        InvalidELF,
    };

  private:
    Code code;

  public:
    operator bool() const {
        return code != Code::Success;
    }

    auto operator==(const Code code) const -> bool {
        return code == this->code;
    }

    auto as_int() const -> unsigned int {
        return static_cast<unsigned int>(code);
    }

    Error() : code(Code::Success) {}

    Error(const Code code) : code(code) {}
};

class Success {
  public:
    operator Error() {
        return Error::Code::Success;
    }
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

    Result(T data) : data(std::move(data)) {}

    Result(const Error error = Error()) : data(error) {}

    Result(const Error::Code error) : data(error) {}
};
