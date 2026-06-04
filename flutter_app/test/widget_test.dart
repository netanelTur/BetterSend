// Smoke test: verify the splash screen mounts and shows the tagline.

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

import 'package:bettersend/screens/splash_screen.dart';

void main() {
	testWidgets('Splash shows BetterSend title', (WidgetTester tester) async {
		await tester.pumpWidget(MaterialApp(
			home: SplashScreen(onDone: () {}),
		));
		expect(find.text('BetterSend'), findsOneWidget);
		expect(find.text('Better Send Than Sorry'), findsOneWidget);
	});
}
