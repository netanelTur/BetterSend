import 'dart:async';
import 'dart:ffi';
import 'dart:io';
import 'dart:math';
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

typedef _NativeSendFile = Void Function(Pointer<Void>, Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>);
typedef _DartSendFile   = void Function(Pointer<Void>, Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>);

typedef _NativeSendClip = Void Function(Pointer<Void>, Pointer<Utf8>, Pointer<Utf8>);
typedef _DartSendClip   = void Function(Pointer<Void>, Pointer<Utf8>, Pointer<Utf8>);

typedef _NativeAcceptTransfer  = Void Function(Pointer<Void>, Pointer<Utf8>);
typedef _DartAcceptTransfer    = void Function(Pointer<Void>, Pointer<Utf8>);
typedef _NativeDeclineTransfer = Void Function(Pointer<Void>, Pointer<Utf8>);
typedef _DartDeclineTransfer   = void Function(Pointer<Void>, Pointer<Utf8>);

// C callback: void(*)(const char* transferId, const char* senderName,
//                     const char* filename, long long size)
typedef NativeTransferReqCb = Void Function(
	Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>, Int64);
typedef _NativeSetReqCb = Void Function(
	Pointer<Void>, Pointer<NativeFunction<NativeTransferReqCb>>);
typedef _DartSetReqCb   = void Function(
	Pointer<Void>, Pointer<NativeFunction<NativeTransferReqCb>>);

// C callback: void(*)(const char* transferId)
typedef NativeTransferDeclineCb = Void Function(Pointer<Utf8>);
typedef _NativeSetDeclineCb = Void Function(
	Pointer<Void>, Pointer<NativeFunction<NativeTransferDeclineCb>>);
typedef _DartSetDeclineCb   = void Function(
	Pointer<Void>, Pointer<NativeFunction<NativeTransferDeclineCb>>);

// C callback: void(*)(const char* peerName, int status)
//   status: 0=connecting, 1=joined, 2=ready (reachable), 3=failed
typedef NativeConnectStatusCb = Void Function(Pointer<Utf8>, Int32);
typedef _NativeConnectPeer = Void Function(
	Pointer<Void>, Pointer<Utf8>, Pointer<NativeFunction<NativeConnectStatusCb>>);
typedef _DartConnectPeer   = void Function(
	Pointer<Void>, Pointer<Utf8>, Pointer<NativeFunction<NativeConnectStatusCb>>);

// C callback: void(*)(int dir, const char* peer, const char* name, long long done, long long total)
//   dir: 0 = sending, 1 = receiving
typedef NativeProgressCb = Void Function(
	Int32, Pointer<Utf8>, Pointer<Utf8>, Int64, Int64);
typedef _NativeSetProgressCb = Void Function(
	Pointer<Void>, Pointer<NativeFunction<NativeProgressCb>>);
typedef _DartSetProgressCb   = void Function(
	Pointer<Void>, Pointer<NativeFunction<NativeProgressCb>>);

typedef _NativeSetSaveDir = Void Function(Pointer<Void>, Pointer<Utf8>);
typedef _DartSetSaveDir   = void Function(Pointer<Void>, Pointer<Utf8>);

typedef _NativeSetName = Void Function(Pointer<Void>, Pointer<Utf8>);
typedef _DartSetName   = void Function(Pointer<Void>, Pointer<Utf8>);

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
final _acceptTransfer   = _lib.lookupFunction<_NativeAcceptTransfer,  _DartAcceptTransfer>  ('bettersend_accept_transfer');
final _declineTransfer  = _lib.lookupFunction<_NativeDeclineTransfer, _DartDeclineTransfer> ('bettersend_decline_transfer');
final _setReqCb         = _lib.lookupFunction<_NativeSetReqCb,        _DartSetReqCb>        ('bettersend_set_request_callback');
final _setDeclineCb     = _lib.lookupFunction<_NativeSetDeclineCb,    _DartSetDeclineCb>    ('bettersend_set_decline_callback');
final _setSaveDir       = _lib.lookupFunction<_NativeSetSaveDir,      _DartSetSaveDir>      ('bettersend_set_save_dir');
final _setDeviceName    = _lib.lookupFunction<_NativeSetName,        _DartSetName>         ('bettersend_set_device_name');
final _connectPeer      = _lib.lookupFunction<_NativeConnectPeer,    _DartConnectPeer>     ('bettersend_connect_peer');
final _setProgressCb    = _lib.lookupFunction<_NativeSetProgressCb,  _DartSetProgressCb>   ('bettersend_set_progress_callback');
final _freeCstr         = _lib.lookupFunction<_NativeFreeCstr,        _DartFreeCstr>        ('bettersend_free_cstr');

// ── 4. BetterSendBridge ───────────────────────────────────────────────────────

// Overall connect deadline for the UI spinner. Keep in lockstep with
// kConnectTimeoutSec in cpp_core/include/Constants.h.
const Duration _kConnectTimeout = Duration(seconds: 25);

class BetterSendBridge {
	late final Pointer<Void> _handle;
	NativeCallable<NativeDeviceFoundCb>?      _discoveryCb;
	NativeCallable<NativeTransferRecvCb>?     _transferCb;
	NativeCallable<NativeTransferReqCb>?      _requestCb;
	NativeCallable<NativeTransferDeclineCb>?  _declineCb;
	NativeCallable<NativeConnectStatusCb>?    _connectCb;
	NativeCallable<NativeProgressCb>?         _progressCb;
	// One completer per in-flight connect, keyed by peer name. The native
	// status callback (status 2/3) resolves it; a Dart-side timer is the
	// safety net so the UI spinner can never hang past _kConnectTimeout.
	final Map<String, Completer<bool>>        _connectCompleters = {};

	static final Random _rng = Random();

	BetterSendBridge(String deviceName) {
		final namePtr = deviceName.toNativeUtf8();
		_handle = _create(namePtr);
		malloc.free(namePtr);
	}

	String _newTransferId() {
		final ts  = DateTime.now().microsecondsSinceEpoch;
		final rnd = _rng.nextInt(1 << 32);
		return '$ts-${rnd.toRadixString(16)}';
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

	/// Connect to a specific discovered [device]: the native layer GATT-reads
	/// the peer's hotspot credentials and joins its Wi-Fi (Mac), or waits for
	/// the peer to become reachable (Windows host). Completes `true` once the
	/// peer is reachable, `false` on failure or after [_kConnectTimeout].
	///
	/// Tapping the same device twice while a connect is in flight returns the
	/// same future rather than starting a second join.
	Future<bool> connectToPeer(DiscoveredDevice device) {
		final existing = _connectCompleters[device.name];
		if (existing != null && !existing.isCompleted) return existing.future;

		// Lazily create the shared status listener. NativeCallable.listener
		// posts to the Dart isolate — safe from the C worker thread. The
		// peerName is heap-allocated in C; we own it and must _freeCstr it.
		_connectCb ??= NativeCallable<NativeConnectStatusCb>.listener(
			(Pointer<Utf8> namePtr, int status) {
				final name = namePtr.cast<Utf8>().toDartString();
				_freeCstr(namePtr.cast<Utf8>());
				// 0=connecting, 1=joined are progress-only. 2/3 are terminal.
				if (status == 2 || status == 3) {
					final c = _connectCompleters.remove(name);
					if (c != null && !c.isCompleted) c.complete(status == 2);
				}
			},
		);

		final completer = Completer<bool>();
		_connectCompleters[device.name] = completer;

		// Safety net: the native Mac worker's worst case (GATT 20s + join 24s +
		// Hello 10s) can exceed the deadline, so cap the spinner here.
		final timer = Timer(_kConnectTimeout, () {
			if (!completer.isCompleted) {
				_connectCompleters.remove(device.name);
				completer.complete(false);
			}
		});
		completer.future.whenComplete(timer.cancel);

		final namePtr = device.name.toNativeUtf8();
		try {
			_connectPeer(_handle, namePtr, _connectCb!.nativeFunction);
		} finally {
			malloc.free(namePtr);
		}
		return completer.future;
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

	/// Send a Request control to [device] for the file at [filePath]. The
	/// actual file bytes are only transmitted once the peer responds with
	/// Accept (handled by the native control plane). Returns the transferId
	/// the caller can use to correlate decline events.
	String sendFile(DiscoveredDevice device, String filePath) {
		final id = _newTransferId();
		final namePtr = device.name.toNativeUtf8();
		final pathPtr = filePath.toNativeUtf8();
		final idPtr   = id.toNativeUtf8();
		try {
			_sendFile(_handle, namePtr, pathPtr, idPtr);
		} finally {
			malloc.free(namePtr);
			malloc.free(pathPtr);
			malloc.free(idPtr);
		}
		return id;
	}

	/// Set the directory where incoming files are saved. Pass an empty string
	/// to fall back to the OS temp folder. Takes effect for the next transfer.
	void setSaveDir(String dir) {
		final dirPtr = dir.toNativeUtf8();
		try {
			_setSaveDir(_handle, dirPtr);
		} finally {
			malloc.free(dirPtr);
		}
	}

	/// Change the display name peers see. Updates both the BLE advert (live
	/// re-advertise) and the name stamped into outgoing TCP headers. The native
	/// side clamps to the 20-byte BLE budget on a UTF-8 boundary.
	void setDeviceName(String name) {
		final namePtr = name.toNativeUtf8();
		try {
			_setDeviceName(_handle, namePtr);
		} finally {
			malloc.free(namePtr);
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

	/// Accept an incoming transfer request previously surfaced by
	/// [onIncomingRequest]. Sends an Accept control back; the peer then
	/// pushes the actual file, which arrives via [startServer] onReceive.
	void acceptTransfer(String transferId) {
		final idPtr = transferId.toNativeUtf8();
		try {
			_acceptTransfer(_handle, idPtr);
		} finally {
			malloc.free(idPtr);
		}
	}

	/// Decline an incoming transfer request. The peer is notified and the
	/// sender side fires its onDeclined callback.
	void declineTransfer(String transferId) {
		final idPtr = transferId.toNativeUtf8();
		try {
			_declineTransfer(_handle, idPtr);
		} finally {
			malloc.free(idPtr);
		}
	}

	/// Register the incoming-request callback. Fires when a peer sends a
	/// Request control to this device.
	void onIncomingRequest(void Function(IncomingRequest) cb) {
		_requestCb?.close();
		_requestCb = NativeCallable<NativeTransferReqCb>.listener(
			(Pointer<Utf8> idPtr, Pointer<Utf8> senderPtr,
			 Pointer<Utf8> namePtr, int sizeBytes) {
				final id     = idPtr.cast<Utf8>().toDartString();
				final sender = senderPtr.cast<Utf8>().toDartString();
				final name   = namePtr.cast<Utf8>().toDartString();
				_freeCstr(idPtr.cast<Utf8>());
				_freeCstr(senderPtr.cast<Utf8>());
				_freeCstr(namePtr.cast<Utf8>());
				cb(IncomingRequest(
					transferId: id,
					senderName: sender,
					filename:   name,
					sizeBytes:  sizeBytes,
				));
			},
		);
		_setReqCb(_handle, _requestCb!.nativeFunction);
	}

	/// Register the outgoing-decline callback. Fires when a peer rejects a
	/// request this device sent.
	void onTransferDeclined(void Function(String transferId) cb) {
		_declineCb?.close();
		_declineCb = NativeCallable<NativeTransferDeclineCb>.listener(
			(Pointer<Utf8> idPtr) {
				final id = idPtr.cast<Utf8>().toDartString();
				_freeCstr(idPtr.cast<Utf8>());
				cb(id);
			},
		);
		_setDeclineCb(_handle, _declineCb!.nativeFunction);
	}

	/// Register a progress callback for in-flight file transfers (both
	/// directions). Fires repeatedly (throttled to integer-percent in native)
	/// until the transfer completes. The native layer heap-allocates the
	/// strings; we own them and must call _freeCstr.
	void onProgress(void Function(TransferProgress) cb) {
		_progressCb?.close();
		_progressCb = NativeCallable<NativeProgressCb>.listener(
			(int dir, Pointer<Utf8> peerPtr, Pointer<Utf8> namePtr,
			 int done, int total) {
				final peer = peerPtr.cast<Utf8>().toDartString();
				final name = namePtr.cast<Utf8>().toDartString();
				_freeCstr(peerPtr.cast<Utf8>());
				_freeCstr(namePtr.cast<Utf8>());
				cb(TransferProgress(
					sending:     dir == 0,
					peerName:    peer,
					fileName:    name,
					transferred: done,
					total:       total,
				));
			},
		);
		_setProgressCb(_handle, _progressCb!.nativeFunction);
	}

	void dispose() {
		_discoveryCb?.close();
		_transferCb?.close();
		_requestCb?.close();
		_declineCb?.close();
		_connectCb?.close();
		_progressCb?.close();
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

class IncomingRequest {
	final String transferId;
	final String senderName;
	final String filename;
	final int    sizeBytes;
	const IncomingRequest({
		required this.transferId,
		required this.senderName,
		required this.filename,
		required this.sizeBytes,
	});
}

enum TransferType { file, clipboard }

/// Progress of an in-flight file transfer, emitted via [BetterSendBridge.onProgress].
class TransferProgress {
	final bool   sending;     // true = outgoing (we send), false = incoming
	final String peerName;
	final String fileName;
	final int    transferred;
	final int    total;
	const TransferProgress({
		required this.sending,
		required this.peerName,
		required this.fileName,
		required this.transferred,
		required this.total,
	});

	double get fraction => total > 0 ? (transferred / total).clamp(0.0, 1.0) : 0.0;
	bool   get done     => total > 0 && transferred >= total;
}
