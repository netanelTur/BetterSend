import 'dart:async';

import 'package:file_picker/file_picker.dart';
import 'package:flutter/material.dart';
import 'package:path/path.dart' as p;

import '../ffi_bridge.dart';
import 'transfer_screen.dart';

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
	Timer?                          _pruner;

	@override
	void initState() {
		super.initState();
		_initBridge();
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
		widget.bridge.startAdvertising(9000);
		widget.bridge.startDiscovery(onFound: (d) {
			setState(() {
				_lastSeen[d.name] = DateTime.now();
				if (!_devices.any((e) => e.name == d.name)) _devices.add(d);
			});
		});
		_pruner = Timer.periodic(_kPeerHeartbeat, _prune);
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

	Future<void> _pickAndSend(DiscoveredDevice device) async {
		final result = await FilePicker.platform.pickFiles();
		if (result == null || result.files.isEmpty) return;
		final path = result.files.single.path;
		if (path == null) return;

		widget.bridge.sendFile(device, path);
		if (!mounted) return;

		await Navigator.push(context, MaterialPageRoute(
			builder: (_) => TransferScreen(
				device:   device,
				fileName: p.basename(path),
			),
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
								const Text('Nearby Devices',
									style: TextStyle(fontWeight: FontWeight.bold, fontSize: 16)),
								const Spacer(),
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
										onSend: () => _pickAndSend(_devices[i]),
									),
								),
						),

						const SizedBox(height: 24),

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
