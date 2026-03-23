import 'dart:ffi';
import 'dart:io';
import 'package:ffi/ffi.dart';

// ── ffi_bridge.dart ───────────────────────────────────────────────────────────
// כל הגדרות ה-FFI לחיבור Flutter ↔ bettersend_core (.so / .dylib / .dll).
//
// קובץ זה אחראי על:
//   1. טעינת ה-shared library לפי פלטפורמה
//   2. הגדרת typedefs של כל פונקציות ה-C API
//   3. מחלקת BetterSendBridge — wrapper נוח לקריאה מה-UI
//
// ── איך dart:ffi עובד ─────────────────────────────────────────────────────────
//   לכל פונקציה C צריך להגדיר:
//   a. NativeFunction typedef — החתימה ב-C (עם Pointer, Int32, etc.)
//   b. DartFunction typedef — החתימה ב-Dart (עם Pointer, int, etc.)
//   c. lookup — טעינת הפונקציה מה-library
//
// תיעוד: https://dart.dev/guides/libraries/c-interop

// ── 1. Load library ───────────────────────────────────────────────────────────

DynamicLibrary _loadLibrary() {
  if (Platform.isAndroid) {
    return DynamicLibrary.open('libbettersend_core.so');
  } else if (Platform.isIOS) {
    // ב-iOS ה-library מוטמעת בתוך ה-binary (static linking)
    return DynamicLibrary.process();
  } else if (Platform.isMacOS) {
    return DynamicLibrary.open('libbettersend_core.dylib');
  } else if (Platform.isWindows) {
    return DynamicLibrary.open('bettersend_core.dll');
  }
  throw UnsupportedError('Platform not supported: ${Platform.operatingSystem}');
}

final _lib = _loadLibrary();

// ── 2. Native typedefs ────────────────────────────────────────────────────────
//
// TODO: הגדר typedef לכל פונקציה ב-bettersend_api.cpp
//
// דוגמה לפונקציה פשוטה:
//   typedef NativeBettersendCreate = Pointer<Void> Function(Pointer<Utf8> deviceName);
//   typedef DartBettersendCreate   = Pointer<Void> Function(Pointer<Utf8> deviceName);
//
// דוגמה ל-callback (DeviceFoundCallback):
//   typedef NativeDeviceFoundCallback = Void Function(
//     Pointer<Utf8> name, Pointer<Utf8> ip, Int32 port);
//   typedef DartDeviceFoundCallback = void Function(
//     Pointer<Utf8> name, Pointer<Utf8> ip, int port);
//
// הגדר typedefs עבור:
//   bettersend_create
//   bettersend_destroy
//   bettersend_start_server
//   bettersend_start_advertising
//   bettersend_start_discovery
//   bettersend_send_file
//   bettersend_send_clipboard
//   bettersend_clipboard_read    (שלב 2)
//   bettersend_clipboard_write   (שלב 2)

// ── 3. Lookup functions ───────────────────────────────────────────────────────
//
// TODO: לאחר הגדרת ה-typedefs, טען כל פונקציה כך:
//   final _bettersendCreate = _lib.lookupFunction<
//     NativeBettersendCreate, DartBettersendCreate>('bettersend_create');

// ── 4. BetterSendBridge ───────────────────────────────────────────────────────

/// Wrapper נוח שמחביא את הפרטים של FFI מ-UI code.
///
/// שימוש מה-UI:
///   final bridge = BetterSendBridge('iPhone-Yoni');
///   bridge.startServer(9000, onReceive: (transfer) { setState(...) });
///   bridge.startDiscovery(onFound: (device) { setState(...) });
///   bridge.sendFile(device, '/path/to/photo.jpg');
///   bridge.dispose();
class BetterSendBridge {
  // TODO: הוסף Pointer<Void> _handle לאחר שתממש bettersend_create

  BetterSendBridge(String deviceName) {
    // TODO: _handle = _bettersendCreate(deviceName.toNativeUtf8())
  }

  /// הפעל TCP server.
  /// [port] — הפורט שעליו להאזין (ברירת מחדל: 9000)
  /// [onReceive] — יקרא ב-thread של Dart עם פרטי ההעברה
  void startServer(int port, {required Function(ReceivedTransfer) onReceive}) {
    // TODO: צור NativeCallable עבור TransferReceivedCallback
    //       קרא ל-bettersend_start_server(_handle, port, callback.nativeFunction)
  }

  /// התחל פרסום ברשת.
  void startAdvertising(int port) {
    // TODO: bettersend_start_advertising(_handle, port)
  }

  /// התחל גילוי מכשירים ברשת.
  /// [onFound] — יקרא לכל מכשיר חדש שנמצא
  void startDiscovery({required Function(DiscoveredDevice) onFound}) {
    // TODO: צור NativeCallable עבור DeviceFoundCallback
    //       קרא ל-bettersend_start_discovery(_handle, callback.nativeFunction)
  }

  /// שלח קובץ למכשיר.
  void sendFile(DiscoveredDevice device, String filePath) {
    // TODO: bettersend_send_file(_handle, ip, port, filePath)
  }

  /// שלח clipboard text למכשיר.
  void sendClipboard(DiscoveredDevice device, String text) {
    // TODO: bettersend_send_clipboard(_handle, ip, port, text)
  }

  /// שחרר משאבים. קרא ל-dispose() כשה-widget נסגר.
  void dispose() {
    // TODO: bettersend_destroy(_handle)
  }
}

// ── Data classes ──────────────────────────────────────────────────────────────

/// מכשיר שנמצא ע"י mDNS.
class DiscoveredDevice {
  final String name;
  final String ip;
  final int port;
  const DiscoveredDevice({required this.name, required this.ip, required this.port});
}

/// העברה שהתקבלה.
class ReceivedTransfer {
  final TransferType type;
  final String senderName;
  final String name;       // שם קובץ (ריק עבור clipboard)
  final String data;       // path לקובץ או clipboard text
  final int sizeBytes;
  const ReceivedTransfer({
    required this.type,
    required this.senderName,
    required this.name,
    required this.data,
    required this.sizeBytes,
  });
}

enum TransferType { file, clipboard }
