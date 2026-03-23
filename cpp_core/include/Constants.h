#pragma once
#include <cstdint>

// ── BetterSend — Compile-time constants ──────────────────────────────────────
// All magic numbers live here. Never hardcode these in logic.

namespace BetterSend {

    // ── Network ───────────────────────────────────────────────────────────────
    inline constexpr int     kDefaultPort        = 9000;
    inline constexpr int     kMdnsPort           = 5353;
    inline constexpr int     kMdnsTtlSeconds     = 255;
    inline constexpr char    kServiceType[]      = "_bettersend._tcp.local.";
    inline constexpr char    kMdnsMulticastAddr[] = "224.0.0.251";

    // ── Protocol ──────────────────────────────────────────────────────────────
    inline constexpr uint32_t kHeaderLengthBytes  = 4;     // size prefix (big-endian)
    inline constexpr uint32_t kMaxHeaderSizeBytes = 4096;  // guard against malformed headers
    inline constexpr uint32_t kReceiveBufferSize  = 65536; // 64 KB read chunks

    // ── Device ────────────────────────────────────────────────────────────────
    inline constexpr int kMaxDeviceNameLen = 64;

    // ── Logger ────────────────────────────────────────────────────────────────
    inline constexpr char kDefaultLogFile[] = "bettersend_debug.log";
    inline constexpr int  kLogQueueCapacity = 256; // max buffered entries before flush

} // namespace BetterSend
