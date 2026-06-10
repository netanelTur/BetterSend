import 'dart:convert';
import 'dart:io';

import 'package:file_picker/file_picker.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:path_provider/path_provider.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../ffi_bridge.dart';

// ── settings_screen.dart ──────────────────────────────────────────────────────
// User preferences, persisted via shared_preferences:
//   - Display name peers see (live re-advertise via bridge.setDeviceName)
//   - Folder where received files are saved (bridge.setSaveDir)
//
// Both keys are read at startup (main.dart loads the name; home_screen loads
// the folder) so a relaunch restores the user's choices.

const String kPrefDisplayName = 'display_name';
const String kPrefSaveDir      = 'save_dir';

// BLE advertisements carry the name in a 31-byte budget; the native side caps
// it at this many BYTES (kBleMaxNameLen in cpp_core/include/Constants.h). We
// enforce the same limit in the UI so the user never silently loses characters.
const int kDisplayNameMaxBytes = 20;

class SettingsScreen extends StatefulWidget {
	final BetterSendBridge bridge;
	const SettingsScreen({super.key, required this.bridge});

	@override
	State<SettingsScreen> createState() => _SettingsScreenState();
}

class _SettingsScreenState extends State<SettingsScreen> {
	final TextEditingController _nameCtrl = TextEditingController();
	String? _saveDir;

	@override
	void initState() {
		super.initState();
		_load();
	}

	@override
	void dispose() {
		_nameCtrl.dispose();
		super.dispose();
	}

	Future<void> _load() async {
		final prefs = await SharedPreferences.getInstance();
		// Mirror main.dart: saved name, else the OS hostname.
		String name = prefs.getString(kPrefDisplayName) ?? '';
		if (name.isEmpty) {
			try {
				name = Platform.localHostname;
			} catch (_) {
				name = 'BetterSend';
			}
		}
		final dir = prefs.getString(kPrefSaveDir) ??
			(await getDownloadsDirectory())?.path;
		if (!mounted) return;
		setState(() {
			_nameCtrl.text = name;
			_saveDir = dir;
		});
	}

	int get _nameBytes => utf8.encode(_nameCtrl.text).length;

	Future<void> _saveName() async {
		final name = _nameCtrl.text.trim();
		if (name.isEmpty) {
			ScaffoldMessenger.of(context).showSnackBar(
				const SnackBar(content: Text('Name cannot be empty')));
			return;
		}
		final prefs = await SharedPreferences.getInstance();
		await prefs.setString(kPrefDisplayName, name);
		widget.bridge.setDeviceName(name);   // live re-advertise + header rename
		if (!mounted) return;
		ScaffoldMessenger.of(context).showSnackBar(
			SnackBar(content: Text("Now visible to others as '$name'")));
	}

	Future<void> _pickSaveDir() async {
		final picked = await FilePicker.platform.getDirectoryPath(
			dialogTitle: 'Choose where received files are saved');
		if (picked == null || !mounted) return;
		final prefs = await SharedPreferences.getInstance();
		await prefs.setString(kPrefSaveDir, picked);
		widget.bridge.setSaveDir(picked);
		setState(() => _saveDir = picked);
	}

	@override
	Widget build(BuildContext context) {
		return Scaffold(
			appBar: AppBar(title: const Text('Settings')),
			body: ListView(
				padding: const EdgeInsets.all(24.0),
				children: [
					// ── Display name ───────────────────────────────────────────
					const Text('Display name',
						style: TextStyle(fontWeight: FontWeight.bold, fontSize: 16)),
					const SizedBox(height: 4),
					const Text('What other devices see when they discover you.',
						style: TextStyle(color: Colors.grey, fontSize: 12)),
					const SizedBox(height: 12),
					TextField(
						controller: _nameCtrl,
						// Byte-budget validation, NOT character count: maxLength would
						// count runes and let a Hebrew/emoji name overflow 20 bytes.
						inputFormatters: [_Utf8ByteLimitFormatter(kDisplayNameMaxBytes)],
						onChanged: (_) => setState(() {}),  // refresh the byte counter
						decoration: InputDecoration(
							border: const OutlineInputBorder(),
							hintText: 'e.g. Yonatan-Mac',
							helperText: '$_nameBytes / $kDisplayNameMaxBytes bytes',
							suffixIcon: IconButton(
								icon: const Icon(Icons.check),
								tooltip: 'Save name',
								onPressed: _saveName,
							),
						),
						onSubmitted: (_) => _saveName(),
					),

					const SizedBox(height: 32),

					// ── Save folder ────────────────────────────────────────────
					const Text('Received files',
						style: TextStyle(fontWeight: FontWeight.bold, fontSize: 16)),
					const SizedBox(height: 12),
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
				],
			),
		);
	}
}

// Rejects any edit whose UTF-8 byte length exceeds [maxBytes]. Keeps the old
// value when an edit would overflow, so the field never holds a string the
// native BLE advert can't carry intact.
class _Utf8ByteLimitFormatter extends TextInputFormatter {
	final int maxBytes;
	_Utf8ByteLimitFormatter(this.maxBytes);

	@override
	TextEditingValue formatEditUpdate(
			TextEditingValue oldValue, TextEditingValue newValue) {
		if (utf8.encode(newValue.text).length > maxBytes) return oldValue;
		return newValue;
	}
}
