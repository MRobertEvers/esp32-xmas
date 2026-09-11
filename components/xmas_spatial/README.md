# Relative ornament layout with ESP32-S3 Wi-Fi FTM

`xmas_spatial` discovers **four or more** participating boards on an existing
Wi-Fi LAN, collects pairwise Fine Timing Measurement (FTM) distances, and
distributes a relative 3D layout. It adds no hardware. All filtering, matrix
operations, optimization, election, scheduling, and packet serialization are
implemented here in C. There are **no third-party dependencies**; the adapter
uses ESP-IDF's Wi-Fi, networking, FreeRTOS, and timer APIs, and the solver uses
the standard C runtime/math functions.

This is an experimental positioning system. A successful numerical fit does
not establish that Wi-Fi measurements reflect real distances. Test enclosed
ornaments in their actual orientations before using directions for effects.

## Files and integration

| File | Responsibility |
|---|---|
| `include/xmas_spatial.h`, `xmas_spatial_esp.c` | ESP-IDF lifecycle, UDP transport, FTM events |
| `include/xmas_spatial_cluster.h`, `xmas_spatial_cluster.c` | Portable discovery, election, retries, membership, snapshot assembly |
| `include/xmas_spatial_math.h`, `xmas_spatial_math.c` | Portable filtering, MDS, eigensolver, robust refinement, directions |
| `tests/test_spatial.c` | Known geometry and a simulated lossy multi-board network |
| `../../examples/spatial_layout/` | Independently buildable firmware, no model caches or renderer needed |

The renderer is not automatically connected to Wi-Fi or changed by this module.
To integrate, add `xmas_spatial` to its component's `REQUIRES`, initialize and
connect the application's station, then call:

```c
#include "xmas_spatial.h"

// Call after the station obtains an IPv4 address. Its netif is application-owned.
xs_service_config_t cfg = xs_service_defaults(station_netif);
cfg.cluster.capacity = 16;       // includes this board; set the same capacity on all
cfg.cluster.group = 0x584d4153;   // same group and UDP port on every ornament
xs_service_t *spatial;
ESP_ERROR_CHECK(xs_service_start(&cfg, &spatial));

// In an application task; allocate once, outside a render loop.
xs_node_t nodes[16];
xs_info_t info;
esp_err_t result = xs_service_copy(spatial, nodes, 16, &info);
if (result == ESP_OK) {
    // nodes[0..info.count-1]: stable MAC IDs and x,y,z positions in metres.
    // Check info.quality.rank, rms_m, max_residual_m, eigen_ratio and age_ms.
    // info.version changes on a new complete layout. It resets after rejoin.
}
// Stop from an application task when finished. Do not call copy concurrently
// with stop, or use the service pointer after stop returns.
// xs_service_stop(spatial);
```

Required sdkconfig settings (provided in the demo's defaults):

```ini
CONFIG_ESP_WIFI_FTM_ENABLE=y
CONFIG_ESP_WIFI_FTM_INITIATOR_SUPPORT=y
CONFIG_ESP_WIFI_FTM_RESPONDER_SUPPORT=y
```

Use ESP-IDF **5.1 or later** with the FTM report API. The standalone demo has been
compiled and linked for ESP32-S3 against **ESP-IDF 5.1.6**. The station must be
started and connected in `WIFI_MODE_STA` when starting the service. The service
owns the FTM engine and adds a hidden, password-protected SoftAP responder in
`WIFI_MODE_APSTA`; it restores station-only mode and the old modem-sleep setting
when stopped. The responder needs no AP IP netif, DHCP server, or client
associations: FTM uses management frames. Existing router credentials and
reconnection logic stay with the application. Use `WIFI_STORAGE_RAM` before
starting if configuration changes should not be stored in flash.

The upstream router **does not need FTM support**. Every ornament's local SoftAP
is its ranging target. A router's ordinary ping/packet round-trip time is not
used to estimate distance. RSSI is not silently substituted when FTM fails.

## Network and size requirements

- Participating boards must use the same group, UDP port, IPv4 broadcast domain,
  and **2.4 GHz channel**. Wi-Fi client isolation must be disabled. A mesh SSID
  can span channels; use a single-channel AP for this version. Cross-channel
  peers are discovered, but their range attempts fail and prevent a full map.
- Default UDP port: **42173**. This small protocol assumes a trusted LAN; the
  group number separates installations but provides no authentication.
- Capacity is a runtime allocation, **not hard-coded to five**. It defaults to
  16 and may be configured from 4 through the protocol's 65535-node limit,
  subject to RAM. That upper limit is an encoding limit, not a practical ESP32
  deployment size. An allocation failure is reported, not truncated.
- Discovery above capacity produces `XS_CAPACITY` and invalidates the map. It
  does not quietly map an arbitrary subset. Unresponsive/nonparticipating
  devices cannot be discovered; this service has no independent attendance list.
- Storage is **O(N²)**. With the current seven-sample windows, model/cluster
  allocations are roughly `64*N*N + 110*N` bytes, plus small control structures,
  allocator overhead, an 8 KiB task stack, and ESP-IDF's Wi-Fi/network memory.
  Capacity 16 is around 18 KiB for model/cluster arrays; 32 around 68 KiB.
  The renderer already uses substantial internal RAM on rev 1, so integration
  may require memory budgeting or rev 2's PSRAM.
- A sweep has `N*(N-1)/2` FTM sessions; directions alternate each sweep. Default
  filtering needs three fresh sessions per pair before the first map. For five
  boards that is **30 successful sessions**, plus five seconds of membership
  settling, retry time, and any failed sessions. Time scales quadratically;
  radio airtime limits useful size before protocol identifiers do.
- Defaults: HELLO every 2 s, peer expiry after 20 s, request retry every 750 ms,
  FTM timeout 4 s, snapshot repeat every 5 s. Sample and layout expiry are both
  30 minutes by default. Reduce them for moving ornaments, keeping expiry longer
  than several complete sweeps. This is intended for static or slowly changing
  layouts, not real-time tracking.

## Mathematics

The equations and implementation reasoning are also documented immediately
above the relevant functions in `xmas_spatial_math.c`.

### 1. Pair estimates and uncertainty

Each successful FTM session produces one distance estimate. ESP-IDF reports
`dist_est` in centimetres; the adapter converts it to metres. The driver handles
the radio timestamps and its calibration. We do not treat software packet
arrival time as radio travel time. Failed, zero, implausibly large, and nonfinite
measurements do not enter the graph.

For each unordered pair `(i,j)`, retain the last seven session estimates with
individual monotonic timestamps. Discard expired observations, then use:

```text
d_ij     = median(samples_ij)
MAD_ij   = median(abs(samples_ij - d_ij))
sigma_ij = 1.4826 * MAD_ij
w_ij     = 1 / (sigma_ij^2 + noise_floor^2)
```

The factor 1.4826 makes MAD comparable to standard deviation for Gaussian
noise. Median/MAD resist fewer than half the samples being gross outliers.
The noise floor defaults to 0.15 m; it is a tuning parameter, **not a claim of
15 cm Wi-Fi accuracy**. We deliberately do not divide uncertainty by the square
root of sample count: repeated samples may share the same multipath bias.

The solver requires fresh estimates for **all pairs**. A sparse or disconnected
graph returns `XS_INCOMPLETE`. Shortest-path distances through other ornaments
are not inserted as if they were measured Euclidean distances; a connected
graph alone does not determine a rigid 3D layout.

### 2. Classical multidimensional scaling

Let `D` be the symmetric distance matrix with zero diagonal, and let
`J = I - 11ᵀ/N` subtract the mean. Construct:

```text
B = -0.5 * J * (D elementwise squared) * J
```

For consistent Euclidean distances, `B = X Xᵀ`, where rows of `X` are centered
positions. The hand-written cyclic Jacobi eigensolver computes
`B = V Lambda Vᵀ`. The initial three coordinates are the eigenvectors for the
three largest positive eigenvalues, scaled by their square roots. Negative
eigenvalues caused by inconsistent data are omitted. Matrix storage is O(N²);
each Jacobi sweep is O(N³).

### 3. Robust refinement

Minimize the weighted distance disagreement over all positions simultaneously:

```text
E(X) = sum_(i<j) w_ij * rho(||X_i-X_j|| - d_ij)

rho(e) = 0.5*e^2                  if |e| <= h
         h*(|e| - 0.5*h)          otherwise
```

This is Huber loss, with `h = 0.30 m` by default. Its gradient uses the unit
vector along each pair and the clipped residual `clamp(e,-h,h)`. Backtracking
Armijo line search accepts only steps that decrease the objective. Positions
are centered after each step to remove translation. We try the MDS start, the
preceding layout when available, and a small deterministic perturbation to
reduce sensitivity to a flat stationary initialization. This is nonconvex
optimization; multiple starts do not guarantee the global optimum.

### 4. Coordinate frame and quality

The centroid is `(0,0,0)`. A well-spread set of board IDs determines the axes:
one long baseline defines x, a third point defines the xy plane, and a fourth
chooses the otherwise arbitrary mirror image. These reference IDs are retained
while membership is unchanged. They need no measured/known physical positions.

The frame has **no compass direction, gravity alignment, or board-facing
orientation**. Membership changes, coordinator reboots, or a degenerate anchor
configuration can change the frame. Aligning it to a previous installation or
preserving it across power cycles is not implemented. The positions' covariance
eigenvalues report numerical rank and the smallest/largest eigenvalue ratio;
near-flat/linear geometry makes some directions unstable even with small errors.

`rms_m` and `max_residual_m` measure disagreement with the input distances,
**not ground-truth position error**. `converged` means the optimizer met its
stopping criterion; it does not certify the measurements or uniqueness.

An N-point 3D shape has `3N-6` continuous parameters after removing translation
and rotation, while a complete graph gives `N(N-1)/2` distances. Four boards
have 6 parameters and 6 edges: no redundancy. Five have 9 parameters and 10
edges. More boards give more consistency constraints, but an internally
consistent biased distance matrix can still yield the wrong physical layout.

### 5. Directions and custom buckets

`xs_direction(a,b,...)` computes `(b-a)/||b-a||`, returning false for coincident
points. For direction from the group center, use `a=(0,0,0)`.

`xs_bucket(point,directions,count)` selects the greatest normalized dot product,
equivalently the smallest angular separation. It accepts any bucket count. For
example, ten buckets can be eight directions around the xy plane plus +/-z:

```c
static const xs_vec3_t buckets[10] = {
    {1,0,0}, {1,1,0}, {0,1,0}, {-1,1,0},
    {-1,0,0}, {-1,-1,0}, {0,-1,0}, {1,-1,0},
    {0,0,1}, {0,0,-1}
};
int bucket = xs_bucket(nodes[i].position, buckets, 10);
// -1 means undefined, including an ornament at the centroid.
```

These are arbitrary map axes. A cube's six faces plus eight corners would be
14 directions, so callers can choose the actual convention they want.

## Coordination and packet handling

The lexicographically smallest live station MAC is the coordinator. Membership
changes invalidate observations and layouts, cancel active work, and restart a
settling interval before ranging. HELLO advertises the **SoftAP MAC**, which is
different from the station MAC used as the board ID, plus channel and boot nonce.
Station IPv4 addresses are learned from UDP sources rather than trusted payloads.

One pair is scheduled at a time. The coordinator sends a sequenced JOB to its
initiator; results are accepted only for the matching job, initiator, target,
and boot identities. A retry has the same sequence and gets the cached result,
so duplicate packets cannot count as fresh range samples. Timeouts move on to
the next pair; later sweeps retry failed links. Elections are eventually
consistent, not consensus across partitions: after a partition, each reachable
group of at least four may map itself; merging groups resets membership.

Coordinates use signed millimetres on the wire, with one small POINT datagram
per board. Snapshot IDs, membership IDs, boot nonces, index tracking, quality
metadata checks and expiry prevent partial or mixed-version snapshots from
being exposed. Complete snapshots are periodically rebroadcast to recover lost
fragments. A duplicate old snapshot does not refresh its timestamp. Boot nonces
are random session identifiers, not authentication. A delayed old HELLO may
temporarily trigger rediscovery; fresh periodic HELLOs restore membership.

The ESP adapter serializes core state on one worker task. The Wi-Fi event handler
only releases the driver's retained report and queues its summary. FTM has no
user token in its SDK event; the adapter does not start a replacement session
until the preceding terminal event is consumed. It matches the peer MAC and
its own generation token to prevent a canceled session's result from becoming
a different pair's measurement. A driver that never reports termination leaves
ranging blocked until restart; it never creates a synthetic distance.

## Verification

From the repository root:

```sh
sh components/xmas_spatial/tests/run.sh
```

The test runner uses only a C compiler, libm and compiler sanitizers. Tests cover
known 4/5/12/37-node 3D layouts; planar/linear layouts; repeated outliers; biased
edges; invalid inputs; per-sample expiry; frame repeatability; directions;
discovery and election; joining, rebooting and leaving; packet loss, duplication
and reordering; insufficient nodes; capacity overflow; group isolation; stale
layouts; malformed packets; failed ranging and recovery. macOS defaults to
UBSan because the bundled ASan runtime on macOS 26.5 can hang during process
initialization. Linux defaults to ASan+UBSan; `SANITIZERS` overrides the choice.

The network tests feed geometric ground truth into simulated radio sessions.
They verify algorithms and protocol behavior, **not real Wi-Fi FTM accuracy or
radio coexistence**. Hardware acceptance should compare measured versus known
pair separations and resulting directions across enclosed-board orientations,
then repeat on the decorated tree. Enable DEBUG logs for `xmas_spatial` to inspect
per-session status, accepted-frame counts and distance estimates.

Background references (the implementation here is original):

- [Espressif Wi-Fi FTM](https://docs.espressif.com/projects/esp-idf/en/v5.1.6/esp32s3/api-guides/wifi.html#fine-timing-measurement-ftm)
- [Espressif FTM example/API usage](https://github.com/espressif/esp-idf/tree/v5.1.6/examples/wifi/ftm)
- [MDS mathematical background](https://scikit-learn.org/stable/modules/manifold.html#multidimensional-scaling)

The scikit-learn link is a reference only; no Python or numerical package is
used by either the module or its tests.
