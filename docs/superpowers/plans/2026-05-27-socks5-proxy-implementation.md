# SOCKS5 Proxy Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a working SOCKS5 TCP CONNECT proxy (IPv4) in Python and C++ for learning networking and socket programming.

**Architecture:** Two independent server binaries sharing the same protocol. A shared test client validates both. Python handles high-level protocol logic with threads; C++ does the same with WinSock2 and manual byte manipulation.

**Tech Stack:** Python 3 (stdlib: `socket`, `threading`), C++17 (WinSock2, CMake), curl / test_client.py for integration testing.

---

### Task 1: Python SOCKS5 Server

**Files:**
- Create: `D:\ПАГНА\socks5-py\server.py`

- [ ] **Step 1: Create server skeleton with socket setup**

Write the initial server that binds to a port and accepts connections:

```python
import socket
import threading

SOCKS5_VERSION = 5
DEFAULT_PORT = 1080

def handle_client(client_socket):
    """Обрабатывает одно SOCKS5-соединение от клиента."""
    pass

def start_server(host="0.0.0.0", port=DEFAULT_PORT):
    """Запускает SOCKS5-прокси сервер."""
    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind((host, port))
    server.listen(5)
    print(f"[+] SOCKS5 сервер запущен на {host}:{port}")
    while True:
        client, addr = server.accept()
        print(f"[*] Новое соединение от {addr}")
        threading.Thread(target=handle_client, args=(client,), daemon=True).start()

if __name__ == "__main__":
    start_server()
```

- [ ] **Step 2: Implement SOCKS5 handshake (Фаза 1)**

Replace `handle_client` with handshake logic:

```python
def handle_client(client):
    try:
        # Фаза 1: Handshake — клиент предлагает методы аутентификации
        greeting = client.recv(2)          # [VER=5, NMETHODS]
        if greeting[0] != SOCKS5_VERSION:
            client.close()
            return
        
        nmethods = greeting[1]             # сколько методов аутентификации
        methods = client.recv(nmethods)    # список методов
        
        # Выбираем метод 0x00 (No Authentication)
        client.send(bytes([SOCKS5_VERSION, 0x00]))
        print("[+] Handshake пройден (no auth)")
        
        # Фаза 2: Запрос — клиент просит соединить с целью
        request = client.recv(4)           # [VER, CMD, RSV, ATYP]
        if request[0] != SOCKS5_VERSION or request[1] != 0x01:
            client.close()
            return
        
        atype = request[3]
        if atype == 0x01:  # IPv4
            addr_bytes = client.recv(4)
            dst_addr = socket.inet_ntoa(addr_bytes)
            port_bytes = client.recv(2)
            dst_port = port_bytes[0] << 8 | port_bytes[1]
        else:
            print("[-] Поддерживается только IPv4")
            client.close()
            return
        
        print(f"[*] Запрос: {dst_addr}:{dst_port}")
        
        # Подключаемся к цели
        remote = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        remote.settimeout(30)
        remote.connect((dst_addr, dst_port))
        
        # Отправляем ответ об успехе
        reply = bytes([SOCKS5_VERSION, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00])
        client.send(reply)
        print("[+] Соединение с целью установлено, начинаю релей")
        
        # Фаза 3: Релей — пересылаем данные в обе стороны
        def forward(src, dst, name):
            """Копирует данные из src в dst пока соединение открыто."""
            try:
                while True:
                    data = src.recv(4096)
                    if not data:
                        break
                    dst.sendall(data)
            except:
                pass
            finally:
                try:
                    src.close()
                except:
                    pass
                try:
                    dst.close()
                except:
                    pass
                print(f"[-] {name} соединение закрыто")
        
        t1 = threading.Thread(target=forward, args=(client, remote, "клиент->цель"))
        t2 = threading.Thread(target=forward, args=(remote, client, "цель->клиент"))
        t1.daemon = True
        t2.daemon = True
        t1.start()
        t2.start()
        t1.join()
        t2.join()
        
    except Exception as e:
        print(f"[-] Ошибка: {e}")
    finally:
        try:
            client.close()
        except:
            pass
```

- [ ] **Step 3: Test the Python server**

Run: `cd D:\ПАГНА\socks5-py; python server.py`

In another terminal:
```powershell
curl --socks5 127.0.0.1:1080 http://example.com
```

Expected: HTML from example.com is returned. Server console shows handshake + request + relay messages.

---

### Task 2: Python Test Client

**Files:**
- Create: `D:\ПАГНА\test_client.py`

- [ ] **Step 1: Write the SOCKS5 test client**

A standalone script that connects through our proxy to a target:

```python
import socket
import sys

SOCKS5_VERSION = 5
PORT = 1080  # порт прокси-сервера

def socks5_connect(proxy_host, proxy_port, target_host, target_port):
    """
    Подключается к target_host:target_port через SOCKS5-прокси.
    Возвращает кортеж (соединение, None) или (None, ошибка).
    """
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(10)
        s.connect((proxy_host, proxy_port))
        
        # Фаза 1: Handshake
        s.send(bytes([SOCKS5_VERSION, 1, 0x00]))  # VER=5, 1 метод, метод=no auth
        response = s.recv(2)
        if response != bytes([SOCKS5_VERSION, 0x00]):
            s.close()
            return None, "Ошибка handshake: сервер не выбрал no auth"
        
        # Фаза 2: Запрос (IPv4)
        addr_bytes = socket.inet_aton(target_host)  # преобразуем "1.2.3.4" в 4 байта
        port_bytes = target_port.to_bytes(2, 'big')
        request = bytes([SOCKS5_VERSION, 0x01, 0x00, 0x01]) + addr_bytes + port_bytes
        s.send(request)
        
        response = s.recv(10)  # [VER, REP, RSV, ATYP, BND.ADDR(4), BND.PORT(2)]
        if response[1] != 0x00:
            error_codes = {0x01: "general failure", 0x02: "not allowed",
                           0x03: "network unreachable", 0x04: "host unreachable",
                           0x05: "connection refused", 0x06: "TTL expired",
                           0x07: "command not supported", 0x08: "addr type not supported"}
            err_msg = error_codes.get(response[1], f"unknown error {response[1]}")
            s.close()
            return None, f"Ошибка соединения с целью: {err_msg}"
        
        print(f"[+] Соединение с {target_host}:{target_port} установлено через прокси")
        return s, None
        
    except socket.timeout:
        return None, "Таймаут при подключении к прокси"
    except Exception as e:
        return None, f"Ошибка: {e}"

def main():
    if len(sys.argv) < 3:
        print("Использование: python test_client.py <целевой_хост> <целевой_порт>")
        print("Пример: python test_client.py example.com 80")
        return
    
    target_host = sys.argv[1]
    target_port = int(sys.argv[2])
    proxy_host = "127.0.0.1"
    proxy_port = PORT
    
    conn, error = socks5_connect(proxy_host, proxy_port, target_host, target_port)
    if error:
        print(f"[-] {error}")
        return
    
    # Отправляем HTTP-запрос
    if target_port == 80:
        request = f"GET / HTTP/1.1\r\nHost: {target_host}\r\nConnection: close\r\n\r\n"
        conn.send(request.encode())
        response = conn.recv(4096)
        print(f"[+] Получено {len(response)} байт ответа:")
        print(response.decode("utf-8", errors="replace")[:500])
    
    conn.close()

if __name__ == "__main__":
    main()
```

- [ ] **Step 2: Test with Python server**

Start Python server in one terminal:
```powershell
cd D:\ПАГНА\socks5-py; python server.py
```

In another terminal:
```powershell
cd D:\ПАГНА; python test_client.py example.com 80
```

Expected: HTTP response from example.com printed.

---

### Task 3: C++ SOCKS5 Server with CMake

**Files:**
- Create: `D:\ПАГНА\socks5-cpp\server.cpp`
- Create: `D:\ПАГНА\socks5-cpp\CMakeLists.txt`

- [ ] **Step 1: Create CMakeLists.txt**

```cmake
cmake_minimum_required(VERSION 3.10)
project(socks5-proxy LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
add_executable(server server.cpp)

# Получаем флаги компилятора для winsock2
if(WIN32)
    target_link_libraries(server ws2_32)
endif()
```

- [ ] **Step 2: Write the C++ server**

```cpp
// SOCKS5 прокси-сервер на C++ (WinSock2)
// Компиляция: используй CLion (открой папку socks5-cpp как проект)
// Или вручную: g++ server.cpp -o server -lws2_32

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <thread>
#include <cstring>

#pragma comment(lib, "ws2_32.lib")

const int SOCKS5_VERSION = 5;
const int DEFAULT_PORT = 1080;
const int BUFFER_SIZE = 8192;

// Пересылает данные из src в dst пока соединение открыто
void forward_data(SOCKET src, SOCKET dst, const char* name) {
    char buf[BUFFER_SIZE];
    int n;
    while ((n = recv(src, buf, BUFFER_SIZE, 0)) > 0) {
        int total_sent = 0;
        while (total_sent < n) {
            int sent = send(dst, buf + total_sent, n - total_sent, 0);
            if (sent == SOCKET_ERROR) {
                closesocket(src);
                closesocket(dst);
                return;
            }
            total_sent += sent;
        }
    }
    closesocket(src);
    closesocket(dst);
    std::cout << "[-] " << name << " соединение закрыто" << std::endl;
}

// Обрабатывает одного клиента: handshake -> запрос -> релей
void handle_client(SOCKET client) {
    char buf[BUFFER_SIZE];

    // Фаза 1: Handshake
    // Клиент присылает: [VER, NMETHODS, METHODS...]
    int n = recv(client, buf, 2, 0);
    if (n != 2 || buf[0] != SOCKS5_VERSION) {
        closesocket(client);
        return;
    }

    int nmethods = buf[1];
    n = recv(client, buf, nmethods, 0);  // методы нас не волнуют
    if (n != nmethods) {
        closesocket(client);
        return;
    }

    // Отвечаем: выбираем No Authentication (0x00)
    char handshake_response[] = {SOCKS5_VERSION, 0x00};
    send(client, handshake_response, 2, 0);
    std::cout << "[+] Handshake пройден (no auth)" << std::endl;

    // Фаза 2: Запрос
    // [VER=5, CMD=0x01(CONNECT), RSV=0x00, ATYP, DST.ADDR, DST.PORT]
    n = recv(client, buf, 4, 0);
    if (n != 4 || buf[0] != SOCKS5_VERSION || buf[1] != 0x01) {
        closesocket(client);
        return;
    }

    char atype = buf[3];
    if (atype != 0x01) {
        std::cout << "[-] Поддерживается только IPv4" << std::endl;
        closesocket(client);
        return;
    }

    // Читаем IPv4 адрес (4 байта) и порт (2 байта)
    char addr_port[6];
    n = recv(client, addr_port, 6, 0);
    if (n != 6) {
        closesocket(client);
        return;
    }

    // Форматируем IP в строку для вывода
    sockaddr_in target_addr = {};
    target_addr.sin_family = AF_INET;
    std::memcpy(&target_addr.sin_addr, addr_port, 4);       // копируем 4 байта IP
    std::memcpy(&target_addr.sin_port, addr_port + 4, 2);   // копируем 2 байта порта

    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &target_addr.sin_addr, ip_str, sizeof(ip_str));
    int port = ntohs(target_addr.sin_port);
    std::cout << "[*] Запрос: " << ip_str << ":" << port << std::endl;

    // Подключаемся к цели
    SOCKET remote = socket(AF_INET, SOCK_STREAM, 0);
    if (remote == INVALID_SOCKET) {
        closesocket(client);
        return;
    }

    // Таймаут на connect (5 секунд)
    DWORD timeout = 5000;
    setsockopt(remote, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
    setsockopt(remote, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));

    if (connect(remote, (sockaddr*)&target_addr, sizeof(target_addr)) == SOCKET_ERROR) {
        std::cout << "[-] Не удалось подключиться к цели" << std::endl;
        closesocket(client);
        closesocket(remote);
        return;
    }

    // Отправляем ответ об успехе
    // [VER=5, REP=0, RSV=0, ATYP=1, BND.ADDR=0.0.0.0, BND.PORT=0]
    char reply[] = {5, 0, 0, 1, 0, 0, 0, 0, 0, 0};
    send(client, reply, sizeof(reply), 0);
    std::cout << "[+] Соединение с целью установлено, начинаю релей" << std::endl;

    // Фаза 3: Релей — два потока для дуплексной передачи
    std::thread(forward_data, client, remote, "клиент->цель").detach();
    std::thread(forward_data, remote, client, "цель->клиент").detach();
}

int main() {
    // Инициализация WinSock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "[-] Ошибка WSAStartup" << std::endl;
        return 1;
    }

    // Создаём серверный сокет
    SOCKET server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server == INVALID_SOCKET) {
        std::cerr << "[-] Ошибка socket()" << std::endl;
        WSACleanup();
        return 1;
    }

    // Привязываем к порту
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(DEFAULT_PORT);
    addr.sin_addr.s_addr = INADDR_ANY;  // 0.0.0.0

    if (bind(server, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        std::cerr << "[-] Ошибка bind()" << std::endl;
        closesocket(server);
        WSACleanup();
        return 1;
    }

    // Начинаем слушать
    if (listen(server, SOMAXCONN) == SOCKET_ERROR) {
        std::cerr << "[-] Ошибка listen()" << std::endl;
        closesocket(server);
        WSACleanup();
        return 1;
    }

    std::cout << "[+] SOCKS5 сервер запущен на 0.0.0.0:" << DEFAULT_PORT << std::endl;

    // Главный цикл: принимаем соединения
    while (true) {
        SOCKET client = accept(server, nullptr, nullptr);
        if (client == INVALID_SOCKET) {
            std::cerr << "[-] Ошибка accept()" << std::endl;
            continue;
        }
        std::cout << "[*] Новое соединение" << std::endl;
        // Запускаем обработку в отдельном потоке
        std::thread(handle_client, client).detach();
    }

    closesocket(server);
    WSACleanup();
    return 0;
}
```

- [ ] **Step 3: Build and test**

Build in CLion (open `D:\ПАГНА\socks5-cpp` as project, click Build).

Or via command line with MinGW:
```powershell
cd D:\ПАГНА\socks5-cpp
g++ server.cpp -o server.exe -lws2_32 -std=c++17
.\server.exe
```

Test in another terminal:
```powershell
cd D:\ПАГНА; python test_client.py example.com 80
```

Or with curl:
```powershell
curl --socks5 127.0.0.1:1080 http://example.com
```

Expected: HTTP response from example.com.

---

### Task 4: Notes on the differences

После того как обе реализации заработают, полезно сравнить:

**Python vs C++ — ключевые различия:**

| Аспект | Python | C++ |
|--------|--------|-----|
| Инициализация сокетов | `socket.socket(...)` — одна строка | `WSAStartup()` + `socket()` + проверка `INVALID_SOCKET` |
| Парсинг байтов | `addr_bytes = client.recv(4)` / `socket.inet_ntoa()` | `recv()` в `char[]` + `inet_ntop()` + `memcpy` |
| Конвертация порядка байт | Автоматическая | Явная: `htons()`, `ntohs()`, `big-endian` вручную |
| Отправка данных | `sendall()` — гарантирует отправку всех байт | Цикл `send()` — проверять `SOCKET_ERROR` и досылать |
| Закрытие соединения | GC + `close()` — прощает забывчивость | Явный `closesocket()` — утечка дескрипторов |
| Диспатч потоков | `threading.Thread(...).start()` | `std::thread(...).detach()` — ручное управление жизнью |
| Обработка ошибок | `try/except` — перехватывает всё | `SOCKET_ERROR == -1` — проверять каждый вызов |
| Управление памятью | Автоматическое (GC) | Вручную: `char buf[8192]` на стеке |
