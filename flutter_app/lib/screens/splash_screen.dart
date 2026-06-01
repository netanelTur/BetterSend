import 'package:flutter/material.dart';

// ── SplashScreen ──────────────────────────────────────────────────────────────
// Opening screen. Fades in "Better Send Than Sorry", then calls onDone.

class SplashScreen extends StatefulWidget {
	const SplashScreen({super.key, required this.onDone});
	final VoidCallback onDone;

	@override
	State<SplashScreen> createState() => _SplashScreenState();
}

class _SplashScreenState extends State<SplashScreen>
		with SingleTickerProviderStateMixin {
	late final AnimationController _ctrl;
	late final Animation<double> _fade;

	@override
	void initState() {
		super.initState();
		_ctrl = AnimationController(
			vsync: this,
			duration: const Duration(milliseconds: 900),
		);
		_fade = CurvedAnimation(parent: _ctrl, curve: Curves.easeIn);
		_ctrl.forward();
		Future.delayed(const Duration(milliseconds: 2200), widget.onDone);
	}

	@override
	void dispose() {
		_ctrl.dispose();
		super.dispose();
	}

	@override
	Widget build(BuildContext context) {
		return Scaffold(
			backgroundColor: const Color(0xFF0F0F1A),
			body: Center(
				child: FadeTransition(
					opacity: _fade,
					child: Column(
						mainAxisSize: MainAxisSize.min,
						children: const [
							Text(
								'BetterSend',
								style: TextStyle(
									fontSize: 42,
									fontWeight: FontWeight.w700,
									color: Colors.white,
									letterSpacing: 1.5,
								),
							),
							SizedBox(height: 10),
							Text(
								'Better Send Than Sorry',
								style: TextStyle(
									fontSize: 15,
									color: Color(0xFF7777AA),
									letterSpacing: 1.2,
								),
							),
						],
					),
				),
			),
		);
	}
}
