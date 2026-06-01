import 'package:flutter/material.dart';
import 'ffi_bridge.dart';
import 'screens/home_screen.dart';
import 'screens/splash_screen.dart';

// ── main.dart ─────────────────────────────────────────────────────────────────
// App entry point.
//
// Responsibilities:
//   1. Initialize BetterSendBridge with device name
//   2. Show SplashScreen, then navigate to HomeScreen
//   3. Pass bridge down to HomeScreen

void main() {
	// TODO: read device name from device_info_plus or let user set it in settings
	const String deviceName = 'MyDevice';
	final bridge = BetterSendBridge(deviceName);
	runApp(BetterSendApp(bridge: bridge));
}

class BetterSendApp extends StatefulWidget {
	final BetterSendBridge bridge;
	const BetterSendApp({super.key, required this.bridge});

	@override
	State<BetterSendApp> createState() => _BetterSendAppState();
}

class _BetterSendAppState extends State<BetterSendApp> {
	bool _showSplash = true;

	@override
	Widget build(BuildContext context) {
		return MaterialApp(
			title: 'BetterSend',
			theme: ThemeData(
				colorScheme: ColorScheme.fromSeed(seedColor: Colors.blue),
				useMaterial3: true,
			),
			home: _showSplash
				? SplashScreen(onDone: () => setState(() => _showSplash = false))
				: HomeScreen(bridge: widget.bridge),
		);
	}
}
