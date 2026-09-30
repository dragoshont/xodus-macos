#!/usr/bin/env bash

set -euo pipefail

export PATH="/opt/homebrew/bin:$PATH"

bottles_root="$HOME/Library/Application Support/CrossOver/Bottles"
bottle="${XODUS_RUNTIME_TEST_BOTTLE:-XGameRuntimeTest}"
cxwine="/Applications/CrossOver.app/Contents/SharedSupport/CrossOver/bin/wine"
work_dir="$(mktemp -d)"
socket_path="/tmp/xodus-afunix-smoke.sock"

cleanup() {
    if [[ -n "${server_pid:-}" ]] && kill -0 "$server_pid" 2>/dev/null; then
        kill "$server_pid" 2>/dev/null || true
        wait "$server_pid" 2>/dev/null || true
    fi
    rm -f "$socket_path"
    rm -rf "$work_dir"
}
trap cleanup EXIT

if [[ ! -x "$cxwine" ]]; then
    echo "CrossOver Wine is not installed." >&2
    exit 1
fi

if [[ ! -d "$bottles_root/$bottle" ]]; then
    echo "CrossOver bottle is missing: $bottle" >&2
    exit 1
fi

cat >"$work_dir/client.c" <<'EOF'
#include <winsock2.h>
#include <afunix.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    static const char path[] = "Z:\\tmp\\xodus-afunix-smoke.sock";
    static const char ping[] = "PING";
    char response[5] = {0};
    WSADATA wsa;
    struct sockaddr_un address = {0};
    SOCKET sock;

    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 10;

    sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) {
        fprintf(stderr, "socket_error=%d\n", WSAGetLastError());
        return 11;
    }

    address.sun_family = AF_UNIX;
    if (sizeof(path) > sizeof(address.sun_path)) return 12;
    memcpy(address.sun_path, path, sizeof(path));

    if (connect(sock, (const struct sockaddr *)&address, sizeof(address)) == SOCKET_ERROR) {
        fprintf(stderr, "connect_error=%d\n", WSAGetLastError());
        return 13;
    }

    if (send(sock, ping, 4, 0) != 4) return 14;
    if (recv(sock, response, 4, 0) != 4) return 15;

    closesocket(sock);
    WSACleanup();

    if (memcmp(response, "PONG", 4) != 0) return 16;
    puts("AF_UNIX_OK");
    return 0;
}
EOF

cat >"$work_dir/server.py" <<'EOF'
import os
import socket

path = "/tmp/xodus-afunix-smoke.sock"
try:
    os.unlink(path)
except FileNotFoundError:
    pass

with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as server:
    server.bind(path)
    os.chmod(path, 0o600)
    server.listen(1)
    connection, _ = server.accept()
    with connection:
        if connection.recv(4) != b"PING":
            raise SystemExit(2)
        connection.sendall(b"PONG")
EOF

x86_64-w64-mingw32-gcc \
    -Wall \
    -Wextra \
    -Werror \
    -o "$work_dir/client.exe" \
    "$work_dir/client.c" \
    -lws2_32

python3 "$work_dir/server.py" &
server_pid=$!

for _ in {1..30}; do
    [[ -S "$socket_path" ]] && break
    kill -0 "$server_pid" 2>/dev/null || break
    sleep 0.1
done

if [[ ! -S "$socket_path" ]]; then
    echo "Unix socket server did not start." >&2
    exit 1
fi

"$cxwine" --bottle "$bottle" --no-gui "$work_dir/client.exe"
wait "$server_pid"
