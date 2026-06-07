# CLI Overhaul Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace basic `--log-ips` flag with full CLI: modes (`log`/`send`/`upload`), INI config, interactive first-run setup, all flags with short aliases.

**Architecture:** All in `server.cpp`. New functions: `parse_args`, `read_config`, `write_config`, `interactive_setup`, `run_upload`, `print_help`. Existing classes (`IPCollector`, `MikroTikClient`, `upload_loop`, `forward_data`, `handle_client`) unchanged. `main()` becomes a dispatcher.

**Tech Stack:** C++17, WinSock2, WinHTTP

**Files:**
- Modify: `socks5-cpp/server.cpp`

---

### Task 1: Add struct Config + print_help()

**Files:**
- Modify: `socks5-cpp/server.cpp` — add after `static std::mutex file_mutex;` (line 29), before `// Константы протокола SOCKS5`

- [ ] **Step 1: Add Config struct and print_help**

After line 29, before `// Константы протокола` comment, add:

```cpp
struct Config {
    enum Mode { LOG, SEND, UPLOAD } mode = LOG;
    std::string log_path = "ips.log";
    std::string config_path = "config.ini";
    std::string router_host;
    std::string router_user;
    std::string router_pass;
    bool new_log = false;
};

void print_help() {
    std::cout << "Использование: server.exe [ключи]\n"
              << "\n"
              << "Ключи:\n"
              << "  -m, --mode <mode>    Режим: log, send, upload (default: log)\n"
              << "  -i, --log-ips <path> Путь к лог-файлу (default: ips.log)\n"
              << "  -r, --router <host>  Адрес MikroTik\n"
              << "  -u, --user <user>    Имя пользователя MikroTik\n"
              << "  -p, --pass <pass>    Пароль (если нет — запросим)\n"
              << "  -c, --config <path>  Путь к config.ini (default: config.ini)\n"
              << "  -n, --new            Очистить лог-файл перед стартом\n"
              << "  -h, --help           Эта справка\n"
              << std::endl;
}
```

- [ ] **Step 2: Build check**

Run: `$env:Path = "C:\msys64\ucrt64\bin;" + $env:Path; cd socks5-cpp; g++ server.cpp -o server.exe -lws2_32 -lwinhttp -std=c++17`
Expected: clean compile (new struct unused, but no errors).

- [ ] **Step 3: Commit**

```bash
git add socks5-cpp/server.cpp
git commit -m "Add Config struct and print_help()"
```

---

### Task 2: Add parse_args()

**Files:**
- Modify: `socks5-cpp/server.cpp` — add after `print_help()`, before `class IPCollector`

- [ ] **Step 1: Add parse_args function**

After `print_help()` (end of Task 1), before `class IPCollector`, add:

```cpp
void parse_args(int argc, char* argv[], Config& cfg) {
    for (int i = 1; i < argc; i++) {
        const char* a = argv[i];

        auto match = [&](const char* s, const char* l) {
            return strcmp(a, s) == 0 || strcmp(a, l) == 0;
        };
        auto next = [&]() -> const char* {
            if (++i >= argc) {
                std::cerr << "Ошибка: " << a << " требует значение" << std::endl;
                exit(1);
            }
            return argv[i];
        };

        if (match("-h", "--help")) {
            print_help();
            exit(0);
        } else if (match("-m", "--mode")) {
            const char* v = next();
            if (strcmp(v, "log") == 0) cfg.mode = Config::LOG;
            else if (strcmp(v, "send") == 0) cfg.mode = Config::SEND;
            else if (strcmp(v, "upload") == 0) cfg.mode = Config::UPLOAD;
            else {
                std::cerr << "Неизвестный режим: " << v << " (log/send/upload)" << std::endl;
                exit(1);
            }
        } else if (match("-i", "--log-ips")) {
            cfg.log_path = next();
        } else if (match("-r", "--router")) {
            cfg.router_host = next();
        } else if (match("-u", "--user")) {
            cfg.router_user = next();
        } else if (match("-p", "--pass")) {
            cfg.router_pass = next();
        } else if (match("-c", "--config")) {
            cfg.config_path = next();
        } else if (match("-n", "--new")) {
            cfg.new_log = true;
        } else {
            std::cerr << "Неизвестный ключ: " << a << std::endl;
            print_help();
            exit(1);
        }
    }
}
```

- [ ] **Step 2: Build check**

Run: `g++ server.cpp -o server.exe -lws2_32 -lwinhttp -std=c++17`
Expected: clean compile (parse_args defined but not called yet).

- [ ] **Step 3: Commit**

```bash
git add socks5-cpp/server.cpp
git commit -m "Add parse_args() with all CLI flags"
```

---

### Task 3: Add config read/write functions

**Files:**
- Modify: `socks5-cpp/server.cpp` — add after `parse_args()`, before `class IPCollector`

- [ ] **Step 1: Add read_config**

After `parse_args()`, add:

```cpp
bool read_config(const std::string& path, Config& cfg) {
    FILE* f = fopen(path.c_str(), "r");
    if (!f) return false;

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = '\0';

        if (strncmp(line, "host=", 5) == 0) cfg.router_host = line + 5;
        else if (strncmp(line, "user=", 5) == 0) cfg.router_user = line + 5;
        else if (strncmp(line, "pass=", 5) == 0) cfg.router_pass = line + 5;
    }
    fclose(f);
    return true;
}
```

- [ ] **Step 2: Add write_config**

After `read_config()`, add:

```cpp
bool write_config(const std::string& path, const Config& cfg) {
    FILE* f = fopen(path.c_str(), "w");
    if (!f) return false;
    fprintf(f, "[router]\n");
    fprintf(f, "host=%s\n", cfg.router_host.c_str());
    fprintf(f, "user=%s\n", cfg.router_user.c_str());
    fprintf(f, "pass=%s\n", cfg.router_pass.c_str());
    fclose(f);
    return true;
}
```

- [ ] **Step 3: Build check**

Run: `g++ server.cpp -o server.exe -lws2_32 -lwinhttp -std=c++17`
Expected: clean compile.

- [ ] **Step 4: Commit**

```bash
git add socks5-cpp/server.cpp
git commit -m "Add read_config() and write_config()"
```

---

### Task 4: Add interactive_setup()

**Files:**
- Modify: `socks5-cpp/server.cpp` — add after `write_config()`, before `class IPCollector`

- [ ] **Step 1: Add interactive_setup**

After `write_config()`, add:

```cpp
void interactive_setup(Config& cfg) {
    std::string input;

    std::cout << "Режим (log/send/upload): ";
    std::cin >> input;
    if (input == "send") cfg.mode = Config::SEND;
    else if (input == "upload") cfg.mode = Config::UPLOAD;
    else cfg.mode = Config::LOG;

    if (cfg.mode != Config::LOG) {
        std::cout << "MikroTik IP: ";
        std::cin >> cfg.router_host;
        std::cout << "Логин: ";
        std::cin >> cfg.router_user;
        if (cfg.router_pass.empty()) {
            std::cout << "Пароль: ";
            std::cin >> cfg.router_pass;
        }
    }

    if (cfg.mode != Config::UPLOAD) {
        std::cout << "Лог-файл [" << cfg.log_path << "]: ";
        std::cin >> input;
        if (!input.empty()) cfg.log_path = input;
    }

    write_config(cfg.config_path, cfg);
    std::cout << "[+] Конфиг сохранён в " << cfg.config_path << std::endl;
}
```

- [ ] **Step 2: Build check**

Run: `g++ server.cpp -o server.exe -lws2_32 -lwinhttp -std=c++17`
Expected: clean compile.

- [ ] **Step 3: Commit**

```bash
git add socks5-cpp/server.cpp
git commit -m "Add interactive_setup()"
```

---

### Task 5: Add run_upload()

**Files:**
- Modify: `socks5-cpp/server.cpp` — add after `interactive_setup()`, before `class IPCollector`

- [ ] **Step 1: Add run_upload**

After `interactive_setup()`, add:

```cpp
void run_upload(const std::string& path, MikroTikClient& client) {
    FILE* f = fopen(path.c_str(), "r");
    if (!f) {
        std::cerr << "[-] Не удалось открыть " << path << std::endl;
        return;
    }

    char line[64];
    int total = 0, ok = 0;
    while (fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = '\0';
        if (len == 0) continue;

        total++;
        if (client.addAddress(line)) ok++;
        else std::cerr << "[-] Ошибка отправки: " << line << std::endl;
    }
    fclose(f);

    std::cout << "[+] Загрузка завершена: " << ok << "/" << total << " OK" << std::endl;
}
```

- [ ] **Step 2: Build check**

Run: `g++ server.cpp -o server.exe -lws2_32 -lwinhttp -std=c++17`
Expected: clean compile.

- [ ] **Step 3: Commit**

```bash
git add socks5-cpp/server.cpp
git commit -m "Add run_upload() for one-shot IP upload mode"
```

---

### Task 6: Refactor main() — dispatcher

**Files:**
- Modify: `socks5-cpp/server.cpp` — replace `main()` (lines 289-374) entirely

- [ ] **Step 1: Replace main() with dispatcher**

Delete everything from `int main(int argc, char* argv[]) {` (line 289) to the final `}` (line 374). Replace with:

```cpp
int main(int argc, char* argv[]) {
    SetConsoleOutputCP(CP_UTF8);

    Config cfg;
    bool has_cli_flags = argc > 1;

    // Always try config first (provides defaults for CLI override)
    bool config_found = read_config(cfg.config_path, cfg);

    // CLI flags override config values
    if (has_cli_flags) {
        parse_args(argc, argv, cfg);
    }

    // Without CLI flags: use config or interactive
    if (!has_cli_flags && config_found) {
        std::cout << "[+] Конфиг загружен: " << cfg.config_path << std::endl;
        cfg.mode = Config::SEND;
        if (cfg.router_pass.empty()) {
            std::cout << "Пароль: ";
            std::cin >> cfg.router_pass;
        }
    }

    if (!has_cli_flags && !config_found) {
        interactive_setup(cfg);
    }

    // --new: clear log file
    if (cfg.new_log) {
        FILE* f = fopen(cfg.log_path.c_str(), "w");
        if (f) fclose(f);
        std::cout << "[+] Лог-файл очищен: " << cfg.log_path << std::endl;
    }

    // Upload mode: one-shot, no server
    if (cfg.mode == Config::UPLOAD) {
        if (cfg.router_host.empty()) {
            std::cerr << "[-] Укажите адрес MikroTik (--router) для режима upload"
                      << std::endl;
            return 1;
        }
        MikroTikClient mikrotik(cfg.router_host, cfg.router_user, cfg.router_pass);
        run_upload(cfg.log_path, mikrotik);
        return 0;
    }

    // Server modes: LOG or SEND
    log_file = fopen(cfg.log_path.c_str(), "a");
    if (!log_file) {
        std::cerr << "[-] Не удалось открыть " << cfg.log_path << " для записи"
                  << std::endl;
    }

    IPCollector collector;

    if (cfg.mode == Config::SEND) {
        if (cfg.router_host.empty()) {
            std::cerr << "[-] Укажите адрес MikroTik (--router) для режима send"
                      << std::endl;
            return 1;
        }
        MikroTikClient mikrotik(cfg.router_host, cfg.router_user, cfg.router_pass);
        std::thread upload(upload_loop, std::ref(collector), std::ref(mikrotik));
        upload.detach();
    }

    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "[-] Ошибка WSAStartup" << std::endl;
        return 1;
    }

    SOCKET server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server == INVALID_SOCKET) {
        std::cerr << "[-] Ошибка socket()" << std::endl;
        WSACleanup();
        return 1;
    }

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(DEFAULT_PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        std::cerr << "[-] Ошибка bind()" << std::endl;
        closesocket(server);
        WSACleanup();
        return 1;
    }

    if (listen(server, SOMAXCONN) == SOCKET_ERROR) {
        std::cerr << "[-] Ошибка listen()" << std::endl;
        closesocket(server);
        WSACleanup();
        return 1;
    }

    std::cout << "[+] SOCKS5 сервер запущен на 0.0.0.0:"
              << DEFAULT_PORT << std::endl;

    while (true) {
        SOCKET client = accept(server, nullptr, nullptr);
        if (client == INVALID_SOCKET) {
            std::cerr << "[-] Ошибка accept()" << std::endl;
            continue;
        }
        std::cout << "[*] Новое соединение" << std::endl;
        std::thread(handle_client, client, std::ref(collector)).detach();
    }

    closesocket(server);
    WSACleanup();
    return 0;
}
```

- [ ] **Step 2: Build check**

Run: `g++ server.cpp -o server.exe -lws2_32 -lwinhttp -std=c++17`
Expected: clean compile.

- [ ] **Step 3: Quick smoke tests**

```bash
# --help should print and exit
.\socks5-cpp\server.exe --help
# Expected: help text printed, exits immediately (no prompts)

# --mode log without router should start server
echo "192.168.1.1`nadmin`npass`n" | .\socks5-cpp\server.exe -m log -i test_ips.log
# Expected: server starts, test_ips.log created (empty)

# --mode send without CLI flags, no config — should enter interactive
# (Skip this automated test — requires interactive input)
```

- [ ] **Step 4: Commit**

```bash
git add socks5-cpp/server.cpp
git commit -m "Refactor main() into mode dispatcher"
```

---

### Task 7: Update AGENTS.md

**Files:**
- Modify: `AGENTS.md`

- [ ] **Step 1: Update commands table**

Replace the existing Run C++ server and Log IPs rows with comprehensive usage:

```
| Run C++ server (log mode) | `.\socks5-cpp\server.exe` (или с config.ini) |
| Run C++ server (send mode) | `.\socks5-cpp\server.exe -m send -r 10.0.0.1 -u admin` |
| Upload log to MikroTik | `.\socks5-cpp\server.exe -m upload -r 10.0.0.1 -u admin` |
| Log IPs to file | `.\socks5-cpp\server.exe -m log -i ips.log` |
| Config file | `.\socks5-cpp\server.exe -c myconfig.ini` |
| Full help | `.\socks5-cpp\server.exe --help` |
```

- [ ] **Step 2: Commit**

```bash
git add AGENTS.md
git commit -m "AGENTS.md: update for CLI overhaul"
```
