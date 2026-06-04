import 'package:flutter/material.dart';
import '../ffi_bridge.dart';

// ── TransferScreen ────────────────────────────────────────────────────────────
// Shown while an outbound send is in flight. The C send path is currently
// synchronous (no progress callbacks yet — Phase 4 hooks them up), so this
// screen runs an indeterminate progress indicator and dismisses on user
// action. Receiving is surfaced via a SnackBar + the Received list on
// HomeScreen, so no Receiving variant is needed here yet.

class TransferScreen extends StatelessWidget {
	final DiscoveredDevice device;
	final String           fileName;
	const TransferScreen({super.key, required this.device, required this.fileName});

	@override
	Widget build(BuildContext context) {
		return Scaffold(
			appBar: AppBar(title: const Text('Sending...')),
			body: Center(
				child: Padding(
					padding: const EdgeInsets.symmetric(horizontal: 32.0),
					child: Column(
						mainAxisSize: MainAxisSize.min,
						children: [
							Icon(Icons.upload,
								size: 72,
								color: Theme.of(context).colorScheme.primary),
							const SizedBox(height: 24),
							Text(fileName,
								style: const TextStyle(fontSize: 18, fontWeight: FontWeight.bold)),
							const SizedBox(height: 4),
							Text('→ ${device.name}',
								style: const TextStyle(color: Colors.grey)),
							const SizedBox(height: 24),
							const LinearProgressIndicator(),
							const SizedBox(height: 32),
							OutlinedButton(
								onPressed: () => Navigator.pop(context),
								child: const Text('Close'),
							),
						],
					),
				),
			),
		);
	}
}
