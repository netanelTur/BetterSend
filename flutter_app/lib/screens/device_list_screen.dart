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

	// Connect to the chosen device's hotspot (spinner) before sending the
	// already-picked content. Mirrors HomeScreen's connect-then-send gate.
	Future<void> _send(BuildContext context, DiscoveredDevice device) async {
		final navigator = Navigator.of(context);
		final messenger = ScaffoldMessenger.of(context);
		var dialogOpen = true;

		showDialog<void>(
			context: context,
			barrierDismissible: false,
			builder: (ctx) => AlertDialog(
				content: Row(
					mainAxisSize: MainAxisSize.min,
					children: [
						const SizedBox(
							width: 22, height: 22,
							child: CircularProgressIndicator(strokeWidth: 2.5),
						),
						const SizedBox(width: 20),
						Expanded(child: Text('Connecting to ${device.name}…')),
					],
				),
				actions: [
					TextButton(
						onPressed: () {
							dialogOpen = false;
							Navigator.pop(ctx);
						},
						child: const Text('Cancel'),
					),
				],
			),
		);

		final ok = await bridge.connectToPeer(device);
		if (!dialogOpen) return;
		dialogOpen = false;
		navigator.pop(); // close the spinner

		if (!ok) {
			messenger.showSnackBar(SnackBar(
				content: Text('Could not connect to ${device.name}.'),
			));
			return;
		}

		if (filePath != null) {
			bridge.sendFile(device, filePath!);
			navigator.push(MaterialPageRoute(
				builder: (_) => TransferScreen(
					device:   device,
					fileName: p.basename(filePath!),
				),
			));
		} else if (clipboardText != null) {
			bridge.sendClipboard(device, clipboardText!);
			navigator.pop();
		}
	}
}
