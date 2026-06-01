import 'dart:ffi';
import 'dart:io';
import 'package:ffi/ffi.dart';

// ── ffi_bridge.dart ───────────────────────────────────────────────────────────
// Flutter ↔ bettersend_core (.so / .dylib / .dll) via dart:ffi.
//
// Mock mode: when the native library is not yet built, _mockMode = true
// and all calls fall back to in-process stubs so the UI runs for development.
//
// Docs: https://dart.dev/guides/libraries/c-interop

// ── 1. Load library ───────────────────────────────────────────────────────────

bool _mockMode = false;

DynamicLibrary? _tryLoadLibrary() {
	try {
		if (Platform.isAndroid) return DynamicLibrary.open('libbettersend_core.so');
		if (Platform.isIOS)     return DynamicLibrary.process();
		if (Platform.isMacOS)   return DynamicLibrary.open('libbettersend_core.dylib');
		if (Platform.isWindows) return DynamicLibrary.open('bettersend_core.dll');
	} catch (_) {}
	return null;
}

final DynamicLibrary? _lib = () {
	final lib = _tryLoadLibrary();
	if (lib == null) _mockMode = true;
	return lib;
}();

// ── 2. Native typedefs ────────────────────────────────────────────────────────

// bettersend_echo(const char* text) → const char*
typedef _NativeEcho = Pointer<Utf8> Function(Pointer<Utf8> text);
typedef _DartEcho   = Pointer<Utf8> Function(Pointer<Utf8> text);

// Called when a new peer is found via mDNS.
// typedef NativeDeviceFoundCallback = Void Function(
//     Pointer<Utf8> name, Pointer<Utf8> ip, Int32 port);
// typedef DartDeviceFoundCallback = void Function(
//     Pointer<Utf8> name, Pointer<Utf8> ip, int port);

// Called when an incoming transfer completes.
// typedef NativeTransferReceivedCallback = Void Function(
//     Int32 type, Pointer<Utf8> senderName, Pointer<Utf8> name,
//     Pointer<Utf8> data, Int64 sizeBytes);

// ── 3. Lookup functions ───────────────────────────────────────────────────────

final _echo = _lib?.lookupFunction<_NativeEcho, _DartEcho>('bettersend_echo');

// TODO: lookup remaining API functions:
//   final _create    = _lib?.lookupFunction<...>('bettersend_create');
//   final _destroy   = _lib?.lookupFunction<...>('bettersend_destroy');
//   final _startServer      = _lib?.lookupFunction<...>('bettersend_start_server');
//   final _startAdvertising = _lib?.lookupFunction<...>('bettersend_start_advertising');
//   final _startDiscovery   = _lib?.lookupFunction<...>('bettersend_start_discovery');
//   final _sendFile         = _lib?.lookupFunction<...>('bettersend_send_file');
//   final _sendClipboard    = _lib?.lookupFunction<...>('bettersend_send_clipboard');

// ── 4. BetterSendBridge ───────────────────────────────────────────────────────

/// Wrapper that hides FFI details from UI code.
///
/// Usage:
///   final bridge = BetterSendBridge('iPhone-Yoni');
///   bridge.startServer(9000, onReceive: (transfer) { setState(...) });
///   bridge.startDiscovery(onFound: (device) { setState(...) });
///   bridge.sendFile(device, '/path/to/photo.jpg');
///   bridge.dispose();
class BetterSendBridge {
	// TODO: Pointer<Void> _handle — add after bettersend_create is looked up

	BetterSendBridge(String deviceName) {
		// TODO: _handle = _create!(deviceName.toNativeUtf8())
	}

	/// Round-trip echo — verifies the Dart ↔ C++ FFI pipeline.
	/// In mock mode: returns '[mock] $text'.
	String echo(String text) {
		if (_mockMode || _echo == null) return '[mock] $text';
		final ptr    = _echo!(text.toNativeUtf8());
		final result = ptr.toDartString();
		// result points into a thread-local C++ buffer — copy before next call
		return result;
	}

	void startServer(int port, {required Function(ReceivedTransfer) onReceive}) {
		// TODO: NativeCallable for TransferReceivedCallback
		//       _startServer!(_handle, port, callback.nativeFunction)
	}

	void startAdvertising(int port) {
		// TODO: _startAdvertising!(_handle, port)
	}

	void startDiscovery({required Function(DiscoveredDevice) onFound}) {
		// TODO: NativeCallable for DeviceFoundCallback
		//       _startDiscovery!(_handle, callback.nativeFunction)
	}

	void sendFile(DiscoveredDevice device, String filePath) {
		// TODO: _sendFile!(_handle, device.ip.toNativeUtf8(), device.port, filePath.toNativeUtf8())
	}

	void sendClipboard(DiscoveredDevice device, String text) {
		// TODO: _sendClipboard!(_handle, device.ip.toNativeUtf8(), device.port, text.toNativeUtf8())
	}

	void dispose() {
		// TODO: _destroy!(_handle)
	}
}

// ── Data classes ──────────────────────────────────────────────────────────────

/// Peer device discovered via mDNS.
class DiscoveredDevice {
	final String name;
	final String ip;
	final int    port;
	const DiscoveredDevice({required this.name, required this.ip, required this.port});
}

/// Completed incoming transfer.
class ReceivedTransfer {
	final TransferType type;
	final String senderName;
	final String name;      // filename (empty for clipboard)
	final String data;      // file path or clipboard text
	final int    sizeBytes;
	const ReceivedTransfer({
		required this.type,
		required this.senderName,
		required this.name,
		required this.data,
		required this.sizeBytes,
	});
}

enum TransferType { file, clipboard }
