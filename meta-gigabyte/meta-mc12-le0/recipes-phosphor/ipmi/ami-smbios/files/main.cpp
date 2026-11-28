// SPDX-License-Identifier: Apache-2.0
//
// AMI SMBIOS transport for the Gigabyte MC12-LE0.
//
// The host BIOS does not use Intel's MDR v2 blob transport. It pushes its
// SMBIOS tables into a 16 MiB DRAM window below the VGA framebuffer
// (0x9e000000) and announces them on NetFn 0x2E under Gigabyte's IANA. This
// provider runs the stock SCU handshake that opens the host write path,
// answers the window-address query, and republishes the pushed tables to
// smbios-mdr through /var/lib/smbios/smbios2 and AgentSynchronizeData.
//
// The window is read through /dev/aspeed-lpc-ctrl, which the DTS points at the
// reserved carve-out, so STRICT_DEVMEM stays on. Only the SCU MMIO poke uses
// /dev/mem.
//
// This is the transport only. NetFn 0x32 (0x3D/0x5D) is deliberately not
// handled here: the BIOS host-interface account needs its own service user and
// review.

#include <ipmid/api.hpp>
#include <ipmid/api-types.hpp>
#include <ipmid/handler.hpp>
#include <ipmid/message.hpp>
#include <ipmid/message/types.hpp>

#include <phosphor-logging/lg2.hpp>

#include <sdbusplus/bus.hpp>
#include <sdbusplus/exception.hpp>
#include <sdbusplus/message.hpp>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <utility>
#include <vector>

namespace
{

// The two OEM IANAs the MC12-LE0's BIOS addresses us with on NetFn 0x2E. The
// framework packs the 3 wire bytes little-endian:
//
//   host sends 0A 3C 00  ->  0x003C0A   every command except 0xCC
//   host sends 5E 2B 00  ->  0x002B5E   the 0xCC entries
constexpr ipmi::Iana ianaInventory = 0x003C0A;
constexpr ipmi::Iana ianaSecondary = 0x002B5E;

// Command numbers observed from the host BIOS on NetFn 0x2E.
constexpr uint8_t cmdAmiAllocateMemory = 0x20;
constexpr uint8_t cmdAmiFreeMemory = 0x21;
constexpr uint8_t cmdAmiReadMemory = 0x30;
constexpr uint8_t cmdAmiCopyMemory = 0x32;
constexpr uint8_t cmdAmiCompareMemory = 0x33;
constexpr uint8_t cmdAmiAddExtendSelEntries = 0xCC;

// Bytes of IANA the framework consumes before dispatching to us.
constexpr size_t ianaBytes = 3;

// Gigabyte's netfn-0x2E handlers validate the exact request length and then
// copy request fields into fixed response offsets; the convention is an echo.
// Responses are not framed by this provider: ipmid prepends the IANA.
constexpr size_t maxEchoBytes = 64;

// COMPARE_MEMORY answer: 0x01 means "not equal". Answering "equal" risks the
// BIOS deciding the BMC already holds the dataset and skipping the push.
constexpr uint8_t compareNotEqual = 0x01;

// Shared-window opcodes, the first payload byte after the IANA.
constexpr uint8_t opSmbiosTransfer = 0x0B;
constexpr uint8_t opSmbiosTransferAlt = 0x13;
constexpr uint8_t opRedfishPost = 0x52;
constexpr uint8_t opSetWriteProtect = 0x24;
constexpr uint8_t opShmAddrQuery = 0x0A;
constexpr uint8_t opGetShmAddr = 0x50;

// The host-push window is a live device that the host may keep writing while
// we read; the driver maps it with pgprot_noncached.
constexpr const char* hostWindowDev = "/dev/aspeed-lpc-ctrl";
constexpr size_t hostWindowBytes = 16 * 1024 * 1024;

// The window base handed back in the GetShmAddr reply: the 16 MiB immediately
// below the host's VGA framebuffer, which is where stock's mem=480M derivation
// lands. Handing back the framebuffer address lets the VGA engine overwrite
// every SMBIOS push.
constexpr uint32_t shmWindowPhys = 0x9E000000u;

struct HostWindowMap
{
    int fd{-1};
    uint8_t* base{static_cast<uint8_t*>(MAP_FAILED)};
};

HostWindowMap& hostWindowMap()
{
    static HostWindowMap m;
    if (m.fd < 0)
    {
        m.fd = ::open(hostWindowDev, O_RDONLY | O_CLOEXEC);
        if (m.fd < 0)
        {
            return m;
        }
        m.base = static_cast<uint8_t*>(
            ::mmap(nullptr, hostWindowBytes, PROT_READ, MAP_SHARED, m.fd, 0));
        if (m.base == MAP_FAILED)
        {
            ::close(m.fd);
            m.fd = -1;
        }
    }
    return m;
}

// --- OpenBMC inventory bridge ------------------------------------------------
//
// The pushed bytes are an SMBIOS table set, usually a 32-bit '_SM_' entry
// point followed by the structure table. smbios-mdr only steps over the 64-bit
// '_SM3_' entry point, so re-emit the structure table behind a synthetic
// '_SM3_' entry point and hand it over through the door the IPMI blob handler
// uses: [MDRSMBIOSHeader][data] in /var/lib/smbios/smbios2, then
// AgentSynchronizeData on xyz.openbmc_project.Smbios.MDR_V2.
constexpr const char* smbiosTableDir = "/var/lib/smbios";
constexpr const char* smbiosTablePath = "/var/lib/smbios/smbios2";
constexpr const char* mdrV2Service = "xyz.openbmc_project.Smbios.MDR_V2";
constexpr const char* mdrV2Interface = "xyz.openbmc_project.Smbios.MDR_V2";
constexpr const char* mdrV2ObjectPath = "/xyz/openbmc_project/Smbios/MDR_V2";
constexpr uint8_t mdrDirVersion = 1;
constexpr uint8_t mdrTypeII = 2;
constexpr size_t smbiosTableStorageSize = 64 * 1024;
constexpr size_t smbiosEntryPoint30Len = 0x18;
constexpr uint64_t mdrSyncTimeoutUs = 5'000'000;

uint16_t rdLe16(const uint8_t* p)
{
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

uint32_t rdLe32(const uint8_t* p)
{
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

uint64_t rdLe64(const uint8_t* p)
{
    return static_cast<uint64_t>(rdLe32(p)) |
           (static_cast<uint64_t>(rdLe32(p + 4)) << 32);
}

void appendLe32(std::vector<uint8_t>& out, uint32_t value)
{
    for (uint32_t i = 0; i < 4; ++i)
    {
        out.push_back(static_cast<uint8_t>(value >> (8 * i)));
    }
}

// Returns quietly on anything it does not recognise; the IPMI reply the BIOS
// is waiting for must not depend on the inventory daemon.
void publishSmbiosToInventory(const uint8_t* data, size_t len)
{
    if (data == nullptr || len < smbiosEntryPoint30Len)
    {
        return;
    }

    size_t ep = len;
    bool epIs64 = false;
    for (size_t i = 0; i + 5 <= len; ++i)
    {
        if (std::memcmp(data + i, "_SM3_", 5) == 0)
        {
            ep = i;
            epIs64 = true;
            break;
        }
    }
    if (ep == len)
    {
        for (size_t i = 0; i + 4 <= len; ++i)
        {
            if (std::memcmp(data + i, "_SM_", 4) == 0)
            {
                ep = i;
                break;
            }
        }
    }
    if (ep == len)
    {
        return;
    }

    size_t tableStart = 0;
    size_t tableLen = 0;
    uint8_t major = 3;
    uint8_t minor = 3;
    if (epIs64)
    {
        if (ep + 0x18 > len)
        {
            return;
        }
        // 64-bit entry point: structure table address at 0x10, version at 7/8.
        tableStart = ep + static_cast<size_t>(rdLe64(data + ep + 0x10));
        tableLen = len > tableStart ? len - tableStart : 0;
        major = data[ep + 7];
        minor = data[ep + 8];
    }
    else
    {
        if (ep + 0x1f > len)
        {
            return;
        }
        // 32-bit entry point: version at 6/7, entry point length at 5, and the
        // structure table length at 0x16. The table follows the entry point.
        const uint8_t epLen = data[ep + 5];
        tableStart = ep + (epLen >= 0x1f && epLen <= 0x20 ? epLen : 0x1f);
        tableLen = rdLe16(data + ep + 0x16);
        major = data[ep + 6];
        minor = data[ep + 7];
    }
    if (tableStart >= len || tableLen == 0)
    {
        return;
    }
    tableLen = std::min(tableLen, len - tableStart);
    tableLen =
        std::min(tableLen, smbiosTableStorageSize - smbiosEntryPoint30Len);
    if (major < 3)
    {
        // smbios-mdr only accepts 3.x, but the structure table is
        // version-independent for the types it reads.
        major = 3;
        minor = 3;
    }

    // Synthetic 64-bit entry point, so smbiosSkipEntryPoint() steps over it.
    std::vector<uint8_t> out(smbiosEntryPoint30Len, 0x00);
    std::memcpy(out.data(), "_SM3_", 5);
    out[6] = static_cast<uint8_t>(smbiosEntryPoint30Len);
    out[7] = major;
    out[8] = minor;
    out[9] = 0x01;
    out[10] = 0x01;
    constexpr uint32_t maxTableSize = 0x80000000u;
    constexpr uint64_t tableAddr = smbiosEntryPoint30Len;
    for (int i = 0; i < 4; ++i)
    {
        out[0x0c + i] = static_cast<uint8_t>(maxTableSize >> (8 * i));
    }
    for (int i = 0; i < 8; ++i)
    {
        out[0x10 + i] = static_cast<uint8_t>(tableAddr >> (8 * i));
    }
    out.insert(out.end(), data + tableStart, data + tableStart + tableLen);

    // The entry point's own 24 bytes must sum to zero mod 256.
    uint8_t sum = 0;
    for (size_t i = 0; i < smbiosEntryPoint30Len; ++i)
    {
        sum = static_cast<uint8_t>(sum + out[i]);
    }
    out[5] = static_cast<uint8_t>(0 - sum);

    if (::access(smbiosTableDir, F_OK) != 0)
    {
        (void)::mkdir(smbiosTableDir, 0755);
    }

    std::vector<uint8_t> file;
    file.reserve(sizeof(uint32_t) * 2 + sizeof(mdrDirVersion) +
                 sizeof(mdrTypeII) + out.size());
    file.push_back(mdrDirVersion);
    file.push_back(mdrTypeII);
    appendLe32(file, static_cast<uint32_t>(std::time(nullptr)));
    appendLe32(file, static_cast<uint32_t>(out.size()));
    file.insert(file.end(), out.begin(), out.end());

    const int fd = ::open(smbiosTablePath,
                          O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0)
    {
        lg2::error("ami-smbios: cannot open {PATH}", "PATH", smbiosTablePath);
        return;
    }
    const ssize_t n = ::write(fd, file.data(), file.size());
    (void)::fsync(fd);
    ::close(fd);
    if (n != static_cast<ssize_t>(file.size()))
    {
        lg2::error("ami-smbios: short write to {PATH}", "PATH", smbiosTablePath);
        return;
    }

    try
    {
        sdbusplus::bus_t bus(ipmid_get_sd_bus_connection());
        auto method = bus.new_method_call(mdrV2Service, mdrV2ObjectPath,
                                          mdrV2Interface, "AgentSynchronizeData");
        auto reply = bus.call(method, mdrSyncTimeoutUs);
        bool synced = false;
        reply.read(synced);
    }
    catch (const sdbusplus::exception_t& e)
    {
        lg2::error("ami-smbios: AgentSynchronizeData failed: {ERR}", "ERR",
                   e.what());
    }
}

// --- host write-path enable (stock endis_share_memory) ----------------------
//
// The stock firmware toggles the platform write gate in endis_share_memory().
// The reconstructed sequence is:
//
//   open /dev/mem, mmap 0x200 at SCU 0x1e6e2000
//   SCU00 = 0x1688a8a8                      <- unlock
//   platform id = u16 at SCU + 0x66
//     if (id == 0x139 || id == 0x13b) && enable    SCU40 |= 0x1000
//   if (disable) { SCU180 &= ~0x2;  SCU40 |= 0x1000; }
//   else         { SCU180 |=  0x2;  if (id not special) SCU40 &= ~0x1000; }
//   SCU00 = 0                               <- re-lock
//
// SCU180 bit1 is SCU180_ENP2A; SCU40 bit12 is the companion bit stock only
// raises for the two special platform ids. SCU2C's region gates come up open
// from u-boot and stay untouched.
constexpr off_t scuBase = 0x1e6e2000;
constexpr size_t scuSpan = 0x200;
constexpr const char* memDev = "/dev/mem";

constexpr size_t scuOffUnlock = 0x00;
constexpr size_t scuOffCtrl40 = 0x40;
constexpr size_t scuOffPcie180 = 0x180;
constexpr size_t scuOffPlatform = 0x66;

constexpr uint32_t scuUnlockKey = 0x1688A8A8u;
constexpr uint32_t scu180EnP2A = 0x00000002u;
constexpr uint32_t scu40Shadow = 0x00001000u;

constexpr uint16_t platformIdSpecialA = 0x0139;
constexpr uint16_t platformIdSpecialB = 0x013B;

int openMemDevice()
{
    int fd = ::open(memDev, O_RDWR | O_SYNC | O_CLOEXEC);
    if (fd >= 0)
    {
        return fd;
    }
    (void)::mknod(memDev, S_IFCHR | 0600, ::makedev(1, 1));
    return ::open(memDev, O_RDWR | O_SYNC | O_CLOEXEC);
}

bool endisShareMemory(bool enable)
{
    const int fd = openMemDevice();
    if (fd < 0)
    {
        return false;
    }
    void* map = ::mmap(nullptr, scuSpan, PROT_READ | PROT_WRITE, MAP_SHARED,
                       fd, scuBase);
    if (map == MAP_FAILED)
    {
        ::close(fd);
        return false;
    }
    volatile uint8_t* b = static_cast<volatile uint8_t*>(map);
    volatile uint32_t* r = reinterpret_cast<volatile uint32_t*>(map);

    const uint16_t platformId =
        *reinterpret_cast<volatile uint16_t*>(b + scuOffPlatform);
    const bool special = (platformId == platformIdSpecialA ||
                          platformId == platformIdSpecialB);

    r[scuOffUnlock / 4] = scuUnlockKey;
    if (enable && special)
    {
        r[scuOffCtrl40 / 4] |= scu40Shadow;
    }
    if (!enable)
    {
        r[scuOffPcie180 / 4] &= ~scu180EnP2A;
        r[scuOffCtrl40 / 4] |= scu40Shadow;
    }
    else
    {
        r[scuOffPcie180 / 4] |= scu180EnP2A;
        if (!special)
        {
            r[scuOffCtrl40 / 4] &= ~scu40Shadow;
        }
    }
    r[scuOffUnlock / 4] = 0;

    ::munmap(map, scuSpan);
    ::close(fd);
    return true;
}

void enableHostPushPath()
{
    if (!endisShareMemory(true))
    {
        lg2::error("ami-smbios: cannot map the SCU to enable the host push "
                   "path");
    }
}

ipmi::RspType<std::vector<uint8_t>> handleAmiOemCommand(
    ipmi::message::Request::ptr request, const std::vector<uint8_t>& body)
{
    const ipmi::Context::ptr ctx = request->ctx;
    const uint8_t cmd = ctx->cmd;

    // The framework already consumed the 3-byte IANA before dispatching, but
    // Payload::data()/size() report the whole buffer.
    const uint8_t* raw = request->payload.data();
    const size_t rawLen = request->payload.size();
    ipmi::Iana iana = 0;
    if (rawLen >= ianaBytes)
    {
        iana = static_cast<ipmi::Iana>(raw[0] | (raw[1] << 8) |
                                       (raw[2] << 16));
    }

    // The push is complete by the time it is announced, so publish from here.
    if (!body.empty() && iana == ianaInventory && body.size() >= 3 &&
        (body[0] == opSmbiosTransfer || body[0] == opSmbiosTransferAlt))
    {
        const size_t windowLen = static_cast<size_t>(body[1]) |
                                 (static_cast<size_t>(body[2]) << 8);
        if (windowLen > 0)
        {
            HostWindowMap& win = hostWindowMap();
            if (win.base != MAP_FAILED)
            {
                publishSmbiosToInventory(win.base, windowLen);
            }
            else
            {
                lg2::error("ami-smbios: host-push window is not mapped");
            }
        }
    }

    std::vector<uint8_t> reply;
    if (body.size() <= maxEchoBytes)
    {
        reply = body;
    }

    switch (cmd)
    {
        case cmdAmiAllocateMemory:
            // Carry only the leading id, not the buffer name.
            reply.assign(body.begin(),
                         body.begin() + std::min<size_t>(4, body.size()));
            break;
        case cmdAmiFreeMemory:
        case cmdAmiCopyMemory:
        case 0x11:
        case cmdAmiAddExtendSelEntries:
            reply.clear();
            break;
        case cmdAmiReadMemory:
            if (body.size() >= 12)
            {
                constexpr uint32_t maxReadBack = 240;
                const uint32_t len = static_cast<uint32_t>(body[8]) |
                                     (static_cast<uint32_t>(body[9]) << 8) |
                                     (static_cast<uint32_t>(body[10]) << 16) |
                                     (static_cast<uint32_t>(body[11]) << 24);
                if (len > 0 && len <= maxReadBack)
                {
                    reply.assign(len, 0x00);
                }
            }
            break;
        case cmdAmiCompareMemory:
            reply.assign(1, compareNotEqual);
            break;
        default:
            break;
    }

    // Stock-shaped replies for the window-anchored ops.
    if (!body.empty() && iana == ianaInventory)
    {
        const uint8_t op = body[0];
        std::vector<uint8_t> stock;
        if (op == opShmAddrQuery || (op == opGetShmAddr && body.size() >= 6))
        {
            // _GetShmAddr_ re-asserts the write path before answering.
            if (!endisShareMemory(true))
            {
                lg2::error("ami-smbios: cannot re-enable the host write path");
            }
            stock = {static_cast<uint8_t>(shmWindowPhys),
                     static_cast<uint8_t>(shmWindowPhys >> 8),
                     static_cast<uint8_t>(shmWindowPhys >> 16),
                     static_cast<uint8_t>(shmWindowPhys >> 24), 0x01};
        }
        else if (op == opSetWriteProtect && body.size() >= 2)
        {
            // body[1] 0 opens the window, 1 closes it. The reply stays an echo.
            const bool enable = (body[1] == 0x00);
            if (!endisShareMemory(enable))
            {
                lg2::error("ami-smbios: cannot toggle the host write path");
            }
        }
        else if ((op == opSmbiosTransfer || op == opSmbiosTransferAlt) &&
                 body.size() >= 5)
        {
            stock = {body[3], body[4], 0x00, 0x00, 0x00};
        }
        else if (op == opRedfishPost && body.size() >= 6)
        {
            // The Redfish dataset is not consumed yet; keep the stock reply
            // shape so the BIOS does not stall on it.
            stock = {body[1], body[2], body[3], body[4], body[5],
                     0x00,     0x00,     0x00,     0x01};
        }
        if (!stock.empty())
        {
            reply = std::move(stock);
        }
    }

    return ipmi::responseSuccess(reply);
}

void registerAmiSmbios() __attribute__((constructor));

void registerAmiSmbios()
{
    enableHostPushPath();

    for (const ipmi::Iana iana : {ianaInventory, ianaSecondary})
    {
        // The host KCS channel is session-less, which ipmid runs at Admin
        // privilege. Requiring Admin keeps network User/Operator sessions out
        // of the SCU write path.
        ipmi::registerOemHandler(
            ipmi::prioOemBase, iana, ipmi::cmdWildcard,
            ipmi::Privilege::Admin,
            [](ipmi::message::Request::ptr request,
               const std::vector<uint8_t>& body) {
                return handleAmiOemCommand(request, body);
            });
    }

    lg2::info("ami-smbios: registered NetFn 0x2E handlers for IANA "
              "{IANA1}/{IANA2}",
              "IANA1", lg2::hex, ianaInventory, "IANA2", lg2::hex,
              ianaSecondary);
}

} // namespace
