"""
Serve the bundles and the catalogue to the device.

    python server/serve.py            # serves build/ on port 8080

Then set the display's "model server" to http://<this machine>:8080 -- in the
setup form when provisioning, or on the control page afterwards.

## There is deliberately almost nothing here

The device fetches exactly two kinds of thing: `/catalog.json` and
`/bundles/*.xmb`. Both are static files. Any web server at all can do this, and
the device asks for nothing else -- no API, no CORS headers, no content
negotiation -- which is why the setting is a base URL rather than a protocol.
This script exists so there is something to run without choosing one, and it
prints the address to type into the phone.
"""

import http.server
import os
import socket
import sys

try:
    from zeroconf import ServiceInfo, Zeroconf
except ImportError:
    Zeroconf = None

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ROOT = os.environ.get("SERVE_DIR", os.path.join(REPO, "dist"))
PORT = int(os.environ.get("PORT", "8080"))


class Handler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=ROOT, **kwargs)

    def end_headers(self):
        # The device re-fetches the catalogue whenever the page is opened, and
        # a cached one hides a model that was just baked.
        self.send_header("Cache-Control", "no-store")
        super().end_headers()

    def log_message(self, fmt, *args):
        sys.stderr.write("  %s\n" % (fmt % args))


def local_address():
    """The address the device will have to use -- not 127.0.0.1, which is this
    machine talking to itself and means nothing to an ESP32 on the WiFi."""
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        # No packet is sent; this just asks the routing table which interface
        # would be used to reach the outside world.
        s.connect(("8.8.8.8", 53))
        return s.getsockname()[0]
    except OSError:
        return "127.0.0.1"
    finally:
        s.close()


def advertise(address, port):
    """
    Announce this server over Bonjour so the display finds it by itself.

    THIS IS THE ONE PIECE OF TYPING WORTH REMOVING. Nothing on either phone
    will hand a Wi-Fi password to a device that is not using Matter or DPP, so
    the password has to be typed -- but the server's address does not. The
    display browses for this service and configures itself, which takes the
    setup form down to one field that matters.

    A private service type, not _http._tcp: a home network is full of HTTP
    servers and none of the others have models on them.
    """
    if Zeroconf is None:
        print("zeroconf not installed (pip install zeroconf) --")
        print("the display will not find this server on its own.")
        return None

    info = ServiceInfo(
        "_xmasmodels._tcp.local.",
        "models._xmasmodels._tcp.local.",
        addresses=[socket.inet_aton(address)],
        port=port,
        properties={"path": "/"},
        server="xmasmodels.local.",
    )

    zc = Zeroconf()
    zc.register_service(info)
    print("  advertising as _xmasmodels._tcp -- the display can find this by itself")
    return zc


def main():
    catalog = os.path.join(ROOT, "catalog.json")
    if not os.path.exists(catalog):
        print("warning: %s does not exist yet -- run server/bake_bundles.py first\n" % catalog)

    address = local_address()
    print("serving %s" % ROOT)
    print()
    print("  set the display's model server to:  http://%s:%d" % (address, PORT))
    print()

    zc = advertise(address, PORT)

    try:
        http.server.ThreadingHTTPServer(("0.0.0.0", PORT), Handler).serve_forever()
    finally:
        if zc:
            zc.close()


if __name__ == "__main__":
    main()
