#pragma once
#include "register.hpp"

namespace usb {
union HCSPARAMS1Bitmap {
    uint32_t data[1];
    struct {
        uint32_t max_device_slots : 8;
        uint32_t max_interrupters : 11;
        uint32_t : 5;
        uint32_t max_ports : 8;
    } __attribute__((packed)) bits;
} __attribute__((packed));

union HCSPARAMS2Bitmap {
    uint32_t data[1];
    struct {
        uint32_t isochronous_scheduling_threshold : 4;
        uint32_t event_ring_segment_table_max : 4;
        uint32_t : 13;
        uint32_t max_scratchpad_buffers_high : 5;
        uint32_t scratchpad_restore : 1;
        uint32_t max_scratchpad_buffers_low : 5;
    } __attribute__((packed)) bits;
} __attribute__((packed));

union HCSPARAMS3Bitmap {
    uint32_t data[1];
    struct {
        uint32_t u1_device_eixt_latency : 8;
        uint32_t : 8;
        uint32_t u2_device_eixt_latency : 16;
    } __attribute__((packed)) bits;
} __attribute__((packed));

union HCCPARAMS1Bitmap {
    uint32_t data[1];
    struct {
        uint32_t addressing_capability_64 : 1;
        uint32_t bw_negotiation_capability : 1;
        uint32_t context_size : 1;
        uint32_t port_power_control : 1;
        uint32_t port_indicators : 1;
        uint32_t light_hc_reset_capability : 1;
        uint32_t latency_tolerance_messaging_capability : 1;
        uint32_t no_secondary_sid_support : 1;
        uint32_t parse_all_event_data : 1;
        uint32_t stopped_short_packet_capability : 1;
        uint32_t stopped_edtla_capability : 1;
        uint32_t contiguous_frame_id_capability : 1;
        uint32_t maximum_primary_stream_array_size : 4;
        uint32_t xhci_extended_capabilities_pointer : 16;
    } __attribute__((packed)) bits;
} __attribute__((packed));

union DBOFFBitmap {
    uint32_t data[1];
    struct {
        uint32_t : 2;
        uint32_t doorbell_array_offset : 30;
    } __attribute__((packed)) bits;

    auto get_offset() const -> uint32_t {
        return bits.doorbell_array_offset << 2;
    }
} __attribute__((packed));

union RTSOFFBitmap {
    uint32_t data[1];
    struct {
        uint32_t : 5;
        uint32_t runtime_register_space_offset : 27;
    } __attribute__((packed)) bits;

    auto get_offset() const -> uint32_t {
        return bits.runtime_register_space_offset << 5;
    }
} __attribute__((packed));

union HCCPARAMS2Bitmap {
    uint32_t data[1];
    struct {
        uint32_t u3_entry_capability : 1;
        uint32_t configure_endpoint_command_max_exit_latency_too_large_capability : 1;
        uint32_t force_save_context_capability : 1;
        uint32_t compliance_transition_capability : 1;
        uint32_t large_esit_payload_capability : 1;
        uint32_t configuration_information_capability : 1;
        uint32_t : 26;
    } __attribute__((packed)) bits;
} __attribute__((packed));

struct CapabilityRegisters {
    MemoryMappedRegister<DefaultBitmap<uint8_t>>  caplength;
    MemoryMappedRegister<DefaultBitmap<uint8_t>>  reserved1;
    MemoryMappedRegister<DefaultBitmap<uint16_t>> hciversion;
    MemoryMappedRegister<HCSPARAMS1Bitmap>        hcsparams1;
    MemoryMappedRegister<HCSPARAMS2Bitmap>        hcsparams2;
    MemoryMappedRegister<HCSPARAMS3Bitmap>        hcsparams3;
    MemoryMappedRegister<HCCPARAMS1Bitmap>        hccparams1;
    MemoryMappedRegister<DBOFFBitmap>             dboff;
    MemoryMappedRegister<RTSOFFBitmap>            rtsoff;
    MemoryMappedRegister<HCCPARAMS2Bitmap>        hccparams2;
} __attribute__((packed));

union USBCMDBitmap {
    uint32_t data[1];
    struct {
        uint32_t run_stop : 1;
        uint32_t host_controller_reset : 1;
        uint32_t interrupter_enable : 1;
        uint32_t host_system_error_enable : 1;
        uint32_t : 3;
        uint32_t lignt_host_controller_reset : 1;
        uint32_t controller_save_state : 1;
        uint32_t controller_restore_state : 1;
        uint32_t enable_wrap_event : 1;
        uint32_t enable_u3_mfindex_stop : 1;
        uint32_t stopped_short_packet_enable : 1;
        uint32_t cem_enable : 1;
        uint32_t : 18;
    } __attribute__((packed)) bits;
} __attribute__((packed));

union USBSTSBitmap {
    uint32_t data[1];
    struct {
        uint32_t host_controller_halted : 1;
        uint32_t : 1;
        uint32_t host_system_error : 1;
        uint32_t event_interrupt : 1;
        uint32_t port_change_detect : 1;
        uint32_t : 3;
        uint32_t save_state_status : 1;
        uint32_t restore_state_status : 1;
        uint32_t save_restore_error : 1;
        uint32_t controller_not_ready : 1;
        uint32_t host_controller_error : 1;
        uint32_t : 19;
    } __attribute__((packed)) bits;
} __attribute__((packed));

union CRCRBitmap {
    uint64_t data[1];
    struct {
        uint64_t ring_cycle_state : 1;
        uint64_t command_stop : 1;
        uint64_t command_abort : 1;
        uint64_t command_ring_running : 1;
        uint64_t : 2;
        uint64_t command_ring_pointer : 58;
    } __attribute__((packed)) bits;

    auto get_pointer() const -> uint64_t {
        return bits.command_ring_pointer << 6;
    }

    auto set_pointer(const uint64_t value) -> void {
        bits.command_ring_pointer = value >> 6;
    }
} __attribute__((packed));

union DCBAAPBitmap {
    uint64_t data[1];
    struct {
        uint64_t : 6;
        uint64_t device_context_base_address_array_pointer : 26;
    } __attribute__((packed)) bits;

    auto get_pointer() const -> uint64_t {
        return bits.device_context_base_address_array_pointer << 6;
    }

    auto set_pointer(const uint64_t value) -> void {
        bits.device_context_base_address_array_pointer = value >> 6;
    }
} __attribute__((packed));

union CONFIGBitmap {
    uint32_t data[1];
    struct {
        uint32_t max_device_slots_enabled : 8;
        uint32_t u3_entry_enable : 1;
        uint32_t configuration_information_enable : 1;
        uint32_t : 22;
    } __attribute__((packed)) bits;
} __attribute__((packed));

struct OperationalRegisters {
    MemoryMappedRegister<USBCMDBitmap>            usbcmd;
    MemoryMappedRegister<USBSTSBitmap>            usbsts;
    MemoryMappedRegister<DefaultBitmap<uint32_t>> pagesize;
    uint32_t                                      reserved1[2];
    MemoryMappedRegister<DefaultBitmap<uint32_t>> dnctrl;
    MemoryMappedRegister<CRCRBitmap>              crcr;
    uint32_t                                      reserved2[4];
    MemoryMappedRegister<DCBAAPBitmap>            dcbaap;
    MemoryMappedRegister<CONFIGBitmap>            config;
} __attribute__((packed));

union PORTSCBitmap {
    uint32_t data[1];
    struct {
        uint32_t current_connect_status : 1;
        uint32_t port_enabled_disabled : 1;
        uint32_t : 1;
        uint32_t over_current_active : 1;
        uint32_t port_reset : 1;
        uint32_t port_link_state : 4;
        uint32_t port_power : 1;
        uint32_t port_speed : 4;
        uint32_t port_indicator_control : 2;
        uint32_t port_link_state_write_strobe : 1;
        uint32_t connect_status_change : 1;
        uint32_t port_enabled_disabled_change : 1;
        uint32_t warm_port_reset_change : 1;
        uint32_t over_current_change : 1;
        uint32_t port_reset_change : 1;
        uint32_t port_link_state_change : 1;
        uint32_t port_config_error_change : 1;
        uint32_t cold_attach_status : 1;
        uint32_t wake_on_connect_enable : 1;
        uint32_t wake_on_disconnect_enable : 1;
        uint32_t wake_on_over_current_enable : 1;
        uint32_t : 2;
        uint32_t device_removable : 1;
        uint32_t warm_port_reset : 1;
    } __attribute__((packed)) bits;
} __attribute__((packed));

union PORTPMSCBitmap {
    uint32_t data[1];
    struct { // definition for USB3 protocol
        uint32_t u1_timeout : 8;
        uint32_t u2_timeout : 8;
        uint32_t force_link_pm_accept : 1;
        uint32_t : 15;
    } __attribute__((packed)) bits_usb3;
} __attribute__((packed));

union PORTLIBitmap {
    uint32_t data[1];
    struct { // definition for USB3 protocol
        uint32_t link_error_count : 16;
        uint32_t rx_lane_count : 4;
        uint32_t tx_lane_count : 4;
        uint32_t : 8;
    } __attribute__((packed)) bits_usb3;
} __attribute__((packed));

union PORTHLPMCBitmap {
    uint32_t data[1];
    struct { // definition for USB2 protocol
        uint32_t host_initiated_resume_duration_mode : 2;
        uint32_t l1_timeout : 8;
        uint32_t best_effort_service_latency_deep : 4;
        uint32_t : 18;
    } __attribute__((packed)) bits_usb2;
} __attribute__((packed));

struct PortRegisterSet {
    MemoryMappedRegister<PORTSCBitmap>    portsc;
    MemoryMappedRegister<PORTPMSCBitmap>  portpmsc;
    MemoryMappedRegister<PORTLIBitmap>    portli;
    MemoryMappedRegister<PORTHLPMCBitmap> porthlpmc;
} __attribute__((packed));

using PortRegisterSetArray = ArrayWrapper<PortRegisterSet>;

union IMANBitmap {
    uint32_t data[1];
    struct {
        uint32_t interrupt_pending : 1;
        uint32_t interrupt_enable : 1;
        uint32_t : 30;
    } __attribute__((packed)) bits;
} __attribute__((packed));

union IMODBitmap {
    uint32_t data[1];
    struct {
        uint32_t interrupt_moderation_interval : 16;
        uint32_t interrupt_moderation_counter : 16;
    } __attribute__((packed)) bits;
} __attribute__((packed));

union ERSTSZBitmap {
    uint32_t data[1];
    struct {
        uint32_t event_ring_segment_table_size : 16;
        uint32_t : 16;
    } __attribute__((packed)) bits;

    auto get_size() const -> uint16_t {
        return bits.event_ring_segment_table_size;
    }

    auto set_size(const uint16_t value) -> void {
        bits.event_ring_segment_table_size = value;
    }
} __attribute__((packed));

union ERSTBABitmap {
    uint64_t data[1];
    struct {
        uint64_t : 6;
        uint64_t event_ring_segment_table_base_address : 58;
    } __attribute__((packed)) bits;

    auto get_pointer() const -> uint64_t {
        return bits.event_ring_segment_table_base_address << 6;
    }

    auto set_pointer(const uint64_t value) -> void {
        bits.event_ring_segment_table_base_address = value >> 6;
    }
} __attribute__((packed));

union ERDPBitmap {
    uint64_t data[1];
    struct {
        uint64_t dequeue_erst_segment_index : 3;
        uint64_t event_handler_busy : 1;
        uint64_t event_ring_dequeue_pointer : 60;
    } __attribute__((packed)) bits;

    auto get_pointer() const -> uint64_t {
        return bits.event_ring_dequeue_pointer << 4;
    }

    auto set_pointer(const uint64_t value) -> void {
        bits.event_ring_dequeue_pointer = value >> 4;
    }
} __attribute__((packed));

struct InterrupterRegisterSet {
    MemoryMappedRegister<IMANBitmap>   iman;
    MemoryMappedRegister<IMODBitmap>   imod;
    MemoryMappedRegister<ERSTSZBitmap> erstsz;
    uint32_t                           reserved;
    MemoryMappedRegister<ERSTBABitmap> erstba;
    MemoryMappedRegister<ERDPBitmap>   erdp;
} __attribute__((packed));

using InterrupterRegisterSetArray = ArrayWrapper<InterrupterRegisterSet>;

union DoorbellBitmap {
    uint32_t data[1];
    struct {
        uint32_t db_target : 8;
        uint32_t : 8;
        uint32_t db_stream_id : 16;
    } __attribute__((packed)) bits;
} __attribute__((packed));

class DoorbellRegister {
    MemoryMappedRegister<DoorbellBitmap> reg;

  public:
    auto ring(const uint8_t target, const uint16_t stream_id = 0) -> void {
        auto value              = DoorbellBitmap();
        value.bits.db_target    = target;
        value.bits.db_stream_id = stream_id;
        reg.write(value);
    }
};

using DoorbellRegisterArray = ArrayWrapper<DoorbellRegister>;

union ExtendedRegisterBitmap {
    uint32_t data[1];
    struct {
        uint32_t capability_id : 8;
        uint32_t next_pointer : 8;
        uint32_t value : 16;
    } __attribute__((packed)) bits;
} __attribute__((packed));

class ExtendedRegisterList {
  private:
    template <class Ptr, class Disp>
    static auto add_or_null(const Ptr p, const Disp d) -> Ptr {
        return d == 0 ? nullptr : p + d;
    }

  public:
    using ValueType = MemoryMappedRegister<ExtendedRegisterBitmap>;

    class Iterator {
      private:
        ValueType* reg;

      public:
        auto      operator->() const -> ValueType* { return reg; }
        auto      operator*() const -> ValueType& { return *reg; }
        auto      operator==(const Iterator o) const -> bool { return reg == o.reg; }
        auto      operator!=(const Iterator o) const -> bool { return reg != o.reg; }
        Iterator& operator++() {
            if(reg) {
                reg = add_or_null(reg, reg->read().bits.next_pointer);
                static_assert(sizeof(*reg) == 4);
            }
            return *this;
        }
        Iterator(ValueType* const reg) : reg(reg) {}
    };

  private:
    Iterator first;

  public:
    auto begin() const -> Iterator {
        return first;
    }

    auto end() const -> Iterator {
        return Iterator(nullptr);
    }

    ExtendedRegisterList(const uint64_t mmio_base, const HCCPARAMS1Bitmap hccp) : first(add_or_null(reinterpret_cast<ValueType*>(mmio_base), hccp.bits.xhci_extended_capabilities_pointer)) {}
};

union USBLEGSUPBitmap {
    uint32_t data[1];
    struct {
        uint32_t capability_id : 8;
        uint32_t next_pointer : 8;
        uint32_t hc_bios_owned_semaphore : 1;
        uint32_t : 7;
        uint32_t hc_os_owned_semaphore : 1;
        uint32_t : 7;
    } __attribute__((packed)) bits;
} __attribute__((packed));
} // namespace usb
