#!/usr/bin/env python3
"""
Fetch a Samsung Tizen / Smart Monitor remote-control token over the LAN.

The TV shows an allow/deny prompt on the first connection without a token.
After you press Allow, the TV sends ms.channel.connect with the token.

Usage:
  python3 script/fetch_tizen_token.py --ip 192.168.0.24
  python3 script/fetch_tizen_token.py --ip 192.168.0.24 --name WaveHome
  python3 script/fetch_tizen_token.py --ip 192.168.0.24 --verify --token 33055060
  python3 script/fetch_tizen_token.py --config config/appliances.json --id samsung-monitor
"""

from __future__ import annotations

import argparse
import base64
import json
import os
import socket
import ssl
import struct
import sys
import time
from pathlib import Path
from typing import Any


def b64_name(name: str) -> str:
    return base64.b64encode(name.encode("utf-8")).decode("ascii")


def ws_key() -> str:
    return base64.b64encode(os.urandom(16)).decode("ascii")


def read_http_headers(sock: ssl.SSLSocket) -> str:
    data = b""
    while b"\r\n\r\n" not in data:
        chunk = sock.recv(4096)
        if not chunk:
            break
        data += chunk
    return data.decode("latin-1", errors="replace")


def read_ws_text_frame(sock: ssl.SSLSocket, timeout_sec: float) -> str | None:
    sock.settimeout(timeout_sec)
    try:
        header = sock.recv(2)
    except (TimeoutError, socket.timeout, ssl.SSLWantReadError):
        return None
    if len(header) < 2:
        return None

    masked = header[1] & 0x80
    length = header[1] & 0x7F
    if length == 126:
        length = struct.unpack(">H", sock.recv(2))[0]
    elif length == 127:
        length = struct.unpack(">Q", sock.recv(8))[0]

    mask = sock.recv(4) if masked else b""
    payload = b""
    while len(payload) < length:
        payload += sock.recv(length - len(payload))

    if masked:
        payload = bytes(b ^ mask[i % 4] for i, b in enumerate(payload))
    return payload.decode("utf-8", errors="replace")


def parse_channel_event(payload: str) -> tuple[str, str | None]:
    try:
        message = json.loads(payload)
    except json.JSONDecodeError:
        return "unknown", None

    event = message.get("event", "")
    if event == "ms.channel.unauthorized":
        return "unauthorized", None
    if event == "ms.channel.connect":
        data = message.get("data") or {}
        token = data.get("token")
        return "connect", str(token) if token is not None else None
    return event or "unknown", None


def wait_for_port(host: str, port: int, timeout_sec: float) -> bool:
    deadline = time.time() + timeout_sec
    while time.time() < deadline:
        try:
            with socket.create_connection((host, port), timeout=1.0):
                return True
        except OSError:
            time.sleep(0.5)
    return False


def connect_and_wait(
    host: str,
    port: int,
    client_name: str,
    token: str | None,
    wait_sec: float,
) -> tuple[str, str | None]:
    encoded_name = b64_name(client_name)
    path = f"/api/v2/channels/samsung.remote.control?name={encoded_name}"
    if token:
        path += f"&token={token}"

    raw = socket.create_connection((host, port), timeout=5)
    ctx = ssl.create_default_context()
    ctx.check_hostname = False
    ctx.verify_mode = ssl.CERT_NONE
    sock = ctx.wrap_socket(raw, server_hostname=host)

    request = (
        f"GET {path} HTTP/1.1\r\n"
        f"Host: {host}:{port}\r\n"
        f"Upgrade: websocket\r\n"
        f"Connection: Upgrade\r\n"
        f"Sec-WebSocket-Version: 13\r\n"
        f"Sec-WebSocket-Key: {ws_key()}\r\n\r\n"
    )
    sock.sendall(request.encode("ascii"))

    response = read_http_headers(sock)
    status = response.split("\r\n", 1)[0] if response else ""
    if " 101 " not in status:
        raise RuntimeError(f"websocket upgrade failed: {status or 'no response'}")

    # With a saved token the TV may not send another connect event.
    if token:
        return "ready", token

    deadline = time.time() + wait_sec
    while time.time() < deadline:
        payload = read_ws_text_frame(sock, min(1.0, deadline - time.time()))
        if payload is None:
            continue
        if not payload:
            continue
        event, learned = parse_channel_event(payload)
        if event == "connect" and learned:
            return event, learned
        if event == "unauthorized":
            raise RuntimeError("TV denied the connection (ms.channel.unauthorized)")

    raise TimeoutError(
        f"timed out after {wait_sec:.0f}s waiting for TV allow prompt / token"
    )


def load_appliance_from_config(config_path: Path, appliance_id: str) -> dict[str, Any]:
    root = json.loads(config_path.read_text(encoding="utf-8"))
    for item in root.get("appliances", []):
        if item.get("id") == appliance_id:
            return item
    raise KeyError(f"appliance id not found in {config_path}: {appliance_id}")


def appliance_connection_defaults(appliance: dict[str, Any]) -> tuple[str, str, int, str | None]:
    transport = appliance.get("transport") or {}
    options = transport.get("options") or {}
    metadata = appliance.get("metadata") or {}

    host = options.get("ip") or metadata.get("ip") or ""
    if not host and transport.get("endpoint", "").startswith("wss://"):
        host = transport["endpoint"].split("://", 1)[1].split(":", 1)[0]

    if not host:
        raise ValueError("could not determine TV IP from appliance config")

    name = options.get("name") or appliance.get("name") or "WaveHome"
    port = int(options.get("securePort", 8002))
    token = options.get("token")
    return host, name, port, str(token) if token else None


def write_token_to_config(config_path: Path, appliance_id: str, token: str) -> None:
    root = json.loads(config_path.read_text(encoding="utf-8"))
    updated = False
    for item in root.get("appliances", []):
        if item.get("id") != appliance_id:
            continue
        options = item.setdefault("transport", {}).setdefault("options", {})
        options["token"] = token
        updated = True
        break
    if not updated:
        raise KeyError(f"appliance id not found in {config_path}: {appliance_id}")
    config_path.write_text(json.dumps(root, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Fetch Samsung Tizen remote token")
    parser.add_argument("--ip", help="TV IP address")
    parser.add_argument("--port", type=int, default=8002, help="secure websocket port (default: 8002)")
    parser.add_argument("--name", default="WaveHome", help="client name shown on TV")
    parser.add_argument("--token", help="existing token to verify instead of pairing")
    parser.add_argument("--verify", action="store_true", help="verify --token (implies --token)")
    parser.add_argument("--wait", type=float, default=60.0, help="seconds to wait for TV allow prompt")
    parser.add_argument("--port-wait", type=float, default=20.0, help="seconds to wait for TCP port")
    parser.add_argument("--config", type=Path, help="appliances.json path")
    parser.add_argument("--id", default="samsung-monitor", help="appliance id inside config")
    parser.add_argument(
        "--write-config",
        action="store_true",
        help="write fetched token back into appliances.json",
    )
    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()

    host = args.ip
    client_name = args.name
    port = args.port
    token = args.token

    if args.config:
        appliance = load_appliance_from_config(args.config, args.id)
        cfg_host, cfg_name, cfg_port, cfg_token = appliance_connection_defaults(appliance)
        host = host or cfg_host
        client_name = cfg_name if args.name == "WaveHome" else args.name
        port = cfg_port if args.port == 8002 else args.port
        token = token or cfg_token

    if not host:
        parser.error("--ip is required unless --config is provided")

    print(f"target: {host}:{port}")
    print(f"client: {client_name}")

    if not wait_for_port(host, port, args.port_wait):
        print(f"error: port {port} is not open on {host}", file=sys.stderr)
        print("hint: turn the TV on and enable IP remote in network settings", file=sys.stderr)
        return 1

    try:
        if args.verify:
            if not token:
                parser.error("--verify requires --token or a token in --config")
            print(f"mode: verify existing token ({token})")
            event, learned = connect_and_wait(host, port, client_name, token, wait_sec=3.0)
            print("result: token works")
            print(f"token: {learned}")
            return 0

        if token:
            print(f"config token (ignored for pairing): {token}")
        print("mode: pair (press Allow on the TV when prompted)")

        event, learned = connect_and_wait(
            host,
            port,
            client_name,
            None,
            wait_sec=args.wait,
        )
    except TimeoutError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    except OSError as exc:
        print(f"error: connection failed: {exc}", file=sys.stderr)
        return 1
    except RuntimeError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    if not learned:
        print("error: no token received", file=sys.stderr)
        return 1

    print(f"event: {event}")
    print(f"token: {learned}")
    print()
    print("appliances.json snippet:")
    print(f'  "token": "{learned}",')

    if args.write_config:
        if not args.config:
            parser.error("--write-config requires --config")
        write_token_to_config(args.config, args.id, learned)
        print()
        print(f"updated: {args.config} ({args.id})")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
