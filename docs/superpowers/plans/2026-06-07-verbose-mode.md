# Verbose Mode Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add `--verbose`/`-v` CLI flag with levels `full`, `ip`, `silent` to control console output.

**Architecture:** New `Logger` class wraps all `cout`/`cerr` calls with verbosity gating. Single instance created in `main()`, passed by reference to `handle_client()` and `forward_data()`.

**Tech Stack:** C++17, WinSock2, single-file `socks5-cpp/server.cpp`.

---

### Task 1: Add Verbosity enum and Logger class

**Files:**
- Modify: `socks5-cpp/server.cpp:29-40` (after includes, before `Config` struct)

- [ ] **Step 1: Add `enum Verbosity` and `Logger` class after includes**

Insert after `#pragma comment(lib, "winhttp.lib")` (line 28):

```cpp
enum Verbosity { FULL, IP, SILENT };

class Logger {
    Verbosity level_;
public:
    Logger(Verbosity level = FULL) : level_(level) {}

    void setLevel(Verbosity level) { level_ = level; }

    void log(Verbosity msg_level, const std::string& msg) {
        if (level_ != SILENT && msg_level <= level_) {
            std::cout << msg << std::endl;
        }
    }

    void error(const std::string& msg) {
        if (level_ != SILENT) {
            std::cerr << msg << std::endl;
        }
    }
};
```

- [ ] **Step 2: Build check**

Run: `cd socks5-cpp; g++ server.cpp -o server.exe -lws2_32 -lwinhttp -std=c++17 -static 2>&1`
Expected: builds with no errors

- [ ] **Step 3: Commit**

```bash
git add socks5-cpp/server.cpp
git commit -m "feat: add Verbosity enum and Logger class"
```

---

### Task 2: Add `verbosity` field to Config and CLI parsing

**Files:**
- Modify: `socks5-cpp/server.cpp` — `Config` struct, `parse_args()`, `print_help()`

- [ ] **Step 1: Add `Verbosity verbosity` field to Config**

In the `Config` struct (line 32-40), add after `bool new_log = false;`:
```cpp
Verbosity verbosity = FULL;
```

- [ ] **Step 2: Add `-v`/`--verbose` parsing to `parse_args()`**

In `parse_args()`, before the `else` block for `-n`/`--new` (around line 117), add:
```cpp
} else if (match("-v", "--verbose")) {
    const char* v = next();
    if (strcmp(v, "full") == 0) cfg.verbosity = FULL;
    else if (strcmp(v, "ip") == 0) cfg.verbosity = IP;
    else if (strcmp(v, "silent") == 0) cfg.verbosity = SILENT;
    else {
        std::cerr << "Неизвестный уровень verbosity: " << v
                  << " (full/ip/silent)" << std::endl;
        exit(1);
    }
```

- [ ] **Step 3: Add `-v` to help text in `print_help()`**

In `print_help()`, before `<< "  -h, --help"`, add:
```cpp
              << "  -v, --verbose <level> Уровень вывода: full, ip, silent (default: full)\n"
```

- [ ] **Step 4: Build check**

Run: `cd socks5-cpp; g++ server.cpp -o server.exe -lws2_32 -lwinhttp -std=c++17 -static 2>&1`
Expected: builds cleanly

- [ ] **Step 5: Commit**

```bash
git add socks5-cpp/server.cpp
git commit -m "feat: add verbosity field and CLI parsing"
```

---

### Task 3: Wire Logger through main, handle_client, forward_data

**Files:**
- Modify: `socks5-cpp/server.cpp` — signatures of `handle_client()`, `forward_data()`, and `main()`

- [ ] **Step 1: Add `Logger&` parameter to `forward_data()`**

Change signature (line 332):
```cpp
void forward_data(SOCKET src, SOCKET dst, const char* name, Logger& log) {
```

Replace the log line at end of function (line 349):
```cpp
    log.log(FULL, std::string("[-] ") + name + " завершён");
```

- [ ] **Step 2: Add `Logger&` parameter to `handle_client()`**

Change signature (line 356):
```cpp
void handle_client(SOCKET client, IPCollector& collector, Logger& log) {
```

Replace all `std::cout` and `std::cerr` calls inside `handle_client()`:

Line 377 (handshake):
```cpp
    log.log(FULL, "[+] Handshake пройден (no auth)");
```

Line 389 (only ipv4):
```cpp
    log.log(FULL, "[-] Поддерживается только IPv4");
```

Line 415 (target):
```cpp
    log.log(IP, std::string("[*] Цель: ") + ip_str + ":" + std::to_string(port));
```

Line 439 (connect error):
```cpp
    log.log(FULL, "[-] Не удалось подключиться к цели");
```

Line 450 (relay start):
```cpp
    log.log(FULL, "[+] Соединение с целью установлено, начинаю релей");
```

Update thread launches at lines 455-456:
```cpp
    std::thread(forward_data, client, remote, "клиент->цель", std::ref(log)).detach();
    std::thread(forward_data, remote, client, "цель->клиент", std::ref(log)).detach();
```

- [ ] **Step 3: Wire Logger in `main()`**

After Config is fully set up (after the `--new` block, around line 491), create logger:
```cpp
    Logger log(cfg.verbosity);
```

Replace all `std::cout` and `std::cerr` calls in `main()`:

Line 475 (config loaded):
```cpp
    log.log(IP, "[+] Конфиг загружен: " + cfg.config_path);
```

Lines 490-491 (log cleared):
```cpp
    log.log(FULL, "[+] Лог-файл очищен: " + cfg.log_path);
```

Line 557 (server started):
```cpp
    log.log(IP, "[+] SOCKS5 сервер запущен на 0.0.0.0:" + std::to_string(DEFAULT_PORT));
```

Line 566 (new connection):
```cpp
    log.log(FULL, "[*] Новое соединение");
```

Thread launch at line 567:
```cpp
    std::thread(handle_client, client, std::ref(collector), std::ref(log)).detach();
```

- [ ] **Step 4: Build check**

Run: `cd socks5-cpp; g++ server.cpp -o server.exe -lws2_32 -lwinhttp -std=c++17 -static 2>&1`
Expected: builds cleanly

- [ ] **Step 5: Commit**

```bash
git add socks5-cpp/server.cpp
git commit -m "feat: wire Logger through main/handle_client/forward_data"
```

---

### Task 4: Update AGENTS.md

**Files:**
- Modify: `AGENTS.md`

- [ ] **Step 1: Add `-v` to CLI usage examples and docs**

In AGENTS.md, add to the "Full CLI" bullet (line 47):
```
- Full CLI: `-m`/`--mode`, `-i`/`--log-ips`, `-r`/`--router`, `-u`/`--user`, `-p`/`--pass`, `-c`/`--config`, `-n`/`--new`, `-v`/`--verbose`, `-h`/`--help`
```

Add example usage (under testing section or as new row):
```
| Run C++ server (silent mode) | `.\socks5-cpp\server.exe -v silent` |
| Run C++ server (ip-only mode) | `.\socks5-cpp\server.exe -v ip` |
```

- [ ] **Step 2: Commit**

```bash
git add AGENTS.md
git commit -m "docs: add verbose flag to AGENTS.md"
```

---

### Verification

- [ ] **Build with MinGW:** `cd socks5-cpp; g++ server.cpp -o server.exe -lws2_32 -lwinhttp -std=c++17 -static`
- [ ] **Build with CMake:** `cd socks5-cpp; cmake -G Ninja .; ninja`
- [ ] **Smoke test (default):** `.\socks5-cpp\server.exe -m log` → should show all messages
- [ ] **Smoke test (ip):** `.\socks5-cpp\server.exe -m log -v ip` → should show only target IPs and startup banner
- [ ] **Smoke test (silent):** `.\socks5-cpp\server.exe -m log -v silent` → should show nothing
