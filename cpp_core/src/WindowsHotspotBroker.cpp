// ── WindowsHotspotBroker.cpp ─────────────────────────────────────────────────
// Windows implementation of BetterSend::IConnectionBroker — host role only.
// Built only when the host platform is Windows (see CMakeLists.txt guard).
//
// Brings up Windows Mobile Hotspot via WinRT
// NetworkOperatorTetheringManager. The hotspot's gateway IP is the
// fixed 192.168.137.1 that Windows Internet Connection Sharing always
// assigns to the host adapter, so the client (Mac) can connect to it
// after joining the SSID. SSID + Passphrase are read from the
// current Mobile Hotspot configuration — we don't rewrite them
// because changing them requires admin privileges and Phase 1 just
// needs to share the existing values over BLE GATT.
//
// Known limitation (Phase 1): NetworkOperatorTetheringManager requires
// a non-null InternetConnectionProfile, even though the hotspot itself
// works without an upstream link. If the dev machine is fully offline
// (no Wi-Fi adapter, no LAN, no cellular) Start will fail. Phase 4
// will explore the lower-level Wlan Hosted Network APIs to bypass this.

#include "IConnectionBroker.h"
#include "Logger.h"

#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Networking.Connectivity.h>
#include <winrt/Windows.Networking.NetworkOperators.h>

#include <stdexcept>
#include <string>

namespace BetterSend {

namespace {

constexpr const char* kComponent = "HotspotBroker";

namespace winrt_conn = winrt::Windows::Networking::Connectivity;
namespace winrt_net  = winrt::Windows::Networking::NetworkOperators;

std::string hstringToStdString(const winrt::hstring& h) {
	return winrt::to_string(h);
}

} // namespace

class WindowsHotspotBroker : public IConnectionBroker {
public:
	WindowsHotspotBroker() {
		try {
			winrt::init_apartment(winrt::apartment_type::multi_threaded);
		} catch (const winrt::hresult_error& e) {
			BS_LOG_DEBUG(kComponent, "init_apartment skipped: {:#x}",
				static_cast<uint32_t>(e.code().value));
		}
	}

	~WindowsHotspotBroker() override { stop(); }

	[[nodiscard]] Credentials startHost() override {
		BS_LOG_INFO(kComponent, "startHost: bringing up Mobile Hotspot");

		auto profile = winrt_conn::NetworkInformation::GetInternetConnectionProfile();
		if (!profile) {
			throw std::runtime_error(
				"No InternetConnectionProfile — Windows Mobile Hotspot requires "
				"an upstream network. Connect any adapter and retry.");
		}

		manager_ = winrt_net::NetworkOperatorTetheringManager::CreateFromConnectionProfile(profile);
		auto config = manager_.GetCurrentAccessPointConfiguration();

		auto status = manager_.TetheringOperationalState();
		if (status != winrt_net::TetheringOperationalState::On) {
			auto result = manager_.StartTetheringAsync().get();
			auto rstat  = result.Status();
			if (rstat != winrt_net::TetheringOperationStatus::Success) {
				BS_LOG_ERROR(kComponent, "StartTetheringAsync status={}",
					static_cast<int>(rstat));
				throw std::runtime_error("StartTetheringAsync failed");
			}
			running_ = true;
		} else {
			BS_LOG_INFO(kComponent, "Hotspot already running, reusing config");
			running_ = true;
		}

		Credentials c{};
		c.ssid   = hstringToStdString(config.Ssid());
		c.psk    = hstringToStdString(config.Passphrase());
		// Windows ICS gateway is always 192.168.137.1 — the Mac will connect
		// here once it joins the SSID.
		c.hostIp = "192.168.137.1";

		BS_LOG_INFO(kComponent,
			"Hotspot up: ssid='{}' hostIp={} pskLen={}",
			c.ssid, c.hostIp, c.psk.size());
		return c;
	}

	bool joinNetwork(const std::string& /*ssid*/, const std::string& /*psk*/) override {
		throw std::runtime_error(
			"WindowsHotspotBroker: client role not supported. "
			"Windows side hosts; the peer joins via its own broker.");
	}

	void stop() override {
		if (!running_) return;
		running_ = false;
		BS_LOG_INFO(kComponent, "Stopping hotspot");
		try {
			manager_.StopTetheringAsync().get();
		} catch (const winrt::hresult_error& e) {
			BS_LOG_WARN(kComponent, "StopTetheringAsync failed: {:#x} — {}",
				static_cast<uint32_t>(e.code().value),
				hstringToStdString(e.message()));
		}
	}

private:
	winrt_net::NetworkOperatorTetheringManager manager_{nullptr};
	bool                                       running_{false};
};

std::unique_ptr<IConnectionBroker> makeConnectionBroker() {
	return std::make_unique<WindowsHotspotBroker>();
}

} // namespace BetterSend
