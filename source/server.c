#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <inttypes.h>
#include "../libs/document.h"
#include "../libs/command.h"
#include "../libs/markdown.h"

#ifndef SIGRTMIN
#define SIG_REGISTER SIGUSR1
#define SIG_ACK      SIGUSR2
#else
#define SIG_REGISTER SIGRTMIN
#define SIG_ACK      (SIGRTMIN + 1)
#endif

typedef enum role_perm {
    ROLE_READ,
    ROLE_WRITE
} role_perm;

// Role linked list
typedef struct role {
    char *username;
    role_perm perm;
    struct role *next;
} role;

// Client linked list
typedef struct client {
    pid_t                pid;
    int                  fd_s2c;
    char                *username;
    role_perm            perm;
    struct client       *next;
} client;

// Shared variables
document *doc = NULL;
role *roles = NULL;
client *clients = NULL;
long update_interval_ms = 0;
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t commands_mutex = PTHREAD_MUTEX_INITIALIZER;

void load_roles(const char *path);
role *find_role(const char *username);
void *client_handler(void *arg);
void *broadcast_thread(void *arg);
void setup_signal_handler(void);

// SIG_REGISTER handler spawns a thread to handle a new client
void sig_rtmin_handler(int signo, siginfo_t *info, void *ucontext) {
    (void)signo; (void)ucontext;
    pid_t sender = info->si_pid;
    pid_t *argp = malloc(sizeof(*argp));
    if (!argp) {
        perror("malloc");
        return;
    }
    *argp = sender;
    pthread_t tid;
    if (pthread_create(&tid, NULL, client_handler, argp) != 0) {
        perror("pthread_create");
        free(argp);
        return;
    }
    pthread_detach(tid);
}

// Register handler for SIG_REGISTER using sigaction
void setup_signal_handler(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = sig_rtmin_handler;
    sa.sa_flags = SA_SIGINFO | SA_RESTART;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIG_REGISTER, &sa, NULL) == -1) {
        perror("sigaction(SIG_REGISTER)");
        exit(EXIT_FAILURE);
    }
}

role *find_role(const char *username) {
    for (role *r = roles; r; r = r->next) {
        if (strcmp(r->username, username) == 0) {
            return r;
        }
    }
    return NULL;
}

void *client_handler(void *arg) {

    // Block SIG_REGISTER before any threads
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIG_REGISTER);
    if (pthread_sigmask(SIG_BLOCK, &mask, NULL) == -1) {
        perror("sigprocmask(SIG_BLOCK)");
        exit(EXIT_FAILURE);
    }

    pid_t client_pid = *(pid_t*)arg;
    free(arg);

    // Create and open FIFOs
    char fifo_c2s[64], fifo_s2c[64];
    snprintf(fifo_c2s, sizeof(fifo_c2s), "FIFO_C2S_%d", client_pid);
    snprintf(fifo_s2c, sizeof(fifo_s2c), "FIFO_S2C_%d", client_pid);
    unlink(fifo_c2s);
    unlink(fifo_s2c);
    if (mkfifo(fifo_c2s, 0666) < 0 || mkfifo(fifo_s2c, 0666) < 0) {
        perror("mkfifo");
        return NULL;
    }

    // Notify client
    if (kill(client_pid, SIG_ACK) == -1) {
        perror("kill(SIG_ACK)");
    }

    int fd_c2s = open(fifo_c2s, O_RDONLY);
    int fd_s2c = open(fifo_s2c, O_WRONLY);
    if (fd_c2s < 0 || fd_s2c < 0) {
        perror("open fifo");
        unlink(fifo_c2s); unlink(fifo_s2c);
        return NULL;
    }

    // Read username
    FILE *in = fdopen(fd_c2s, "r");
    char ubuf[256];
    if (!fgets(ubuf, sizeof(ubuf), in)) goto reject;
    if (ubuf[strlen(ubuf)-1] == '\n') ubuf[strlen(ubuf)-1] = '\0';
    role *r = find_role(ubuf);

    pthread_mutex_lock(&commands_mutex);
    if (r) {
        char *content = markdown_flatten(doc);
        size_t doc_len = document_length(doc);
        const char *perm_str = (r->perm == ROLE_WRITE) ? "write" : "read";
        int header_len = snprintf(
            NULL, 0,
            "%s\n%" PRIu64 "\n%zu\n",
            perm_str,
            doc->version,
            doc_len
        );
        size_t total_len = (size_t)header_len + doc_len + 1;
        char *buf = malloc(total_len);
        snprintf(
            buf,
            header_len + 1,   
            "%s\n%" PRIu64 "\n%zu\n",
            perm_str,
            doc->version,
            doc_len
        );
        memcpy(buf + header_len, content, doc_len);
        size_t sent = 0;
        while (sent < total_len) {
            ssize_t w = write(fd_s2c, buf + sent, total_len - sent);
            sent += (size_t)w;
        }
        free(buf);
        free(content);
    }
    pthread_mutex_unlock(&commands_mutex);
    if (!r) {
    reject:
        dprintf(fd_s2c, "Reject UNAUTHORISED\n");
        sleep(1);
        close(fd_c2s); close(fd_s2c);
        unlink(fifo_c2s); unlink(fifo_s2c);
        return NULL;
    }

    // Register client
    client *c = malloc(sizeof(*c));
    c->pid = client_pid;
    c->fd_s2c = fd_s2c;
    c->username = strdup(ubuf);
    c->perm = r->perm;
    pthread_mutex_lock(&clients_mutex);
    c->next = clients;
    clients = c;
    pthread_mutex_unlock(&clients_mutex);

    // Process client commands
    char cmdbuf[512];
    while (fgets(cmdbuf, sizeof(cmdbuf), in)) {
        size_t L = strlen(cmdbuf);
        if (L == 0 || cmdbuf[L-1] != '\n') continue;
        cmdbuf[L-1] = '\0';
        if (strcmp(cmdbuf, "DISCONNECT") == 0) {
            break;
        } 
        pthread_mutex_lock(&commands_mutex);
        parse_command(doc, strdup(cmdbuf));
        const char *reject_msg = NULL;
        if (find_role(c->username)->perm == ROLE_READ) {
            reject_msg = "Reject UNAUTHORISED";
            // send 'Reject UNAUTHORISED\n' to client
            dprintf(fd_s2c, "%s\n", reject_msg);
        }
        command_set_username(doc->commands, c->username, reject_msg);
        pthread_mutex_unlock(&commands_mutex);
    }

    // Cleanup
    close(fd_c2s); close(fd_s2c);
    unlink(fifo_c2s); unlink(fifo_s2c);
    pthread_mutex_lock(&clients_mutex);
      client **pp = &clients;
      while (*pp && *pp!=c) pp = &(*pp)->next;
      if (*pp) *pp = c->next;
    pthread_mutex_unlock(&clients_mutex);
    free(c->username); free(c);
    return NULL;
}

void load_roles(const char *path) {
    FILE *fp = fopen(path, "r");
    if (!fp) {
        perror("Failed to open roles file");
        return;
    }
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        char username[256], perm_str[256];
        if (sscanf(line, " %255s %255s", username, perm_str) != 2)
            continue;
        role *new_role = malloc(sizeof(role));
        new_role->username = strdup(username);
        new_role->perm = (strcmp(perm_str, "read") == 0) ? ROLE_READ : ROLE_WRITE;
        new_role->next = roles;
        roles = new_role;
    }
    fclose(fp);
}

void *broadcast_thread(void *arg) {

    // Block SIG_REGISTER before any threads
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIG_REGISTER);
    if (pthread_sigmask(SIG_BLOCK, &mask, NULL) == -1) {
        perror("sigprocmask(SIG_BLOCK)");
        exit(EXIT_FAILURE);
    }

    (void)arg;
    for (;;) {
        struct timespec _ts;
        _ts.tv_sec = update_interval_ms / 1000;
        _ts.tv_nsec = (update_interval_ms % 1000) * 1000000L;
        nanosleep(&_ts, NULL);

        pthread_mutex_lock(&commands_mutex);
        if (doc->commands) {
            markdown_increment_version(doc);

            // Broadcast updated document to all connected clients
            char *content = markdown_flatten(doc);
            size_t doc_len = document_length(doc);

            pthread_mutex_lock(&clients_mutex);
            for (client *c = clients; c; c = c->next) {
                const char *perm_str = (c->perm == ROLE_WRITE) ? "write" : "read";
                int header_len = snprintf(
                    NULL, 0,
                    "%s\n%" PRIu64 "\n%zu\n",
                    perm_str,
                    doc->version,
                    doc_len
                );
                size_t total_len = (size_t)header_len + doc_len;
                char *buf = malloc(total_len + 1);
                snprintf(
                    buf,
                    header_len + 1,
                    "%s\n%" PRIu64 "\n%zu\n",
                    perm_str,
                    doc->version,
                    doc_len
                );
                memcpy(buf + header_len, content, doc_len);
                size_t sent = 0;
                while (sent < total_len) {
                    ssize_t w = write(c->fd_s2c, buf + sent, total_len - sent);
                    if (w <= 0) break;
                    sent += (size_t)w;
                }
                free(buf);
            }
            pthread_mutex_unlock(&clients_mutex);

            free(content);
        }
        pthread_mutex_unlock(&commands_mutex);
        
    }
    return NULL;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <TIME_INTERVAL_ms>\n", argv[0]);
        return 1;
    }
    int ms = atoi(argv[1]);
    if (ms <= 0) {
        fprintf(stderr, "Invalid interval\n");
        return EXIT_FAILURE;
    }
    update_interval_ms = (long)ms;

    // 1) print PID
    printf("Server PID: %d\n", getpid());
    fflush(stdout);

    // 2) load roles
    load_roles("roles.txt");

    // 3) init document
    doc = markdown_init();

    // 4) start broadcast thread
    pthread_t btid;
    pthread_create(&btid, NULL, broadcast_thread, NULL);
    pthread_detach(btid);

    // 5) register signal handler
    setup_signal_handler();

    // 6) wait for QUIT
    char cmdbuf[16];
    while (fgets(cmdbuf, sizeof(cmdbuf), stdin)) {
        if (strcmp(cmdbuf, "QUIT\n") == 0) {
            pthread_mutex_lock(&clients_mutex);
            int n = 0;
            for (client *c = clients; c; c = c->next) n++;
            pthread_mutex_unlock(&clients_mutex);
            if (n == 0) {

                // save document in "doc.md"
                FILE *fp = fopen("doc.md", "w");
                if (fp) {
                    char *content = markdown_flatten(doc);
                    fwrite(content, 1, document_length(doc), fp);
                    free(content);
                    fclose(fp);
                } else {
                    perror("Failed to save document");
                }

                document_destroy(doc);
                while (roles) {
                    role *r = roles->next;
                    free(roles->username);
                    free(roles);
                    roles = r;
                }
                while (clients) {
                    client *c = clients->next;
                    free(clients->username);
                    free(clients);
                    clients = c;
                }
                return 0;
            } else {
                printf("QUIT rejected, %d clients still connected.\n", n);
            }
        } else if (strcmp(cmdbuf, "DOC?\n") == 0) {
            char *content = markdown_flatten(doc);
            printf("Document content:\n%s\n", content);
            free(content);

        }
    }

    return 0;
}