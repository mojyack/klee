#pragma once
#include <vector>

#include "../interrupt/vector.hpp"
#include "../log.hpp"
#include "../mutex.hpp"
#include "../pci.hpp"
#include "ata.hpp"
#include "fis.hpp"
#include "struct.hpp"

namespace ahci {
namespace internal {
inline auto dump_bits(const uint32_t value) -> void {
    for(auto i = 0; i < 32; i += 1) {
        if(value & (1 << i)) {
            printk("%d(%02X) ", i, i);
        }
    }
    printk("\n");
}

inline auto set_dwords(volatile uint32_t& base, volatile uint32_t& upper, const auto data) -> void {
    const auto v = reinterpret_cast<uintptr_t>(data);
    base         = v & 0x00000000FFFFFFFF;
    upper        = (v & 0xFFFFFFFF00000000) >> 32;
}

} // namespace internal

class Controller;

struct DeviceInfo {
    size_t bytes_per_sector;
    size_t total_sectors;
};

struct IdentifySync {
    std::atomic_uint32_t count = 0;
    Event                event;
};

class SATADevice {
  private:
    friend auto initialize(const pci::Device& dev) -> std::unique_ptr<Controller>;

    static constexpr auto bytes_per_sector = size_t(512);

    struct CommandHeaderResource {
        std::unique_ptr<uint8_t> command_table;
        Event*                   on_complete;

        auto construct_command_table(const size_t num_prd) -> internal::CommandTable& {
            const auto size = sizeof(internal::CommandTable) + sizeof(internal::PRDTEntry) * num_prd;
            command_table.reset(new uint8_t[size]);
            return *reinterpret_cast<internal::CommandTable*>(command_table.get());
        }
    };

    enum class Operation {
        None,
        Identify,
        Read,
        Write,
    };

    uint32_t                                 num_command_slots;
    volatile internal::HBAPort*              port;
    std::unique_ptr<internal::CommandHeader> command_list;
    std::vector<CommandHeaderResource>       command_list_resources;
    std::unique_ptr<internal::HBAFIS>        received_fis;
    std::array<Operation, 32>                running_operations = {Operation::None};
    std::unique_ptr<uint8_t>                 identify_buffer;
    IdentifySync*                            identify_sync;
    size_t                                   lba_size = 0;
    std::array<char, 20>                     model_name;

    auto wait_compelete(const int slot) -> bool {
    loop:
        if((port->ci & (1 << slot)) == 0) {
            return true;
        }
        if(port->is.bits.tfes) {
            logger(LogLevel::Error, "ahci: task file error\n");
            return false;
        }
        goto loop;
    }

    auto get_command_header_at(const size_t slot) -> std::pair<internal::CommandHeader&, CommandHeaderResource&> {
        return {command_list.get()[slot], command_list_resources[slot]};
    }

    auto emit_h2d_command(uint8_t* buffer, size_t buffer_size, const size_t bytes_transfer, const internal::RegH2DFIS& cfis, const Operation operation, Event* const on_complete) -> bool {
        using namespace internal;

        const auto slot = port->find_lazy_command_slot(num_command_slots);
        if(slot == -1) {
            logger(LogLevel::Error, "ahci: cannot find free slot\n");
            return false;
        }

        constexpr auto dbc_max = 0x400000;
        const auto     num_prd = (bytes_transfer + dbc_max - 1) / dbc_max;

        if(bytes_transfer > buffer_size) {
            logger(LogLevel::Error, "ahci: buffer too small\n");
            return false;
        }

        auto [command_header, resource] = get_command_header_at(slot);
        auto& command_table             = resource.construct_command_table(num_prd);
        resource.on_complete            = on_complete;

        command_header.cfl   = sizeof(RegH2DFIS) / sizeof(uint32_t);
        command_header.w     = 0;
        command_header.prdtl = num_prd;
        set_dwords(command_header.ctba, command_header.ctbau, &command_table);

        for(auto i = size_t(0); i < num_prd; i += 1) {
            auto& prd = command_table.prdt_entry[i];
            set_dwords(prd.dba, prd.dbau, buffer);
            prd.dbc = buffer_size < dbc_max ? buffer_size : dbc_max;
            prd.i   = 0;
            buffer += dbc_max;
            buffer_size -= dbc_max;
        }

        *reinterpret_cast<RegH2DFIS*>(command_table.cfis) = cfis;

        auto           spin     = size_t(0);
        constexpr auto spin_max = 0xFFFF;
        while(port->tfd & (ata::TaskFile::DeviceBusy | ata::TaskFile::DeviceDataRequested) && spin < spin_max) {
            spin += 1;
        }

        if(spin == spin_max) {
            logger(LogLevel::Error, "ahci: port hung detected\n");
            return false;
        }

        running_operations[slot] = operation;
        port->ci                 = 1 << slot;
        return true;
    }

    auto set_cfis_lba(internal::RegH2DFIS& cfis, const uint64_t sector, const uint32_t count) -> void {
        cfis.lba0   = sector;
        cfis.lba1   = sector >> 8;
        cfis.lba2   = sector >> 16;
        cfis.lba3   = sector >> 24;
        cfis.lba4   = sector >> 32;
        cfis.lba5   = sector >> 40;
        cfis.device = 1 << 6; // LBA
        cfis.countl = count;
        cfis.counth = count >> 8;
    }

  public:
    auto on_interrupt() -> void {
        for(auto slot = 0; slot < running_operations.size(); slot += 1) {
            switch(running_operations[slot]) {
            case Operation::None:
                break;
            case Operation::Identify: {
                wait_compelete(slot);
                auto& identifier = *reinterpret_cast<ata::DeviceIdentifier*>(identify_buffer.get());
                lba_size         = *reinterpret_cast<uint64_t*>(identifier.available_48bit_lba);
                for(auto i = 0; i < 10; i += 1) {
                    const auto w          = identifier.model_name[i];
                    model_name[i * 2]     = (w & 0xFF00) >> 8;
                    model_name[i * 2 + 1] = w & 0xFF;
                }
                logger(LogLevel::Info, "ahci: disk identified: \"%.20s\" %luMiB\n", model_name.data(), lba_size * bytes_per_sector / 1024 / 1024);
                identify_buffer.reset();
                identify_sync->count++;
                identify_sync->event.notify();
                running_operations[slot] = Operation::None;
            } break;
            case Operation::Read:
            case Operation::Write:
                wait_compelete(slot);
                command_list_resources[slot].on_complete->notify();
                running_operations[slot] = Operation::None;
                break;
            }
        }
    }

    auto identify(IdentifySync& sync) -> bool {
        using namespace internal;

        auto cfis = RegH2DFIS();
        memset(&cfis, 0, sizeof(RegH2DFIS));
        cfis.fis_type = FISType::RegH2D;
        cfis.c        = 1;
        cfis.pmport   = 0;
        cfis.featurel = 1;
        cfis.command  = ata::Commands::IdentifyDevice;

        identify_buffer.reset(new uint8_t[bytes_per_sector]);
        identify_sync = &sync;

        return emit_h2d_command(identify_buffer.get(), bytes_per_sector, bytes_per_sector, cfis, Operation::Identify, nullptr);
    }

    auto read(const uint64_t sector, const uint32_t count, uint8_t* buffer, const size_t buffer_size, Event& on_complete) -> bool {
        using namespace internal;

        auto cfis = RegH2DFIS();
        memset(&cfis, 0, sizeof(RegH2DFIS));
        cfis.fis_type = FISType::RegH2D;
        cfis.c        = 1;
        cfis.pmport   = 0;
        cfis.featurel = 1;
        cfis.command  = ata::Commands::ReadDMAExt;
        set_cfis_lba(cfis, sector, count);

        return emit_h2d_command(buffer, buffer_size, count * bytes_per_sector, cfis, Operation::Read, &on_complete);
    }

    auto write(const uint64_t sector, const uint32_t count, const uint8_t* buffer, const size_t buffer_size, Event& on_complete) -> bool {
        using namespace internal;

        auto cfis = RegH2DFIS();
        memset(&cfis, 0, sizeof(RegH2DFIS));
        cfis.fis_type = FISType::RegH2D;
        cfis.c        = 1;
        cfis.pmport   = 0;
        cfis.featurel = 1;
        cfis.command  = ata::Commands::WriteDMAExt;
        set_cfis_lba(cfis, sector, count);

        return emit_h2d_command(const_cast<uint8_t*>(buffer), buffer_size, count * bytes_per_sector, cfis, Operation::Write, &on_complete);
    }

    auto get_info() const -> DeviceInfo {
        return {bytes_per_sector, lba_size};
    }
};

class Controller {
  private:
    volatile internal::HBAHeader* hba;
    std::vector<SATADevice>       ports;
    std::array<int8_t, 32>        available_ports;
    IdentifySync                  identify_sync;

  public:
    auto on_interrupt() -> void {
        const auto f = hba->is;
        for(auto i = 0; i < available_ports.size() && available_ports[i] != -1; i += 1) {
            if(f & (1u << available_ports[i])) {
                ports[i].on_interrupt();
            }
        }
    }

    auto wait_identify() -> void {
        while(true) {
            if(identify_sync.count == ports.size()) {
                return;
            }
            identify_sync.event.wait();
        }
    }

    auto get_devices() -> std::vector<SATADevice>& {
        return ports;
    }

    Controller(volatile internal::HBAHeader& hba, std::vector<SATADevice> ports) : hba(&hba),
                                                                                   ports(std::move(ports)) {
        auto b = available_ports.begin();
        for(auto i = 0; i < 32; i += 1) {
            if(!(hba.pi & 1u << i)) {
                continue;
            }
            *b = i;
            b += 1;
        }
        *b = -1;

        for(auto& d : this->ports) {
            d.identify(identify_sync);
        }

        process::manager->post_kernel_message_with_cli(MessageType::AHCIInterrupt);
    }
};

inline auto initialize(const pci::Device& dev) -> std::unique_ptr<Controller> {
    using namespace internal;

    logger(LogLevel::Debug, "ahci: controller found at %d.%d.%d\n", dev.bus, dev.device, dev.function);
    const auto abar = dev.read_bar(5);
    if(!abar) {
        logger(LogLevel::Error, "ahci: failed to read bar\n");
        return nullptr;
    }

    const auto     hba_addr   = abar.as_value() & ~static_cast<uint64_t>(0x0F);
    volatile auto& hba_header = *reinterpret_cast<HBAHeader*>(hba_addr);

    if(!hba_header.cap.s64a) {
        logger(LogLevel::Error, "ahci: hba does not support 64-bit addressing\n");
        return nullptr;
    }

    auto devices      = std::vector<SATADevice>();
    hba_header.ghc.ae = 1;
    hba_header.ghc.ie = 1;

    for(auto i = 0; i < 32; i += 1) {
        if(!(hba_header.pi & 1u << i)) {
            continue;
        }

        volatile auto& port = hba_header.ports[i];

        const auto ssts = port.ssts;
        const auto ipm  = (ssts >> 8) & 0x0F;
        const auto det  = ssts & 0x0F;

        constexpr auto ipm_active  = 1;
        constexpr auto det_present = 3;

        // check drive status
        if(det != det_present) {
            continue;
        }
        if(ipm != ipm_active) {
            continue;
        }

        logger(LogLevel::Debug, "ahci: port %d = ", i);

        switch(port.sig) {
        case HBAPort::Signature::ATA:
            logger(LogLevel::Debug, "ATA\n");
            break;
        case HBAPort::Signature::ATAPI:
            logger(LogLevel::Debug, "ATAPI\n");
            break;
        case HBAPort::Signature::ATASEMB:
            logger(LogLevel::Debug, "ATASEMB\n");
            break;
        case HBAPort::Signature::PM:
            logger(LogLevel::Debug, "PM\n");
            break;
        default:
            logger(LogLevel::Debug, "unknown(%08lx)\n", port.sig);
            break;
        }

        if(port.sig != HBAPort::Signature::ATA) {
            continue;
        }

        // initialize port

        port.stop();
        port.serr    = uint32_t(-1);
        port.ie      = uint32_t(-1);
        port.is.data = uint32_t(-1);

        auto device              = SATADevice();
        device.num_command_slots = hba_header.cap.ncs + 1;
        device.port              = &port;

        device.command_list.reset(new(std::align_val_t{0x1000}) CommandHeader[device.num_command_slots]);
        memset(device.command_list.get(), 0, sizeof(CommandHeader) * device.num_command_slots);

        device.received_fis.reset(new HBAFIS);
        memset(device.received_fis.get(), 0, sizeof(HBAFIS));

        device.command_list_resources.resize(device.num_command_slots);

        set_dwords(port.clb, port.clbu, reinterpret_cast<uintptr_t>(device.command_list.get()));
        set_dwords(port.fb, port.fbu, reinterpret_cast<uintptr_t>(device.received_fis.get()));
        devices.emplace_back(std::move(device));
        port.start();
    }

    const auto bsp_local_apic_id = *reinterpret_cast<const uint32_t*>(0xFEE00020) >> 24;
    if(const auto error = dev.configure_msi_fixed_destination(bsp_local_apic_id, ::pci::MSITriggerMode::Level, ::pci::MSIDeliveryMode::Fixed, ::interrupt::Vector::AHCI, 0)) {
        logger(LogLevel::Error, "ahci: failed to setup msi: %d\n", error.as_int());
        return nullptr;
    }
    return std::unique_ptr<Controller>(new Controller(hba_header, std::move(devices)));
}
} // namespace ahci
