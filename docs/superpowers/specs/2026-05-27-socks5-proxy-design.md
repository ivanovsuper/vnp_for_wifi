# SOCKS5 Proxy: Design Document

## 1. Цель проекта

Создать две независимые реализации SOCKS5-прокси (RFC 1928) на Python и C++ для:

- Обучения работе с сокетами, сетевыми протоколами и межъязыковыми различиями
- Получения рабочего прокси-сервера для личного использования
- Фундамента для будущего расширения до полноценного VPN (добавление UDP, шифрования, tun)

## 2. Архитектура

```
D:\ПАГНА\
├── socks5-py\               # Python-реализация
│   └── server.py            # точка входа (запуск, accept, dispatch)
├── socks5-cpp\              # C++-реализация
│   ├── server.cpp           # точка входа + WinSock2
│   └── CMakeLists.txt       # сборка (MinGW / MSVC)
└── test_client.py           # клиент для тестирования обеих реализаций
```

Клиент и сервер — отдельные процессы. Клиент подключается через любой SOCKS5-совместимый софт (curl --socks5, Firefox, Telegram) или через `test_client.py`.

## 3. Протокол SOCKS5 (RFC 1928)

### Фаза 1: Handshake (аутентификация)

```
Клиент → Сервер: [VER=5, NMETHODS=1, METHODS=[0x00]]
Сервер → Клиент: [VER=5, METHOD=0x00]    # no auth
```

Сейчас поддерживаем только `0x00 (No Authentication)`.

### Фаза 2: Запрос

```
Клиент → Сервер:
[VER=5, CMD=0x01, RSV=0x00, ATYP, DST.ADDR, DST.PORT]
```

- `CMD=0x01` = TCP CONNECT (только он, BIND/UDP позже)
- `ATYP` = `0x01` (IPv4, 4 байта)
- `DST.ADDR` = 4 байта IPv4 (network byte order)
- `DST.PORT` = 2 байта (network byte order)

```
Сервер → Клиент:
[VER=5, REP=0x00, RSV=0x00, ATYP=0x01, BND.ADDR=0.0.0.0, BND.PORT=0]
```

- `REP=0x00` = success

### Фаза 3: Релей (forwarding)

Сервер создаёт отдельный TCP-сокет к `DST.ADDR:DST.PORT` и начинает пересылать данные в обе стороны между клиентом и целевым сервером до закрытия любого из соединений.

## 4. Реализация: Python (`socks5-py/server.py`)

**Зависимости:** нет (только `socket`, `threading`, `struct` из stdlib)

**Управление:** один поток accept + два потока relay на каждое соединение

**Логика:**
1. Создать `socket(AF_INET, SOCK_STREAM)`, bind на `0.0.0.0:1080`, listen
2. В цикле `accept()` → `threading.Thread(target=handle, args=(client,)).start()`
3. `handle(client)`: парсит рукопожатие → парсит запрос → `socket.connect((ip, port))` → reply → два relay-потока

**Relay-функция:**
```python
def forward(src, dst):
    try:
        while data := src.recv(8192):
            dst.sendall(data)
    except:
        pass
    finally:
        src.close()
        dst.close()
```

**Почему `sendall` вместо `send`:** Python `send()` может отправить не все байты, `sendall()` гарантирует полную отправку.

**Потоки:** один relay-поток читает с клиента и пишет на цель, второй — наоборот. Если один поток завершился (соединение закрыто), второй нужно закрыть принудительно — для этого используем флаг или закрытие сокета через исключение.

## 5. Реализация: C++ (`socks5-cpp/server.cpp`)

**Зависимости:** WinSock2 (`ws2_32.lib`) — стандартная библиотека Windows

**Управление:** ручное — `WSAStartup`, `socket()`, `bind()`, `listen()`, `accept()`, `recv()`, `send()`, `closesocket()`, `WSACleanup`

**Логика — та же, но вручную:**

1. `WSAStartup(MAKEWORD(2,2), &wsa)` — инициализация WinSock
2. Создать сокет, bind, listen — все syscall'ы явные
3. `accept()` → `CreateThread` или `std::thread` → хендлер
4. В хендлере: ручной парсинг байтов (смещения руками), извлечение IPv4 через `memcpy`
5. `connect()` к цели → reply → два relay-потока

**Особенности:**
- `send()` может отправить не все байты → нужен цикл доотправки
- `recv()` пишет в `char[]` на стеке (нет GC)
- Нужно явно закрывать сокеты (`closesocket`)
- `htons()`/`htonl()` — ручная конвертация порядка байт
- `#pragma comment(lib, "ws2_32.lib")` для линковки

**CMakeLists.txt:**
```cmake
cmake_minimum_required(VERSION 3.10)
project(socks5-proxy LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
add_executable(server server.cpp)
target_link_libraries(server ws2_32)
```

## 6. Альтернативы и выбор

| Аспект | Выбор | Почему |
|--------|-------|--------|
| Протокол | SOCKS5 | RFC, TCP, можно расширить до VPN |
| Начальный ATYP | IPv4 | Минимум сложности для первого старта |
| Языки | Python + C++ | Разные уровни абстракции, сравнение |
| Сборка C++ | CMake | CLion-нативный, портабельный |

## 7. Критерии успеха (MVP)

- [ ] Python-сервер принимает соединение и релеит TCP-трафик через SOCKS5
- [ ] C++-сервер делает то же самое
- [ ] `curl --socks5 127.0.0.1:1080 http://example.com` возвращает HTML
- [ ] Проверено, что `test_client.py` подключается к обоим серверам

## 8. Следующие шаги (после MVP)

1. Добавить IPv6 (ATYP=4)
2. Добавить доменные имена (ATYP=3, DNS-резолв на сервере)
3. UDP ASSOCIATE (CMD=3) — мост к VPN
4. Шифрование трафика
5. Поддержка TUN/TAP (полноценный VPN)

## 9. Примечания

- Порт по умолчанию: 1080 (SOCKS5 IANA)
- Никаких внешних библиотек — только stdlib / WinSock
- Код с подробными комментариями на русском (для обучения)
- Для тестирования из внешней сети: ngrok (бесплатно) или локальный запуск на localhost
