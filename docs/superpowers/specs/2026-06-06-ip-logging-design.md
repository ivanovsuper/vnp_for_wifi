# IP File Logging: Design Document

## 1. Цель

Добавить запись целевых IP-адресов SOCKS5-соединений в текстовый файл (один IP на строку) для
тестирования и анализа перед отправкой в MikroTik.

Режим управляется CLI-флагом `--log-ips`. Без флага поведение не меняется.

## 2. CLI-флаг

Формат: `--log-ips[=путь]`

```
.\server.exe                                # только MikroTik (как сейчас)
.\server.exe --log-ips                      # лог в ips.log
.\server.exe --log-ips=ips.txt              # лог в ips.txt
.\server.exe --log-ips=C:\temp\dump.txt     # абсолютный путь
```

Парсинг — ручной проход по `argv` в `main()`, без внешних библиотек. Флаг не отменяет
интерактивный ввод MikroTik — оба работают вместе.

## 3. Хранение состояния

Глобальная переменная `FILE* log_file = nullptr;` и `std::mutex file_mutex;`.

- `log_file == nullptr` → файловое логирование отключено
- `log_file != nullptr` → открыт для дописывания (`"a"`)

## 4. Запись IP

В `handle_client()`, после `collector.add(ip_str)`:

```cpp
if (log_file) {
    std::lock_guard<std::mutex> lock(file_mutex);
    fprintf(log_file, "%s\n", ip_str);
    fflush(log_file);
}
```

- `fflush` после каждой строки — данные не теряются при падении сервера
- Мьютекс — защита от одновременной записи из нескольких relay-потоков
- Формат: один IP на строку, без timestamp

## 5. Инициализация

В `main()`, после парсинга `argv` и до запуска сервера:

```cpp
const char* log_path = nullptr;
// парсим --log-ips[=path], устанавливаем log_path
if (log_path) {
    log_file = fopen(log_path, "a");
}
```

Файл не закрывается явно — ОС закрывает при завершении процесса. Если `fopen` упал —
пишем в `std::cerr` и продолжаем без файлового лога.

## 6. Изменяемые файлы

- `socks5-cpp/server.cpp` — все изменения
