// SPDX-License-Identifier: Apache-2.0
//
// Request tracer for the MC12-LE0 IPMI OEM work.
//
// This is not the SMBIOS transport provider. It is a pass-through filter for
// bring-up images: it records every IPMI request (netfn, cmd, channel,
// privilege and a bounded body) so unknown OEM commands and BIOS POST
// sequences can still be captured without rebuilding the core provider.
//
// Bodies on NetFn App (users/sessions) and NetFn 0x32 cmd 0x5D (host-interface
// credentials) are redacted. A filter sees requests only, not replies.
//
// journald on the AST2500 is slow and lossy under POST bursts, so the record
// goes to /tmp/ami-ipmi-trace.log; read it with cat.

#include <ipmid/api-types.hpp>
#include <ipmid/api.hpp>
#include <ipmid/filter.hpp>
#include <ipmid/message.hpp>

#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <string>
#include <vector>

namespace
{

constexpr const char* tracePath = "/tmp/ami-ipmi-trace.log";
constexpr size_t maxLoggedBody = 192;

std::string toHex(uint32_t value, int width)
{
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%0*X", width, value);
    return buf;
}

std::string timestamp()
{
    timespec ts{};
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0)
    {
        return "?";
    }

    std::tm tm{};
    localtime_r(&ts.tv_sec, &tm);

    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm);

    char out[48];
    std::snprintf(out, sizeof(out), "%s.%03ld", buf, ts.tv_nsec / 1000000);
    return out;
}

void append(const std::string& line)
{
    int fd = ::open(tracePath, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
    if (fd < 0)
    {
        return;
    }

    std::string out = line;
    out += '\n';
    // Best effort: a failed trace write must never affect the IPMI reply.
    ssize_t ignored = ::write(fd, out.data(), out.size());
    (void)ignored;
    ::close(fd);
}

bool redactBody(uint8_t netFn, uint8_t cmd)
{
    if (netFn == ipmi::netFnApp)
    {
        return true;
    }
    if (netFn == ipmi::netFnOemTwo && cmd == 0x5D)
    {
        return true;
    }
    return false;
}

std::string hexDump(const std::vector<uint8_t>& data)
{
    const size_t shown = std::min(data.size(), maxLoggedBody);
    std::string out;
    out.reserve(shown * 3 + 16);

    char tmp[4];
    for (size_t i = 0; i < shown; i++)
    {
        std::snprintf(tmp, sizeof(tmp), "%02X ",
                      static_cast<unsigned>(data[i]));
        out += tmp;
    }
    if (shown < data.size())
    {
        out += "... (";
        out += std::to_string(data.size() - shown);
        out += " more)";
    }
    return out;
}

ipmi::Cc traceRequest(ipmi::message::Request::ptr request)
{
    const ipmi::Context::ptr ctx = request->ctx;

    std::string line = timestamp();
    line += " netfn=0x";
    line += toHex(ctx->netFn, 2);
    line += " cmd=0x";
    line += toHex(ctx->cmd, 2);
    line += " chan=";
    line += std::to_string(static_cast<unsigned>(ctx->channel));
    line += " priv=";
    line += std::to_string(static_cast<unsigned>(ctx->priv));
    line += " len=";
    line += std::to_string(request->payload.raw.size());

    if (redactBody(ctx->netFn, ctx->cmd))
    {
        line += " body=<redacted>";
    }
    else
    {
        const std::vector<uint8_t> body(request->payload.raw.begin(),
                                        request->payload.raw.end());
        line += " body=[";
        line += hexDump(body);
        line += "]";
    }

    append(line);
    return ipmi::ccSuccess;
}

void registerTrace() __attribute__((constructor));

void registerTrace()
{
    // prioOemBase precedes the whitelist filter, so this also sees commands
    // that are later rejected.
    (void)ipmi::registerFilter(
        ipmi::prioOemBase, [](ipmi::message::Request::ptr request) {
            return traceRequest(request);
        });
    append(timestamp() + " ami-smbios-trace: registered");
}

} // namespace
