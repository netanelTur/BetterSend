import 'dart:io';
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
	// Each machine advertises a unique name so peers can distinguish each other.
	// Platform.localHostname returns the OS hostname (Windows, macOS, Linux);
	// on mobile we'd fall back to device_info_plus, but Phase 1 is desktop-only.
	String deviceName;
	try {
		deviceName = Platform.localHostname;
	} catch (_) {
		deviceName = 'BetterSend';
	}
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
