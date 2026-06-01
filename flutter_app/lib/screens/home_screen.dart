import 'package:flutter/material.dart';
import '../ffi_bridge.dart';
import 'device_list_screen.dart';

// ── HomeScreen ────────────────────────────────────────────────────────────────
// Main screen: nearby devices, recent transfers, send FAB.
//
// Design prompt (when building full UI):
//   "Design the HomeScreen for BetterSend, a cross-platform AirDrop alternative.
//    Style: clean, modern, iOS-inspired but works on Android too.
//    Layout:
//      - AppBar: app name left, settings gear icon right
//      - 'Nearby Devices' section: horizontal scroll row of device cards
//        Each card: device icon (phone/laptop), device name, subtle ping animation
//        when scanning. Tap → navigates to DeviceListScreen.
//      - 'Recent Transfers' section: vertical list, each item shows
//        file icon, filename/Clipboard, sender name, time ago, file size.
//      - FAB: circular, send icon, bottom-right. Tap → file picker then DeviceListScreen.
//    Empty states: 'Scanning for devices...' with animated radar wave SVG.
//    Color scheme: blue-to-indigo gradient accent, white cards, dark text."

class HomeScreen extends StatefulWidget {
	final BetterSendBridge bridge;
	const HomeScreen({super.key, required this.bridge});

	@override
	State<HomeScreen> createState() => _HomeScreenState();
}

class _HomeScreenState extends State<HomeScreen> {
	final List<DiscoveredDevice> _devices  = [];
	final List<ReceivedTransfer> _received = [];
	String _echoResult = '';

	@override
	void initState() {
		super.initState();
		_initBridge();
	}

	void _initBridge() {
		// TODO: wire up bridge callbacks
		// widget.bridge.startServer(9000, onReceive: (t) {
		//   setState(() => _received.insert(0, t));
		// });
		// widget.bridge.startAdvertising(9000);
		// widget.bridge.startDiscovery(onFound: (d) {
		//   setState(() {
		//     if (!_devices.any((e) => e.ip == d.ip)) _devices.add(d);
		//   });
		// });
	}

	@override
	void dispose() {
		widget.bridge.dispose();
		super.dispose();
	}

	void _runEcho() {
		final result = widget.bridge.echo('Hello World');
		setState(() => _echoResult = result);
	}

	@override
	Widget build(BuildContext context) {
		return Scaffold(
			appBar: AppBar(
				title: const Text('BetterSend'),
				actions: [
					IconButton(
						icon: const Icon(Icons.settings),
						onPressed: () {/* TODO: settings screen */},
					),
				],
			),
			body: Padding(
				padding: const EdgeInsets.all(24.0),
				child: Column(
					crossAxisAlignment: CrossAxisAlignment.stretch,
					children: [
						// ── Hello World: FFI pipeline smoke test ──────────────────
						Card(
							child: Padding(
								padding: const EdgeInsets.all(16.0),
								child: Column(
									crossAxisAlignment: CrossAxisAlignment.start,
									children: [
										const Text(
											'FFI Pipeline Test',
											style: TextStyle(fontWeight: FontWeight.bold, fontSize: 14),
										),
										const SizedBox(height: 8),
										ElevatedButton(
											onPressed: _runEcho,
											child: const Text('Send "Hello World" → C++'),
										),
										if (_echoResult.isNotEmpty) ...[
											const SizedBox(height: 8),
											Text(
												_echoResult,
												style: TextStyle(
													color: _echoResult.startsWith('[mock]')
														? Colors.orange
														: Colors.green,
													fontFamily: 'monospace',
												),
											),
										],
									],
								),
							),
						),
						const SizedBox(height: 24),

						// TODO: NearbyDevicesSection(devices: _devices)
						// TODO: RecentTransfersSection(transfers: _received)
						const Expanded(
							child: Center(
								child: Text(
									'Scanning for devices...',
									style: TextStyle(color: Colors.grey),
								),
							),
						),
					],
				),
			),
			floatingActionButton: FloatingActionButton(
				onPressed: _onSendTap,
				child: const Icon(Icons.send),
			),
		);
	}

	void _onSendTap() {
		// TODO: open file picker, then navigate to DeviceListScreen with filePath
		Navigator.push(
			context,
			MaterialPageRoute(
				builder: (_) => DeviceListScreen(
					devices: _devices,
					bridge: widget.bridge,
				),
			),
		);
	}
}
