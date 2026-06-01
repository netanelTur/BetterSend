import 'package:flutter/material.dart';
import '../ffi_bridge.dart';

// ── TransferScreen ────────────────────────────────────────────────────────────
// Shown during active file/clipboard transfers.
//
// Design prompt (when building full UI):
//   "Design the TransferScreen for BetterSend — shown during file/clipboard transfers.
//    Two modes:
//      Sending: large animated upload arrow, filename bold, '→ DeviceName' subtitle,
//               LinearProgressIndicator with percentage, speed (MB/s), Cancel button.
//      Receiving: large animated download arrow, sender name + filename,
//                 same progress bar. On completion: green checkmark animation,
//                 'Open File' button (for files) or 'Copy' button (for clipboard).
//    Background: subtle gradient matching HomeScreen.
//    Smooth transition: progress bar fills with Tween animation driven by real data."

class TransferScreen extends StatefulWidget {
	final DiscoveredDevice? device;        // non-null when sending
	final ReceivedTransfer? received;      // non-null when receiving

	const TransferScreen({
		super.key,
		this.device,
		this.received,
	});

	@override
	State<TransferScreen> createState() => _TransferScreenState();
}

class _TransferScreenState extends State<TransferScreen> {
	double _progress = 0.0;
	bool   _done     = false;

	bool get _isSending => widget.device != null;

	@override
	Widget build(BuildContext context) {
		return Scaffold(
			appBar: AppBar(
				title: Text(_isSending ? 'Sending...' : 'Receiving...'),
			),
			body: Center(
				child: Padding(
					padding: const EdgeInsets.symmetric(horizontal: 32.0),
					child: Column(
						mainAxisSize: MainAxisSize.min,
						children: [
							// TODO: animated arrow icon (up=sending, down=receiving)
							Icon(
								_isSending ? Icons.upload : Icons.download,
								size: 64,
								color: Theme.of(context).colorScheme.primary,
							),
							const SizedBox(height: 24),

							// TODO: pull real name from widget.device / widget.received
							const Text(
								'filename.jpg',
								style: TextStyle(fontSize: 18, fontWeight: FontWeight.bold),
							),
							const SizedBox(height: 8),

							LinearProgressIndicator(value: _progress),
							const SizedBox(height: 8),
							Text('${(_progress * 100).toInt()}%'),

							const SizedBox(height: 32),

							if (!_done && _isSending)
								OutlinedButton(
									onPressed: () => Navigator.pop(context),
									child: const Text('Cancel'),
								),

							if (_done) ...[
								const Icon(Icons.check_circle_outline,
									color: Colors.green, size: 56),
								const SizedBox(height: 8),
								const Text('Done!',
									style: TextStyle(fontSize: 16, fontWeight: FontWeight.bold)),
								// TODO: 'Open File' / 'Copy to Clipboard' action button
							],
						],
					),
				),
			),
		);
	}
}
