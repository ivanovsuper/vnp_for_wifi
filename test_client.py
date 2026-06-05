# SOCKS5 тестовый клиент
# Подключается к целевому хосту через наш прокси-сервер
# и отправляет HTTP GET запрос.
#
# Использование: python test_client.py <хост> <порт>
# Пример:      python test_client.py example.com 80

import socket
import sys

SOCKS5_VERSION = 5
PROXY_PORT = 1080  # порт нашего прокси-сервера

def socks5_connect(proxy_host, proxy_port, target_host, target_port):
    """Подключается к target_host:target_port через SOCKS5-прокси.

    Возвращает кортеж (соединение, None) при успехе
    или (None, сообщение_об_ошибке) при неудаче.
    """
    try:
        # Если передан домен, а не IP — резолвим его
        try:
            socket.inet_aton(target_host)
        except OSError:
            print(f"[*] Резолв домена {target_host} -> IP...")
            target_host = socket.gethostbyname(target_host)
            print(f"[*] IP: {target_host}")

        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(10)
        s.connect((proxy_host, proxy_port))

        # Фаза 1: Handshake — предлагаем метод No Authentication (0x00)
        s.send(bytes([SOCKS5_VERSION, 1, 0x00]))
        response = s.recv(2)
        if response != bytes([SOCKS5_VERSION, 0x00]):
            s.close()
            return None, "Ошибка handshake: сервер не выбрал no auth"

        # Фаза 2: Запрос на соединение с целевым хостом
        # [VER=5, CMD=1(CONNECT), RSV=0, ATYP=1(IPv4), DST.ADDR, DST.PORT]
        addr_bytes = socket.inet_aton(target_host)
        port_bytes = target_port.to_bytes(2, 'big')
        request = bytes([SOCKS5_VERSION, 1, 0, 1]) + addr_bytes + port_bytes
        s.send(request)

        # Читаем ответ: [VER, REP, RSV, ATYP, BND.ADDR(4), BND.PORT(2)]
        response = s.recv(10)
        if response[1] != 0x00:
            error_codes = {
                1: "general failure",
                2: "connection not allowed",
                3: "network unreachable",
                4: "host unreachable",
                5: "connection refused",
                6: "TTL expired",
                7: "command not supported",
                8: "address type not supported"
            }
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
    proxy_port = PROXY_PORT

    conn, error = socks5_connect(proxy_host, proxy_port, target_host, target_port)
    if error:
        print(f"[-] {error}")
        return

    # Если порт 80 — отправляем HTTP-запрос
    if target_port == 80:
        request = (
            f"GET / HTTP/1.1\r\n"
            f"Host: {target_host}\r\n"
            f"Connection: close\r\n"
            f"\r\n"
        )
        conn.send(request.encode())
        response = conn.recv(8192)
        print(f"[+] Получено {len(response)} байт ответа:")
        print(response.decode("utf-8", errors="replace")[:1000])
    else:
        print("[*] Соединение установлено, но HTTP не отправлен (порт не 80)")

    conn.close()

if __name__ == "__main__":
    main()
