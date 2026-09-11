# Spatial layout demo

Standalone ESP32-S3 firmware for testing the
[xmas_spatial component](../../components/xmas_spatial/README.md) on **4 or more
boards**. It does not need the OSRS model caches, display, or renderer build.
Each board joins the same existing Wi-Fi network and serial-prints the shared
relative 3D layout in metres.

With ESP-IDF 5.1+ exported in your shell:

```sh
cd examples/spatial_layout
idf.py set-target esp32s3
idf.py menuconfig
# Spatial layout demo: set Wi-Fi SSID/password, capacity and common group ID.
idf.py build
idf.py -p /dev/cu.YOUR_BOARD flash monitor
```

Flash the same configuration to each board. Unique MAC addresses provide IDs;
there is no per-board index to configure. Default capacity is 16, including the
local board. Set it to the intended maximum board count that fits RAM. Do not
commit the generated sdkconfig containing Wi-Fi credentials.

Use one **2.4 GHz channel**, a common IPv4 broadcast domain, and no Wi-Fi client
isolation. The router itself need not support FTM. Boards create their own FTM
responder SoftAPs while remaining connected to your router. This demo owns the
application's station reconnect loop; the reusable module does not.

After discovery settles, the lowest-MAC board schedules pair measurements.
The first layout needs three successful sessions per pair: 18 sessions for four
boards, 30 for five. FTM failures may extend that indefinitely. Logs show state,
discovered peers and completed pair estimates; every new layout prints each
board's MAC and coordinates plus rank, RMS disagreement and maximum residual.

States: `0=waiting`, `1=settling`, `2=collecting`, `3=layout`, `4=capacity exceeded`,
`5=numerical failure`. A rank-1/rank-2 layout is usable only with its corresponding
geometric limitations. A low residual is agreement with measured distances,
not proof of physical accuracy. Five boards offer only one redundant geometric
constraint, so compare results against where the ornaments actually hang.

No physical boards were flashed as part of the host/build verification. Start
with this demo to characterize the radio before integrating it into the renderer.
See the component README and source comments for the algorithms, memory costs,
API integration, wire format and host tests.
