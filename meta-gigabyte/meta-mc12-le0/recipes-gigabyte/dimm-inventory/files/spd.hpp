#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace dimm
{

// Module type, JEDEC DDR4 SPD byte 3.
enum class ModuleType
{
    RDIMM,
    UDIMM,
    SODIMM,
    LRDIMM,
    MiniRDIMM,
    MiniUDIMM,
    MiniLRDIMM,
    Other,
    Unknown,
};

// Everything we publish is derived from fields that have been checked against
// known DDR4 UDIMMs.
struct SpdInfo
{
    std::string dimmType;      // "DDR4"
    ModuleType moduleType{ModuleType::Unknown};
    std::string moduleTypeStr; // "UDIMM"

    uint32_t capacityMiB{};
    uint8_t rankCount{};
    uint8_t busWidthBits{};   // module data bus width, e.g. 64
    uint8_t eccWidthBits{};   // 0 for non-ECC UDIMM
    uint8_t deviceWidthBits{}; // per-device width, e.g. 8

    std::string partNumber;
    // Manufacturer name is intentionally not decoded: JEP106 is a large,
    // periodically revised table, and a partial mapping is more misleading
    // than an absent property.
    std::string serialNumber;
};

// Decodes a DDR4 SPD page-0 image (256 bytes). Page-1 fields (part number and
// serial) are decoded only when a 512-byte image is supplied; on this board
// the host leaves the EEPROM on page 1 and the kernel cannot switch pages, so
// those fields are normally absent.
//
// Returns nullopt unless the image is a DDR4 SPD (byte 0 == 0x23, byte 2 ==
// 0x0C) AND the page-0 JEDEC CRC16 field checks out. If a full 512-byte image
// is supplied, the page-1 CRC must check out as well. The CRC check is what
// makes this safe to gate inventory on: a slot with no DIMM, or a device that
// is not really an SPD, will not produce an object.
//
// NOTE: deliberately does not report operating speed. The timing region of
// the SPDs on this board does not decode to a sane JEDEC bin, so exposing
// OperatingSpeedMhz from SPD would publish a wrong number; the configured
// speed is taken from SMBIOS Type 17 instead, see smbios.hpp.
std::optional<SpdInfo> decodeDdr4Spd(const std::vector<uint8_t>& spd);

// CRC-16/XMODEM (poly 0x1021, init 0x0000), as used by JEDEC DDR4 SPD.
uint16_t crc16(const uint8_t* data, size_t len);

} // namespace dimm
