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

// C callback: void(*)(int type, const char* sender, const char* name,
//                     const char* data, long long sizeBytes)
typedef NativeTransferRecvCb = Void Function(
	Int32, Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>, Int64);

typedef _NativeStartServer = Void Function(
	Pointer<Void>, Int32, Pointer<NativeFunction<NativeTransferRecvCb>>);
typedef _DartStartServer   = void Function(
	Pointer<Void>, int, Pointer<NativeFunction<NativeTransferRecvCb>>);

typedef _NativeSendFile = Void Function(Pointer<Void>, Pointer<Utf8>, Pointer<Utf8>);
typedef _DartSendFile   = void Function(Pointer<Void>, Pointer<Utf8>, Pointer<Utf8>);

typedef _NativeSendClip = Void Function(Pointer<Void>, Pointer<Utf8>, Pointer<Utf8>);
typedef _DartSendClip   = void Function(Pointer<Void>, Pointer<Utf8>, Pointer<Utf8>);

typedef _NativeFreeCstr = Void Function(Pointer<Utf8>);
typedef _DartFreeCstr   = void Function(Pointer<Utf8>);

// ── 3. Lookup functions ───────────────────────────────────────────────────────

final _create           = _lib.lookupFunction<_NativeCreate,      _DartCreate>     ('bettersend_create');
final _destroy          = _lib.lookupFunction<_NativeDestroy,     _DartDestroy>    ('bettersend_destroy');
final _startAdvertising = _lib.lookupFunction<_NativeStartAdv,    _DartStartAdv>   ('bettersend_start_advertising');
final _startDiscovery   = _lib.lookupFunction<_NativeStartDisc,   _DartStartDisc>  ('bettersend_start_discovery');
final _startServerN     = _lib.lookupFunction<_NativeStartServer, _DartStartServer>('bettersend_start_server');
final _sendFile         = _lib.lookupFunction<_NativeSendFile,    _DartSendFile>   ('bettersend_send_file');
final _sendClipboard    = _lib.lookupFunction<_NativeSendClip,    _DartSendClip>   ('bettersend_send_clipboard');
final _freeCstr         = _lib.lookupFunction<_NativeFreeCstr,    _DartFreeCstr>   ('bettersend_free_cstr');

// ── 4. BetterSendBridge ───────────────────────────────────────────────────────

class BetterSendBridge {
	late final Pointer<Void> _handle;
	NativeCallable<NativeDeviceFoundCb>?   _discoveryCb;
	NativeCallable<NativeTransferRecvCb>?  _transferCb;

	BetterSendBridge(String deviceName) {
		final namePtr = deviceName.toNativeUtf8();
		_handle = _create(namePtr);
		malloc.free(namePtr);
	}

	/// Advertise this device via BLE so peers can find us.
	void startAdvertising(int port) {
		_startAdvertising(_handle, port);
	}

	/// Scan for nearby BetterSend devices; [onFound] called for each.
	void startDiscovery({required void Function(DiscoveredDevice) onFound}) {
		// NativeCallable.listener posts callback to the Dart isolate — safe from C threads.
		// Explicit Pointer<Utf8> annotation: with listener mode, runtime instances arrive
		// as Pointer<Never> and the typed toDartString extension would not resolve otherwise.
		// C++ heap-allocates name/ip; we own them and must call _freeCstr.
		_discoveryCb = NativeCallable<NativeDeviceFoundCb>.listener(
			(Pointer<Utf8> namePtr, Pointer<Utf8> ipPtr, int port) {
				final name = namePtr.cast<Utf8>().toDartString();
				final ip   = ipPtr.cast<Utf8>().toDartString();
				_freeCstr(namePtr.cast<Utf8>());
				_freeCstr(ipPtr.cast<Utf8>());
				onFound(DiscoveredDevice(name: name, ip: ip, port: port));
			},
		);
		_startDiscovery(_handle, _discoveryCb!.nativeFunction);
	}

	/// Start the TCP server listening on [port]; [onReceive] fires for each
	/// incoming file or clipboard. Hello control messages are filtered out
	/// in the C layer and never reach Dart.
	void startServer(int port, {required void Function(ReceivedTransfer) onReceive}) {
		_transferCb = NativeCallable<NativeTransferRecvCb>.listener(
			(int type, Pointer<Utf8> senderPtr, Pointer<Utf8> namePtr,
			 Pointer<Utf8> dataPtr, int sizeBytes) {
				final sender = senderPtr.cast<Utf8>().toDartString();
				final name   = namePtr.cast<Utf8>().toDartString();
				final data   = dataPtr.cast<Utf8>().toDartString();
				_freeCstr(senderPtr.cast<Utf8>());
				_freeCstr(namePtr.cast<Utf8>());
				_freeCstr(dataPtr.cast<Utf8>());
				onReceive(ReceivedTransfer(
					type:       type == 0 ? TransferType.file : TransferType.clipboard,
					senderName: sender,
					name:       name,
					data:       data,
					sizeBytes:  sizeBytes,
				));
			},
		);
		_startServerN(_handle, port, _transferCb!.nativeFunction);
	}

	/// Send the file at [filePath] to the device named [device]. The C
	/// layer resolves [device.name] to the peer's hotspot-subnet IP that
	/// was learned during BLE/hotspot pairing.
	void sendFile(DiscoveredDevice device, String filePath) {
		final namePtr = device.name.toNativeUtf8();
		final pathPtr = filePath.toNativeUtf8();
		try {
			_sendFile(_handle, namePtr, pathPtr);
		} finally {
			malloc.free(namePtr);
			malloc.free(pathPtr);
		}
	}

	/// Send a clipboard text to [device].
	void sendClipboard(DiscoveredDevice device, String text) {
		final namePtr = device.name.toNativeUtf8();
		final textPtr = text.toNativeUtf8();
		try {
			_sendClipboard(_handle, namePtr, textPtr);
		} finally {
			malloc.free(namePtr);
			malloc.free(textPtr);
		}
	}

	void dispose() {
		_discoveryCb?.close();
		_transferCb?.close();
		_destroy(_handle);
	}
}

// ── Data classes ──────────────────────────────────────────────────────────────

enum DeviceKind { desktop, mobile, unknown }

class DiscoveredDevice {
	final String name;
	final String ip;
	final int    port;
	const DiscoveredDevice({required this.name, required this.ip, required this.port});

	// Phase 1 has no on-the-wire device-type field yet (the advertisement
	// budget is tight enough as is). Until BleDiscovery encodes a type byte
	// we infer from the human name — Mac and Windows desktops fall through
	// to desktop; iPhone / iPad / Pixel / Galaxy etc. light up mobile.
	DeviceKind get kind {
		final n = name.toLowerCase();
		if (n.contains('iphone')   ||
		    n.contains('ipad')     ||
		    n.contains('android')  ||
		    n.contains('pixel')    ||
		    n.contains('galaxy')   ||
		    n.contains('xiaomi')   ||
		    n.contains('oneplus')) {
			return DeviceKind.mobile;
		}
		// Phase 1 peers are all Mac/Windows desktops, so default there.
		// "Mac-XX:XX:..." fallback names from BleDiscovery_Windows also hit this.
		return DeviceKind.desktop;
	}
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
