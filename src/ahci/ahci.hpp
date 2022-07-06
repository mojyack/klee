#pragma once
#include <vector>

#include "../interrupt.hpp"
#include "../log.hpp"
#include "../memory-manager.hpp"
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

class SATADevice {
  private:
    friend auto initialize(const pci::Device& dev) -> std::optional<Controller>;

    static constexpr auto prd_per_command_table = 1;
    static constexpr auto frame_per_prd         = 1;
    static constexpr auto bytes_per_sector      = 512;

    using CommandTable = internal::CommandTable<prd_per_command_table>;

    struct CommandHeaderResource {
        std::unique_ptr<CommandTable>                   command_table;
        std::array<SmartFrameID, prd_per_command_table> prds;
    };

    enum class Operation {
        None,
        Identify,
    };

    uint32_t                                 num_command_slots;
    volatile internal::HBAPort*              port;
    std::unique_ptr<internal::CommandHeader> command_list;
    std::vector<CommandHeaderResource>       command_list_resources;
    std::unique_ptr<internal::HBAFIS>        received_fis;
    std::array<Operation, 32>                running_operations = {Operation::None};
    size_t                                   lba_size           = 0;
    std::array<char, 20>                     model_name;

    auto wait_compelete(const int slot) -> bool {
    loop:
        if((port->ci & (1 << slot)) == 0) {
            return true;
        }
        if(port->is.bits.tfes) {
            logger(LogLevel::Error, "[ahci] task file error\n");
            return false;
        }
        goto loop;
    }

  public:
    auto on_interrupt() -> void {
        for(auto slot = 0; slot < running_operations.size(); slot += 1) {
            switch(running_operations[slot]) {
            case Operation::None:
                break;
            case Operation::Identify: {
                wait_compelete(slot);
                auto&       identifier = *reinterpret_cast<ata::DeviceIdentifier*>(command_list_resources[slot].prds[0]->get_frame());
                lba_size               = *reinterpret_cast<uint64_t*>(identifier.available_48bit_lba);
                for(auto i = 0; i < 10; i += 1) {
                    const auto w          = identifier.model_name[i];
                    model_name[i * 2]     = (w & 0xFF00) >> 8;
                    model_name[i * 2 + 1] = w & 0xFF;
                }
                printk("[ahci] disk identified: \"%.20s\" %luMiB\n", model_name.data(), lba_size * bytes_per_sector / 1024 / 1024);
                running_operations[slot] = Operation::None;
            } break;
            }
        }
    }

    auto identify() -> void {
        using namespace internal;

        const auto slot = port->find_lazy_command_slot(num_command_slots);
        if(slot == -1) {
            logger(LogLevel::Error, "[ahci] cannot find free slot\n");
            return;
        }

        auto& command_header = command_list.get()[slot];
        command_header.cfl   = sizeof(RegH2DFIS) / sizeof(uint32_t);
        command_header.w     = 0;

        auto& command_table = *command_list_resources[slot].command_table;
        auto& cfis          = *reinterpret_cast<RegH2DFIS*>(command_table.cfis);
        memset(&cfis, 0, sizeof(RegH2DFIS));
        cfis.fis_type = FISType::RegH2D;
        cfis.c        = 1;
        cfis.pmport   = 0;
        cfis.featurel = 1;
        cfis.command  = ata::Commands::IdentifyDevice;

        auto           spin     = size_t(0);
        constexpr auto spin_max = 0xFFFF;
        while(port->tfd & (ata::TaskFile::DeviceBusy | ata::TaskFile::DeviceDataRequested) && spin < spin_max) {
            spin += 1;
        }

        if(spin == spin_max) {
            logger(LogLevel::Error, "[ahci] port hang detected\n");
            return;
        }

        running_operations[slot] = Operation::Identify;
        port->ci                 = 1 << slot;
        return;

        while(true) {
            if((port->ci & (1 << slot)) == 0) {
                break;
            }
            if(port->is.bits.tfes) {
                logger(LogLevel::Error, "[ahci] task file error\n");
                return;
            }
        }

        const auto data = reinterpret_cast<uint8_t*>(command_list_resources[slot].prds[0]->get_frame());
        for(auto i = 0; i < 512; i += 1) {
            printk("%02X ", data[i]);
            if((i % 32) == 0) {
                printk("\n");
            }
        }
    }
};

class Controller {
  private:
    volatile internal::HBAHeader* hba;
    std::vector<SATADevice>       ports;
    std::array<int8_t, 32>        available_ports;

  public:
    auto on_interrupt() -> void {
        const auto f = hba->is;
        for(auto i = 0; i < available_ports.size() && available_ports[i] != -1; i += 1) {
            if(f & (1u << available_ports[i])) {
                ports[i].on_interrupt();
            }
        }
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
            d.identify();
        }
    }
};

inline auto initialize(const pci::Device& dev) -> std::optional<Controller> {
    using namespace internal;

    printk("[ahci] controller found at %d.%d.%d\n", dev.bus, dev.device, dev.function);
    const auto abar = dev.read_bar(5);
    if(!abar) {
        logger(LogLevel::Error, "[ahci] failed to read bar\n");
        return std::nullopt;
    }

    const auto     hba_addr   = abar.as_value() & ~static_cast<uint64_t>(0x0F);
    volatile auto& hba_header = *reinterpret_cast<HBAHeader*>(hba_addr);

    if(!hba_header.cap.s64a) {
        logger(LogLevel::Error, "[ahci] hba does not support 64-bit addressing\n");
        return std::nullopt;
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

        printk("[ahci] port %d = ", i);

        switch(port.sig) {
        case HBAPort::Signature::ATA:
            printk("ATA\n");
            break;
        case HBAPort::Signature::ATAPI:
            printk("ATAPI\n");
            break;
        case HBAPort::Signature::ATASEMB:
            printk("ATASEMB\n");
            break;
        case HBAPort::Signature::PM:
            printk("PM\n");
            break;
        default:
            printk("unknown(%08lx)\n", port.sig);
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
        for(auto i = 0; i < device.num_command_slots; i += 1) {
            auto& command_header = device.command_list.get()[i];
            command_header.prdtl = SATADevice::prd_per_command_table;

            auto resource = SATADevice::CommandHeaderResource();
            resource.command_table.reset(new SATADevice::CommandTable);
            set_dwords(command_header.ctba, command_header.ctbau, resource.command_table.get());
            for(auto p = 0; p < SATADevice::prd_per_command_table; p += 1) {
                const auto frame = allocator->allocate(SATADevice::frame_per_prd);
                if(!frame) {
                    logger(LogLevel::Error, "[ahci] failed to allocate frames\n");
                    goto next_port;
                }
                resource.prds[p]         = SmartFrameID(frame.as_value(), SATADevice::frame_per_prd);
                const auto database_addr = reinterpret_cast<uintptr_t>(resource.prds[p]->get_frame());
                auto&      entry         = (*resource.command_table).prdt_entry[p];
                set_dwords(entry.dba, entry.dbau, database_addr);
                entry.dbc = bytes_per_frame * SATADevice::frame_per_prd - 1;
            }
            device.command_list_resources[i] = std::move(resource);
        }
        set_dwords(port.clb, port.clbu, reinterpret_cast<uintptr_t>(device.command_list.get()));
        set_dwords(port.fb, port.fbu, reinterpret_cast<uintptr_t>(device.received_fis.get()));
        devices.emplace_back(std::move(device));
        port.start();
    next_port:
        continue;
    }

    const auto bsp_local_apic_id = *reinterpret_cast<const uint32_t*>(0xFEE00020) >> 24;
    if(const auto error = dev.configure_msi_fixed_destination(bsp_local_apic_id, ::pci::MSITriggerMode::Level, ::pci::MSIDeliveryMode::Fixed, ::interrupt::InterruptVector::Number::AHCI, 0)) {
        logger(LogLevel::Error, "[ahci] failed to setup msi(%d)\n", error);
    }
    return Controller(hba_header, std::move(devices));
}
} // namespace ahci
