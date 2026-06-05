# MikroTik Address List Integration: Design Document

## 1. Цель

При каждом SOCKS5-соединении прокси-сервер получает целевой IP-адрес (к кому клиент подключается). Нужно автоматически добавлять эти адреса в `address-list` MikroTik (`rkn_wg_ip`), чтобы роутер маршрутизировал трафик на эти IP через WG-интерфейс.

Каждый IP должен попасть в список ровно один раз, независимо от числа соединений.

## 2. Архитектура

Добавление в существующий C++ сервер (`socks5-cpp/server.cpp`):

```
┌─────────────────┐     каждый вызов handle_client()
│  IPCollector     │◄──── add(ip) — потокобезопасно
│  - seen: set     │
│  - sent: set     │
│  - mutex         │
└────────┬─────────┘
         │ getNewIPs() — раз в 30 сек
         ▼
┌─────────────────┐     POST /rest/ip/firewall/address-list/add
│  MikroTikClient  │────► { .query = "/ip/firewall/address-list/add",
│  - base_url      │       .address = ip }
│  - auth (basic)  │
│  - WinHTTP       │
└─────────────────┘
```

Новые сущности (все в `server.cpp`):

- **`IPCollector`** — потокобезопасное хранилище seen/sent IP
- **`MikroTikClient`** — REST-клиент через WinHTTP (встроен в Windows, линковка `winhttp.lib`)
- **Фоновый поток выгрузки** — просыпается раз в 30 сек, отправляет новые IP

## 3. Компоненты

### 3.1 IPCollector

```cpp
class IPCollector {
    std::set<std::string> seen_;   // все когда-либо встреченные IP
    std::set<std::string> sent_;   // успешно отправленные на MikroTik
    std::mutex mtx_;
public:
    void add(const std::string& ip);         // handle_client вызывает
    std::vector<std::string> getNewIPs();    // фоновый поток вызывает
    void markSent(const std::vector<std::string>& ips);  // после успеха
};
```

`getNewIPs()` возвращает `seen_ \ sent_` (есть в seen, но нет в sent).

### 3.2 MikroTikClient

```cpp
class MikroTikClient {
    std::string base_url_;   // http://192.168.88.1/rest
    std::string auth_;       // base64(login:password)
public:
    MikroTikClient(const std::string& host, const std::string& user,
                   const std::string& pass);
    bool addAddress(const std::string& ip);
};
```

Формат запроса:

```
POST /rest/ip/firewall/address-list/add
Authorization: Basic base64(login:pass)
Content-Type: application/json

{ ".query": "/ip/firewall/address-list/add",
  "address": "1.2.3.4",
  "list": "rkn_wg_ip" }
```

WinHTTP: WinHttpOpen → WinHttpConnect → WinHttpOpenRequest → WinHttpSendRequest → WinHttpReceiveResponse → WinHttpReadData.

### 3.3 Фоновый поток

```cpp
void upload_loop(IPCollector& collector, MikroTikClient& mikrotik) {
    while (true) {
        std::this_thread::sleep_for(30s);
        auto ips = collector.getNewIPs();
        if (ips.empty()) continue;

        bool all_ok = true;
        for (const auto& ip : ips) {
            if (!mikrotik.addAddress(ip)) {
                all_ok = false;
                std::cerr << "[-] Ошибка отправки " << ip << std::endl;
            }
        }
        if (all_ok) {
            collector.markSent(ips);
        }
        // Если ошибка — IP остаются в «неотправленных», 
        // повтор на следующем тике
    }
}
```

### 3.4 Интеграция с handle_client

Добавить одну строку после извлечения IP:

```cpp
std::cout << "[*] Цель: " << ip_str << ":" << port << std::endl;
collector.add(ip_str);   // <-- новая строка
```

## 4. Потокобезопасность

- `IPCollector` под `mutex`. `handle_client` (любой relay-поток) пишет, фоновый поток читает.
- `MikroTikClient` не разделяет состояние — каждый вызов `addAddress()` создаёт свой `HINTERNET`.
- Вывод в `std::cout` в несколько потоков — был и раньше (допустимо для консоли).

## 5. Интерактивный ввод при запуске

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

    // ... далее существующий код ...
}
```

## 6. Обработка ошибок

- Ошибка подключения/авторизации MikroTik: лог в `std::cerr`, повтор на следующем тике
- IP не теряются: остаются в `seen_`, не переходят в `sent_`
- Таймауты: WinHTTP таймаут по умолчанию можно выставить через `WinHttpSetTimeouts`
- При `SOCKET_ERROR` в relay — сервер не крашится, только логирует

## 7. Ограничения

- Базовые адреса (Basic auth) — `login:password`, пароль в открытом виде
- REST API MikroTik на порту 80 (можно задать через `host:port` позже)
- Только `/ip/firewall/address-list/add` — не удаляем старые записи
- Минимальная зависимость: `winhttp.lib` + `ws2_32.lib`
