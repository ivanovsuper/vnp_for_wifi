# CLI Overhaul: Design Document

## 1. Цель

Переработать CLI C++ SOCKS5-сервера: добавить режимы работы (`log`, `send`, `upload`),
полноценный парсинг аргументов, поддержку INI-конфига, интерактивный диалог при первом
запуске.

## 2. CLI-флаги

| Длинный | Короткий | Описание |
|---------|----------|----------|
| `--mode` | `-m` | Режим: `log` / `send` / `upload` |
| `--log-ips` | `-i` | Путь к лог-файлу (default: `ips.log`) |
| `--router` | `-r` | Адрес MikroTik (host:port) |
| `--user` | `-u` | Имя пользователя MikroTik |
| `--pass` | `-p` | Пароль (если нет — запросить) |
| `--config` | `-c` | Путь к INI-файлу (default: `config.ini`) |
| `--new` | `-n` | Очистить лог-файл перед стартом |
| `--help` | `-h` | Справка, выход |

Примеры:

```
server.exe -m log -i my_ips.txt           # только лог, без MikroTik
server.exe -m send -r 10.0.0.1 -u admin  # send, пароль запросится
server.exe -m upload                      # разовая загрузка лога
server.exe -c my.conf                     # из своего конфига, режим send
server.exe                                # если есть config.ini → send, иначе диалог
```

## 3. Config.ini

Формат:

```ini
[router]
host=192.168.88.1
user=admin
pass=
```

Ручной парсинг (без внешних библиотек). Поля:
- `host` — адрес MikroTik
- `user` — логин
- `pass` — пароль (может быть пустым → запросить)

## 4. struct Config

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
```

## 5. Логика запуска (`main()`)

```
main()
  │
  ├─ parse_args()         → заполняет Config из argv
  ├─ --help?              → print_help(), exit(0)
  │
  ├─ config_loaded?       → читает config.ini, Config перекрывается CLI
  ├─ нет флагов и конфига → interactive_setup() → write_config() → Config
  │
  ├─ --new?               → очистить лог-файл
  │
  ├─ mode == upload?
  │    └─ MikroTikClient(read from Config) → run_upload(Config.log_path) → exit(0)
  │
  ├─ mode == log?         → сервер без MikroTik, только лог
  │    └─ IPCollector → log_file → server loop
  │
  └─ mode == send?        → сервер + MikroTik + лог
       └─ IPCollector → MikroTikClient → upload_loop → log_file → server loop
```

## 6. Новые функции

Все в `server.cpp` (до `main()`):

- `parse_args(int argc, char* argv[], Config& cfg)` — заполняет Config из CLI
- `print_help()` — выводит справку
- `bool read_config(const std::string& path, Config& cfg)` — читает INI
- `bool write_config(const std::string& path, const Config& cfg)` — пишет INI
- `void interactive_setup(Config& cfg)` — диалог с пользователем
- `void run_upload(const std::string& path, MikroTikClient& client)` — разовая загрузка

## 7. Режим upload

1. Открыть `Config.log_path` (по умолчанию `ips.log`)
2. Читать построчно, каждый непустой IP
3. Для каждого вызвать `MikroTikClient.addAddress(ip)`
4. Вывести статистику: сколько отправлено, сколько ошибок
5. Завершиться

## 8. Существующий код

- `IPCollector`, `MikroTikClient`, `upload_loop`, `forward_data`, `handle_client` — не меняются
- Глобалы `log_file`, `file_mutex` — остаются (используются в `handle_client`)
- `upload_loop` запускается только в режиме `send`

## 9. Изменяемые файлы

- `socks5-cpp/server.cpp` — все изменения
