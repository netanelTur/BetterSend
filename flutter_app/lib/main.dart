import 'package:flutter/material.dart';
import 'ffi_bridge.dart';
import 'screens/home_screen.dart';

// ── main.dart ─────────────────────────────────────────────────────────────────
// נקודת הכניסה לאפליקציה.
//
// אחריות:
//   1. אתחול ה-BetterSendBridge עם שם המכשיר
//   2. הפעלת ה-MaterialApp עם הנתיבים המתאימים
//   3. העברת ה-bridge ל-HomeScreen

void main() {
  // TODO: קרא שם מכשיר מ-device info (package: device_info_plus)
  //       או תן למשתמש לבחור בהגדרות
  const String deviceName = 'MyDevice'; // placeholder

  final bridge = BetterSendBridge(deviceName);

  runApp(BetterSendApp(bridge: bridge));
}

class BetterSendApp extends StatelessWidget {
  final BetterSendBridge bridge;
  const BetterSendApp({super.key, required this.bridge});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'BetterSend',
      theme: ThemeData(
        colorScheme: ColorScheme.fromSeed(seedColor: Colors.blue),
        useMaterial3: true,
      ),
      home: HomeScreen(bridge: bridge),
    );
  }
}
