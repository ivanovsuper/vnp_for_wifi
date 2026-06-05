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
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")

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
    std::cout << "[-] " << name << " завершён" << std::endl;
}

// handle_client — обрабатывает одно SOCKS5-соединение.
// Фаза 1: Handshake (выбор метода аутентификации)
// Фаза 2: Запрос (извлечение целевого адреса и порта)
// Фаза 3: Релей (двусторонняя пересылка данных)
void handle_client(SOCKET client, IPCollector& collector) {
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
    std::cout << "[+] Handshake пройден (no auth)" << std::endl;

    // ---- Фаза 2: Запрос ----
    // [VER=5, CMD=1(CONNECT), RSV=0, ATYP, DST.ADDR, DST.PORT]
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
    std::cout << "[*] Цель: " << ip_str << ":" << port << std::endl;
    collector.add(ip_str);

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
        std::cout << "[-] Не удалось подключиться к цели" << std::endl;
        closesocket(client);
        closesocket(remote);
        return;
    }

    // Отправляем ответ об успехе:
    // [VER=5, REP=0(успех), RSV=0, ATYP=1(IPv4),
    //  BND.ADDR=0.0.0.0, BND.PORT=0]
    char reply[] = {5, 0, 0, 1, 0, 0, 0, 0, 0, 0};
    send(client, reply, sizeof(reply), 0);
    std::cout << "[+] Соединение с целью установлено, начинаю релей" << std::endl;

    // ---- Фаза 3: Релей ----
    // Два потока для двусторонней пересылки данных
    // Каждый поток копирует данные из одного сокета в другой
    std::thread(forward_data, client, remote, "клиент->цель").detach();
    std::thread(forward_data, remote, client, "цель->клиент").detach();
}

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
