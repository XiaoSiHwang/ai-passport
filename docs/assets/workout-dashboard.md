**English** · [简体中文](workout-dashboard.zh_CN.md)

# Workout dashboard

This application boots directly into its own 240 × 320 mosaic dashboard. It
reuses the BSP, not the hardware-test menu or demo visual shell. The unchanged
default 8 MB partition layout stores network settings and one verified workout
snapshot in NVS; no filesystem partition is needed.

## Controls and pages

| Page | Up / Down | OK | Long OK |
| --- | --- | --- | --- |
| Dashboard / details | Switch week and month | Open / close details | Page menu |
| Page menu | Select an entry | Enter the selected page | Dashboard |
| Network | Switch the two QR steps | Start setup, or switch steps while active | Stop setup and return to menu |
| More pages | Reserved | Reserved | Page menu |

The menu contains the dashboard, network configuration, manual synchronization,
and a reserved page for future functions. Long Down on the network page opens a
confirmation to clear Wi-Fi and the server address. OK confirms; Up/Down cancels.
Clearing network configuration retains the last workout snapshot.

The three buttons are physical controls, not on-screen buttons. Idle backlight
brightness drops to 20% after 30 seconds and turns off after 60 seconds. The
first key wakes the screen without executing an action. Wi-Fi modem power saving
and automatic light sleep are enabled; actual board power and USB behavior need
device measurement. Audio and Bluetooth services are not started.

## Configure Wi-Fi and the interface

With no saved settings, the device automatically starts a temporary WPA2 hotspot.
Open **Network** from the page menu to see the setup codes. Existing settings
reconnect automatically; reconfiguration must be opened from this menu.

1. Scan the first QR code with the phone's camera to join the device hotspot.
   Its session-specific name and random 12-character password also appear on the
   device, so the phone can join manually when automatic QR joining is unavailable.
2. Press Up, Down or OK to show the second QR code. Keep the phone connected to
   the hotspot and scan this code to open the local configuration page.
3. Enter the 2.4 GHz Wi-Fi SSID, password and server address. Open Wi-Fi networks
   accept an empty password. SSIDs are limited to 32 UTF-8 bytes and passwords to
   8–63 bytes when nonempty. The phone page supports Chinese SSIDs; the device
   deliberately does not display arbitrary SSID text using its fixed font subset.
4. Use the computer's reachable LAN address, for example
   `http://192.168.1.20:8000`, or its full `/api/workout` URL. Base addresses are
   normalized by appending `/api/workout`. Do not use `localhost` or `127.0.0.1`.
   IPv4/DNS hosts, optional ports and HTTP/HTTPS are supported; IPv6 literals,
   embedded credentials, query strings and fragments are rejected.
5. Wait for association and an IP address. The device verifies the candidate
   connection for up to 20 seconds, then commits the whole configuration. A failed
   attempt preserves the previous configuration and reconnects to it. The phone
   may lose the hotspot while APSTA changes radio channels: check the device's
   result before submitting again.

The backend must listen on its LAN interface (`0.0.0.0` rather than loopback),
and the device must be able to reach the server through the router/firewall.
Saving Wi-Fi settings confirms a Wi-Fi connection; successful data synchronization
is a separate result. HTTPS validates certificates using the ESP-IDF CA bundle
and first synchronizes the clock with `pool.ntp.org` when needed.

Setup closes 10 seconds after a successful save, immediately on leaving the
network page, or after a 10-minute timeout. The local page requires the random
session token carried by the second QR code. Credentials and QR parameters are
never logged or compiled into the firmware. No public web service or phone app
is required; automatic captive-portal popups are not implemented.

## Data, offline behavior and failure handling

The client requests `GET /api/workout` every five minutes while connected;
transient errors retry after 30 seconds. The worker handles HTTP, parsing and
Flash writes separately from input/UI processing. Responses are limited to 4 KB
and 10 nesting levels, with bounded JSON complexity. Only a successful envelope
with complete, valid month/week summaries is accepted. Month buckets are date
ranges 1–7, 8–14 and so on, not ISO weeks. Weekly buckets cover Monday–Sunday and
may cross month/year boundaries.

Only distances, durations, paces, goals, buckets, dates and the upstream update
time are retained. Invalid JSON, failed requests or a response older than the
current snapshot do not replace the last valid data. Zero distance, zero goals
and an unavailable battery reading have explicit display fallbacks.

Snapshots have a schema version, source identity and CRC. A single committed NVS
blob preserves the previous value across interrupted writes under NVS transaction
semantics. Boot reads the blob before requesting fresh data. An unchanged response
is not rewritten; failed cache writes are retried on a later synchronization.
Cached snapshots are intentionally retained without an expiry to satisfy offline
use across shutdowns. Their dates remain unchanged, and the display shows an
offline/old-data state and last synchronization time. There is only one snapshot,
so storage cannot grow with history. Changing the server hides a previous source's
snapshot until the new source succeeds; clearing network settings keeps it.

The interface accepts Chinese only from the verified fixed UI inventory. All
text widgets/draw labels explicitly select 12/16/20 px subset fonts; the large
numbers use an original 35 px pixel font. See the [font assets](../../assets/README.md).

## Validation and device acceptance

Run `./tools/validate.sh`. The static gate covers navigation, dates, URL bounds,
CRC and failed storage commits. The firmware gate also tests the actual decoder
against ESP-IDF's cJSON and renders the actual UI/fonts/QR widgets with LVGL 9.5,
including repeated page/QR cleanup with a 24 KB LVGL pool. These are software
checks; they do not establish board timing, Wi-Fi range, power consumption or
physical display quality.

On the device, verify both QR scans, manual joining, wrong-password recovery,
router absence, IP acquisition, backend 200/502/timeout behavior, cache-write
failure reporting, disconnected restart, power interruption while saving, server
changes, credential clearing and repeated setup entry/exit. Inspect Chinese,
numbers, all page states and rounded screen boundaries; record free heap/largest
block with Wi-Fi, HTTP, HTTPS and the phone connected. A successful flash alone
does not constitute passing these checks.

Use the verified merged image for an intentional full refresh. A merged flash
can reset NVS; preserving configuration/cache requires compatible segmented
flashing. Follow the [firmware data policy](../development/engineering/firmware-layout.md#flashing-and-stored-data).
