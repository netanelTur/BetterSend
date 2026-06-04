import 'package:flutter/material.dart';
import 'package:path/path.dart' as p;

import '../ffi_bridge.dart';
import 'transfer_screen.dart';

// ── DeviceListScreen ──────────────────────────────────────────────────────────
// Standalone device picker used when the user already has content to send
// (file path or clipboard text) and only needs to pick the destination.
// HomeScreen does its own send flow inline (pick file → pick device), so
// this screen is reserved for the share-sheet / drag-and-drop entry points
// that will land in Phase 4.

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
							leading: Icon(device.kind == DeviceKind.mobile
								? Icons.phone_android
								: Icons.computer),
							title: Text(device.name),
							subtitle: Text(device.ip),
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
			Navigator.push(context, MaterialPageRoute(
				builder: (_) => TransferScreen(
					device:   device,
					fileName: p.basename(filePath!),
				),
			));
		} else if (clipboardText != null) {
			bridge.sendClipboard(device, clipboardText!);
			Navigator.pop(context);
		}
	}
}
