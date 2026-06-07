# Verbose Mode for C++ SOCKS5 Server

## Summary

Add a `--verbose` / `-v` CLI flag to control console output verbosity in `socks5-cpp/server.cpp`.

## Levels

| Level    | Behavior |
|----------|----------|
| `full`   | All current `cout` output (default, backward compatible) |
| `ip`     | Only target IPs and server startup banner; no handshake/relay/debug messages |
| `silent` | No `cout` or `cerr` output |

## Implementation

- `enum Verbosity { FULL, IP, SILENT }` in `Config` struct, default `FULL`
- New `Logger` class with:
  - `log(Verbosity, msg)` — prints to `cout` if `msg_level <= current_level`
  - `error(msg)` — prints to `cerr` unless `SILENT`
- CLI: `-v full|ip|silent`, `--verbose full|ip|silent`
- Logger instance created in `main()`, passed by reference to `handle_client()` → `forward_data()`

## Message classification

| Level | Messages |
|-------|----------|
| FULL  | `[+] Handshake`, `[-] Поддерживается только IPv4`, `[-] Не удалось подключиться`, `[+] Соединение установлено`, `[*] Новое соединение`, `[-] ...завершён` |
| IP    | `[*] Цель: X.X.X.X:PORT`, `[+] SOCKS5 сервер запущен` |
| SILENT| nothing |

## Compatibility

- CLI-only, no config file changes
- Default is `full`, all existing scripts/workflows unaffected
