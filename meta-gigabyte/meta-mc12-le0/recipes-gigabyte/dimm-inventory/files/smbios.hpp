#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace smbios
{

// One SMBIOS Type 17 (Memory Device) entry, to the extent this daemon needs
// it. Everything else Type 17 carries (rank, width, ECC, voltages) is already
// decoded from the SPD EEPROM by spd.cpp and is not read here.
//
// The fields used here are the locator, manufacturer, serial number, part
// number, capacity and configured speed. The serial pairs an EEPROM with its
// host Type 17 entry; manufacturer and part number are published only when
// the host supplies a non-placeholder value.
struct MemoryDevice
{
    std::string deviceLocator;
    std::string bankLocator;
    std::string manufacturer;
    std::string serialNumber;
    std::string partNumber;
    uint32_t capacityMiB{};
    uint16_t configuredSpeedMhz{};
};

// Reads the SMBIOS transport file that smbios-mdr keeps (/var/lib/smbios/
// smbios2): a 10-byte MDRSMBIOSHeader followed by an SMBIOS entry point and
// the raw structure table, as written either by ami-smbios (this board's
// AMI push) or by smbios-mdr's own IPMI blob handler.
//
// Returns the Type 17 entries in the order the host BIOS emitted them, or an
// empty vector if the file is absent or does not parse -- which is the normal
// state before the host has POSTed, and must therefore not be an error.
std::vector<MemoryDevice> readMemoryDevices(const std::string& path);

} // namespace smbios
