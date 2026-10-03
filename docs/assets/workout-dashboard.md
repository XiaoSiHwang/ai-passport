**English** · [简体中文](workout-dashboard.zh_CN.md)

# Workout dashboard

This application boots into its own 240 × 320 clock and calendar home screen. It
reuses the BSP, not the hardware-test menu or demo visual shell. The unchanged
default 8 MB partition layout stores network settings and one verified workout
snapshot and independent AI snapshots in NVS; no filesystem partition is needed.

All device pages use the fixed light preview palette: a pale background, light-green
panels, dark-green text and mosaic marks. Host light/dark appearance does not change
the firmware palette.

## Controls and pages

| Page | Up / Down | OK | Long OK |
| --- | --- | --- | --- |
| Home | Select calendar, Codex quota or GLM quota | Open month calendar or selected quota | Page menu |
| Month calendar / almanac | Previous / next day | Switch calendar / almanac | Page menu |
| Workout dashboard / details | Switch week and month | Open / close details | Page menu |
| AI quota pages | Switch Codex / GLM; return to five hours | Cycle five hours, seven days, Token heatmap | Page menu |
| Token heatmap | Previous / next date, wrapping within 30 days | Return to five-hour quota | Page menu |
| Page menu | Select an entry | Enter the selected page | Home |
| Network and interface | Select Wi-Fi, server or add configuration | Open selection/setup | Page menu |
| Wi-Fi / server list | Select a saved entry; three rows per page | Switch selection; an empty list opens setup | Network and interface |
| Connection status | Select reconnect/sync or add configuration | Run the selected action | Network and interface |
| Setup QR codes | Switch the two QR steps | Start setup, or switch steps while active | Stop setup and return to network settings |
| Codex tasks / details | Switch the three returned tasks | Open / close details; retry a failed sync | Page menu |
| Approval reminder | No page action | Dismiss locally | Dismiss locally |

The menu order is home, workout dashboard, AI usage, Codex tasks, manual synchronization,
and settings. Settings opens the network/interface hub; manual sync returns to home.
Long Down on the network page opens a
confirmation to clear all saved Wi-Fi networks and server addresses. OK confirms;
Up/Down cancels and closes provisioning. Long Down is also available on the setup page.
Clearing network configuration retains the workout and AI snapshots.

The three buttons are physical controls, not on-screen buttons. Idle backlight
brightness drops to 20% after 30 seconds and turns off after 60 seconds. The
first key wakes the screen without executing an action. Wi-Fi modem power saving
and automatic light sleep are enabled; actual board power and USB behavior need
device measurement. Audio and Bluetooth services are not started.

## Clock, calendar and almanac

Home shows time, Gregorian date/weekday, Chinese lunar date, festival/holiday
labels, auspicious-day classification and short suitable/unsuitable activity
excerpts. It has no running-distance summary. The two five-hour AI quota values
remain below: unavailable is `--`, exhausted is `0%`, and retained values are
labeled cached. The month calendar displays lunar days, festivals/solar terms,
holidays and adjusted workdays. Up/Down moves one day across month/year boundaries;
OK opens or closes the selected day's almanac. Almanac lists show up to six
activities with ellipses for longer lists and an explicit folk-reference/excerpt label.

Time uses China standard time (UTC+8), independently of API timestamp offsets.
After obtaining an IP address, the network worker starts background SNTP through
`pool.ntp.org` for HTTP and HTTPS configurations. Failed clock sync does not block
HTTP data; HTTPS retains its certificate-time requirement. Until a plausible clock
is available, home displays unknown time and calibration labels. Minute/date
changes refresh without an API snapshot. Midnight updates home while retaining
a date being inspected; re-entering the calendar selects the new current day.
Once synchronized, the clock and calendar continue offline. A cold power cycle
requires synchronization again. The backlight policy remains unchanged.

Read-only calendar data covers 2000–2099 in Flash, without daily network requests
or NVS writes. Lunar dates, solar terms, deities and activity lists are generated
from [lunar-javascript 1.7.7](https://github.com/6tail/lunar-javascript), under its
[MIT license](../../main/passport_calendar_license.txt). Classification follows
the library's midnight day convention and is traditional folk reference.
Annual holidays and adjusted workdays are supplied only for 2026, according to the
[State Council arrangements](https://zwfw.gansu.gov.cn/huixian/zczx/tzgg/art/2025/art_715c16a75e4d4c289c295e77772c7274.html).
Other years show fixed/lunar festivals and solar terms without guessing statutory
adjustments. Updating annual arrangements requires a firmware update.

Regenerate the calendar before the font inventory. The generator verifies the
pinned source SHA-256 and deduplicates activity strings:

```text
curl -fL https://cdn.jsdelivr.net/npm/lunar-javascript@1.7.7/lunar.js -o /tmp/passport-lunar.js
node tools/generate_passport_calendar.js /tmp/passport-lunar.js
python3 tools/generate_workout_fonts.py --font <NotoSansCJKsc-Regular.otf> --converter <lv_font_conv.js>
```

## Configure Wi-Fi and the interface

With no saved settings, the device automatically starts a temporary WPA2 hotspot.
Open **Network and interface**, then **Add configuration** to see the setup codes.
Existing settings reconnect automatically; reconfiguration is opened from this menu.

1. Scan the first QR code with the phone's camera to join the device hotspot.
   Its session-specific name and random 12-character password also appear on the
   device, so the phone can join manually when automatic QR joining is unavailable.
2. Press Up, Down or OK to show the second QR code. Keep the phone connected to
   the hotspot and scan this code to open the local configuration page.
3. Enter the 2.4 GHz Wi-Fi SSID, password and server address. Open Wi-Fi networks
   accept an empty password. SSIDs are limited to 32 UTF-8 bytes and passwords to
   8–63 bytes when nonempty. The device displays ASCII and CJK glyphs included
   in its separate 16 px network font. Unsupported characters, including emoji,
   appear as explicit Unicode codes instead of missing-glyph boxes.
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
setup page, or after a 10-minute timeout. The local page requires the random
session token carried by the second QR code. Credentials and QR parameters are
never logged or compiled into the firmware. No public web service or phone app
is required; automatic captive-portal popups are not implemented.

## Saved networks and servers

Successful provisioning retains independent histories of up to five Wi-Fi networks
and five complete, normalized server URLs. An existing SSID updates its password;
an existing URL is reused. At capacity, adding a new item replaces the oldest item
other than the current selection. New configuration becomes active only after an
IP address is obtained and the whole history blob is saved.

In **Network and interface**, choose **Switch Wi-Fi** or **Switch server**.
Up/Down moves the highlighted row; OK switches; Long OK returns. The current
selection has a separate text marker. Long names are truncated in rows and shown
in full by a scrolling label below the list. Switching Wi-Fi keeps the server;
switching server keeps the Wi-Fi. Choosing the current server requests fresh data
without rewriting its preference. Servers can also be selected while offline.

On startup, disconnection or lost IP, the worker tries the last successful Wi-Fi,
then the other saved networks, allowing 20 seconds for each IP acquisition. After
all fail it pauses for 30 seconds before starting another round. Automatic cycling
pauses while the setup hotspot is active to avoid unexpected channel changes.
Manual Wi-Fi selection gets one 20-second attempt; on connection or preference-save
failure the worker restores the previous selection and resumes automatic recovery.
An automatically recovered connection stays usable if preference storage fails;
the worker retries that write every 30 seconds.

Disconnection, or exhausting one complete retry round, while viewing the dashboard
opens the connection status page with retry and add-configuration actions. Other pages and the idle backlight
policy remain usable. A server failure does not rotate Wi-Fi networks. An IP
address does not prove Internet access; this version does not implement a separate
Internet probe or automatically change servers. Server selection switches workout,
Codex and GLM endpoints together, hides old-source data and requests fresh data.

The versioned, checksummed `profiles` NVS blob holds both histories and active
indices. If absent, the old `network` configuration is inherited in memory;
the next changed/successful configuration writes the new blob. Corrupt or
unsupported history reports a storage failure instead of silently falling back
to legacy settings. Workout/AI caches and partitions keep their formats. The
legacy key remains for migration; older firmware cannot read the history and may
see its old single configuration. Explicit clearing removes both keys and all
history, retaining caches. Networks overwritten before upgrading cannot be recovered.

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
offline/old-data state and last synchronization time. There is only one workout snapshot,
so storage cannot grow with history. Changing the server hides a previous source's
snapshot until the new source succeeds; clearing network settings keeps it.

Fixed Chinese UI text uses the verified inventory, including punctuation. Labels
explicitly select 12/16/20 px subset fonts; large numbers use an original 35 px
pixel font; the clock uses 50 px Noto Sans digits. Dynamic names use a separate 16 px font stored in Flash with explicit
Unicode-code fallback. See the [font assets](../../assets/README.md).

## AI usage

The quota-first page shows remaining and used quota, a 20-block remaining-quota meter,
reset time with its API time-zone offset, and separate quota/Token fetch times.
Five-hour quota includes today's cumulative Tokens; seven-day quota includes the
last 30 natural days, including today. These are independent statistics, not
Tokens consumed within five hours or seven days. The long quota remains seven
days: no monthly quota or rolling 24-hour Token statistic is inferred.
Up/Down switches Codex/GLM; OK cycles five hours, seven days and the Token heatmap. Enter AI usage
from its home summary or the menu. No computer-resource page
or `/api/system` request is included.

The existing stored `/api/workout` URL remains unchanged. Replace only that suffix
with `/api/codex/quota`, `/api/glm/quota`, `/api/codex/tokens` or `/api/glm/tokens`, preserving scheme, host, port and any
reverse-proxy prefix. No new Wi-Fi setup or provider credential is needed on the
device; provider credentials remain managed by the backend.

Quota and Token endpoints refresh independently every minute on all pages, retry failures after 30 seconds,
and run one request per worker tick to handle configuration commands between requests.
Workout refreshes every five minutes. Startup/reconnection and manual sync fetch both
workout, quota and Token data immediately; request duration can delay the next refresh slightly.
One failed provider/workout request does not stop the others. Missing windows show
`--`; an available zero means exhausted. Unknown reset times are labeled unavailable.
Plan names outside the printable ASCII subset are omitted without rejecting the data.

The heatmap shows a Monday-first calendar grid for the backend's 30-day window,
using up to six rows across month/year boundaries. Colors use shared thresholds:
zero, below 200K, below 500K, below 1M, and at least 1M. Missing days have diagonal
marks and an explicit unknown label when selected; a known zero remains zero.
Up/Down selects a date, whose usage is shown below the grid in K, M or 100-million units; the latest
two date buckets remain visible below it. Headline/recent amounts use K, M and
100-million units, rounded to at most two decimals with promotion at boundaries;
exceptionally large recent amounts use whole 100-million units and, if needed,
day-of-month labels to fit. The window and selected date retain their month labels.
The Codex source retains its original date buckets and reports an unspecified
statistics timezone; GLM uses Asia/Shanghai. Today is the window's final bucket,
not a sliding 24 hours. A synchronized device clock suppresses yesterday's
cached amount after midnight until a new window arrives. Before clock sync,
retained daily data is labeled recent rather than today. Partial totals show
the known-day count and an incomplete-data notice; all-unknown totals show `--`.
Token failures retain quota independently and mark retained Token data old.

Each provider has versioned, source-bound, CRC-checked NVS blobs (`codex`, `glm`,
`codex_tokens`, `glm_tokens`). Token blobs use separate keys; existing quota,
workout and network data formats and the partition layout are unchanged.
Invalid or older responses retain its last valid data. Offline/restarted devices keep
snapshots indefinitely and show old data. Failed commits keep the displayed data and
retry storage on a later successful fetch; unchanged snapshots are not rewritten.
Changing the server hides old-source snapshots; clearing settings keeps snapshots.
Workout/AI blob formats and partitions remain compatible. The new network-history
schema inherits the old single configuration as described above; a compatible
segmented firmware update can retain it.

## Codex tasks and approval reminders

Open **Codex tasks** from the page menu. The approved light layout shows one
task at a time: project, title, status, current step, recorded elapsed time,
last successful fetch and position. Up/Down switches tasks; OK opens details.
Selection follows the task ID when backend priority ordering changes. The
backend returns at most three tasks; details report the omitted count. A title
that exceeds four detail lines scrolls in full. Boot opens the clock/calendar home.

The saved workout URL derives `/api/codex/tasks` and `/api/codex/alerts`, preserving
its origin and reverse-proxy prefix. Both poll every five seconds with a three-second
HTTP timeout, independently of workout/quota/Token results. All seven endpoints share the
existing worker, with one request per tick, two monitoring turns between legacy
requests (including Token queries) and round-robin scheduling within each group to prevent starvation.
Slow requests and HTTPS clock synchronization can delay polling; the
end-to-end latency needs measurement on the device. A missing monitor endpoint
retries after 30 seconds; other failures retry after ten seconds. OK on a failed
monitor page requests only monitor retries.

The schema-version-1 decoder accepts the backend's bounded UTF-8 envelopes,
31-bit revisions/cursors, three tasks and five alerts per page. Malformed,
oversized, unsupported-schema or older-revision responses retain the previous
RAM snapshot. Wi-Fi loss, backend failure, host offline, backend `fresh=false`,
unknown approval or a snapshot older than 15 seconds is explicitly non-live.
The header describes collection/synchronization health separately from the task
badge: a confirmed ended/interrupted turn retains its final state and duration
even when the collector is non-live. Silence never marks a task ended.

Each of the three displayed tasks has a RAM clock keyed by stream, task ID and
start time (400 bytes total). Its first snapshot supplies the elapsed baseline;
running and approval-waiting turns then advance every second on the device's
monotonic clock. Refreshes preserve the subsecond phase and only correct active
timers forwards. Polling delays, failed requests, Wi-Fi loss and incomplete
collector evidence do not freeze or rewind the estimate; the UI identifies
non-live timing as a local estimate. Unknown state/approval freezes at the last
recorded update. An ended/interrupted snapshot replaces the estimate with its
authoritative stop time and stops the clock. Switching tasks preserves clocks;
new turns, streams, removed tasks and source resets discard previous baselines.
Clock differences can affect the estimate, and invalid or reversed timestamps
display `--`. Ended/interrupted means the turn stopped, not that the user's
requirement succeeded.

First connection and device reboot request alerts without `after`, establishing
a silent cursor baseline. Existing approval requests remain visible in task state.
Short disconnections keep the cursor; `has_more` reads the next page promptly.
Only a new `requested`, `fresh=true` alert wakes the screen and opens the amber
reminder over the current page. Cursor ordering and a bounded recent-ID set prevent
replays. A full five-item notification queue stops cursor advancement until it
can accept the remaining items. Resolved, superseded, unknown and stale events
advance the cursor silently. A 410 or changed stream rebuilds the baseline and
refreshes tasks; switching/clearing the server discards the old stream and queued
notifications.

OK or Long OK only closes the reminder and returns to the underlying page;
it does not approve, deny or send a decision to Codex. The task badge remains until
the backend changes its state. A later snapshot can dismiss a reminder whose
request is resolved/superseded/unknown; offline or a later failed/stale task read closes an
open reminder. Queued alerts older than 15 seconds cannot wake the device. New
valid reminders reset the normal 30/60-second backlight policy. Task data, cursors
and reminders stay in bounded RAM and are not written to NVS; reboot establishes
a new silent baseline. Existing workout, quota and network storage formats stay
compatible. No collector Hooks, desktop trust or backend credentials are changed
by the firmware.

Dynamic projects, titles, steps and summaries use matching 12/16 px CJK fallback
fonts. Unsupported characters or glyphs exceeding the row's vertical metrics
show an explicit Unicode code. Lists/popups truncate with ellipses; details show
the full supported title. Read endpoints follow the backend's trusted-LAN model;
public deployment requires a separate authentication design.

## Validation and device acceptance

Run `./tools/validate.sh`. The static gate covers navigation, dates, URL bounds,
CRC and failed storage commits. The firmware gate also tests the actual decoder
against ESP-IDF's cJSON and renders the actual UI/fonts/QR widgets with LVGL 9.5,
including repeated page/QR cleanup with a 24 KB LVGL pool. These are software
checks; they do not establish board timing, Wi-Fi range, power consumption or
physical display quality.

Calendar checks cover all supported Gregorian dates, leap months, solar terms,
festival/holiday/workday boundaries, auspicious and non-auspicious dates, UTC+8
midnight, unknown clocks, navigation bounds and HTTP background clock startup.
LVGL renders cover home, lunar dates, holidays, six-week months, almanac excerpts,
missing/exhausted quotas and minute changes. Repeated calendar/page transitions
retain the 24 KB pool without growth. Device checks must verify actual SNTP,
midnight, offline timing, Chinese glyphs, rounded corners and physical keys.

The gate also covers history bounds/deduplication/migration, independent selections,
failed commits, retry/cooldown timing, UTF-8 decoding, and fault injection against
the real worker for stale DHCP/disconnect events, manual recovery, lost IP, setup
pauses, source-cache invalidation and preference-save retries. LVGL rendering covers
the hub, lists, pagination, long/CJK/unsupported names and offline/storage/server
failure states within the same 24 KB pool. The main task uses an 8 KB stack for
nested startup profile reads/validation; the network worker retains its 7 KB stack.
Minimum heap and stack high-water marks still require device checks.

The gate also covers quota URL derivation, CRC/provider isolation, failed commits,
actual cJSON decoding, and LVGL rendering of both providers, weekly windows,
exhausted/missing quota, unknown resets, offline data and unavailable providers.
Device acceptance must exercise those states, provider/period navigation, the
six-item menu, real API responses, offline restart and interrupted writes. Inspect
Chinese glyphs and heap/stack behavior during repeated requests and provisioning.

Codex checks cover actual backend response fixtures, malformed/duplicate fields,
UTF-8/schema/size/count bounds, cursor baselines/replay/expiry, queue backpressure,
source changes, independent scheduling, stale data, selection by ID, continuous
local timing through delayed/failed polling and approval waits, final stop times,
clock identity/reset behavior and the actual
app's local-only dismissal/wake/button-callback boundary. LVGL rendering covers
tasks/details, approval/unknown/ended/interrupted, offline/failed/empty states,
dynamic Chinese, unsupported glyphs, full-title scrolling, timer pixel changes
without a new snapshot and fixed ended-turn pixels within the 24 KB
pool. Device acceptance still needs real collector events, readable Chinese and
screen boundaries, cross-page wake, duplicate suppression, reconnect/410 recovery,
source switching, endpoint failures, measured latency and heap/stack high-water
marks with the new monitoring workload.

On the device, verify both QR scans, manual joining, wrong-password recovery,
router absence, IP acquisition, backend 200/502/timeout behavior, cache-write
failure reporting, disconnected restart, power interruption while saving, server
changes, credential clearing and repeated setup entry/exit. Also test moving between
routers, DHCP timeout, wrong saved passwords, manual switching during automatic
recovery and reboot persistence of independent selections. Inspect Chinese,
numbers, all page states and rounded screen boundaries; record free heap/largest
block with Wi-Fi, HTTP, HTTPS and the phone connected. A successful flash alone
does not constitute passing these checks.

Use the verified merged image for an intentional full refresh. A merged flash
can reset NVS; preserving configuration/cache requires compatible segmented
flashing. Follow the [firmware data policy](../development/engineering/firmware-layout.md#flashing-and-stored-data).
