#!/usr/bin/env python3
"""
kd-wrapper.py — wraps kd.exe so commands can be injected over TCP.

Why: PowerShell/cmd background jobs can't append to a running process's
stdin (stdin is consumed once and EOFs). This wrapper keeps a real
process pipe open to kd.exe, listens on a localhost TCP port, and
forwards each connection's payload to kd's stdin. kd's stdout is
mirrored to the wrapper's stdout (so Monitor / a tail can stream it)
AND written to a log file.

Usage:
  python kd-wrapper.py --mode net --host-ip 192.168.1.20 --port 50000 --key A.B.C.D
  python kd-wrapper.py --mode com --com-port COM5 --com-baud 115200

Send commands from PowerShell:
  Tools\kd-send.ps1 "bp WinMaliKmd!DxgkDdiStartDevice"
  Tools\kd-send.ps1 "g"
"""

import argparse
import os
import socket
import subprocess
import sys
import threading
import time


def build_kd_args(args):
    if args.mode == "net":
        if not (args.port and args.key):
            sys.exit("--mode net requires --port and --key")
        transport = f"net:port={args.port},key={args.key}"
    elif args.mode == "com":
        if not args.com_port:
            sys.exit("--mode com requires --com-port")
        transport = f"com:port={args.com_port},baud={args.com_baud}"
    else:
        sys.exit(f"unknown mode: {args.mode}")
    return [args.kd, "-k", transport, "-y", args.symbol_path]


def reader_thread(proc, log_fh):
    while True:
        chunk = proc.stdout.read(1)
        if not chunk:
            break
        try:
            sys.stdout.buffer.write(chunk)
            sys.stdout.flush()
            log_fh.write(chunk)
            log_fh.flush()
        except Exception:
            break


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--mode", choices=["net", "com"], default="net")
    ap.add_argument("--host-ip", help="(net) target hostip — informational only; kd doesn't need it")
    ap.add_argument("--port", type=int, help="(net) kdnet port")
    ap.add_argument("--key", help="(net) kdnet key 'A.B.C.D'")
    ap.add_argument("--com-port", help="(com) e.g. COM5")
    ap.add_argument("--com-baud", type=int, default=115200)
    ap.add_argument("--cmd-port", type=int, default=5556, help="localhost TCP port for command input")
    ap.add_argument("--log", default="kd-session.log")
    ap.add_argument("--kd", default=r"C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\kd.exe")
    ap.add_argument("--symbol-path",
                    default=r"srv*C:\symbols*https://msdl.microsoft.com/download/symbols")
    args = ap.parse_args()

    kd_cmd = build_kd_args(args)
    print(f"[wrapper] launching: {' '.join(kd_cmd)}", flush=True)
    print(f"[wrapper] log file:  {os.path.abspath(args.log)}", flush=True)
    print(f"[wrapper] cmd port:  127.0.0.1:{args.cmd_port}", flush=True)

    log_fh = open(args.log, "wb", buffering=0)

    proc = subprocess.Popen(
        kd_cmd,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        bufsize=0,
    )

    t = threading.Thread(target=reader_thread, args=(proc, log_fh), daemon=True)
    t.start()

    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("127.0.0.1", args.cmd_port))
    srv.listen(4)
    srv.settimeout(1.0)

    try:
        while proc.poll() is None:
            try:
                conn, _ = srv.accept()
            except socket.timeout:
                continue
            with conn:
                data = b""
                conn.settimeout(2.0)
                try:
                    while True:
                        chunk = conn.recv(4096)
                        if not chunk:
                            break
                        data += chunk
                except socket.timeout:
                    pass
                if not data:
                    continue
                text = data.decode("utf-8", errors="replace")
                if not text.endswith("\n"):
                    text += "\n"
                # Echo into the log so the transcript shows both directions.
                stamp = time.strftime("[%H:%M:%S]")
                marker = f"\n{stamp} >>> {text.rstrip()}\n".encode("utf-8")
                sys.stdout.buffer.write(marker); sys.stdout.flush()
                log_fh.write(marker)
                try:
                    proc.stdin.write(text.encode("utf-8"))
                    proc.stdin.flush()
                except (BrokenPipeError, OSError) as e:
                    print(f"[wrapper] stdin write failed: {e}", flush=True)
                    break
    except KeyboardInterrupt:
        pass
    finally:
        try:
            proc.terminate()
        except Exception:
            pass
        log_fh.close()


if __name__ == "__main__":
    main()
