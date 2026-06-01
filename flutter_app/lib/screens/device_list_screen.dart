import 'package:flutter/material.dart';
import '../ffi_bridge.dart';
import 'transfer_screen.dart';

// ── DeviceListScreen ──────────────────────────────────────────────────────────
// Device picker: user selects which nearby device to send to.
//
// Design prompt (when building full UI):
//   "Design the DeviceListScreen for BetterSend.
//    Purpose: user picks which nearby device to send a file or clipboard to.
//    Layout:
//      - AppBar: 'Choose Device' title, back button
//      - Scanning indicator: pulsing green dot + 'Scanning...' text (top of list)
//      - ListView: each row = device avatar (initials circle), device name bold,
//        IP subtitle small+gray, 'Send' filled button right-aligned.
//      - Empty state: large radar animation SVG centered with 'No devices found yet'.
//      - Pull-to-refresh: triggers re-scan.
//    On 'Send' tap: show a brief haptic + navigate to TransferScreen.
//    Match the same blue-to-indigo accent from HomeScreen."

class DeviceListScreen extends StatelessWidget {
	final List<DiscoveredDevice> devices;
	final BetterSendBridge bridge;
	final String? filePath;       // null when sending clipboard
	final String? clipboardText;  // null when sending a file

	const DeviceListScreen({
		super.key,
		required this.devices,
		required this.bridge,
		this.filePath,
		this.clipboardText,
	});

	@override
	Widget build(BuildContext context) {
		return Scaffold(
			appBar: AppBar(title: const Text('Choose Device')),
			body: devices.isEmpty
				? const Center(
					child: Column(
						mainAxisSize: MainAxisSize.min,
						children: [
							CircularProgressIndicator(),
							SizedBox(height: 16),
							Text('Scanning for devices...'),
						],
					),
				)
				: ListView.builder(
					itemCount: devices.length,
					itemBuilder: (context, index) {
						final device = devices[index];
						return ListTile(
							leading: const Icon(Icons.phone_android),
							title: Text(device.name),
							subtitle: Text('${device.ip}:${device.port}'),
							trailing: ElevatedButton(
								onPressed: () => _send(context, device),
								child: const Text('Send'),
							),
						);
					},
				),
		);
	}

	void _send(BuildContext context, DiscoveredDevice device) {
		if (filePath != null) {
			bridge.sendFile(device, filePath!);
		} else if (clipboardText != null) {
			bridge.sendClipboard(device, clipboardText!);
		}
		Navigator.push(
			context,
			MaterialPageRoute(
				builder: (_) => TransferScreen(device: device),
			),
		);
	}
}
