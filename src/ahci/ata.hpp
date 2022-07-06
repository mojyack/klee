#pragma once
#include <cstdint>

namespace ata {
enum Commands : uint8_t {
    Nop                               = 0x00, // none
    CFARequestExtendedErrorCode       = 0x03, // none
    DataSetManagement                 = 0x06, // DMA
    DataSetManagementXL               = 0x07, // DMA
    RequestSenseDataExt               = 0x0B, // none
    GetPhysicalElementStatus          = 0x12, // DMA
    ReadSectors                       = 0x20, // PIO
    ReadSectorsExt                    = 0x24, // PIO
    ReadDMAExt                        = 0x25, // DMA
    ReadStreamDMAExt                  = 0x2A, // DMA
    ReadStreamExt                     = 0x2B, // PIO
    ReadLogExt                        = 0x2F, // PIO
    WriteSectors                      = 0x30, // PIO
    WriteSectorsExt                   = 0x34, // PIO
    WriteDMAExt                       = 0x35, // DMA
    CFAWriteSectorsWithoutErase       = 0x38, // PIO
    WriteStreamDMAExt                 = 0x3A, // DMA
    WriteStreamExt                    = 0x3B, // PIO
    WriteDMAFUAExt                    = 0x3D, // DMA
    WriteLogExt                       = 0x3F, // PIO
    ReadVerifySectors                 = 0x40, // none
    ReadVerifySectorsExt              = 0x42, // none
    ZeroExt                           = 0x44, // none
    WriteUncorrectableExt             = 0x45, // none
    ReadLogDMAExt                     = 0x47, // DMA
    ZACManagementIn                   = 0x4A, // DMA
    ConfigureStream                   = 0x51, // none
    WriteLogDMAExt                    = 0x57, // DMA
    TrustedNonData                    = 0x5B, // none
    TrustedReceive                    = 0x5C, // PIO
    TrustedReceiveDMA                 = 0x5D, // DMA
    TrustedSend                       = 0x5E, // PIO
    TrustedSendDMA                    = 0x5F, // DMA
    ReadFPDMAQueued                   = 0x60, // queued DMA
    WriteFPDMAQueued                  = 0x61, // queued DMA
    NCQNonData                        = 0x63, // none
    SendFPDMAQueued                   = 0x64, // queued DMA
    ReceiveFPDMAQueued                = 0x65, // queued DMA
    SetDateTimeExt                    = 0x77, // none
    AccessibleMaxAddressConfiguration = 0x78, // none
    RemoveElementAndTruncate          = 0x7C, // none
    RestoreElementsAndRebuild         = 0x7D, // none
    RemoveElementAndModifyZones       = 0x7E, // none
    CFATranslateSector                = 0x87, // PIO
    ExecuteDeviceDiagnostic           = 0x90, // none
    DownloadMicrocode                 = 0x92, // PIO
    DownloadMicrocodeDMA              = 0x93, // DMA
    MutateExt                         = 0x96, // none
    ZACManagementOut                  = 0x9F, // DMA
    SMART                             = 0xB0, // PIO
    SetSectorConfiguratonExt          = 0xB2, // none
    SanitizeDevice                    = 0xB4, // none
    ReadDMA                           = 0xC8, // DMA
    WriteDMA                          = 0xCA, // DMA
    CFAWriteMultipleWithoutErase      = 0xCD, // PIO
    StandbyImmediate                  = 0xE0, // none
    IdleImmediate                     = 0xE1, // none
    Standby                           = 0xE2, // none
    Idle                              = 0xE3, // none
    ReadBuffer                        = 0xE4, // PIO
    CheckPowerMode                    = 0xE5, // none
    Sleep                             = 0xE6, // none
    FlushCache                        = 0xE7, // none
    WriteBuffer                       = 0xE8, // PIO
    ReadBufferDMA                     = 0xE9, // DMA
    FlushCacheExt                     = 0xEA, // none
    WriteBufferDMA                    = 0xEB, // DMA
    IdentifyDevice                    = 0xEC, // PIO
    SetFeatures                       = 0xEF, // none
    SecuritySetPassword               = 0xF1, // PIO
    SecurityUnlock                    = 0xF2, // PIO
    SecurityErasePrepare              = 0xF3, // none
    SecurityEraseUnit                 = 0xF4, // PIO
    SecurityFreezeLock                = 0xF5, // none
    SecurityDisablePassword           = 0xF6, // PIO
};

enum TaskFile : uint32_t {
    DeviceBusy          = 0x80, // BSY
    DeviceDataRequested = 0x08, // DRQ
};

struct DeviceIdentifier {
    struct {                              // 0
        uint16_t command_packet_size : 2; // 0b00=12bytes(CD-ROM) 0b01=16bytes
        uint16_t response_imcomplete : 1;
        uint16_t reserved1 : 2;
        uint16_t command_drq : 2;
        uint16_t removable : 1;
        uint16_t device_type : 5; // 0b00000=Direct Access 0b00101=CD-ROM 0b00111=Optical
        uint16_t reserved2 : 1;
        uint16_t protocol_type : 2; // 0b0?=ATA 0b10=ATAPI 0b11=Reserved
    } __attribute__((packed)) general_configuration;
    uint16_t num_cylinders;                         // 1
    uint16_t reserved1;                             // 2
    uint16_t num_heads;                             // 3
    uint16_t unknown1[2];                           // 4
    uint16_t sector_per_track;                      // 6
    uint16_t minimum_isg;                           // 7
    uint16_t reserved2;                             // 8
    uint16_t minimum_plo;                           // 9
    uint16_t serial_number[10];                     // 10
    uint16_t buffer_type;                           // 20
    uint16_t buffer_capacity;                       // 21 *512byte
    uint16_t ecc_byte;                              // 22
    uint16_t firmware_revesion[4];                  // 23
    uint16_t model_name[20];                        // 27
    uint16_t transfer_sector_per_interrupt;         // 47
    uint16_t supports_doubleword_io;                // 48
    uint16_t supports_iordy_lba_dma;                // 49
    uint16_t reserved3;                             // 50
    uint16_t pio_transfer_mode;                     // 51
    uint16_t singleword_dma_mode;                   // 52
    uint16_t extended_fields;                       // 53 :0 54~58 available :1 64~70 available :2 88 available
    uint16_t current_num_cylinders;                 // 54
    uint16_t current_num_heads;                     // 55
    uint16_t current_sector_per_track;              // 56
    uint16_t current_capacity_per_sector[2];        // 57
    uint16_t current_transfer_sector_per_interrupt; // 59
    uint16_t num_assigned_sectors[2];               // 60
    uint16_t supports_singleword_dma;               // 62
    uint16_t supports_multiword_dma;                // 63
    uint16_t supports_advanced_pio;                 // 64
    uint16_t unknown2[4];                           // 65
    uint16_t reserved4[6];                          // 69
    uint16_t queue_size;                            // 75 :0-4 maximum queue depth
    struct {                                        // 76
        // 0
        uint16_t unknown1 : 1;
        uint16_t supports_gen1_signaling_rate : 1;
        uint16_t supports_gen2_signaling_rate : 1;
        uint16_t supports_gen3_signaling_rate : 1;
        uint16_t unknown2 : 4;
        uint16_t supports_ncq_scheme : 1;
        uint16_t unknown3 : 4;
        uint16_t supports_unload_while_ncq_commands_outstanding : 1;
        uint16_t supports_ncq_priority_information : 1;
        uint16_t supports_read_log_dma_ext : 1;

        // 1
        uint16_t unknown4 : 1;
        uint16_t gen2_signaling_speed_of_3gbps : 3;
        uint16_t supports_ncq_streaming : 1;
        uint16_t supports_ncq_queue_management : 1;
        uint16_t unknown5 : 10;

        // 2
        uint16_t unknown6 : 1;
        uint16_t enable_nonzero_buffer_offset_in_dma_setup_fis : 1;
        uint16_t enable_dma_setup_auto_active_optimization : 1;
        uint16_t enable_device_initiating_interface_power_management : 1;
        uint16_t enable_in_order_data_delivery : 1;
        uint16_t unknown7 : 11;

        // 3
        uint16_t unknown8;
    } __attribute__((packed)) ext;
    uint16_t major_version; // 80
    uint16_t minor_version; // 81
    struct {                // 82
        // 0
        uint16_t smart : 1;
        uint16_t security_feature : 1;
        uint16_t removable_feature : 1;
        uint16_t power_management : 1;
        uint16_t packet : 1;
        uint16_t write_cache_feature : 1;
        uint16_t lock_ahead : 1;
        uint16_t release_interrupt : 1;
        uint16_t service_interrupt : 1;
        uint16_t device_reset : 1;
        uint16_t hpa_feature : 1;
        uint16_t unknown1 : 1;
        uint16_t write_buffer : 1;
        uint16_t read_buffer : 1;
        uint16_t nop : 1;
        uint16_t unknown2 : 1;

        // 1
        uint16_t download_microcode : 1;
        uint16_t read_write_dma_queued : 1;
        uint16_t cfa_feature : 1;
        uint16_t apm_feature : 1;
        uint16_t removable_media_status_notification_feature : 1;
        uint16_t power_up_in_standby_feature : 1;
        uint16_t set_features_subcommand_after_power_up : 1;
        uint16_t set_max_security_extension : 1;
        uint16_t aam_feature : 1;
        uint16_t lba_feature : 1;
        uint16_t device_configuration_overlay : 1;
        uint16_t flush_cache : 1;
        uint16_t flush_cache_ext : 1;
        uint16_t unknown3 : 3;

        // 2
        uint16_t smart_error_logging : 1;
        uint16_t smart_self_test : 1;
        uint16_t unknown4 : 3;
        uint16_t general_purpose_logging_feature : 1;
        uint16_t unknown5 : 10;
    } __attribute__((packed)) command_set;
    struct { // 85
        // 0
        uint16_t smart_feature : 1;
        uint16_t security_feature : 1;
        uint16_t removable_feature : 1;
        uint16_t power_management_feature : 1;
        uint16_t packet_command_feature : 1;
        uint16_t write_cache_feature : 1;
        uint16_t lock_ahead : 1;
        uint16_t release_interrupt : 1;
        uint16_t service_interrupt : 1;
        uint16_t device_reset : 1;
        uint16_t hpa_feature : 1;
        uint16_t write_buffer : 1;
        uint16_t read_buffer : 1;
        uint16_t nop : 1;
        uint16_t unknown1 : 2;

        // 1
        uint16_t download_microcode : 1;
        uint16_t read_write_dma_queued : 1;
        uint16_t cfa_feature : 1;
        uint16_t apm_feature : 1;
        uint16_t removable_media_status_notification_feature : 1;
        uint16_t power_up_in_standby_feature : 1;
        uint16_t set_features_subcommand_after_power_up : 1;
        uint16_t set_max_security_extension : 1;
        uint16_t aam_feature : 1;
        uint16_t lba_feature : 1;
        uint16_t device_configuration_overlay : 1;
        uint16_t flush_cache : 1;
        uint16_t flush_cache_ext : 1;
        uint16_t unknown2 : 1;
        uint16_t word_119_120 : 1;
        uint16_t unknown3 : 1;
    } __attribute__((packed)) enable;
    uint16_t command_set2;                                                      // 87
    uint16_t supports_ultra_dma;                                                // 88
    uint16_t time_required_for_normal_erase_mode_security_erase_unit_command;   // 89
    uint16_t time_required_for_enhanced_erase_mode_security_erase_unit_command; // 90
    uint16_t current_apm_setting;                                               // 91 :0-7
    uint16_t master_password_revision_code;                                     // 92
    uint16_t hardware_reset_result;                                             // 93
    uint16_t auto_silence_value;                                                // 94
    uint16_t reserved5[5];                                                      // 95
    uint16_t available_48bit_lba[4];                                            // 100
    uint16_t reserved6[2];                                                      // 104
    struct {                                                                    // 106
        uint16_t logical_sectors_per_physical_sector : 4;
        uint16_t unknown1 : 8;
        uint16_t logical_sector_longer_than_512byte : 1;
        uint16_t has_multiple_logical_sector_per_physical_sector : 1;
        uint16_t unknown2 : 2;
    } __attribute__((packed)) logical_sector;
    uint16_t reserved7[21]; // 107
    struct {                // 128
        uint16_t supported : 1;
        uint16_t enabled : 1;
        uint16_t locked : 1;
        uint16_t frozen : 1;
        uint16_t count_expired : 1;
        uint16_t enhanced_erase_supported : 1;
        uint16_t unknown1 : 2;
        uint16_t level : 1;
        uint16_t unknown2 : 7;
    } __attribute__((packed)) security;
    uint16_t reserved8[5]; // 129
    struct {               // 134
        uint16_t fatal : 1;
        uint16_t entropy_source : 1;
        uint16_t drbg : 1;
        uint16_t bist : 1;
        uint16_t unknown1 : 12;
    } __attribute__((packed)) error;
    uint16_t reserved9[74];                                       // 135
    uint16_t alignment_of_logical_blocks_within_a_physical_block; // 209
    uint16_t reserved10[7];                                       // 210
    uint16_t nominal_media_rotation_rate;                         // 217
    uint16_t reserved11[4];                                       // 218
    struct {                                                      // 222
        uint16_t unknown1 : 3;
        uint16_t rev2_5 : 1;
        uint16_t rev2_6 : 1;
        uint16_t rev3_0 : 1;
        uint16_t rev3_1 : 1;
        uint16_t unknown2 : 5;
        uint16_t sata : 1;
        uint16_t unknown3 : 3;
    } __attribute__((packed)) sata;
    uint16_t reserved12[33]; // 223
} __attribute__((packed));

static_assert(sizeof(DeviceIdentifier) == (512));
} // namespace ata
