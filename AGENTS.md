# AGENTS.md — vnp_for_wifi

## What this is

SOCKS5 TCP CONNECT proxy (RFC 1928) — two independent implementations (Python + C++), IPv4 only, no auth.

## Commands

| Action | Command |
|--------|---------|
| Run Python server | `python socks5-py/server.py` |
| Build C++ server (CMake) | `cd socks5-cpp; cmake -G Ninja .; ninja` |
| Build C++ server (MinGW) | `g++ server.cpp -o server.exe -lws2_32 -lwinhttp -std=c++17 -static` (from `socks5-cpp/`) |
| Run C++ server (log mode) | `.\socks5-cpp\server.exe -m log` |
| Run C++ server (send mode) | `.\socks5-cpp\server.exe -m send -r 10.0.0.1 -u admin` |
| Upload log to MikroTik | `.\socks5-cpp\server.exe -m upload -r 10.0.0.1 -u admin` |
| Use custom config | `.\socks5-cpp\server.exe -c myconfig.ini` |
| First-run wizard | `.\socks5-cpp\server.exe` (creates `config.ini`) |
| Run C++ server (silent mode) | `.\socks5-cpp\server.exe -v silent` |
| Run C++ server (ip-only mode) | `.\socks5-cpp\server.exe -v ip` |
| Test with curl | `curl --socks5 127.0.0.1:1080 http://example.com` |
| Test with client | `python test_client.py <host> <port>` (e.g. `example.com 80`) |
| Test client connects to | `127.0.0.1:1080` (hardcoded) |

## Key structure

```
.
├── socks5-py/server.py       # Python SOCKS5 server (stdlib only: socket + threading)
├── socks5-cpp/
│   ├── server.cpp            # C++ SOCKS5 server (WinSock2, Windows-only)
│   └── CMakeLists.txt        # CMake C++17, links ws2_32 + winhttp
├── test_client.py            # Shared test client for both servers
└── docs/superpowers/
    ├── specs/                # Design documents
    └── plans/                # Implementation plans
```

## Architecture notes

- Protocol: SOCKS5 (RFC 1928), phases: Handshake → Request → Relay (bidirectional forwarding)
- Only IPv4 (ATYP=0x01), only CONNECT (CMD=0x01), only No Authentication (0x00)
- Both servers listen on `0.0.0.0:1080`
- Per-connection relay: 2 threads (client↔target), fixed 4096/8192 byte buffers
- C++ server integrates with MikroTik REST API — run in `send` mode to collect + send target IPs to address list `rkn_wg_ip` every 30s; `upload` mode for one-shot log upload
- Modes: `log` (only log IPs to file), `send` (MikroTik + log), `upload` (one-shot log → MikroTik)
- Without CLI flags: reads `config.ini`, or starts interactive first-run wizard
- Config stored in `config.ini` (INI format: `[router]` section with `host`, `user`, `pass`)
- Full CLI: `-m`/`--mode`, `-i`/`--log-ips`, `-r`/`--router`, `-u`/`--user`, `-p`/`--pass`, `-c`/`--config`, `-n`/`--new`, `-v`/`--verbose`, `-h`/`--help`

## C++ specifics (Windows-only)

- Requires `ws2_32` + `winhttp` libs. CMake links both when `WIN32`.
- Pragma comments used for MSVC: `#pragma comment(lib, "ws2_32.lib")` and `#pragma comment(lib, "winhttp.lib")`
- MinGW needs explicit `-lws2_32 -lwinhttp` (pragma comment doesn't work for WinHTTP in MinGW); add `-static` for portable binary
- C++17, console output in UTF-8 via `SetConsoleOutputCP(CP_UTF8)`
- `send()` may send partial data — requires retry loop
- Threads detached via `std::thread(...).detach()`

## Testing

- No test framework, no CI, no linter, no typechecker
- Test via curl or `test_client.py` against a running server
- Python server has no dependencies beyond stdlib
