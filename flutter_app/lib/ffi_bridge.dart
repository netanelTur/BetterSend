import 'dart:ffi';
import 'dart:io';
import 'package:ffi/ffi.dart';

// ── ffi_bridge.dart ───────────────────────────────────────────────────────────
// Flutter ↔ bettersend_core (.dylib/.so/.dll) via dart:ffi.
// No mock mode — real library required.
// Build first: cmake -B build && cmake --build build (from project root)

// ── 1. Load library ───────────────────────────────────────────────────────────

DynamicLibrary _loadLibrary() {
	if (Platform.isAndroid) return DynamicLibrary.open('libbettersend_core.so');
	if (Platform.isIOS)     return DynamicLibrary.process();
	if (Platform.isMacOS)   return DynamicLibrary.open('libbettersend_core.dylib');
	if (Platform.isWindows) return DynamicLibrary.open('bettersend_core.dll');
	throw UnsupportedError('Platform not supported');
}

final DynamicLibrary _lib = _loadLibrary();

// ── 2. Native typedefs ────────────────────────────────────────────────────────

typedef _NativeEcho = Pointer<Utf8> Function(Pointer<Utf8>);
typedef _DartEcho   = Pointer<Utf8> Function(Pointer<Utf8>);

typedef _NativeCreate   = Pointer<Void> Function(Pointer<Utf8>);
typedef _DartCreate     = Pointer<Void> Function(Pointer<Utf8>);

typedef _NativeDestroy  = Void Function(Pointer<Void>);
typedef _DartDestroy    = void Function(Pointer<Void>);

typedef _NativeStartAdv = Void Function(Pointer<Void>, Int32);
typedef _DartStartAdv   = void Function(Pointer<Void>, int);

// C callback: void(*)(const char* name, const char* ip, int port)
typedef NativeDeviceFoundCb = Void Function(Pointer<Utf8>, Pointer<Utf8>, Int32);

typedef _NativeStartDisc = Void Function(
	Pointer<Void>, Pointer<NativeFunction<NativeDeviceFoundCb>>);
typedef _DartStartDisc   = void Function(
	Pointer<Void>, Pointer<NativeFunction<NativeDeviceFoundCb>>);

// ── 3. Lookup functions ───────────────────────────────────────────────────────

final _echo             = _lib.lookupFunction<_NativeEcho,     _DartEcho>    ('bettersend_echo');
final _create           = _lib.lookupFunction<_NativeCreate,   _DartCreate>  ('bettersend_create');
final _destroy          = _lib.lookupFunction<_NativeDestroy,  _DartDestroy> ('bettersend_destroy');
final _startAdvertising = _lib.lookupFunction<_NativeStartAdv, _DartStartAdv>('bettersend_start_advertising');
final _startDiscovery   = _lib.lookupFunction<_NativeStartDisc,_DartStartDisc>('bettersend_start_discovery');

// ── 4. BetterSendBridge ───────────────────────────────────────────────────────

class BetterSendBridge {
	late final Pointer<Void> _handle;
	NativeCallable<NativeDeviceFoundCb>? _discoveryCb;

	BetterSendBridge(String deviceName) {
		final namePtr = deviceName.toNativeUtf8();
		_handle = _create(namePtr);
		malloc.free(namePtr);
	}

	/// Round-trip echo — verifies Dart ↔ C++ FFI pipeline.
	String echo(String text) {
		final ptr = _echo(text.toNativeUtf8());
		return ptr.toDartString();
	}

	/// Advertise this device on the local network via mDNS.
	void startAdvertising(int port) {
		_startAdvertising(_handle, port);
	}

	/// Scan for nearby BetterSend devices; [onFound] called for each.
	void startDiscovery({required void Function(DiscoveredDevice) onFound}) {
		// NativeCallable.listener posts callback to the Dart isolate — safe from C threads
		_discoveryCb = NativeCallable<NativeDeviceFoundCb>.listener(
			(namePtr, ipPtr, port) {
				onFound(DiscoveredDevice(
					name: namePtr.toDartString(),
					ip:   ipPtr.toDartString(),
					port: port,
				));
			},
		);
		_startDiscovery(_handle, _discoveryCb!.nativeFunction);
	}

	void startServer(int port, {required void Function(ReceivedTransfer) onReceive}) {
		// TODO: NativeCallable for TransferReceivedCallback
	}

	void sendFile(DiscoveredDevice device, String filePath) {
		// TODO
	}

	void sendClipboard(DiscoveredDevice device, String text) {
		// TODO
	}

	void dispose() {
		_discoveryCb?.close();
		_destroy(_handle);
	}
}

// ── Data classes ──────────────────────────────────────────────────────────────

class DiscoveredDevice {
	final String name;
	final String ip;
	final int    port;
	const DiscoveredDevice({required this.name, required this.ip, required this.port});
}

class ReceivedTransfer {
	final TransferType type;
	final String senderName;
	final String name;
	final String data;
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
