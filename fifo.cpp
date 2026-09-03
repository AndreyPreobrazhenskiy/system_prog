#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <cstdlib>
#include <ctime>
#include <cstdio>
#include <cstring>

#define FIFO1 "/tmp/fifo1"
#define FIFO2 "/tmp/fifo2"

void delay(){
    constexpr timespec t{1, 0};
    nanosleep(&t,nullptr);
}

void picker(int secret, int read_fd, int write_fd) {
    int guess;

    while (true) {
        delay();
        if (read(read_fd, &guess, sizeof(int)) <= 0) break;

        int result = (guess == secret) ? 1 : 0;
        write(write_fd, &result, sizeof(int));

        if (result) break;
    }
}

void guesser(int N, int read_fd, int write_fd) {
    int attempts = 0;

    while (true) {
        delay();
        int g = rand() % N + 1;
        attempts++;

        write(write_fd, &g, sizeof(int));

        int result;
        if (read(read_fd, &result, sizeof(int)) <= 0) break;

        if (result) {
            printf("PID %d угадал %d за %d попыток\n",
                   getpid(), g, attempts);
            fflush(stdout);
            break;
        }
    }
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Usage: %s N [ROUNDS]\n", argv[0]);
        return 1;
    }

    int N = atoi(argv[1]);
    int rounds = (argc > 2) ? atoi(argv[2]) : 10;

    srand(time(NULL) ^ getpid());

    mkfifo(FIFO1, 0666);
    mkfifo(FIFO2, 0666);

    pid_t pid = fork();

    int fd_read, fd_write;

    if (pid > 0) {
        fd_write = open(FIFO1, O_WRONLY);
        fd_read  = open(FIFO2, O_RDONLY);
    } else {
        fd_read  = open(FIFO1, O_RDONLY);
        fd_write = open(FIFO2, O_WRONLY);
    }

    for (int i = 0; i < rounds; i++) {
        int role = i % 2;
        int secret = rand() % N + 1;

        if ((pid > 0 && role == 0) || (pid == 0 && role == 1)) {
            printf("Раунд %d: PID %d загадывает\n", i+1, getpid());
            fflush(stdout);
            picker(secret, fd_read, fd_write);
        } else {
            printf("Раунд %d: PID %d угадывает\n", i+1, getpid());
            fflush(stdout);
            guesser(N, fd_read, fd_write);
        }
    }

    close(fd_read);
    close(fd_write);

    if (pid > 0) {
        wait(NULL);
        unlink(FIFO1);
        unlink(FIFO2);
    }

    return 0;
}