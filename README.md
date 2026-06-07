# vnp_for_wifi

SOCKS5 TCP CONNECT proxy (RFC 1928) with MikroTik integration. Two independent implementations: Python (stdlib only) and C++ (Windows, WinSock2).

## Quick start

```bash
# Python (no deps needed)
python socks5-py/server.py

# C++ (build with MinGW)
cd socks5-cpp
g++ server.cpp -o server.exe -lws2_32 -lwinhttp -std=c++17 -static
```

Both servers listen on `0.0.0.0:1080`.

## Usage

```bash
# Test with curl
curl --socks5 127.0.0.1:1080 http://example.com

# Test with client
python test_client.py example.com 80
```

## C++ server modes

| Mode | Description |
|------|-------------|
| `log` | Log target IPs to file |
| `send` | Log IPs + send to MikroTik REST API every 30s |
| `upload` | One-shot log upload to MikroTik |

```bash
# First run — interactive wizard (creates config.ini)
.\socks5-cpp\server.exe

# Log mode
.\socks5-cpp\server.exe -m log

# Send to MikroTik
.\socks5-cpp\server.exe -m send -r 10.0.0.1 -u admin
```

## Project structure

```
├── socks5-py/server.py     # Python SOCKS5 server
├── socks5-cpp/
│   ├── server.cpp           # C++ SOCKS5 server
│   └── CMakeLists.txt
├── test_client.py           # Test client for both servers
└── docs/                    # Design specs & plans
```

IPv4 only, no auth, CONNECT only. Per-connection relay with 2 threads (4096/8192 byte buffers).
