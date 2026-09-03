#include "common.h"
#include <csignal>
#include <sys/wait.h>
#include <sys/mman.h>
#include <pthread.h>
#include <cstdlib>
#include <ctime>
#include <string>
#include <poll.h>

pthread_mutex_t* g_log_mutex = nullptr;

void safe_log(const sockaddr_in& addr, const std::string& msg) {
    if (g_log_mutex) pthread_mutex_lock(g_log_mutex);
    std::cout << addr << ": " << msg << std::endl;
    if (g_log_mutex) pthread_mutex_unlock(g_log_mutex);
}


void handle_game(int conn_sock, sockaddr_in client_addr) {
    safe_log(client_addr, "Connected");
    
    struct sigaction sa{};
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    check(sigaction(SIGPIPE, &sa, nullptr));

    std::srand(static_cast<unsigned>(std::time(nullptr) ^ getpid()));
    int target = std::rand() % 100 + 1;
    int attempts = 0;

    while (true) {
        int32_t guess = 0;
        if (!recv_int32(conn_sock, guess)) {
            safe_log(client_addr, "Disconnected unexpectedly");
            break;
        }
        safe_log(client_addr, "Client guess: " + std::to_string(guess));

        if (guess < 1 || guess > 100) {
            if (!send_int32(conn_sock, STATUS_INVALID)) break;
            safe_log(client_addr, "Invalid input");
            continue;
        }
        attempts++;

        int32_t status;
        if (guess < target) status = STATUS_TOO_LOW;
        else if (guess > target) status = STATUS_TOO_HIGH;
        else status = STATUS_CORRECT;

        if (!send_int32(conn_sock, status)) {
            safe_log(client_addr, "Send failed");
            break;
        }
        safe_log(client_addr, "Server status: " + std::to_string(status));

        if (status == STATUS_CORRECT) {
            safe_log(client_addr, "Guessed in " + std::to_string(attempts) + " attempts.");
            break;
        }
    }

    shutdown(conn_sock, SHUT_RDWR);
    close(conn_sock);
    safe_log(client_addr, "Disconnected");
    exit(0);
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: ./server <port>\n";
        return 1;
    }
    unsigned short port = std::stoul(argv[1]);

    // Разделяемый мьютекс
    pthread_mutexattr_t mattr;
    pthread_mutexattr_init(&mattr);
    pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_SHARED);
    g_log_mutex = static_cast<pthread_mutex_t*>(
        mmap(nullptr, sizeof(pthread_mutex_t), PROT_READ | PROT_WRITE,
             MAP_SHARED | MAP_ANONYMOUS, -1, 0)
    );
    check(g_log_mutex != MAP_FAILED ? 0 : -1, "mmap failed");
    pthread_mutex_init(g_log_mutex, &mattr);

    // Предотвращение зомби
    struct sigaction sa_chld{};
    sa_chld.sa_handler = SIG_IGN;
    sigemptyset(&sa_chld.sa_mask);
    sa_chld.sa_flags = SA_RESTART;
    check(sigaction(SIGCHLD, &sa_chld, nullptr), "sigaction failed");

    auto server_addr = local_addr(port);
    int listen_sock = check(make_socket(SOCKET_TYPE));
    check(bind(listen_sock, (sockaddr*)&server_addr, sizeof(server_addr)), "Bind failed");
    check(listen(listen_sock, 5), "Listen failed");

    safe_log(server_addr, "Server started on port " + std::to_string(port));

    while (true) {
        sockaddr_in client_addr{};
        socklen_t addrlen = sizeof(client_addr);
        int conn_sock = check(accept(listen_sock, (sockaddr*)&client_addr, &addrlen), "Accept failed");

        pid_t pid = fork();
        if (pid < 0) {
            safe_log(client_addr, "Fork failed");
            close(conn_sock);
            continue;
        }
        if (pid == 0) {
            close(listen_sock);
            handle_game(conn_sock, client_addr);
        } else {
            close(conn_sock);
        }
    }
}