#include "check.hpp"
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <cstdlib>
#include <ctime>
#include <cstdio>

volatile sig_atomic_t guess_ready = 0; 
volatile sig_atomic_t guess_val = 0;

volatile sig_atomic_t result_ready = 0;
volatile sig_atomic_t result_val = 0; // 1 - угадал, 0 - не угадал

volatile sig_atomic_t peer_dead = 0; // флаг смерти партнёра

// realtime signal handler
void guess_handler(int, siginfo_t* si, void*) {
    guess_val = si->si_value.sival_int;
    guess_ready = 1;
}

void result_handler(int signo) {
    result_val = (signo == SIGUSR1);
    result_ready = 1;
}

void sigchld_handler(int) {
    // цикл приводит к зависанию 
    // while (waitpid(-1, NULL, WNOHANG) > 0); // <- нужно отслеживать состояние (так как он необязательно может завершиться)
    peer_dead = 1;
}

void delay(){
    constexpr timespec t{1, 0};
    nanosleep(&t,nullptr);
}

void wait_flag(volatile sig_atomic_t* flag, sigset_t* wait_mask) {
    // sigset_t oldmask;

    //check(sigprocmask(SIG_BLOCK, waitset, &oldmask)); // только в мейне

    while (!(*flag) && !peer_dead) {
        sigsuspend(wait_mask);
    }

    //check(sigprocmask(SIG_SETMASK, &oldmask, NULL)); // тольк ов мейне
}

bool is_alive(pid_t pid) { // нужно будет добавить в main проверку на зомби
    if (kill(pid, 0) == -1) {
        if (errno == ESRCH) return false;
    }
    return true;
}

// написать функции проверки жив ли родитель и жив ли потомок. результат функции уже использовать в пикер и гессер (указатель на функцию)

void picker(int secret, pid_t other) {
    sigset_t waitset;
    sigemptyset(&waitset);
    sigaddset(&waitset, SIGUSR1); // инверсия (наоборот дргугие сингналы)
    sigaddset(&waitset, SIGUSR2);

    while (!peer_dead) {
        delay();
        guess_ready = 0;

        wait_flag(&guess_ready, &waitset);
        if (peer_dead) break;

        int g = guess_val;

        if (!is_alive(other)) {
            peer_dead = 1;
            break;
        }

        if (g == secret) {
            if (kill(other, SIGUSR1) == -1 && errno == ESRCH)
                peer_dead = 1;
            break;
        } else {
            if (kill(other, SIGUSR2) == -1 && errno == ESRCH)
                peer_dead = 1;
        }
    }
}

void guesser(int N, pid_t other) {
    sigset_t waitset;
    sigemptyset(&waitset);
    sigaddset(&waitset, SIGRTMIN);

    int attempts = 0;

    while (!peer_dead) {
        delay();
        if (!is_alive(other)) {
            peer_dead = 1;
            break;
        }

        int g = rand() % N + 1;
        attempts++;

        union sigval val;
        val.sival_int = g;

        if (sigqueue(other, SIGRTMIN, val) == -1) {
            if (errno == ESRCH) peer_dead = 1;
            break;
        }

        result_ready = 0;
        wait_flag(&result_ready, &waitset);
        if (peer_dead) break;

        if (result_val) {
            printf("PID %d угадал %d за %d попыток\n",
                   getpid(), g, attempts);
            fflush(stdout);
            break;
        }
    }
}

void zombies() {
    waitpid(-1, NULL, WNOHANG);
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Usage: %s N\n", argv[0]);
        return 1;
    }

    int N = atoi(argv[1]);
    if (N < 1) N = 10;

    srand(time(NULL) ^ getpid());

    sigset_t all;
    sigemptyset(&all);
    sigaddset(&all, SIGRTMIN);
    sigaddset(&all, SIGUSR1);
    sigaddset(&all, SIGUSR2);
    // sigaddset(&all, SIGCHLD); // зачем блокируем сигчайлд
    check(sigprocmask(SIG_BLOCK, &all, NULL));

    struct sigaction sa{};

    sa.sa_sigaction = guess_handler;
    sa.sa_flags = SA_SIGINFO;
    check(sigaction(SIGRTMIN, &sa, NULL));

    sa = {};
    sa.sa_handler = result_handler;
    check(sigaction(SIGUSR1, &sa, NULL));
    check(sigaction(SIGUSR2, &sa, NULL));

    sa = {};
    sa.sa_handler = sigchld_handler;
    sa.sa_flags = SA_NOCLDWAIT;
    check(sigaction(SIGCHLD, &sa, NULL));

    pid_t parent_pid = getpid();

    pid_t pid = check(fork());

    pid_t other = (pid == 0) ? parent_pid : pid; // ппид должен вызваться до форка, так безопаснее

    int rounds = 10;
    if (argc > 2) {
        rounds = atoi(argv[2]);
        if (rounds < 1) rounds = 10;
    }

    for (int i = 0; i < rounds && !peer_dead; i++) {
        zombies();
        int role = (i % 2);

        int secret = rand() % N + 1;

        if ((pid > 0 && role == 0) || (pid == 0 && role == 1)) {
            printf("Раунд %d: PID %d загадывает\n", i+1, getpid());
            fflush(stdout);
            picker(secret, other);
        } else {
            printf("Раунд %d: PID %d угадывает\n", i+1, getpid());
            fflush(stdout);
            guesser(N, other);
        }
    }

    if (pid > 0) {
        wait(NULL);
        printf("Parent done\n");
    } else {
        printf("Child done\n");
    }

    return 0;
}