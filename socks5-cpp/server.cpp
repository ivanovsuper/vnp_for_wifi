// SOCKS5 прокси-сервер на C++ (WinSock2)
// Протокол: RFC 1928, TCP CONNECT через IPv4, без аутентификации
//
// Компиляция в CLion: открыть папку socks5-cpp как проект, нажать Build
// Компиляция через MinGW: g++ server.cpp -o server.exe -lws2_32 -std=c++17
// Запуск: .\server.exe
// Тест: curl --socks5 127.0.0.1:1080 http://example.com

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <thread>
#include <cstring>

// Подключаем библиотеку WinSock2 (только для MSVC)
#pragma comment(lib, "ws2_32.lib")

#include <set>
#include <mutex>
#include <vector>
#include <string>
#include <sstream>
#include <chrono>
#include <cstdio>
#include <conio.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")

enum Verbosity { FULL, IP, SILENT };

class Logger {
    Verbosity level_;
public:
    Logger(Verbosity level = FULL) : level_(level) {}

    void setLevel(Verbosity level) { level_ = level; }

    void log(Verbosity msg_level, const std::string& msg) {
        if (level_ != SILENT && msg_level >= level_) {
            std::cout << msg << std::endl;
        }
    }

    void error(const std::string& msg) {
        if (level_ != SILENT) {
            std::cerr << msg << std::endl;
        }
    }
};

static FILE* log_file = nullptr;
static std::mutex file_mutex;

struct Config {
    enum Mode { LOG, SEND, UPLOAD } mode = SEND;
    std::string log_path = "ips.log";
    std::string config_path = "config.ini";
    std::string router_host;
    std::string router_user;
    std::string router_pass;
    bool new_log = false;
    Verbosity verbosity = FULL;
};

void print_help() {
    std::cout << "Использование: server.exe [ключи]\n"
              << "\n"
              << "Ключи:\n"
              << "  -m, --mode <mode>    Режим: log, send, upload (default: send)\n"
              << "  -i, --log-ips <path> Путь к лог-файлу (default: ips.log)\n"
              << "  -r, --router <host>  Адрес MikroTik\n"
              << "  -u, --user <user>    Имя пользователя MikroTik\n"
              << "  -p, --pass <pass>    Пароль (если нет — запросим)\n"
              << "  -c, --config <path>  Путь к config.ini (default: config.ini)\n"
              << "  -v, --verbose <level> Уровень вывода: full, ip, silent (default: full)\n"
              << "  -n, --new            Очистить лог-файл перед стартом\n"
              << "  -h, --help           Эта справка\n"
              << std::endl;
}

std::string read_password(const std::string& prompt) {
    std::cout << prompt;
    std::string pass;
    int ch;
    while ((ch = _getch()) != '\r' && ch != '\n') {
        if (ch == 3) {
            std::cout << std::endl;
            exit(1);
        }
        if (ch == '\b' || ch == 127) {
            if (!pass.empty()) {
                pass.pop_back();
                std::cout << "\b \b";
            }
        } else {
            pass.push_back(static_cast<char>(ch));
            std::cout << '*';
        }
    }
    std::cout << std::endl;
    return pass;
}

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
        } else {
            std::cerr << "Неизвестный ключ: " << a << std::endl;
            print_help();
            exit(1);
        }
    }
}

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

void interactive_setup(Config& cfg) {
    std::string input;

    cfg.mode = Config::SEND;

    std::cout << "MikroTik IP: ";
    std::getline(std::cin, cfg.router_host);
    std::cout << "Логин: ";
    std::getline(std::cin, cfg.router_user);
    if (cfg.router_pass.empty()) {
        cfg.router_pass = read_password("Пароль: ");
    }

    std::cout << "Лог-файл [" << cfg.log_path << "]: ";
    std::getline(std::cin, input);
    if (!input.empty()) cfg.log_path = input;

    write_config(cfg.config_path, cfg);
    std::cout << "[+] Конфиг сохранён в " << cfg.config_path << std::endl;
}

// Константы протокола SOCKS5
const int SOCKS5_VERSION = 5;
const int DEFAULT_PORT = 1080;
const int BUFFER_SIZE = 8192;

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
                                          WINHTTP_ACCESS_TYPE_NO_PROXY,
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

// forward_data — пересылает данные из src в dst пока соединение открыто.
// Читает из src, пишет в dst. send() может отправить не все байты,
// поэтому нужен цикл досылки. При ошибке закрывает оба сокета.
void forward_data(SOCKET src, SOCKET dst, const char* name, Logger& log) {
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
    log.log(FULL, std::string("[-] ") + name + " завершён");
}

// handle_client — обрабатывает одно SOCKS5-соединение.
// Фаза 1: Handshake (выбор метода аутентификации)
// Фаза 2: Запрос (извлечение целевого адреса и порта)
// Фаза 3: Релей (двусторонняя пересылка данных)
void handle_client(SOCKET client, IPCollector& collector, Logger& log) {
    char buf[BUFFER_SIZE];

    // ---- Фаза 1: Handshake ----
    // Клиент присылает: [VER=5, NMETHODS, METHODS...]
    int n = recv(client, buf, 2, 0);
    if (n != 2 || buf[0] != SOCKS5_VERSION) {
        closesocket(client);
        return;
    }

    int nmethods = buf[1];
    n = recv(client, buf, nmethods, 0);
    if (n != nmethods) {
        closesocket(client);
        return;
    }

    // Выбираем метод 0x00 (No Authentication)
    char handshake_response[] = {SOCKS5_VERSION, 0x00};
    send(client, handshake_response, 2, 0);
    log.log(FULL, "[+] Handshake пройден (no auth)");

    // ---- Фаза 2: Запрос ----
    // [VER=5, CMD=1(CONNECT), RSV=0, ATYP, DST.ADDR, DST.PORT]
    n = recv(client, buf, 4, 0);
    if (n != 4 || buf[0] != SOCKS5_VERSION || buf[1] != 0x01) {
        closesocket(client);
        return;
    }

    char atype = buf[3];
    if (atype != 0x01) {
        log.log(FULL, "[-] Поддерживается только IPv4");
        closesocket(client);
        return;
    }

    // Читаем IPv4 адрес (4 байта) и порт (2 байта)
    // Все байты приходят в network byte order (big-endian)
    char addr_port[6];
    n = recv(client, addr_port, 6, 0);
    if (n != 6) {
        closesocket(client);
        return;
    }

    // Заполняем sockaddr_in для connect()
    sockaddr_in target_addr = {};
    target_addr.sin_family = AF_INET;
    // Копируем 4 байта IP-адреса (уже в network byte order)
    std::memcpy(&target_addr.sin_addr, addr_port, 4);
    // Копируем 2 байта порта (тоже в network byte order)
    std::memcpy(&target_addr.sin_port, addr_port + 4, 2);

    // Конвертируем IP в строку для вывода на экран
    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &target_addr.sin_addr, ip_str, sizeof(ip_str));
    int port = ntohs(target_addr.sin_port);
    log.log(IP, std::string("[*] Цель: ") + ip_str + ":" + std::to_string(port));
    collector.add(ip_str);
    if (log_file) {
        std::lock_guard<std::mutex> lock(file_mutex);
        fprintf(log_file, "%s\n", ip_str);
        fflush(log_file);
    }

    // Подключаемся к цели через новый сокет
    SOCKET remote = socket(AF_INET, SOCK_STREAM, 0);
    if (remote == INVALID_SOCKET) {
        closesocket(client);
        return;
    }

    // Таймаут на операции (5 секунд)
    DWORD timeout = 5000;
    setsockopt(remote, SOL_SOCKET, SO_RCVTIMEO,
               (const char*)&timeout, sizeof(timeout));
    setsockopt(remote, SOL_SOCKET, SO_SNDTIMEO,
               (const char*)&timeout, sizeof(timeout));

    if (connect(remote, (sockaddr*)&target_addr, sizeof(target_addr))
        == SOCKET_ERROR) {
        log.log(FULL, "[-] Не удалось подключиться к цели");
        closesocket(client);
        closesocket(remote);
        return;
    }

    // Отправляем ответ об успехе:
    // [VER=5, REP=0(успех), RSV=0, ATYP=1(IPv4),
    //  BND.ADDR=0.0.0.0, BND.PORT=0]
    char reply[] = {5, 0, 0, 1, 0, 0, 0, 0, 0, 0};
    send(client, reply, sizeof(reply), 0);
    log.log(FULL, "[+] Соединение с целью установлено, начинаю релей");

    // ---- Фаза 3: Релей ----
    // Два потока для двусторонней пересылки данных
    // Каждый поток копирует данные из одного сокета в другой
    std::thread(forward_data, client, remote, "клиент->цель", std::ref(log)).detach();
    std::thread(forward_data, remote, client, "цель->клиент", std::ref(log)).detach();
}

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

    Logger log(cfg.verbosity);

    // Without CLI flags: use config or interactive
    if (!has_cli_flags && config_found) {
        log.log(IP, "[+] Конфиг загружен: " + cfg.config_path);
        cfg.mode = Config::SEND;
        if (cfg.router_pass.empty()) {
            cfg.router_pass = read_password("Пароль: ");
        }
    }

    if (!has_cli_flags && !config_found) {
        interactive_setup(cfg);
    }

    // --new: clear log file
    if (cfg.new_log) {
        FILE* f = fopen(cfg.log_path.c_str(), "w");
        if (f) fclose(f);
        log.log(FULL, "[+] Лог-файл очищен: " + cfg.log_path);
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

    log.log(IP, "[+] SOCKS5 сервер запущен на 0.0.0.0:" + std::to_string(DEFAULT_PORT));

    while (true) {
        SOCKET client = accept(server, nullptr, nullptr);
        if (client == INVALID_SOCKET) {
            std::cerr << "[-] Ошибка accept()" << std::endl;
            continue;
        }
        log.log(FULL, "[*] Новое соединение");
        std::thread(handle_client, client, std::ref(collector), std::ref(log)).detach();
    }

    closesocket(server);
    WSACleanup();
    return 0;
}
