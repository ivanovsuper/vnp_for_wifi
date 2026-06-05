# SOCKS5 прокси-сервер на Python
# Протокол: RFC 1928, TCP CONNECT через IPv4, без аутентификации
#
# Запуск: python server.py
# Тест: curl --socks5 127.0.0.1:1080 http://example.com

import socket
import threading

SOCKS5_VERSION = 5
DEFAULT_PORT = 1080

def forward(src, dst, name):
    """Копирует данные из src в dst пока соединение открыто.

    Читает из исходного сокета и отправляет всё в целевой.
    При ошибке или закрытии — закрывает оба сокета.
    """
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
        print(f"[-] {name} завершён")

def handle_client(client):
    """Обрабатывает одно SOCKS5-соединение от клиента."""
    try:
        # Фаза 1: Handshake — клиент предлагает методы аутентификации
        greeting = client.recv(2)
        if greeting[0] != SOCKS5_VERSION:
            client.close()
            return

        nmethods = greeting[1]
        methods = client.recv(nmethods)

        # Выбираем метод 0x00 (No Authentication)
        client.send(bytes([SOCKS5_VERSION, 0x00]))
        print("[+] Handshake пройден (no auth)")

        # Фаза 2: Запрос — клиент просит соединить с целевым хостом
        request = client.recv(4)
        if request[0] != SOCKS5_VERSION or request[1] != 0x01:
            client.close()
            return

        atype = request[3]
        if atype == 0x01:
            # IPv4: читаем 4 байта адреса + 2 байта порта
            addr_bytes = client.recv(4)
            dst_addr = socket.inet_ntoa(addr_bytes)
            port_bytes = client.recv(2)
            dst_port = port_bytes[0] << 8 | port_bytes[1]
        else:
            print("[-] Поддерживается только IPv4")
            client.close()
            return

        print(f"[*] Цель: {dst_addr}:{dst_port}")

        # Открываем TCP-соединение к цели
        remote = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        remote.settimeout(30)
        remote.connect((dst_addr, dst_port))

        # Отправляем ответ об успехе:
        # [VER=5, REP=0(успех), RSV=0, ATYP=1(IPv4), BND.ADDR=0.0.0.0, BND.PORT=0]
        reply = bytes([SOCKS5_VERSION, 0x00, 0x00, 0x01,
                       0x00, 0x00, 0x00, 0x00,
                       0x00, 0x00])
        client.send(reply)
        print("[+] Соединение с целью установлено, начинаю релей")

        # Фаза 3: Релей — два потока для двусторонней передачи
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

def start_server(host="0.0.0.0", port=DEFAULT_PORT):
    """Запускает SOCKS5 прокси-сервер на указанном хосте и порту."""
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
