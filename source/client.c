#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdint.h>
#include <inttypes.h>

int fd_s2c;
int fd_c2s;
char *log_ = NULL;
char user_perm[16] = "read";

// Thread function
static int first_response = 1;
void *server_reader(void *arg) {
    (void)arg;  
    while (1) {
        // read line from server and print it
        char line[256];
        ssize_t len = read(fd_s2c, line, sizeof(line) - 1);
        if (len <= 0) {
            if (len == 0) break; // EOF
            perror("read");
            break;
        }
        line[len] = '\0';

        // Parse permission from first server response
        if (first_response) {
            first_response = 0;
            // First line is the permission
            char *newline = strchr(line, '\n');
            if (newline) {
                size_t perm_len = newline - line;
                if (perm_len < sizeof(user_perm)) {
                    memcpy(user_perm, line, perm_len);
                    user_perm[perm_len] = '\0';
                }
            }
        }
        // append to log (reallocate if needed)
        size_t log_len = strlen(log_);
        size_t new_len = log_len + len + 1;
        char *new_log = realloc(log_, new_len);
        if (!new_log) {
            perror("realloc");
            break;
        }
        log_ = new_log;
        strncat(log_, line, len + 1);

        // print the line
        printf("%s", line);
        fflush(stdout);
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <server_pid> <username>\n", argv[0]);
        return EXIT_FAILURE;
    }
    pid_t server_pid = (pid_t)atoi(argv[1]);
    char *username = argv[2];
    pid_t client_pid = getpid();

    // Block SIGRTMIN+1 to use sigwait
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGRTMIN + 1);
    if (sigprocmask(SIG_BLOCK, &mask, NULL) == -1) {
        perror("sigprocmask");
        return EXIT_FAILURE;
    }

    // Send SIGRTMIN to server to register
    if (kill(server_pid, SIGRTMIN) == -1) {
        perror("kill(SIGRTMIN)");
        return EXIT_FAILURE;
    }

    // Wait for SIGRTMIN+1 acknowledgment
    int sig;
    if (sigwait(&mask, &sig) != 0) {
        perror("sigwait");
        return EXIT_FAILURE;
    }

    // Prepare FIFO names
    char fifo_c2s[64], fifo_s2c[64];
    snprintf(fifo_c2s, sizeof(fifo_c2s), "FIFO_C2S_%d", client_pid);
    snprintf(fifo_s2c, sizeof(fifo_s2c), "FIFO_S2C_%d", client_pid);

    // Open client-to-server FIFO for writing
    fd_c2s = open(fifo_c2s, O_WRONLY);
    if (fd_c2s == -1) {
        perror("open C2S FIFO");
        return EXIT_FAILURE;
    }

    // Open server-to-client FIFO for reading
    fd_s2c = open(fifo_s2c, O_RDONLY);
    if (fd_s2c == -1) {
        perror("open S2C FIFO");
        close(fd_c2s);
        return EXIT_FAILURE;
    }

    // Send username to server
    size_t ulen = strlen(username);
    if (write(fd_c2s, username, ulen) != (ssize_t)ulen || write(fd_c2s, "\n", 1) != 1) {
        perror("write username");
        close(fd_c2s);
        close(fd_s2c);
        return EXIT_FAILURE;
    }

    // Launch thread to read server broadcasts
    log_ = malloc(1);
    log_[0] = '\0'; 
    pthread_t tid;
    if (pthread_create(&tid, NULL, server_reader, NULL) != 0) {
        perror("pthread_create");
        close(fd_c2s);
        close(fd_s2c);
        return EXIT_FAILURE;
    }

    // Main loop: read user input and forward to server
    char line[256];
    while (fgets(line, sizeof(line), stdin)) {
        size_t len = strlen(line);
        if (len == 0) continue;
        // Ensure newline termination
        if (line[len - 1] != '\n' && len + 1 < sizeof(line)) {
            line[len] = '\n';
            line[len + 1] = '\0';
            len++;
        }
        if (strncmp(line, "LOG?", 4) == 0) {
            continue;
        } else if (strncmp(line, "DOC?", 4) == 0) {
            // print document content
            puts(log_);
            continue;
        } else if (strncmp(line, "PERM?", 5) == 0) {
            // print document permission
            puts(user_perm);
            continue;
        } 
        // Send command to server
        if (write(fd_c2s, line, len) != (ssize_t)len) {
            perror("write command");
            break;
        }
        // If user requested disconnect, break
        if (strncmp(line, "DISCONNECT", 10) == 0) {
            break;
        }
    }

    // Cleanup
    close(fd_c2s);
    close(fd_s2c);
    pthread_cancel(tid);
    pthread_join(tid, NULL);

    return EXIT_SUCCESS;
}
