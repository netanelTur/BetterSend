import 'package:flutter/material.dart';
import '../ffi_bridge.dart';
import 'device_list_screen.dart';
import 'transfer_screen.dart';

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
		widget.bridge.startAdvertising(9000);
		widget.bridge.startDiscovery(onFound: (d) {
			setState(() {
				if (!_devices.any((e) => e.ip == d.ip)) _devices.add(d);
			});
		});
	}

	@override
	void dispose() {
		widget.bridge.dispose();
		super.dispose();
	}

	void _runEcho() {
		setState(() => _echoResult = widget.bridge.echo('Hello World'));
	}

	@override
	Widget build(BuildContext context) {
		return Scaffold(
			appBar: AppBar(
				title: const Text('BetterSend'),
				actions: [
					IconButton(
						icon: const Icon(Icons.settings),
						onPressed: () {},
					),
				],
			),
			body: Padding(
				padding: const EdgeInsets.all(24.0),
				child: Column(
					crossAxisAlignment: CrossAxisAlignment.stretch,
					children: [
						// ── FFI smoke test ─────────────────────────────────────────
						Card(
							child: Padding(
								padding: const EdgeInsets.all(16.0),
								child: Column(
									crossAxisAlignment: CrossAxisAlignment.start,
									children: [
										const Text('FFI Pipeline Test',
											style: TextStyle(fontWeight: FontWeight.bold, fontSize: 14)),
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
										onSend: () => _sendToDevice(_devices[i]),
									),
								),
						),
					],
				),
			),
			floatingActionButton: FloatingActionButton(
				onPressed: () => Navigator.push(context, MaterialPageRoute(
					builder: (_) => DeviceListScreen(devices: _devices, bridge: widget.bridge),
				)),
				child: const Icon(Icons.send),
			),
		);
	}

	void _sendToDevice(DiscoveredDevice d) {
		Navigator.push(context, MaterialPageRoute(
			builder: (_) => TransferScreen(device: d),
		));
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
				subtitle: Text('${device.ip}:${device.port}',
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
