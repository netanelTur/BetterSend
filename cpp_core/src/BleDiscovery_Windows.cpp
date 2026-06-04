// ── BleDiscovery_Windows.cpp ─────────────────────────────────────────────────
// Windows implementation of BetterSend::IDiscovery on top of BLE.
// Built only when the host platform is Windows (see CMakeLists.txt guard).
//
// Stack: C++/WinRT projection of WinRT Bluetooth APIs.
//   - BluetoothLEAdvertisementPublisher  → broadcast our service UUID + name
//   - BluetoothLEAdvertisementWatcher    → scan for peers carrying that UUID
//
// Both run concurrently on the WinRT thread pool; the Received event is
// dispatched on a worker thread, matching IDiscovery's "callbacks may come
// from a background thread" contract.

#include "BleDiscovery.h"
#include "Constants.h"
#include "Logger.h"

#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Devices.Bluetooth.Advertisement.h>
#include <winrt/Windows.Storage.Streams.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <string>
#include <cstdio>

namespace BetterSend {

namespace winrt_btle  = winrt::Windows::Devices::Bluetooth::Advertisement;
namespace winrt_strm  = winrt::Windows::Storage::Streams;
using winrt::Windows::Foundation::IInspectable;
using winrt::Windows::Foundation::TypedEventHandler;

constexpr const char* kComponent = "BleDisc";

namespace {

// Format a 48-bit Bluetooth address as "AA:BB:CC:DD:EE:FF".
std::string formatBdAddr(uint64_t addr) {
	char buf[18];
	std::snprintf(buf, sizeof(buf), "%02llX:%02llX:%02llX:%02llX:%02llX:%02llX",
		(addr >> 40) & 0xFF, (addr >> 32) & 0xFF, (addr >> 24) & 0xFF,
		(addr >> 16) & 0xFF, (addr >>  8) & 0xFF,  addr        & 0xFF);
	return std::string(buf);
}

std::string hstringToStdString(const winrt::hstring& h) {
	return winrt::to_string(h);
}

std::string toLowerAscii(const std::string& s) {
	std::string out;
	out.reserve(s.size());
	for (unsigned char c : s) out.push_back(static_cast<char>(std::tolower(c)));
	return out;
}

// Build the ManufacturerData payload: [magic][name UTF-8], trimmed to fit.
winrt_strm::IBuffer makeBetterSendPayload(const std::string& deviceName) {
	winrt_strm::DataWriter writer;
	for (uint8_t b : kBleMagicBytes) writer.WriteByte(b);
	int n = static_cast<int>(deviceName.size());
	if (n > kBleMaxNameLen) n = kBleMaxNameLen;
	for (int i = 0; i < n; ++i) writer.WriteByte(static_cast<uint8_t>(deviceName[i]));
	return writer.DetachBuffer();
}

// Extract the device name from a peer's ManufacturerData section, verifying
// the magic prefix. Returns empty string if the entry isn't ours.
std::string extractPeerName(const winrt_btle::BluetoothLEManufacturerData& md) {
	if (md.CompanyId() != kBleCompanyId) return {};
	auto buf = md.Data();
	if (!buf || buf.Length() < sizeof(kBleMagicBytes)) return {};

	winrt_strm::DataReader reader = winrt_strm::DataReader::FromBuffer(buf);
	for (uint8_t expected : kBleMagicBytes) {
		if (reader.ReadByte() != expected) return {};
	}
	uint32_t remaining = buf.Length() - sizeof(kBleMagicBytes);
	std::string name;
	name.reserve(remaining);
	for (uint32_t i = 0; i < remaining; ++i) name.push_back(static_cast<char>(reader.ReadByte()));
	return name;
}

} // namespace

class BleDiscoveryWindows : public IDiscovery {
public:
	BleDiscoveryWindows() {
		try {
			winrt::init_apartment(winrt::apartment_type::multi_threaded);
		} catch (const winrt::hresult_error& e) {
			// Apartment may already be initialized by another component in the
			// same DLL — that's fine, just log and continue.
			BS_LOG_DEBUG(kComponent, "init_apartment skipped: {:#x}",
				static_cast<uint32_t>(e.code().value));
		}
	}

	~BleDiscoveryWindows() override {
		stop();
	}

	void startAdvertising(const std::string& deviceName, int /*port*/) override {
		BS_LOG_INFO(kComponent, "BLE advertise: name='{}' companyId={:#06x}",
			deviceName, kBleCompanyId);

		publisher_ = winrt_btle::BluetoothLEAdvertisementPublisher();
		auto adv = publisher_.Advertisement();

		// Pack [magic][name] into ManufacturerData. Keeps us inside the 31-byte
		// legacy advertisement budget even with the auto-added AD Flags structure.
		winrt_btle::BluetoothLEManufacturerData mfg;
		mfg.CompanyId(kBleCompanyId);
		mfg.Data(makeBetterSendPayload(deviceName));
		adv.ManufacturerData().Append(mfg);

		publisher_.StatusChanged([this](auto&&, auto const& args) {
			auto status = args.Status();
			BS_LOG_INFO(kComponent, "Advertise status: {}", static_cast<int>(status));
		});

		try {
			publisher_.Start();
			advertising_.store(true);
		} catch (const winrt::hresult_error& e) {
			BS_LOG_ERROR(kComponent, "Advertise Start failed: {:#x} — {}",
				static_cast<uint32_t>(e.code().value),
				hstringToStdString(e.message()));
		}
	}

	void startDiscovery(std::function<void(Device)> onFound) override {
		BS_LOG_INFO(kComponent, "BLE discovery: companyId={:#06x}", kBleCompanyId);
		onFound_ = std::move(onFound);

		watcher_ = winrt_btle::BluetoothLEAdvertisementWatcher();
		watcher_.ScanningMode(winrt_btle::BluetoothLEScanningMode::Active);
		watcher_.AllowExtendedAdvertisements(true);

		// No hardware filter while we debug — every advertisement reaches the
		// callback, and extractPeerName() decides whether it's one of ours.
		// Cheap because BLE adverts in a room are dozens per second, not
		// thousands, and we drop non-BetterSend ones almost immediately.

		watcher_.Received([this](auto&&, auto const& args) {
			handleReceived(args);
		});

		watcher_.Stopped([](auto&&, auto const& args) {
			BS_LOG_INFO(kComponent, "Watcher stopped: error={}",
				static_cast<int>(args.Error()));
		});

		try {
			watcher_.Start();
			scanning_.store(true);
		} catch (const winrt::hresult_error& e) {
			BS_LOG_ERROR(kComponent, "Watcher Start failed: {:#x} — {}",
				static_cast<uint32_t>(e.code().value),
				hstringToStdString(e.message()));
		}
	}

	void stop() override {
		if (advertising_.exchange(false)) {
			try { publisher_.Stop(); } catch (...) {}
			BS_LOG_DEBUG(kComponent, "Advertise stopped");
		}
		if (scanning_.exchange(false)) {
			try { watcher_.Stop(); } catch (...) {}
			BS_LOG_DEBUG(kComponent, "Scan stopped");
		}
	}

private:
	void handleReceived(const winrt_btle::BluetoothLEAdvertisementReceivedEventArgs& args) {
		uint64_t addr = args.BluetoothAddress();
		auto     adv  = args.Advertisement();

		// Path 1 — Windows peer: ManufacturerData (0xFFFF + magic + name)
		// arrives whole in a single packet. Surface immediately.
		for (auto const& md : adv.ManufacturerData()) {
			std::string n = extractPeerName(md);
			if (!n.empty()) { surfaceIfNew(n, addr); return; }
		}

		// Path 2 — Mac peer. macOS sends the ServiceUuid in the primary
		// advertisement and (when room allows) the LocalName in the scan
		// response — two separate events sharing one BluetoothAddress.
		// Cache LocalName per-address so when a ServiceUuid match arrives
		// later we can pull the right name.
		std::string localName = hstringToStdString(adv.LocalName());
		if (!localName.empty()) {
			std::lock_guard lock(nameCacheMu_);
			nameCache_[addr] = localName;
		}

		static const winrt::guid kServiceGuid(kBleServiceUuid);
		bool hasService = false;
		for (auto const& uuid : adv.ServiceUuids()) {
			if (uuid == kServiceGuid) { hasService = true; break; }
		}
		if (!hasService) return; // unrelated, or scan-response-only packet

		std::string name = localName;
		if (name.empty()) {
			std::lock_guard lock(nameCacheMu_);
			auto it = nameCache_.find(addr);
			if (it != nameCache_.end()) name = it->second;
		}
		if (!name.empty()) {
			surfaceIfNew(name, addr);
			return;
		}

		// LocalName never made it to us (Apple drops it from scan response
		// in some configurations). Fall back to querying Windows' Bluetooth
		// system cache via BluetoothLEDevice — it often carries a friendly
		// device name from past pairings or system scans. Async, fire-and-
		// forget; surface once it resolves. Dedupe key handles re-fires.
		{
			std::lock_guard lock(lookupMu_);
			if (!lookupsInFlight_.insert(addr).second) return; // one in flight
		}
		auto op = winrt::Windows::Devices::Bluetooth::BluetoothLEDevice::FromBluetoothAddressAsync(addr);
		op.Completed([this, addr](auto&& sender, winrt::Windows::Foundation::AsyncStatus status) {
			std::string resolved;
			if (status == winrt::Windows::Foundation::AsyncStatus::Completed) {
				try {
					auto device = sender.GetResults();
					if (device != nullptr) resolved = hstringToStdString(device.Name());
				} catch (...) { /* swallow; we'll fall through to MAC fallback */ }
			}
			// Windows hands back "Bluetooth XX:XX:XX:XX:XX:XX" when it has no
			// friendly name cached — useless to the user. Treat as empty.
			if (resolved.rfind("Bluetooth ", 0) == 0) resolved.clear();
			{
				std::lock_guard lock(lookupMu_);
				lookupsInFlight_.erase(addr);
			}
			// Skip emit when we still have no real name. A synthetic
			// "Mac-XX:XX:..." placeholder confused users (it surfaced even
			// for peers that did advertise a LocalName, just on a later
			// advert). Wait for the real name instead.
			if (!resolved.empty()) {
				surfaceIfNew(resolved, addr);
			}
		});
	}

	void surfaceIfNew(const std::string& name, uint64_t addr) {
		// Throttle + canonical-name dedupe. The Apple peer can advertise
		// the same human name under multiple advertisements (Service UUID
		// primary + LocalName scan response, plus the Bluetooth system name
		// in mixed case). Lowercase key collapses every case variant into a
		// single peer entry; the first casing seen wins as the display name.
		const std::string key = toLowerAscii(name);
		const auto now = std::chrono::steady_clock::now();
		std::string canonical;
		{
			std::lock_guard lock(seenMu_);
			auto& entry = throttle_[key];
			if (entry.canonical.empty()) entry.canonical = name;
			if (entry.lastEmit.time_since_epoch().count() != 0 &&
			    now - entry.lastEmit < std::chrono::seconds(kPeerHeartbeatSec)) {
				return;
			}
			entry.lastEmit = now;
			canonical = entry.canonical;
		}
		std::string addrStr = formatBdAddr(addr);
		BS_LOG_INFO(kComponent, "Found peer: name='{}' addr={}", canonical, addrStr);
		if (onFound_) {
			// Phase 1 design note: ip carries the BLE address until the
			// connection broker (WindowsHotspotBroker / MacWifiClientBroker)
			// negotiates a real IP-level transport. port stays kDefaultPort.
			onFound_(Device{canonical, std::string("ble:") + addrStr, kDefaultPort});
		}
	}

	struct ThrottleEntry {
		std::chrono::steady_clock::time_point lastEmit;
		std::string                           canonical;
	};

	winrt_btle::BluetoothLEAdvertisementPublisher  publisher_{nullptr};
	winrt_btle::BluetoothLEAdvertisementWatcher    watcher_{nullptr};
	std::function<void(Device)>                    onFound_;
	std::atomic<bool>                              advertising_{false};
	std::atomic<bool>                              scanning_{false};
	std::mutex                                     seenMu_;
	std::unordered_map<std::string, ThrottleEntry> throttle_;

	std::mutex                                    nameCacheMu_;
	std::unordered_map<uint64_t, std::string>     nameCache_;

	std::mutex                                    lookupMu_;
	std::unordered_set<uint64_t>                  lookupsInFlight_;
};

// ── Factory ───────────────────────────────────────────────────────────────────

std::unique_ptr<IDiscovery> makeBleDiscovery() {
	return std::make_unique<BleDiscoveryWindows>();
}

// Cross-platform makeDiscovery() lives in one .cpp per platform. On Windows
// the production discovery is BLE; MdnsDiscovery.cpp is excluded from the
// Windows build (see CMakeLists.txt).
std::unique_ptr<IDiscovery> makeDiscovery() {
	return makeBleDiscovery();
}

} // namespace BetterSend
