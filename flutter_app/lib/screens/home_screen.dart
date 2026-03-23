// ── ASK CLAUDE FOR UI ─────────────────────────────────────────────────────────
// Prompt to give Claude when you're ready to build the HomeScreen UI:
//
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
// ─────────────────────────────────────────────────────────────────────────────

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
  final List<DiscoveredDevice> _devices = [];
  final List<ReceivedTransfer> _received = [];

  @override
  void initState() {
    super.initState();
    _initBridge();
  }

  void _initBridge() {
    // TODO: wire up bridge callbacks
    // widget.bridge.startServer(BetterSend.kDefaultPort, onReceive: (t) {
    //   setState(() => _received.insert(0, t));
    // });
    // widget.bridge.startAdvertising(BetterSend.kDefaultPort);
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
      body: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          // TODO: NearbyDevicesSection(devices: _devices)
          // TODO: RecentTransfersSection(transfers: _received)
          const Expanded(
            child: Center(child: Text('TODO: implement HomeScreen UI')),
          ),
        ],
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
