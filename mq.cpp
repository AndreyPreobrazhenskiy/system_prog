#include "check.hpp"

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <cerrno>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <mqueue.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <csignal>

const char *mq1_name = "/lab2_mq1"; // o4ered dogadok
const char *mq2_name = "/lab2_mq2"; // o4ered resultatov

int child_status = 0;

void sigchld_handler(int) {}

void stopper() {
    usleep(200000);
}

int parent_peer_alive(pid_t child_id) {
    // proverka }|{ivosti partnera dlya roditel9l
    pid_t r = waitpid(child_id, &child_status, WNOHANG);
    return (r == 0);
}

bool child_peer_alive(pid_t parent_id) {
    // dlya potomka
    if (getppid() != parent_id) return 0;
    // if (kill(parent_id, 0) == 0) return 1;
    // if (errno == EPERM) return 0;
    return 1;
}

int peer_alive(int is_parent, pid_t peer_id) {
    return is_parent ? parent_peer_alive(peer_id)
                     : child_peer_alive(peer_id);
}

int write_int(mqd_t mq, int x, int is_parent, pid_t peer_id) {
    while (1) {
        if (!peer_alive(is_parent, peer_id)) return 0;

        if (mq_send(mq, (const char *)&x, sizeof(x), 0) == 0)
            return 1;

        if (errno == EAGAIN) {
            //stopper();
            continue;
        }

        return 0;
    }
}

int read_int(mqd_t mq, int *x, int is_parent, pid_t peer_id) {
    while (1) {
        if (!peer_alive(is_parent, peer_id)) return 0;

        ssize_t r = mq_receive(mq, (char *)x, sizeof(*x), NULL);

        if (r > 0) return 1;

        if (errno == EAGAIN) {
            //stopper();
            continue;
        }

        return 0;
    }
}

void picker(int secret, mqd_t read_mq, mqd_t write_mq,
            int is_parent, pid_t peer_id, int round) {

    printf("Раунд %d. Процесс %d загадал число\n", round, getpid());
    fflush(stdout);

    while (peer_alive(is_parent, peer_id)) {
        int guess;

        if (!read_int(read_mq, &guess, is_parent, peer_id))
            return;

        printf("Раунд %d. Процесс %d получил %d\n",
               round, getpid(), guess);
        fflush(stdout);

        int answer = (guess == secret);

        if (!write_int(write_mq, answer, is_parent, peer_id))
            return;

        if (answer) break;
    }
}

void guesser(int max_number, mqd_t write_mq, mqd_t read_mq,
             int is_parent, pid_t peer_id, int round) {

    int attempts = 0;

    while (peer_alive(is_parent, peer_id)) {
        int guess = rand() % max_number + 1;
        attempts++;

        printf("Раунд %d. Процесс %d отправил %d\n",
               round, getpid(), guess);
        fflush(stdout);

        if (!write_int(write_mq, guess, is_parent, peer_id))
            return;

        int answer;

        if (!read_int(read_mq, &answer, is_parent, peer_id))
            return;

        if (answer) {
            printf("Раунд %d. Число %d угадано за %d попыток\n",
                   round, guess, attempts);
            fflush(stdout);
            break;
        }
    }
}

void play(int is_parent, mqd_t read_mq, mqd_t write_mq,
          int max_number, int rounds, pid_t peer_id) {

    for (int round = 1;
         round <= rounds && peer_alive(is_parent, peer_id);
         round++) {

        int i_am_picker =
            (round % 2 == 1) ? is_parent : !is_parent;

        // ne4etnbly - roditel zagadblvaet 
        // 4etnbly - potomok zagadblvaet

        int secret = rand() % max_number + 1;

        if (i_am_picker) {
            picker(secret, read_mq, write_mq,
                   is_parent, peer_id, round);
        } else {
            guesser(max_number, write_mq, read_mq,
                    is_parent, peer_id, round);
        }
    }
}

int main(int argc, char* argv[]) {
    int max_number, rounds;

    if (argc >= 3) {
        max_number = atoi(argv[1]);
        rounds = atoi(argv[2]);
    } else {
        printf("Введите N: ");
        check(scanf("%d", &max_number));

        printf("Введите количество раундов: ");
        check(scanf("%d", &rounds));
    }

    struct mq_attr attr{};
    attr.mq_maxmsg = 10;
    attr.mq_msgsize = sizeof(int);

    mq_unlink(mq1_name);
    mq_unlink(mq2_name);

    mqd_t t1 = check(mq_open(mq1_name,
                             O_CREAT | O_RDWR | O_NONBLOCK,
                             0666, &attr));

    mqd_t t2 = check(mq_open(mq2_name,
                             O_CREAT | O_RDWR | O_NONBLOCK,
                             0666, &attr));

    check(mq_close(t1));
    check(mq_close(t2));

    struct sigaction sa{};
    sa.sa_handler = sigchld_handler;
    sigaction(SIGCHLD, &sa, NULL);

    pid_t parent_id = getpid();

    pid_t pid = check(fork());

    srand(time(NULL) ^ getpid());

    if (pid == 0) {
        // === CHILD ===
        //pid_t parent_id = getppid();

        mqd_t read_mq = check(mq_open(mq1_name, O_RDONLY | O_NONBLOCK));
        mqd_t write_mq = check(mq_open(mq2_name, O_WRONLY | O_NONBLOCK));

        play(0, read_mq, write_mq, max_number, rounds, parent_id);

        check(mq_close(read_mq));
        check(mq_close(write_mq));

        printf("Child done\n");
        return 0;
    }

    // === PARENT ===
    mqd_t write_mq = check(mq_open(mq1_name, O_WRONLY | O_NONBLOCK));
    mqd_t read_mq  = check(mq_open(mq2_name, O_RDONLY | O_NONBLOCK));

    play(1, read_mq, write_mq, max_number, rounds, pid);

    check(mq_close(read_mq));
    check(mq_close(write_mq));

    while (1) {
        pid_t r = waitpid(pid, &child_status, 0);

        if (r == pid) break;

        if (r == -1 && errno == EINTR) continue;

        if (r == -1 && errno == ECHILD) break;

        perror("waitpid");
        return 1;
    }

    mq_unlink(mq1_name);
    mq_unlink(mq2_name);

    printf("Parent done\n");
    return 0;
}