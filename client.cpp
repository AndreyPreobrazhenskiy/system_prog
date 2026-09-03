#include "common.h"
#include <string>
#include <iostream>
#include <csignal>
#include <cstring>
#include <limits>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: ./client <server_ip>\n";
        return 1;
    }

    struct sigaction sa_pipe{};
    sa_pipe.sa_handler = SIG_IGN;
    sigemptyset(&sa_pipe.sa_mask);
    sa_pipe.sa_flags = 0;
    check(sigaction(SIGPIPE, &sa_pipe, nullptr), "sigaction failed");

    std::string server_ip = argv[1];
    unsigned short port = SERVER_PORT;

    sockaddr_in dest_addr{};
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(port);
    dest_addr.sin_addr.s_addr = inet_addr(server_ip.c_str());
    
    if (dest_addr.sin_addr.s_addr == INADDR_NONE) {
        std::cerr << "Invalid IP address format\n";
        return 1;
    }

    int sock_fd = check(make_socket(SOCKET_TYPE));
    check(connect(sock_fd, (sockaddr*)&dest_addr, sizeof(dest_addr)), "Connect failed");

    std::cout << "Connected to " << server_ip << ":" << port << "\n";
    std::cout << "Введите число от 1 до 100 (или 0 для выхода):\n";

    while (true) {
        int32_t guess = 0;
        if (!(std::cin >> guess) || guess == 0) {
            if (std::cin.fail()) std::cin.clear();
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            std::cout << "Игра завершена пользователем.\n";
            break;
        }
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

        // Отправляем 4 байта
        if (!send_int32(sock_fd, guess)) {
            std::cout << "\n[Сервер отключился во время отправки.]\n";
            break;
        }

        // Принимаем 4 байта статуса
        int32_t status = 0;
        if (!recv_int32(sock_fd, status)) {
            std::cout << "\n[Сервер отключился. Игра завершена.]\n";
            break;
        }

        // Интерпретация статуса
        switch (status) {
            case STATUS_TOO_LOW:  std::cout << "Сервер: Слишком мало.\n"; break;
            case STATUS_TOO_HIGH: std::cout << "Сервер: Слишком много.\n"; break;
            case STATUS_CORRECT:
                std::cout << "Сервер: Верно! Игра окончена.\n";
                std::cout << "Введите 0 для выхода.\n";
                break;
            case STATUS_INVALID: std::cout << "Сервер: Введите число от 1 до 100.\n"; break;
            default: std::cout << "Сервер: Неизвестный статус.\n"; break;
        }
    }

    shutdown(sock_fd, SHUT_RDWR);
    close(sock_fd);
    return 0;
}