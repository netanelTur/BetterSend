# Failed Attempts — Phase 1 Mac↔Windows Pairing

> **Status:** baseline restored to `80b10a7` source, then one minimal targeted fix added on top — the **eager `ensureHostStarted` on a detached thread** described in [§ "The one targeted fix"](#the-one-targeted-fix-added-on-top-of-80b10a7) below. The 10 MB end-to-end transfer Windows↔Mac is the working bar this state targets.
>
> Every commit listed in [§ Reverted commits](#reverted-commits-newest--oldest--what-each-one-tried-and-why-it-didnt-stick) was force-deleted on `2026-06-07` because together they regressed both discovery (Windows could no longer see the Mac in the nearby-devices list) **and** the transfer pipe (post-loading-UI flow never re-established a Wi-Fi join).
>
> Read this before re-attempting any of these directions. Don't repeat them.

---

## The one targeted fix added on top of `80b10a7`

When we restored `80b10a7` clean and Yonatan ran his Mac side, we hit a **real, structural deadlock that has always existed in this baseline** but was previously masked by lucky timing / hardware state:

- `BleHandshake_Mac` scans BLE filtered by the BetterSend service UUID: `scanForPeripheralsWithServices:@[serviceUuid]`. It only sees peripherals whose advertisement carries that UUID — those are *connectable*. That's a deliberate design choice in [BleHandshake_Mac.mm:144–155](../cpp_core/src/BleHandshake_Mac.mm) — see the header comment "Filtering the scan by service UUID guarantees we only ever see the connectable peer."
- On Windows, **only** `GattServiceProvider::StartAdvertising` carries the service UUID. The other Windows BLE channel — `BluetoothLEAdvertisementPublisher` with `ManufacturerData` (CompanyId + magic + name) — does NOT include the service UUID, and is non-connectable anyway.
- `GattServiceProvider::StartAdvertising` only fires inside `ensureHostStarted` → `handshake->publishPayload(...)`.
- At `80b10a7`, `ensureHostStarted` is called inside the `onFound` callback — i.e., only after Windows sees the Mac on BLE.
- **But** the Mac's `BleDiscovery_Mac` continuously scans the BLE radio (`AllowDuplicates:YES`, `services:nil`) and the `BleHandshake_Mac` spins up its OWN `CBCentralManager` scanning the moment a peer is sighted — both running on Apple-silicon's shared BT/Wi-Fi antenna. That contention starves the Mac's own `CBPeripheralManager` advertise, so Windows' watcher often catches zero Mac packets in the first 20+ seconds.
- Windows never sees Mac → never calls `ensureHostStarted` → GATT service never advertises → Mac's handshake central never finds a connectable peer → `fetchPayload` times out at 20 s → "Empty GATT payload from <peer>".

Observed in the wild on `2026-06-07 17:55`:
```
17:55:28.769  [Mac][BleDisc] Found peer: name='NetanelTur'         ← Mac sees Windows
17:55:28.771  [Mac][API]     Client handshake with 'NetanelTur'    ← auto-join kicks in
17:55:28.771  [Mac][BleHand] fetchPayload: peerId=... timeout=20s  ← starts handshake central
...
17:55:48.774  [Mac][BleHand] fetchPayload timeout/empty            ← 20 s later, dead
17:55:48.774  [Mac][API]     Empty GATT payload from 'NetanelTur'

[Win][BleDisc] (no "Found peer:" line for the Mac at all)
```

### The fix

In [bettersend_api.cpp::bettersend_start_discovery](../cpp_core/src/bettersend_api.cpp), AFTER `ctx->discovery->startDiscovery(...)` returns, spawn `BetterSend::ensureHostStarted(*ctx, kDefaultPort)` on a **detached worker thread**:

```cpp
ctx->discovery->startDiscovery([ctx, onFound](BetterSend::Device d) {
    // ... existing onFound body (incl. the onFound-path ensureHostStarted retry) ...
});
BS_LOG_INFO("API", "Discovery started");

#if defined(_WIN32)
std::thread([ctx]() {
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
    } catch (const winrt::hresult_error&) { /* already initialized — fine */ }
    BS_LOG_INFO("API", "Eager host bring-up: start");
    BetterSend::ensureHostStarted(*ctx, BetterSend::kDefaultPort);
    BS_LOG_INFO("API", "Eager host bring-up: end");
}).detach();
#endif
```

And inside `ensureHostStarted`:
- A **`static std::mutex`** wraps the body so the eager call and the `onFound`-path retry can't race. The existing `if (ctx.isHosting) return;` is necessary but not sufficient under true concurrency.
- A `catch (const winrt::hresult_error& e)` clause is added (logs the HRESULT + message). `winrt::hresult_error` does NOT derive from `std::exception` in the locally-installed Windows SDK (verified at `winrt/base.h:4721` — `struct hresult_error` has no base class), so without this catch a WinRT failure escapes across the FFI boundary into undefined territory.
- A `catch (...)` clause is added as defense-in-depth.

### Why this fix is safe even though commits 0c14b89 + 74bc358 (which tried similar things) are gone

The commits we deleted made TWO mistakes that this targeted fix avoids:

| 0c14b89's mistake | This fix |
|---|---|
| Ran `ensureHostStarted` synchronously BEFORE `startDiscovery`, blocking the FFI thread for 5–60 s on `StartTetheringAsync().get()` and starving the watcher of any Mac adverts during that window. | Runs `ensureHostStarted` on a detached thread that has no relationship to the FFI thread. The watcher comes up in milliseconds. |
| Bundled with a Mac-side scan duty-cycle (55f2e28) that narrowed the advertise window to 1.5 s / 5.5 s — making "watcher starts late" fatal. | Touches Mac code zero. Mac stays at `80b10a7` baseline. |
| Bundled with a deferred-join refactor (9e57042) that left the Mac never auto-connecting to the Windows hotspot. | The auto-join behavior of `80b10a7`'s `clientHandshakeAndJoin` is preserved unchanged. |

The fix is ~25 lines in `bettersend_api.cpp` and nothing else.

### How to know it's working

In the C++ log on the Windows side, you should see, in order at startup:
```
[API] Discovery started
[API] Eager host bring-up: start
[HotspotBroker] startHost: bringing up Mobile Hotspot
[HotspotBroker] Hotspot up: ssid='...' hostIp=192.168.137.1 ...
[BleHand] publishPayload: <N> bytes
[BleHand] GATT service advertising
[API] Host phase up: ssid='...' hostIp=... port=9000
[API] Eager host bring-up: end
```
And within seconds of the Mac running BetterSend:
```
[BleDisc] Found peer: name='Yonatans-MacBook.local' addr=...
```
plus, in the Mac log:
```
[BleHand] Connected; discovering services
[BleHand] Read N bytes from peer
[WifiClient] Joined SSID '...'
[API] Hello from 'Yonatans-MacBook.local' @ 192.168.137.X
```

### Pre-flight check (do this before debugging anything else)

If logs show `[BleDisc] Watcher Start failed: 0x800710df — The device is not ready for use.` and/or `[BleDisc] Advertise status: 5` (Aborted), the **Windows Bluetooth radio is in a not-ready state**. This is almost always caused by one of:

1. **Windows Mobile Hotspot is currently `On`** — on Intel Wireless combo chips the AP-mode Wi-Fi clobbers the BT radio. The most common trigger is a previous run of the app's `ensureHostStarted` left tethering on, then BetterSend was relaunched. Turn it off in `Settings → Network → Mobile hotspot`, or run from PowerShell:
   ```powershell
   $prof = [Windows.Networking.Connectivity.NetworkInformation,Windows.Networking.Connectivity,ContentType=WindowsRuntime]::GetInternetConnectionProfile()
   $mgr  = [Windows.Networking.NetworkOperators.NetworkOperatorTetheringManager,Windows.Networking.NetworkOperators,ContentType=WindowsRuntime]::CreateFromConnectionProfile($prof)
   $op   = $mgr.StopTetheringAsync(); while ($op.Status -eq 0) { Start-Sleep -Milliseconds 200 }
   ```
2. **Bluetooth toggle is OFF in Settings** — turn it on BEFORE launching the app. If turned on mid-run the `Watcher.Start()` has already failed and the code does not retry.
3. **Airplane mode on**.

This is NOT a BetterSend bug — the code does the right thing and surfaces the underlying OS error. If you add a "retry watcher on radio-not-ready" loop later, scope it tight (a few attempts with backoff) so a genuinely-disabled radio doesn't burn a thread forever.

---

## What worked at `80b10a7` (the baseline we restored to)

- Mac sees Windows on BLE → spawns `clientHandshakeAndJoin` (single function, single thread).
- That function: GATT-reads `{ssid, psk, hostIp, port}` → calls `MacWifiClientBroker::joinNetwork` (direct `[CWInterface associateToNetwork:password:error:]` after an open `scanForNetworksWithSSID:nil`) → fires a TCP Hello to `hostIp:port`.
- Windows TCP server intercepts the Hello, records the Mac's hotspot-subnet IP on `PeerInfo.ip`.
- Both directions can now send files because both sides have a real IP for the other.
- The Mac peer also appears in Windows' nearby-devices list because the Mac's `CBPeripheralManager` advert was not being starved — the join completed quickly enough that the antenna contention window was short and bounded.

**Key invariant of the working state:** *one* Mac code path drives "see peer → join → ready to transfer". No deferred join. No pair-status state machine. No `pauseScan`/`pauseAdvertise` dance. No spinner.

---

## Reverted commits (newest → oldest) — what each one tried and why it didn't stick

### 1. `74bc358` — fix(discovery): start watcher before eager host bring-up on Windows

**Idea:** `bettersend_start_discovery` was calling `ensureHostStarted` BEFORE `discovery->startDiscovery`, blocking the FFI thread on `StartTetheringAsync().get()` for 5-60 s and leaving the `BluetoothLEAdvertisementWatcher` unstarted during that window. Reorder so the watcher comes up first.

**Why it didn't fix the user-facing symptom:** the watcher-start delay is real and the reorder is correct in isolation, but it didn't address the more fundamental issue — that the Mac side wasn't *transmitting* adverts that the Windows watcher could catch even when the watcher was up. The reorder addressed scenario "watcher was late," not scenario "Mac is silent."

**Don't repeat:** reordering alone is not enough. If the next regression is "Windows doesn't see Mac," the heartbeat (`BLE adverts in last 15s: total=X serviceMatch=Y`) added in this commit would have told us whether the watcher was hearing anything at all — that's the only useful artifact, and it's gone now since this commit is reverted. If you re-introduce it later, do it as a standalone diagnostic-only commit so you can keep it across architectural changes.

### 2. `55f2e28` — fix(scan duty cycle): implement duty-cycling for BLE scanning to improve advertising reception

**Idea:** On Apple silicon the BT and Wi-Fi share an antenna; a continuous `AllowDuplicates=YES` `CBCentralManager` scan starves the same chip's `CBPeripheralManager` advertise. Duty-cycle the scan (`kBleScanWindowMs=4000` / `kBleAdvertiseWindowMs=1500`) so the advertise gets antenna time.

**Why it didn't fix it:** the duty cycle was theoretically correct but the user (Windows) still reported zero Mac sightings. Either (a) the 27% advertise window was still too short on this hardware, or (b) the second `CBCentralManager` in `BleHandshake_Mac` (created when the first peer is seen and never `nil`-ed out even after `stop()`) was holding the radio anyway. Also folded into this commit was a `TcpTransport` `SO_REUSEADDR` fix that was orthogonal but real — if you re-do the TCP fix, isolate it from anything BLE-related.

**Don't repeat:** don't try to "fix antenna starvation in software" with duty-cycle gymnastics. The *architectural* fix is to not have a `CBCentralManager` scanning continuously in the first place — i.e., return to the synchronous handshake-then-join flow where the scan only runs while it has a job to do, then stops.

### 3. `0c14b89` — fix(discovery): start Windows host eagerly, not on peer-sight

**Idea:** BLE visibility was observed asymmetric: Mac sees Windows, but Windows doesn't see Mac (because Mac advertise was starved by Mac's own handshake central). Gating `ensureHostStarted` on peer-sight then deadlocked — no host → empty GATT → Mac keeps handshaking → Mac advert starved → host never sees Mac. So: bring up hotspot + GATT eagerly, the moment discovery starts.

**Why it didn't fix it:** moving `ensureHostStarted` to the top of `bettersend_start_discovery` BLOCKED the watcher startup for tens of seconds (see #1). The asymmetric-visibility diagnosis was correct, but the cure landed wrong.

**Don't repeat:** "do the slow WinRT thing first" is never the right ordering on the Dart-FFI thread. If you genuinely need eager host bring-up later, do it on a detached thread that calls `winrt::init_apartment(multi_threaded)` itself, and *after* `discovery->startDiscovery()` has constructed and started the watcher. Even then, prefer making the architecture asymmetry-safe so eager bring-up isn't needed.

### 4–6. `0c5c178`, `da2041a`, `0620b95` — Location Services delegate / `requestWhenInUseAuthorization` / prompt at broker construction

**Idea:** macOS 10.15+ needs Location Services authorization for `CWNetwork.ssid` to return a non-nil string. Without it, `CoreWLAN` returns dozens of networks but every `n.ssid` is nil, so the target SSID never matches. The progression: ask for `Always` → ask for `WhenInUse` → wire up a `BSLocationDelegate` so the prompt reliably surfaces → fire the prompt at broker construction so the dialog is visible before any pair attempt.

**Why we don't need them at `80b10a7`:** `80b10a7` already used direct `CoreWLAN` associate and a one-shot `scanForNetworksWithSSID:nil` per peer-sight. The SSID readout worked because `80b10a7`'s test environment had Location Services already granted (or the macOS version was permissive enough). If Yonatan's macOS update later forces the gate, the right move is to add the delegate + prompt **as a small isolated commit on top of the working baseline**, not as part of a larger architectural pivot.

**Don't repeat:** don't bundle "permission/UX polish" with "architecture change." Location auth was a real, separate issue — but it got tangled in the deferred-join regression and we lost the ability to isolate it.

### 7. `5b1bb28` — fix(mac wifi join): log visible SSIDs when target SSID is not found

**Idea:** diagnostic-only — dump every visible SSID when the open scan doesn't find the target.

**Why it's gone:** purely diagnostic, no behaviour change. Cheap to re-add if needed.

**Don't repeat:** there's nothing to repeat — but if you do re-add, do it in its own commit so it survives future reverts.

### 8. `883f38b` — fix(mac wifi join): drop CoreWLAN power-cycle — hangs `scanForNetworks`

**Idea:** an earlier attempt (`89e89a3`) power-cycled the CoreWLAN interface (`setPower:NO` then `setPower:YES`) before scanning, on the theory that a fresh power-on would clear "Resource busy". Reverted in this commit because the `setPower:NO` itself hangs on `scanForNetworks` indefinitely.

**Don't repeat:** **never call `[CWInterface setPower:NO]` followed by a scan.** It hangs the whole flow. We learned this twice; let's not learn it a third time.

### 9. `f901d94` — fix(hotspot): force 2.4 GHz band before StartTethering

**Idea:** Mac's CoreWLAN passive-scans 5 GHz DFS channels and misses a freshly-started Windows hotspot in our pairing window. Forcing `TetheringWiFiBand::TwoPointFourGigahertz` via `ConfigureAccessPointAsync` works around it.

**Why it's gone with the revert:** at `80b10a7`, the Windows hotspot was working without forcing the band (presumably the default already was 2.4 GHz, or the Mac side could find a 5 GHz hotspot fine in that test).

**Don't repeat carelessly:** if at some later point Mac side reports "open scan finds zero networks for ~10 s after Windows turns on hotspot", THIS is the fix to come back to — but isolate it. `ConfigureAccessPointAsync` requires tethering to be Off; reconfiguring while On hangs. Do `StopTetheringAsync().get()` → `ConfigureAccessPointAsync(config).get()` → `StartTetheringAsync().get()`, in that exact order, each with its own try/catch around `winrt::hresult_error`.

### 10. `89e89a3` — fix(mac wifi join): power-cycle CoreWLAN, longer settle, DHCP wait

**Don't repeat:** this is the one that introduced the `setPower:NO` hang documented in #8. **Power-cycling CoreWLAN does not clear "Resource busy" and does break `scanForNetworks`.** The actual root cause of "Resource busy" was the *still-allocated* `CBCentralManager` holding the BT framework — see #14.

### 11. `fa06a61` — fix: update Wi-Fi join attempts and backoff timing

**Don't repeat in isolation:** raising retry counts / backoff windows treats the symptom (intermittent join failures), not the cause. Find the cause first.

### 12. `80d7d27` — fix: adjust Wi-Fi join retry logic and timeout for better stability

**Don't repeat in isolation:** same as #11.

### 13. `26629a6` — feat(hello payload): add Mac-only hello payload for TCP connection

**Idea:** the Mac client TCP-sends a `BetterSendHello\0<name>` magic so the Windows host can learn the Mac's hotspot-subnet IP via `socket.remote_endpoint()`. The Windows API-layer intercepts this body (suppresses surfacing to UI) and records `peer.ip`.

**Why it's gone:** `80b10a7` already had a Hello mechanism functionally equivalent (the older `clientHandshakeAndJoin` path's Hello loop). If we ever need to reintroduce a Hello-only commit on top of the baseline, this one was clean — but make sure the host-side interception and the wire format are still in agreement.

**Don't repeat:** if you reintroduce a Hello payload, keep the host-side `isHelloPayload` interceptor in `bettersend_start_server`'s onReceive closure; otherwise it surfaces as a "Clipboard" transfer to the UI.

### 14. `b37adf1` — fix(BLE pause): nil-out CBManagers + 2 s settle to clear "Resource busy"

**Idea:** `stopScan` / `stopAdvertising` alone weren't enough on Apple silicon to clear the BT framework's hold on the shared radio when CoreWLAN started its open scan. The `CBCentralManager` / `CBPeripheralManager` *objects* — even after stop — kept the BT subsystem holding the radio. Fix: drop the manager references on `pauseScan`/`pauseAdvertise` (ARC dealloc → BT subsystem releases), re-allocate them on `pauseScan` / `resumeAdvertise`, bump the post-pause sleep from 500 ms to 2 s.

**Why it's gone with the revert:** the `pauseScan`/`pauseAdvertise` machinery only exists at all because of the deferred-join split (#16). At `80b10a7` the handshake + join + Hello run synchronously in one thread; the radio is contested only briefly and CoreWLAN doesn't need a "pause BLE first" dance.

**Important learning to preserve:** **if you ever do need to release the BT radio so CoreWLAN can scan on Apple silicon, `stopScan`/`stopAdvertising` is NOT enough. The `CBCentralManager` / `CBPeripheralManager` object must go `nil` (ARC dealloc), and you need ~2 s of settle before CoreWLAN's scan.** This is a real OS-level constraint we paid for in hours of debugging — write it down even if the code that exploits it is gone.

### 15. `2fc1f88` — fix(pair flow): pause BLE during join, Windows immediate-paired, Cancel button

**Idea:** three follow-ups to #16 — pause BLE before `broker->joinNetwork` (the "Resource busy" workaround), make the Windows side of `bettersend_request_pair` fire `kPairPaired` immediately (Windows is by definition already on its own hotspot), and add a Cancel button on the spinner.

**Why it's gone:** all three are only needed because of the deferred-join split (#16). If we reintroduce a spinner later, the Cancel-button pattern (`paired`/`failed` status arriving after the user dismissed the spinner just updates background state, never resurrects the dialog) is a nice UX pattern worth re-using.

**Don't repeat:** the "Windows immediately paired" path was a *workaround for the spinner staring forever* — it skipped the spinner entirely on Windows. That meant Windows never ran any "wait for Mac to join" code, so when the Mac later did join, the IP was learned via the Hello mechanism, not the spinner state. If you reintroduce a spinner UI, mirror the host-side flow with the runHostPairWorker poll, not a fake "instantly paired" shortcut.

### 16. `9e57042` — feat: deferred Wi-Fi join + per-transfer save path picker

**Idea:** previously the Mac auto-joined every visible peer's hotspot the moment GATT credentials arrived. That breaks as soon as a second peer is visible — racing joins scramble Wi-Fi state and one transfer cancels another mid-flight. Split into: `clientHandshakeOnly` (caches `{ssid, psk, hostIp, port}` on the peer record), and `runPairWorker` (driven by a user-tapped `bettersend_request_pair`, fires status callbacks `joining → hello → paired | failed`).

**Why it broke everything (the central failure of this saga):**
1. **Mac stays on its own Wi-Fi indefinitely** until the user taps. Without a join, the Mac's `CBCentralManager` is the only thing on the shared antenna doing anything bursty, and its continuous `AllowDuplicates` scan starves the Mac's `CBPeripheralManager` advertise so badly that Windows' watcher never catches one — hence the user's "I don't see Yonatan in nearby devices."
2. **The "loading" UI is purely cosmetic** — it tells the user a flow is happening, but doesn't change the radio contention.
3. **Every commit that came after this one** (#1 through #15) was a symptom-chasing attempt to recover the lost "Windows sees Mac" behaviour by tweaking the duty cycle, the host bring-up order, the location prompt, etc. — none of them succeeded.

**Don't repeat:** **do not split discovery from join into two user-initiated steps for the Phase 1 Mac↔Windows pair.** Phase 1 has exactly two devices; the multi-peer-race motivation does not apply. Auto-join on peer sight is the right model. If/when Phase 4 brings real multi-peer, the right way to scope deferred-join is per-peer cooldown + a "primary peer" lock — not "user must tap to even attempt".

The per-transfer save path picker (the other half of this commit) is a *good* feature and unrelated to the regression. If we re-add it, do it as its own commit on top of the working baseline.

---

## Cross-cutting "DO NOT repeat" list

These are the load-bearing learnings that survived the revert. Pin them in `.wolf/cerebrum.md` so they outlive the next refactor:

1. **`[CWInterface setPower:NO]` hangs `scanForNetworks`.** Do not power-cycle CoreWLAN as a "fresh start" hack.
2. **On Apple silicon, `stopScan` / `stopAdvertising` does NOT release the BT radio.** The `CBCentralManager` / `CBPeripheralManager` object must go `nil` (ARC dealloc) and you need ~2 s of settle before CoreWLAN can scan without "Resource busy".
3. **`StartTetheringAsync().get()` blocks the calling thread for 5-60 s.** Never call it (or any other `.get()` on a `WinRT` async) on the Dart-FFI thread synchronously before the `BluetoothLEAdvertisementWatcher.Start()`.
4. **`winrt::hresult_error` does NOT derive from `std::exception` in the locally-installed SDK.** Any function exposed across the `extern "C"` FFI boundary that touches WinRT must catch `winrt::hresult_error` AND `...`, in addition to `std::exception`. Otherwise a WinRT failure tears down the function silently across the FFI.
5. **`asio::ip::tcp::acceptor`'s endpoint-taking constructor binds immediately.** Setting `SO_REUSEADDR` after that is too late; the relaunch inside `TIME_WAIT` fails with "Address already in use" and the server silently doesn't listen. Order: `open` → `set_option(reuse_address)` → `bind` → `listen`.
6. **Mac CoreWLAN scan filtered by SSID (`scanForNetworksWithName:`) is a `Resource busy` dead-end on Apple silicon.** Always use `scanForNetworksWithSSID:nil` (open scan) and filter in code.
7. **macOS 10.15+ requires Location Services for `CWNetwork.ssid` readout.** Without it the scan returns every network with `ssid == nil` — looks like "target not visible". A `CLLocationManager` without a delegate is not enough; the prompt is unreliable until you set a `delegate`.
8. **Windows Mobile Hotspot via `NetworkOperatorTetheringManager` requires a non-null `InternetConnectionProfile`** even though the hotspot itself works without an upstream link. If the dev machine is fully offline, `GetInternetConnectionProfile()` returns null and we throw. Lower-level `WlanHostedNetwork` may bypass this in Phase 4.
9. **`ConfigureAccessPointAsync` requires tethering to be Off.** If you ever need to change the hotspot band, do `StopTetheringAsync().get()` → `ConfigureAccessPointAsync().get()` → `StartTetheringAsync().get()`, in that exact order.
10. **Phase 1 has exactly two devices.** Multi-peer concerns are Phase 4. Don't pre-design for them; it cost us 19 commits and a working transfer.
