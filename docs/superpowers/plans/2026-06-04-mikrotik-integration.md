# MikroTik Address List Integration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add IPs from SOCKS5 proxy connections to MikroTik `rkn_wg_ip` address list via REST API.

**Architecture:** IPCollector stores seen/sent IPs threadsafely; MikroTikClient sends new IPs via WinHTTP POST; background upload thread runs every 30s.

**Tech Stack:** C++17, WinSock2, WinHTTP, MikroTik REST API

**Files:**
- Modify: `socks5-cpp/server.cpp`
- Modify: `socks5-cpp/CMakeLists.txt`

---

### Task 1: Add `winhttp` link dependency to CMakeLists.txt

**Files:**
- Modify: `socks5-cpp/CMakeLists.txt`

- [ ] **Step 1: Add winhttp to target_link_libraries**

```
if(WIN32)
    target_link_libraries(server ws2_32 winhttp)
endif()
```

---

### Task 2: Add `IPCollector` class to server.cpp

**Files:**
- Modify: `socks5-cpp/server.cpp` (before `forward_data` function)

- [ ] **Step 1: Add includes and IPCollector class**

After `#pragma comment(lib, "ws2_32.lib")`, add:
```cpp
#include <set>
#include <mutex>
#include <vector>
#include <string>
#include <sstream>
#include <thread>
#include <chrono>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
```

Before `void forward_data(...)`, add:
```cpp
class IPCollector {
    std::set<std::string> seen_;
    std::set<std::string> sent_;
    std::mutex mtx_;
public:
    void add(const std::string& ip) {
        std::lock_guard<std::mutex> lock(mtx_);
        seen_.insert(ip);
    }

    std::vector<std::string> getNewIPs() {
        std::lock_guard<std::mutex> lock(mtx_);
        std::vector<std::string> new_ips;
        for (const auto& ip : seen_) {
            if (sent_.find(ip) == sent_.end()) {
                new_ips.push_back(ip);
            }
        }
        return new_ips;
    }

    void markSent(const std::vector<std::string>& ips) {
        std::lock_guard<std::mutex> lock(mtx_);
        for (const auto& ip : ips) {
            sent_.insert(ip);
        }
    }
};
```

---

### Task 3: Add `MikroTikClient` class to server.cpp

**Files:**
- Modify: `socks5-cpp/server.cpp` (after IPCollector class, before `forward_data`)

- [ ] **Step 1: Add MikroTikClient class**

```cpp
class MikroTikClient {
    std::string host_;
    std::wstring user_, pass_;
public:
    MikroTikClient(const std::string& host, const std::string& user,
                   const std::string& pass) : host_(host) {
        int len = MultiByteToWideChar(CP_UTF8, 0, user.c_str(), -1, NULL, 0);
        user_.resize(len - 1);
        MultiByteToWideChar(CP_UTF8, 0, user.c_str(), -1, &user_[0], len);

        len = MultiByteToWideChar(CP_UTF8, 0, pass.c_str(), -1, NULL, 0);
        pass_.resize(len - 1);
        MultiByteToWideChar(CP_UTF8, 0, pass.c_str(), -1, &pass_[0], len);
    }

    bool addAddress(const std::string& ip) {
        HINTERNET hSession = WinHttpOpen(L"MikroTik Updater/1.0",
                                          WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                          NULL, NULL, 0);
        if (!hSession) return false;

        int wlen = MultiByteToWideChar(CP_UTF8, 0, host_.c_str(), -1, NULL, 0);
        wchar_t* whost = new wchar_t[wlen];
        MultiByteToWideChar(CP_UTF8, 0, host_.c_str(), -1, whost, wlen);

        HINTERNET hConnect = WinHttpConnect(hSession, whost,
                                             INTERNET_DEFAULT_HTTP_PORT, 0);
        delete[] whost;
        if (!hConnect) {
            WinHttpCloseHandle(hSession);
            return false;
        }

        HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST",
                                                 L"/rest/ip/firewall/address-list/add",
                                                 NULL, NULL, NULL, 0);
        if (!hRequest) {
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return false;
        }

        WinHttpSetCredentials(hRequest, WINHTTP_AUTH_TARGET_SERVER,
                               WINHTTP_AUTH_SCHEME_BASIC,
                               user_.c_str(), pass_.c_str(), NULL);

        std::string body = "{\".query\":\"/ip/firewall/address-list/add\","
                           "\"address\":\"" + ip + "\","
                           "\"list\":\"rkn_wg_ip\"}";

        LPCWSTR headers = L"Content-Type: application/json";

        BOOL sent = WinHttpSendRequest(hRequest,
                                        headers, (DWORD)wcslen(headers),
                                        (LPVOID)body.c_str(), (DWORD)body.size(),
                                        (DWORD)body.size(), 0);

        bool ok = false;
        if (sent && WinHttpReceiveResponse(hRequest, NULL)) {
            DWORD status = 0;
            DWORD status_size = sizeof(status);
            WinHttpQueryHeaders(hRequest,
                                 WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                 NULL, &status, &status_size, NULL);
            ok = (status == 200);
        }

        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return ok;
    }
};
```

---

### Task 4: Add upload loop and integrate into `main()`

**Files:**
- Modify: `socks5-cpp/server.cpp` (after MikroTikClient, before `main`)

- [ ] **Step 1: Add upload_loop function**

```cpp
void upload_loop(IPCollector& collector, MikroTikClient& mikrotik) {
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(30));
        auto ips = collector.getNewIPs();
        if (ips.empty()) continue;

        bool all_ok = true;
        for (const auto& ip : ips) {
            if (!mikrotik.addAddress(ip)) {
                all_ok = false;
                std::cerr << "[-] Ошибка отправки " << ip
                          << " на MikroTik" << std::endl;
            }
        }
        if (all_ok) {
            collector.markSent(ips);
        }
    }
}
```

- [ ] **Step 2: Modify `main()` — add interactive input and background thread**

Replace existing `main()` with:

```cpp
int main() {
    SetConsoleOutputCP(CP_UTF8);

    std::string mikrotik_host, mikrotik_user, mikrotik_pass;
    std::cout << "MikroTik IP: ";
    std::cin >> mikrotik_host;
    std::cout << "Логин: ";
    std::cin >> mikrotik_user;
    std::cout << "Пароль: ";
    std::cin >> mikrotik_pass;

    IPCollector collector;
    MikroTikClient mikrotik(mikrotik_host, mikrotik_user, mikrotik_pass);
    std::thread upload(upload_loop, std::ref(collector), std::ref(mikrotik));
    upload.detach();

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

---

### Task 5: Integrate collector into `handle_client`

**Files:**
- Modify: `socks5-cpp/server.cpp`

- [ ] **Step 1: Change handle_client signature to accept collector reference**

Update declaration from:
```cpp
void handle_client(SOCKET client) {
```
to:
```cpp
void handle_client(SOCKET client, IPCollector& collector) {
```

- [ ] **Step 2: Add collector.add() call after extracting IP**

After line:
```cpp
std::cout << "[*] Цель: " << ip_str << ":" << port << std::endl;
```
add:
```cpp
        collector.add(ip_str);
```

---

### Task 6: Rebuild and test

**Files:**
- Run: compile in `socks5-cpp/`

- [ ] **Step 1: Reconfigure CMake and rebuild**

```bash
cd D:\ПАГНА\socks5-cpp
cmake -G "Ninja" .
ninja
```

Expected: clean build with no errors, `server.exe` produced.

- [ ] **Step 2: Run and verify interactive input appears**

```bash
.\server.exe
```

Expected: prompts "MikroTik IP:", "Логин:", "Пароль:" in proper UTF-8.

- [ ] **Step 3: Quick functional test with curl**

```bash
# in another terminal:
curl --socks5 127.0.0.1:1080 http://example.com
```

Expected: server logs the target IP and eventually sends it to MikroTik.
