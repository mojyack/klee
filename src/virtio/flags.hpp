#pragma once
#include <cstdint>

namespace virtio {
using Features = uint64_t;
namespace features {
constexpr auto indirect_ring_descriptors = uint64_t(1) << 28; // the driver can use descriptors with the QueueFlags::Indirect flag set
constexpr auto ring_index_event          = uint64_t(1) << 29; // enable the used_event and the avail_event fields
constexpr auto version1                  = uint64_t(1) << 32;
constexpr auto access_platform           = uint64_t(1) << 33;
constexpr auto packed_ring               = uint64_t(1) << 34; // support for the packed virtqueue layout
constexpr auto in_order                  = uint64_t(1) << 35; // all buffers are used by the device in the same order in which they have been made available
constexpr auto order_platform            = uint64_t(1) << 36;
constexpr auto single_root_iov           = uint64_t(1) << 37; // the device supports Single Root I/O Virtualization
constexpr auto notification_data         = uint64_t(1) << 38; // the driver passes extra data in its device notifications
} // namespace features

using DeviceStatus = uint8_t;
namespace device_status {
constexpr auto reset              = 0;   // write this value to status bit for device reset
constexpr auto acknowledge        = 1;   // the guest OS has found the device and recognized it as a valid virtio device
constexpr auto driver             = 2;   // the guest OS knows how to drive the device
constexpr auto features_ok        = 8;   // the driver has acknowledged all the features it understands, and feature negotiation is complete
constexpr auto driver_ok          = 4;   // the driver is set up and ready to drive the device
constexpr auto failed             = 128; // something went wrong in the guest, and it has given up on the device
constexpr auto device_needs_reset = 64;  // the device has experienced an error from which it can't recover
} // namespace device_status
} // namespace virtio
