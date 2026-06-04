// ── BleHandshake_Windows.cpp ─────────────────────────────────────────────────
// Windows implementation of BetterSend::IPeerHandshake — host role only.
// Built only when the host platform is Windows (CMakeLists.txt guard).
//
// Spins up a WinRT GATT service exposing a single read-only characteristic
// (kBleHandshakeCharUuid) under our service (kBleServiceUuid). When a peer
// reads the characteristic we hand back the latest payload bytes set via
// publishPayload(). Phase 1: the host (Windows) calls publishPayload with
// the {ssid,psk,hostIp,port} JSON the Mac side will read and parse.
//
// Client role isn't supported here — fetchPayload throws. Windows is the
// host in Phase 1 (see CLAUDE.md tie-break).
//
// On StartAdvertising the GattServiceProvider broadcasts the BetterSend
// service UUID in addition to the existing BluetoothLEAdvertisementPublisher
// (which carries ManufacturerData). Both run concurrently — Windows BLE
// time-multiplexes them and Mac's scan dedupes by peripheral identity.

#include "IPeerHandshake.h"
#include "Constants.h"
#include "Logger.h"

#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Devices.Bluetooth.GenericAttributeProfile.h>
#include <winrt/Windows.Storage.Streams.h>

#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

namespace BetterSend {

namespace {

constexpr const char* kComponent = "BleHand";

namespace winrt_bt    = winrt::Windows::Devices::Bluetooth;
namespace winrt_gatt  = winrt::Windows::Devices::Bluetooth::GenericAttributeProfile;
namespace winrt_strm  = winrt::Windows::Storage::Streams;

std::string hstringToStdString(const winrt::hstring& h) {
	return winrt::to_string(h);
}

} // namespace

class BleHandshakeWindows : public IPeerHandshake {
public:
	BleHandshakeWindows() {
		try {
			winrt::init_apartment(winrt::apartment_type::multi_threaded);
		} catch (const winrt::hresult_error& e) {
			BS_LOG_DEBUG(kComponent, "init_apartment skipped: {:#x}",
				static_cast<uint32_t>(e.code().value));
		}
	}

	~BleHandshakeWindows() override { stop(); }

	void publishPayload(const std::string& payload) override {
		BS_LOG_INFO(kComponent, "publishPayload: {} bytes", payload.size());
		{
			std::lock_guard lock(payloadMu_);
			payload_.assign(payload.begin(), payload.end());
		}
		if (!provider_) {
			ensureProvider();
		}
	}

	[[nodiscard]] std::string fetchPayload(const std::string& /*peerId*/,
	                                       int /*timeoutSeconds*/) override {
		throw std::runtime_error(
			"BleHandshake_Windows: client role not supported. "
			"Windows publishes; the peer reads via its own handshake impl.");
	}

	void stop() override {
		if (provider_ && advertising_) {
			try {
				provider_.StopAdvertising();
				BS_LOG_INFO(kComponent, "GATT advertising stopped");
			} catch (const winrt::hresult_error& e) {
				BS_LOG_WARN(kComponent, "StopAdvertising failed: {:#x} — {}",
					static_cast<uint32_t>(e.code().value),
					hstringToStdString(e.message()));
			}
		}
		advertising_ = false;
		provider_    = nullptr;
		characteristic_ = nullptr;
	}

private:
	void ensureProvider() {
		const winrt::guid serviceGuid(kBleServiceUuid);
		const winrt::guid charGuid(kBleHandshakeCharUuid);

		auto svcResult = winrt_gatt::GattServiceProvider::CreateAsync(serviceGuid).get();
		if (svcResult.Error() != winrt_bt::BluetoothError::Success) {
			BS_LOG_ERROR(kComponent, "GattServiceProvider::CreateAsync error={}",
				static_cast<int>(svcResult.Error()));
			throw std::runtime_error("GattServiceProvider::CreateAsync failed");
		}
		provider_ = svcResult.ServiceProvider();

		winrt_gatt::GattLocalCharacteristicParameters charParams;
		charParams.CharacteristicProperties(winrt_gatt::GattCharacteristicProperties::Read);
		charParams.ReadProtectionLevel(winrt_gatt::GattProtectionLevel::Plain);

		auto charResult = provider_.Service()
			.CreateCharacteristicAsync(charGuid, charParams).get();
		if (charResult.Error() != winrt_bt::BluetoothError::Success) {
			BS_LOG_ERROR(kComponent, "CreateCharacteristicAsync error={}",
				static_cast<int>(charResult.Error()));
			throw std::runtime_error("CreateCharacteristicAsync failed");
		}
		characteristic_ = charResult.Characteristic();

		characteristic_.ReadRequested(
			[this](winrt_gatt::GattLocalCharacteristic const&,
			       winrt_gatt::GattReadRequestedEventArgs const& args) {
				auto deferral = args.GetDeferral();
				try {
					auto req = args.GetRequestAsync().get();
					if (req) {
						std::vector<uint8_t> snap;
						{
							std::lock_guard lock(payloadMu_);
							snap = payload_;
						}
						winrt_strm::DataWriter writer;
						if (!snap.empty()) {
							writer.WriteBytes(winrt::array_view<const uint8_t>(snap));
						}
						req.RespondWithValue(writer.DetachBuffer());
						BS_LOG_INFO(kComponent, "Served {} bytes to peer", snap.size());
					}
				} catch (const winrt::hresult_error& e) {
					BS_LOG_ERROR(kComponent, "ReadRequested failed: {:#x} — {}",
						static_cast<uint32_t>(e.code().value),
						hstringToStdString(e.message()));
				}
				deferral.Complete();
			});

		winrt_gatt::GattServiceProviderAdvertisingParameters advParams;
		advParams.IsConnectable(true);
		advParams.IsDiscoverable(true);
		provider_.StartAdvertising(advParams);
		advertising_ = true;
		BS_LOG_INFO(kComponent, "GATT service advertising");
	}

	winrt_gatt::GattServiceProvider      provider_{nullptr};
	winrt_gatt::GattLocalCharacteristic  characteristic_{nullptr};
	std::mutex                           payloadMu_;
	std::vector<uint8_t>                 payload_;
	bool                                 advertising_{false};
};

std::unique_ptr<IPeerHandshake> makePeerHandshake() {
	return std::make_unique<BleHandshakeWindows>();
}

} // namespace BetterSend
