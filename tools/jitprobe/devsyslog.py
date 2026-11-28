# Streams a connected iOS device's syslog (lockdown service com.apple.syslog_relay) through the Mac's usbmuxd, over
# USB or else usbmuxd's network connection, using the Mac's existing pairing record for the device. Needs no root and
# no third-party packages; `log collect --device-udid` needs root, and devicectl has no log command.
# Use: python3 -I tools/jitprobe/devsyslog.py <udid> <raw-out> <filtered-out> <regex>
# Every line goes to raw-out and the lines matching regex (case-insensitive) to filtered-out, until interrupted.
# raw-out holds every process's log, other apps included: keep it under build/ (gitignored) and delete it after use.
# The pairing record's host key is written to a 0600 temporary file only for as long as ssl needs to load it.
import os
import plistlib
import re
import socket
import ssl
import struct
import sys
import tempfile
import time

MUX = "/var/run/usbmuxd"


def mux_connect():
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.connect(MUX)
    return s


def mux_send(s, msg, tag=1):
    payload = plistlib.dumps(msg)
    s.sendall(struct.pack("<IIII", 16 + len(payload), 1, 8, tag) + payload)


def recv_exact(s, n):
    buf = b""
    while len(buf) < n:
        chunk = s.recv(n - len(buf))
        if not chunk:
            raise EOFError("connection closed")
        buf += chunk
    return buf


def mux_recv(s):
    length, _version, _type, _tag = struct.unpack("<IIII", recv_exact(s, 16))
    return plistlib.loads(recv_exact(s, length - 16))


def mux_request(msg):
    s = mux_connect()
    base = {"ClientVersionString": "devsyslog", "ProgName": "devsyslog"}
    base.update(msg)
    mux_send(s, base)
    reply = mux_recv(s)
    s.close()
    return reply


# USB when the device is on the cable, else usbmuxd's network connection (Wi-Fi sync), which carries lockdown too
def device_id(udid):
    found = {}
    for d in mux_request({"MessageType": "ListDevices"})["DeviceList"]:
        props = d["Properties"]
        if props.get("SerialNumber") == udid:
            found[props.get("ConnectionType")] = d["DeviceID"]
    for connection in ("USB", "Network"):
        if connection in found:
            return found[connection], connection
    raise SystemExit("device %s not connected (USB or network)" % udid)


def mux_tunnel(dev, port):
    s = mux_connect()
    mux_send(s, {"ClientVersionString": "devsyslog", "ProgName": "devsyslog", "MessageType": "Connect",
                 "DeviceID": dev, "PortNumber": socket.htons(port)})
    reply = mux_recv(s)
    if reply.get("Number") != 0:
        raise SystemExit("usbmuxd connect to port %d failed: %r" % (port, reply))
    return s


def ld_send(s, msg):
    payload = plistlib.dumps(msg)
    s.sendall(struct.pack(">I", len(payload)) + payload)


def ld_recv(s):
    (length,) = struct.unpack(">I", recv_exact(s, 4))
    return plistlib.loads(recv_exact(s, length))


def tls_context(record, tmp):
    cert = os.path.join(tmp, "c.pem")
    key = os.path.join(tmp, "k.pem")
    for path, data in ((cert, record["HostCertificate"]), (key, record["HostPrivateKey"])):
        fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
        os.write(fd, data)
        os.close(fd)
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    ctx.check_hostname = False
    ctx.verify_mode = ssl.CERT_NONE
    ctx.set_ciphers("ALL:!aNULL:!eNULL:@SECLEVEL=0")
    ctx.load_cert_chain(cert, key)
    os.unlink(cert)
    os.unlink(key)
    return ctx


def start_service(udid, dev, record, ctx, name):
    s = mux_tunnel(dev, 62078)
    ld_send(s, {"Label": "devsyslog", "Request": "QueryType"})
    ld_recv(s)
    ld_send(s, {"Label": "devsyslog", "Request": "StartSession", "HostID": record["HostID"],
                "SystemBUID": record["SystemBUID"]})
    session = ld_recv(s)
    if "Error" in session:
        raise SystemExit("StartSession: %r" % session)
    if session.get("EnableSessionSSL"):
        s = ctx.wrap_socket(s)
    ld_send(s, {"Label": "devsyslog", "Request": "StartService", "Service": name})
    service = ld_recv(s)
    if "Port" not in service:
        raise SystemExit("StartService %s: %r" % (name, service))
    return service


def main():
    udid, raw_path, filtered_path, pattern = sys.argv[1:5]
    match = re.compile(pattern, re.IGNORECASE)
    dev, connection = device_id(udid)
    record = plistlib.loads(mux_request({"MessageType": "ReadPairRecord", "PairRecordID": udid})["PairRecordData"])
    with tempfile.TemporaryDirectory() as tmp:
        ctx = tls_context(record, tmp)
    service = start_service(udid, dev, record, ctx, "com.apple.syslog_relay")
    s = mux_tunnel(dev, service["Port"])
    if service.get("EnableServiceSSL"):
        s = ctx.wrap_socket(s)
    raw = open(raw_path, "a", buffering=1)
    filtered = open(filtered_path, "a", buffering=1)
    stamp = time.strftime("%H:%M:%S")
    print("[devsyslog] %s streaming %s over %s (service port %d, ssl %s)" % (stamp, udid, connection, service["Port"],
                                                                             service.get("EnableServiceSSL", False)),
          file=filtered, flush=True)
    pending = b""
    while True:
        chunk = s.recv(65536)
        if not chunk:
            print("[devsyslog] stream closed", file=filtered, flush=True)
            return
        pending += chunk
        *lines, pending = pending.split(b"\n")
        for line in lines:
            text = line.replace(b"\x00", b"").decode("utf-8", "replace")
            text = re.sub(r"\\^\[\[[0-9;]*m", "", text)
            raw.write(text + "\n")
            if match.search(text):
                filtered.write(text + "\n")


if __name__ == "__main__":
    main()
