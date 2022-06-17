#pragma once
#include <cstdint>

namespace usb {
enum class RecipientRequestType {
    Device    = 0,
    Interface = 1,
    Endpoint  = 2,
    Other     = 3,
};

enum class TypeRequestType {
    Standard = 0,
    Class    = 1,
    Vendor   = 2,
};

enum class DirectionRequestType {
    Out = 0,
    In  = 1,
};

enum class Request {
    GetStatus         = 0,
    ClearFeature      = 1,
    SetFeature        = 3,
    SetAddress        = 5,
    GetDescriptor     = 6,
    SetDescriptor     = 7,
    GetConfiguration  = 8,
    SetConfiguration  = 9,
    GetInterface      = 10,
    SetInterface      = 11,
    SynchFrame        = 12,
    SetEncryption     = 13,
    GetEncryption     = 14,
    SetHandshake      = 15,
    GetHandshake      = 16,
    SetConnection     = 17,
    SetSecurityData   = 18,
    GetSecurityData   = 19,
    SetWUSBData       = 20,
    LoopbackDataWrite = 21,
    LoopbackDataRead  = 22,
    SetInterfaceDS    = 23,
    SetSel            = 48,
    SetIsochDelay     = 49,

    // HID class specific
    GetReport   = 1,
    SetProtocol = 11,
};

enum class DescriptorType {
    Device                                     = 1,
    Configuration                              = 2,
    String                                     = 3,
    Interface                                  = 4,
    Endpoint                                   = 5,
    InterfacePower                             = 8,
    OTG                                        = 9,
    Debug                                      = 10,
    InterfaceAssociation                       = 11,
    BOS                                        = 15,
    DeviceCapability                           = 16,
    HID                                        = 33,
    SuperspeedUSBEndpointCompanion             = 48,
    SuperspeedPlusIsochronousEndpointCompanion = 49,
};

struct SetupData {
    union {
        uint8_t data;
        struct {
            uint8_t recipient : 5;
            uint8_t type : 2;
            uint8_t direction : 1;
        } bits;
    } request_type;
    uint8_t  request;
    uint16_t value;
    uint16_t index;
    uint16_t length;

    auto operator==(const SetupData& o) const -> bool {
        return request_type.data == o.request_type.data &&
               request == o.request &&
               value == o.value &&
               index == o.index &&
               length == o.length;
    }
};
} // namespace usb
