# IP File Logging Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add `--log-ips[=path]` CLI flag to C++ SOCKS5 server that writes each target IP to a text file for testing/analysis.

**Architecture:** Global `FILE*` + `std::mutex` for thread-safe append from `handle_client`. CLI flag parsed manually from `argv`. Append mode (`"a"`), one IP per line, `fflush` after each write.

**Tech Stack:** C++17, WinSock2, stdio

**Files:**
- Modify: `socks5-cpp/server.cpp`

---

### Task 1: Add global state (`log_file` + `file_mutex`)

**Files:**
- Modify: `socks5-cpp/server.cpp` — add globals before `main()`, after existing `#include` and `#pragma` lines, before `IPCollector` class

- [ ] **Step 1: Add global variables**

After `#pragma comment(lib, "winhttp.lib")` (line 25), add:

```cpp
#include <cstdio>
#include <mutex>

static FILE* log_file = nullptr;
static std::mutex file_mutex;
```

`<cstdio>` and `<mutex>` may already be included — if so, just add the two statics.

- [ ] **Step 2: Verify it compiles**

Run: `cd socks5-cpp; g++ server.cpp -o server.exe -lws2_32 -std=c++17`
Expected: clean compile, `server.exe` produced. Ignore unused-variable warnings for now.

---

### Task 2: Parse `--log-ips` flag in `main()`

**Files:**
- Modify: `socks5-cpp/server.cpp` — add argument parsing at the top of `main()`, before `SetConsoleOutputCP`

- [ ] **Step 1: Add argument parsing**

Replace the start of `main()` from:

```cpp
int main() {
    SetConsoleOutputCP(CP_UTF8);
```

to:

```cpp
int main(int argc, char* argv[]) {
    const char* log_path = "ips.log";
    bool log_enabled = false;

    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "--log-ips", 9) == 0) {
            log_enabled = true;
            const char* eq = strchr(argv[i], '=');
            if (eq) {
                log_path = eq + 1;
            }
        }
    }

    if (log_enabled) {
        log_file = fopen(log_path, "a");
        if (!log_file) {
            std::cerr << "[-] Не удалось открыть " << log_path << " для записи"
                      << std::endl;
        }
    }

    SetConsoleOutputCP(CP_UTF8);
```

Add `#include <cstring>` at the top of the file (for `strncmp`, `strchr`) — check if it's already included via other headers; `<ws2tcpip.h>` or `<cstring>` may already provide it. If `<cstring>` is already included (line 13: `#include <cstring>`), no need to add.

- [ ] **Step 2: Build and verify flag parsing**

Run: `cd socks5-cpp; g++ server.cpp -o server.exe -lws2_32 -std=c++17`
Expected: clean compile.

Run: `.\socks5-cpp\server.exe --log-ips`
Expected: MikroTik prompts appear as before, `ips.log` created (empty file).

---

### Task 3: Write IP to file in `handle_client()`

**Files:**
- Modify: `socks5-cpp/server.cpp` — add write after `collector.add(ip_str)` in `handle_client()`

- [ ] **Step 1: Add file write in handle_client**

Find the two lines in `handle_client()` (around line 241-242):

```cpp
    std::cout << "[*] Цель: " << ip_str << ":" << port << std::endl;
    collector.add(ip_str);
```

Replace with:

```cpp
    std::cout << "[*] Цель: " << ip_str << ":" << port << std::endl;
    collector.add(ip_str);
    if (log_file) {
        std::lock_guard<std::mutex> lock(file_mutex);
        fprintf(log_file, "%s\n", ip_str);
        fflush(log_file);
    }
```

- [ ] **Step 2: Build and test end-to-end**

Run: `cd socks5-cpp; g++ server.cpp -o server.exe -lws2_32 -std=c++17`
Expected: clean compile.

Start server with flag: `.\socks5-cpp\server.exe --log-ips`
(Enter dummy MikroTik credentials when prompted)

In another terminal: `curl --socks5 127.0.0.1:1080 http://example.com`

Verify `ips.log` contains the target IP (e.g., `93.184.216.34` for example.com).

- [ ] **Step 3: Test without flag (regression)**

Run: `.\socks5-cpp\server.exe`
Expected: no `ips.log` created, no file-writing behavior, everything works as before.

- [ ] **Step 4: Test with custom path**

Run: `.\socks5-cpp\server.exe --log-ips=C:\temp\test_ips.txt`
Expected: IPs written to `C:\temp\test_ips.txt`.

---

### Task 4: Update AGENTS.md with new command

**Files:**
- Modify: `AGENTS.md`

- [ ] **Step 1: Add `--log-ips` to commands table**

In the Commands table, add a row:

```
| Log IPs to file | `.\socks5-cpp\server.exe --log-ips` (default: `ips.log`) |
```
