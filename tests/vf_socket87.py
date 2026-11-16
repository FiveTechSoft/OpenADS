#!/usr/bin/env python3
"""Run exact-vf Harbour probes over a real disposable TCP server."""
import argparse
import os
import pathlib
import socket
import subprocess
import tempfile
import time


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--server', required=True)
    p.add_argument('--probe', required=True, action='append')
    p.add_argument('--large-directory', action='store_true')
    args = p.parse_args()
    with tempfile.TemporaryDirectory(prefix='openads-vf87-') as tmp:
        root = pathlib.Path(tmp)
        data = root / 'data'
        data.mkdir()
        outside=root/'outside'
        outside.mkdir()
        (outside/'marker.bin').write_bytes(b'outside untouched')
        boundary=data/'boundary'
        boundary.mkdir()
        (boundary/'target.bin').write_bytes(b'inside content')
        try:
            (boundary/'outside-link').symlink_to(outside/'marker.bin')
            (boundary/'inside-link').symlink_to(boundary/'target.bin')
            (boundary/'links-ready').touch()
        except OSError:
            pass
        if hasattr(os,'mkfifo'):
            os.mkfifo(boundary/'fifo')
            (boundary/'fifo-ready').touch()
        if args.large_directory:
            folder = data / 'large-directory'
            folder.mkdir()
            for n in range(12000):
                (folder / ('%05d-' % n + 'x' * 110)).touch()
        with socket.socket() as sock:
            sock.bind(('127.0.0.1', 0))
            port = sock.getsockname()[1]
        with (root / 'server.txt').open('wb') as log:
            server = subprocess.Popen([str(pathlib.Path(args.server).resolve()), '--host', '127.0.0.1',
                                       '--port', str(port), '--data', str(data), '--enable_file_func'],
                                      cwd=root, stdout=log, stderr=subprocess.STDOUT)
            try:
                limit = time.monotonic() + 25
                while True:
                    if server.poll() is not None:
                        raise RuntimeError('vf87 server exited: ' + (root / 'server.txt').read_text(errors='replace'))
                    try:
                        with socket.create_connection(('127.0.0.1', port), timeout=.2):
                            break
                    except OSError:
                        if time.monotonic() >= limit:
                            raise
                        time.sleep(.1)
                uri = 'tcp://127.0.0.1:%d/' % port + data.as_posix()
                for probe in args.probe:
                    result = subprocess.run([str(pathlib.Path(probe).resolve()), uri], cwd=root,
                                            capture_output=True, text=True, timeout=90)
                    print(result.stdout, end='')
                    if result.returncode or ('PASS' not in result.stdout and 'differences=0' not in result.stdout):
                        raise RuntimeError('vf87 probe failed: ' + result.stdout + result.stderr)
                assert (outside/'marker.bin').read_bytes()==b'outside untouched'
                print('VF87-ACTUAL-TCP PASS')
            finally:
                server.terminate()
                try:
                    server.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    server.kill()
                    server.wait(timeout=10)


if __name__ == '__main__':
    main()
