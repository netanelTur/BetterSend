#pragma once
#include <cstdint>

// ── BetterSend — Compile-time constants ──────────────────────────────────────
// All magic numbers live here. Never hardcode these in logic.

namespace BetterSend {

// ── Network ───────────────────────────────────────────────────────────────────
inline constexpr int     kDefaultPort         = 9000;
inline constexpr int     kMdnsPort            = 5353;
inline constexpr int     kMdnsTtlSeconds      = 255;
inline constexpr char    kServiceType[]       = "_bettersend._tcp.local.";
inline constexpr char    kMdnsMulticastAddr[] = "224.0.0.251";

// ── BLE (Phase 1 cross-platform discovery — no shared network needed) ─────────
// GATT service UUID for the post-discovery handshake phase (read SSID + PSK
// from peer once a hotspot needs bringing up). Not used during advertising:
// a 128-bit UUID alone consumes 18 of the 31 legacy-advertisement bytes,
// leaving no room for a name. v4 random, custom range. Same on every platform.
inline constexpr char    kBleServiceUuid[]    = "5BE77ECD-A1B2-4F1A-8C5D-7F8E9D2C4A6B";

// GATT characteristic UUID inside the BetterSend service. Read-only,
// returns UTF-8 JSON: {"ssid":"...","psk":"...","hostIp":"...","port":N}.
// Used by IPeerHandshake to deliver hotspot credentials from the host
// (Windows in Phase 1) to the client (Mac) over BLE after discovery.
inline constexpr char    kBleHandshakeCharUuid[] = "5BE77ECD-A1B2-4F1A-8C5D-7F8E9D2C4A6C";

// Maximum payload bytes for the handshake characteristic. The hotspot
// credentials JSON fits comfortably inside one GATT MTU (default 23,
// negotiable up to ~512). 256 is a safe ceiling.
inline constexpr int     kBleHandshakeMaxBytes = 256;

// Advertising marker. Bluetooth SIG company ID 0xFFFF is reserved for
// development/internal use. We tag every BetterSend advertisement with this
// company ID plus a 4-byte magic prefix, then pack the device name behind it:
//
//   ManufacturerData = [companyId=0xFFFF][magic=BE 77 EC D0][deviceName UTF-8]
//
// Total advertisement payload (AD Flags 3 + ManufacturerData 1+1+2+4+N = 8+N)
// = 11+N bytes. A 20-char device name still fits comfortably under 31.
inline constexpr uint16_t kBleCompanyId       = 0xFFFF;
inline constexpr uint8_t  kBleMagicBytes[]    = {0xBE, 0x77, 0xEC, 0xD0};
inline constexpr int      kBleMaxNameLen      = 20;

// ── Protocol ──────────────────────────────────────────────────────────────────
inline constexpr uint32_t kHeaderLengthBytes  = 4;     // size prefix (big-endian)
inline constexpr uint32_t kMaxHeaderSizeBytes = 4096;  // guard against malformed headers
inline constexpr uint32_t kReceiveBufferSize  = 65536; // 64 KB read chunks

// ── Device ────────────────────────────────────────────────────────────────────
inline constexpr int kMaxDeviceNameLen = 64;

// ── Presence ──────────────────────────────────────────────────────────────────
// BLE advertisements arrive 10–30 times per second per peer. To keep the FFI
// callback rate low we emit at most once per heartbeat interval. The Flutter
// side uses the same heartbeat as a "last seen" tick: peers that go silent
// for more than `kPeerStaleSec` seconds are pruned from the visible list,
// which avoids stale cached entries from previous sessions.
inline constexpr int kPeerHeartbeatSec = 2;
inline constexpr int kPeerStaleSec     = 10;

// Minimum cooldown between full handshake attempts for the same peer.
// BLE discovery fires every couple seconds (kPeerHeartbeatSec); without a
// per-peer cooldown a failed Wi-Fi join queues another join request before
// the previous CoreWLAN scan finishes, causing back-to-back "Resource busy"
// errors. After this many seconds we let the next attempt through.
inline constexpr int kPeerRetrySec     = 30;

// ── Logger ────────────────────────────────────────────────────────────────────
inline constexpr char kDefaultLogFile[] = "bettersend_debug.log";

} // namespace BetterSend
