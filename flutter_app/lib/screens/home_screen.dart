import 'dart:async';

import 'package:file_picker/file_picker.dart';
import 'package:flutter/material.dart';
import 'package:path_provider/path_provider.dart';

import '../ffi_bridge.dart';

// Liveness windows — keep in sync with cpp_core/include/Constants.h.
const Duration _kPeerHeartbeat = Duration(seconds: 2);
const Duration _kPeerStale     = Duration(seconds: 10);

class HomeScreen extends StatefulWidget {
	final BetterSendBridge bridge;
	const HomeScreen({super.key, required this.bridge});

	@override
	State<HomeScreen> createState() => _HomeScreenState();
}

class _HomeScreenState extends State<HomeScreen> {
	final List<DiscoveredDevice>    _devices   = [];
	final Map<String, DateTime>     _lastSeen  = {};
	final List<ReceivedTransfer>    _received  = [];
	// Live file-transfer progress, keyed by direction+filename. Updated from
	// the native progress callback; an entry is cleared shortly after it hits
	// 100% so completed transfers don't linger.
	final Map<String, TransferProgress> _progress = {};
	Timer?                          _pruner;
	String?                         _saveDir;

	@override
	void initState() {
		super.initState();
		_initBridge();
		_initSaveDir();
	}

	// Default the save folder to the OS Downloads dir so files land somewhere
	// sensible before the user picks. Pushed down to native immediately.
	Future<void> _initSaveDir() async {
		final dir = await getDownloadsDirectory();
		if (dir == null || !mounted) return;
		widget.bridge.setSaveDir(dir.path);
		setState(() => _saveDir = dir.path);
	}

	Future<void> _pickSaveDir() async {
		final picked = await FilePicker.platform.getDirectoryPath(
			dialogTitle: 'Choose where received files are saved');
		if (picked == null || !mounted) return;
		widget.bridge.setSaveDir(picked);
		setState(() => _saveDir = picked);
	}

	void _initBridge() {
		// Order matters: TCP server must be listening before the Mac fires
		// its post-join Hello at the Windows host, otherwise Windows never
		// learns the Mac's hotspot IP and can't send back to it.
		widget.bridge.startServer(9000, onReceive: (t) {
			setState(() => _received.add(t));
			if (mounted) {
				_showReceivedSnack(t);
			}
		});
		widget.bridge.onIncomingRequest(_handleIncomingRequest);
		widget.bridge.onTransferDeclined(_handleDeclined);
		widget.bridge.onProgress(_handleProgress);
		widget.bridge.startAdvertising(9000);
		widget.bridge.startDiscovery(onFound: (d) {
			setState(() {
				// Two discovery channels can surface the SAME physical Mac under
				// different identities: BLE gives a synthetic placeholder
				// ("BetterSend-<hex>", ip "ble:...") because CoreBluetooth on
				// Apple silicon never sends Windows a usable LocalName; the TCP
				// Hello (after the Mac joins the hotspot) gives the real device
				// name with a routable hotspot IPv4. They can't be correlated
				// (macOS hides its BLE address, the Hello carries no BLE token),
				// and the list dedups by name, so both would show as two cards.
				// Phase 1 has exactly one remote peer, so collapse on identity:
				// a routable card supersedes any ble-only placeholder.
				final routable = !d.ip.startsWith('ble:');
				if (routable) {
					// The Hello card is the only one that can actually send (the
					// native send path needs a non-"ble:" ip; tapping the
					// placeholder polls the wrong peer key and always times out).
					// Drop the placeholder so only the sendable card remains.
					_devices.removeWhere((e) => e.ip.startsWith('ble:'));
				} else if (_devices.any((e) => !e.ip.startsWith('ble:'))) {
					// A routable card already represents this peer. Don't re-add
					// the placeholder; instead treat the ongoing BLE advert as a
					// liveness proxy — the Hello fires only once on connect, so
					// without this the routable card would go stale and prune
					// mid-session. Live-derived guard, not a sticky flag: if the
					// routable card later prunes, this branch stops matching and
					// the placeholder is re-admitted, so reconnect self-heals.
					final r = _devices.firstWhere((e) => !e.ip.startsWith('ble:'));
					_lastSeen[r.name] = DateTime.now();
					return;
				}
				_lastSeen[d.name] = DateTime.now();
				if (!_devices.any((e) => e.name == d.name)) _devices.add(d);
			});
		});
		_pruner = Timer.periodic(_kPeerHeartbeat, _prune);
	}

	void _handleIncomingRequest(IncomingRequest req) {
		if (!mounted) return;
		showDialog<void>(
			context: context,
			barrierDismissible: false,
			builder: (ctx) => AlertDialog(
				title: const Text('Incoming file'),
				content: Column(
					mainAxisSize: MainAxisSize.min,
					crossAxisAlignment: CrossAxisAlignment.start,
					children: [
						Text('${req.senderName} wants to send you:',
							style: const TextStyle(color: Colors.grey)),
						const SizedBox(height: 8),
						Text(req.filename,
							style: const TextStyle(fontWeight: FontWeight.bold)),
						Text(_fmtBytes(req.sizeBytes),
							style: const TextStyle(color: Colors.grey, fontSize: 12)),
					],
				),
				actions: [
					TextButton(
						onPressed: () {
							widget.bridge.declineTransfer(req.transferId);
							Navigator.pop(ctx);
						},
						child: const Text('Decline'),
					),
					ElevatedButton(
						onPressed: () {
							widget.bridge.acceptTransfer(req.transferId);
							Navigator.pop(ctx);
						},
						child: const Text('Accept'),
					),
				],
			),
		);
	}

	void _handleDeclined(String transferId) {
		if (!mounted) return;
		ScaffoldMessenger.of(context).showSnackBar(
			const SnackBar(content: Text('Peer declined the transfer.')),
		);
	}

	void _handleProgress(TransferProgress p) {
		if (!mounted) return;
		final key = '${p.sending ? 'out' : 'in'}:${p.fileName}';
		setState(() => _progress[key] = p);
		if (p.done) {
			// Let the bar show 100% briefly, then clear it.
			Future.delayed(const Duration(milliseconds: 1500), () {
				if (mounted) setState(() => _progress.remove(key));
			});
		}
	}

	Widget _progressCard(TransferProgress p) {
		final pct  = (p.fraction * 100).toStringAsFixed(0);
		final verb = p.sending ? 'Sending' : 'Receiving';
		return Card(
			child: Padding(
				padding: const EdgeInsets.all(12),
				child: Column(
					crossAxisAlignment: CrossAxisAlignment.start,
					children: [
						Row(
							children: [
								Icon(p.sending ? Icons.upload : Icons.download, size: 20),
								const SizedBox(width: 8),
								Expanded(
									child: Text('$verb ${p.fileName}',
										overflow: TextOverflow.ellipsis,
										style: const TextStyle(fontWeight: FontWeight.bold)),
								),
								Text('$pct%',
									style: const TextStyle(fontFeatures: [FontFeature.tabularFigures()])),
							],
						),
						const SizedBox(height: 8),
						ClipRRect(
							borderRadius: BorderRadius.circular(4),
							child: LinearProgressIndicator(value: p.fraction, minHeight: 6),
						),
					],
				),
			),
		);
	}

	static String _fmtBytes(int bytes) {
		if (bytes < 1024) return '$bytes B';
		if (bytes < 1024 * 1024) return '${(bytes / 1024).toStringAsFixed(1)} KB';
		if (bytes < 1024 * 1024 * 1024) return '${(bytes / 1024 / 1024).toStringAsFixed(1)} MB';
		return '${(bytes / 1024 / 1024 / 1024).toStringAsFixed(2)} GB';
	}

	void _prune(Timer _) {
		final cutoff = DateTime.now().subtract(_kPeerStale);
		final stale = _lastSeen.entries
			.where((e) => e.value.isBefore(cutoff))
			.map((e) => e.key)
			.toList();
		if (stale.isEmpty) return;
		setState(() {
			for (final name in stale) {
				_devices.removeWhere((d) => d.name == name);
				_lastSeen.remove(name);
			}
		});
	}

	@override
	void dispose() {
		_pruner?.cancel();
		widget.bridge.dispose();
		super.dispose();
	}

	void _showReceivedSnack(ReceivedTransfer t) {
		final label = t.type == TransferType.file
			? 'File from ${t.senderName}: ${t.name}'
			: 'Clipboard from ${t.senderName}';
		ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text(label)));
	}

	// Tap a device → connect to its hotspot first (spinner), then pick a file
	// and send. The Mac no longer auto-joins on discovery; the join happens
	// only here, for the one device the user chose.
	Future<void> _connectAndSend(DiscoveredDevice device) async {
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

		final ok = await widget.bridge.connectToPeer(device);

		// User cancelled: the connect result is ignored (background state),
		// the dialog is never resurrected.
		if (!dialogOpen || !mounted) return;
		dialogOpen = false;
		Navigator.of(context, rootNavigator: true).pop(); // close the spinner

		if (!ok) {
			messenger.showSnackBar(SnackBar(
				content: Text('Could not connect to ${device.name}. Tap to retry.'),
			));
			return;
		}

		// Connected → pick a file and send via the existing request flow.
		final result = await FilePicker.platform.pickFiles();
		if (result == null || result.files.isEmpty) return;
		final path = result.files.single.path;
		if (path == null) return;

		widget.bridge.sendFile(device, path);
		if (!mounted) return;
		messenger.showSnackBar(SnackBar(
			content: Text('Request sent to ${device.name}; waiting for accept...'),
		));
	}

	@override
	Widget build(BuildContext context) {
		return Scaffold(
			appBar: AppBar(title: const Text('BetterSend')),
			body: Padding(
				padding: const EdgeInsets.all(24.0),
				child: Column(
					crossAxisAlignment: CrossAxisAlignment.stretch,
					children: [
						// ── Nearby devices ─────────────────────────────────────────
						Row(
							children: [
								const Expanded(
									child: Text('Nearby Devices',
										style: TextStyle(fontWeight: FontWeight.bold, fontSize: 16),
										overflow: TextOverflow.ellipsis),
								),
								if (_devices.isEmpty)
									const SizedBox(
										width: 16, height: 16,
										child: CircularProgressIndicator(strokeWidth: 2),
									),
							],
						),
						const SizedBox(height: 8),

						Expanded(
							child: _devices.isEmpty
								? const Center(
									child: Text('Scanning for devices...',
										style: TextStyle(color: Colors.grey)),
								)
								: ListView.builder(
									itemCount: _devices.length,
									itemBuilder: (_, i) => _DeviceCard(
										device: _devices[i],
										onSend: () => _connectAndSend(_devices[i]),
									),
								),
						),

						// ── Active transfers ──────────────────────────────────────
						if (_progress.isNotEmpty) ...[
							const SizedBox(height: 16),
							const Text('Transfers',
								style: TextStyle(fontWeight: FontWeight.bold, fontSize: 16)),
							const SizedBox(height: 8),
							..._progress.values.map(_progressCard),
						],

						const SizedBox(height: 24),

						// ── Save folder ────────────────────────────────────────────
						Card(
							child: ListTile(
								leading: const Icon(Icons.folder, size: 28),
								title: const Text('Save received files to',
									style: TextStyle(fontWeight: FontWeight.bold, fontSize: 13)),
								subtitle: Text(_saveDir ?? 'Default (temp folder)',
									style: const TextStyle(fontFamily: 'monospace', fontSize: 11),
									overflow: TextOverflow.ellipsis),
								trailing: TextButton(
									onPressed: _pickSaveDir,
									child: const Text('Change'),
								),
							),
						),

						const SizedBox(height: 16),

						// ── Received transfers ────────────────────────────────────
						const Text('Received',
							style: TextStyle(fontWeight: FontWeight.bold, fontSize: 16)),
						const SizedBox(height: 8),
						Expanded(
							child: _received.isEmpty
								? const Center(
									child: Text('Nothing received yet.',
										style: TextStyle(color: Colors.grey)),
								)
								: ListView.builder(
									itemCount: _received.length,
									itemBuilder: (_, i) => _ReceivedCard(transfer: _received[i]),
								),
						),
					],
				),
			),
		);
	}
}

// ── Device card ───────────────────────────────────────────────────────────────

class _DeviceCard extends StatelessWidget {
	final DiscoveredDevice device;
	final VoidCallback onSend;
	const _DeviceCard({required this.device, required this.onSend});

	IconData _iconFor(DeviceKind kind) {
		switch (kind) {
			case DeviceKind.desktop: return Icons.computer;
			case DeviceKind.mobile:  return Icons.phone_android;
			case DeviceKind.unknown: return Icons.devices_other;
		}
	}

	@override
	Widget build(BuildContext context) {
		return Card(
			child: ListTile(
				leading: Icon(_iconFor(device.kind), size: 36),
				title: Text(device.name,
					style: const TextStyle(fontWeight: FontWeight.bold)),
				subtitle: Text(device.ip,
					style: const TextStyle(fontFamily: 'monospace', fontSize: 12)),
				trailing: IconButton(
					icon: const Icon(Icons.send),
					tooltip: 'Send to ${device.name}',
					onPressed: onSend,
				),
			),
		);
	}
}

// ── Received card ─────────────────────────────────────────────────────────────

class _ReceivedCard extends StatelessWidget {
	final ReceivedTransfer transfer;
	const _ReceivedCard({required this.transfer});

	@override
	Widget build(BuildContext context) {
		final isFile = transfer.type == TransferType.file;
		final title  = isFile ? transfer.name : 'Clipboard text';
		final subtitle = isFile
			? 'From ${transfer.senderName} · saved to: ${transfer.data}'
			: 'From ${transfer.senderName} · ${transfer.sizeBytes} chars';
		return Card(
			child: ListTile(
				leading: Icon(isFile ? Icons.insert_drive_file : Icons.content_paste,
					size: 32),
				title: Text(title, style: const TextStyle(fontWeight: FontWeight.bold)),
				subtitle: Text(subtitle, style: const TextStyle(fontSize: 12)),
			),
		);
	}
}
