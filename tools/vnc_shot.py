"""One screenshot of a VMware VM through its built-in VNC server (RemoteDisplay.vnc.enabled in the
.vmx) — only the VM's own screen, never the host's. Raw encoding, no authentication (the lab VMs set
no VNC password).

    python tools/vnc_shot.py <port> <out.png> [--host 127.0.0.1] [--timeout 20] [--wake]

Exit 0 with the PNG written; 1 when the VM does not answer (powered off, still booting the VNC
server); 2 when it asks for a password.
"""
import argparse
import socket
import struct
import sys

from PIL import Image


def recv_exact(sock, n):
    data = bytearray()
    while len(data) < n:
        chunk = sock.recv(n - len(data))
        if not chunk:
            raise ConnectionError('connection closed')
        data += chunk
    return bytes(data)


def shot(host, port, out, timeout, wake=False):
    sock = socket.create_connection((host, port), timeout=timeout)
    sock.settimeout(timeout)
    version = recv_exact(sock, 12)
    if not version.startswith(b'RFB '):
        raise ConnectionError('not a VNC server')
    sock.sendall(b'RFB 003.008\n')
    count = recv_exact(sock, 1)[0]
    if count == 0:
        reason_len = struct.unpack('>I', recv_exact(sock, 4))[0]
        raise ConnectionError(recv_exact(sock, reason_len).decode(errors='replace'))
    types = recv_exact(sock, count)
    if 1 not in types:
        return 2
    sock.sendall(b'\x01')
    if struct.unpack('>I', recv_exact(sock, 4))[0] != 0:
        raise ConnectionError('VNC security handshake failed')
    sock.sendall(b'\x01')  # shared
    width, height = struct.unpack('>HH', recv_exact(sock, 4))
    recv_exact(sock, 16)  # server pixel format
    name_len = struct.unpack('>I', recv_exact(sock, 4))[0]
    recv_exact(sock, name_len)
    # 32 bpp, depth 24, little endian, true colour, BGRX.
    pixel_format = struct.pack('>BBBBHHHBBB3x', 32, 24, 0, 1, 255, 255, 255, 16, 8, 0)
    sock.sendall(struct.pack('>B3x', 0) + pixel_format)
    sock.sendall(struct.pack('>BxHi', 2, 1, 0))  # SetEncodings: Raw
    if wake:
        # A pointer move wakes a display Windows turned off after idle time; give it a moment.
        import time
        for x in (width // 2, width // 2 + 8, width // 2):
            sock.sendall(struct.pack('>BBHH', 5, 0, x, height // 2))
            time.sleep(0.3)
        for down in (1, 0):  # and a Shift press: Windows wakes the display on input
            sock.sendall(struct.pack('>BBxxI', 4, down, 0xFFE1))
            time.sleep(0.2)
        time.sleep(5)
    sock.sendall(struct.pack('>BBHHHH', 3, 0, 0, 0, width, height))
    image = Image.new('RGB', (width, height))
    covered = 0
    while covered < width * height:
        kind = recv_exact(sock, 1)[0]
        if kind == 0:  # FramebufferUpdate
            recv_exact(sock, 1)
            rects = struct.unpack('>H', recv_exact(sock, 2))[0]
            for _ in range(rects):
                x, y, w, h, encoding = struct.unpack('>HHHHi', recv_exact(sock, 12))
                if encoding == -223:  # DesktopSize pseudo-encoding: the size changed, start over
                    width, height = w, h
                    image = Image.new('RGB', (width, height))
                    covered = 0
                    sock.sendall(struct.pack('>BBHHHH', 3, 0, 0, 0, width, height))
                    continue
                if encoding != 0:
                    raise ConnectionError(f'unexpected encoding {encoding}')
                raw = recv_exact(sock, w * h * 4)
                tile = Image.frombuffer('RGB', (w, h), raw, 'raw', 'BGRX', 0, 1)
                image.paste(tile, (x, y))
                covered += w * h
        elif kind == 1:  # SetColourMapEntries
            recv_exact(sock, 3)
            n = struct.unpack('>H', recv_exact(sock, 2))[0]
            recv_exact(sock, n * 6)
        elif kind == 2:  # Bell
            pass
        elif kind == 3:  # ServerCutText
            recv_exact(sock, 3)
            n = struct.unpack('>I', recv_exact(sock, 4))[0]
            recv_exact(sock, n)
        else:
            raise ConnectionError(f'unexpected message {kind}')
    sock.close()
    image.save(out)
    return 0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('port', type=int)
    parser.add_argument('out')
    parser.add_argument('--host', default='127.0.0.1')
    parser.add_argument('--timeout', type=float, default=20)
    parser.add_argument('--wake', action='store_true', help='move the pointer first (a display turned off after idle time)')
    args = parser.parse_args()
    try:
        return shot(args.host, args.port, args.out, args.timeout, args.wake)
    except (OSError, ConnectionError) as error:
        print(f'vnc: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
