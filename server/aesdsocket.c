
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <syslog.h>
#include <netdb.h>
#include <arpa/inet.h>

#define PORT "9000"
#define BACKLOG 10
#define DATA_FILE "/var/tmp/aesdsocketdata"
#define BUFFER_SIZE 1024

static volatile sig_atomic_t exit_requested = 0;

static int handle_client(int client_fd);
static int get_client_ip(struct sockaddr_storage *client_addr, char *ip_buffer, size_t ip_buffer_size);
static void signal_handler(int signo);
static int append_packet(const char *packet, size_t packet_length);
static int send_file_to_client(int client_fd);
static int send_all(int fd, const char *buffer, size_t length);

int main(int argc, char *argv[])
{
    int server_fd = -1;
    int daemon_mode = 0;

    struct addrinfo hints;
    struct addrinfo *servinfo = NULL;
    struct addrinfo *iter = NULL;

    // CLA
    if (argc > 2) {
        fprintf(stderr, "Usage: %s [-d]\n", argv[0]);
        return -1;
    }

    if (argc == 2) {
        if (strcmp(argv[1], "-d") == 0) {
            daemon_mode = 1;
        }
        else {
            fprintf(stderr, "Usage: %s [-d]\n", argv[0]);
            return -1;
        }
    }

    openlog("aesdsocket", LOG_PID | LOG_CONS, LOG_USER);

    // Signal Handling
    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));

    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);

    sa.sa_flags = 0;

    if (sigaction(SIGINT, &sa, NULL) == -1) {
        syslog(LOG_ERR, "sigaction(SIGINT) failed: %s", strerror(errno));
        closelog();
        return -1;
    }

    if (sigaction(SIGTERM, &sa, NULL) == -1) {
        syslog(LOG_ERR, "sigaction(SIGTERM) failed: %s", strerror(errno));
        closelog();
        return -1;
    }


    // Server Address Configuration
    memset(&hints, 0, sizeof(hints));

    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    hints.ai_protocol = 0;


    // Local Address Info
    int status = getaddrinfo(NULL, PORT, &hints, &servinfo);

    if (status != 0) {
        syslog(LOG_ERR, "getaddrinfo: %s", gai_strerror(status));
        closelog();
        return -1;
    }


    // Address Iteration
    for (iter = servinfo; iter != NULL; iter = iter->ai_next) {

        server_fd =
            socket(iter->ai_family, iter->ai_socktype, iter->ai_protocol);

        if (server_fd == -1) {
            continue;
        }

        int opt = 1;
        if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {

            syslog(LOG_ERR, "setsockopt failed: %s", strerror(errno));

            close(server_fd);
            server_fd = -1;
            continue;
        }


        /*
         * Bind to port 9000.
         */
        if (bind(server_fd, iter->ai_addr, iter->ai_addrlen) == 0) {
            break;
        }

        close(server_fd);
        server_fd = -1;
    }

    if (iter == NULL || server_fd == -1) {

        syslog(LOG_ERR,
               "Failed to bind socket");

        freeaddrinfo(servinfo);
        closelog();

        return -1;
    }

    // Daemon vs Process
    if (daemon_mode) {
        pid_t pid = fork();

        if (pid < 0) {
            syslog(LOG_ERR, "fork failed: %s", strerror(errno));
            close(server_fd);
            freeaddrinfo(servinfo);
            closelog();
            return -1;
        }

        if (pid > 0) {
            close(server_fd);
            freeaddrinfo(servinfo);
            closelog();
            return 0;
        }

        if (setsid() == -1) {
            syslog(LOG_ERR, "setsid failed: %s", strerror(errno));
            close(server_fd);
            freeaddrinfo(servinfo);
            closelog();
            return -1;
        }

        int devnull = open("/dev/null", O_RDWR);
        if (devnull == -1) {
            syslog(LOG_ERR, "Failed to open /dev/null: %s", strerror(errno));
            close(server_fd);
            freeaddrinfo(servinfo);
            closelog();
            return -1;
        }

        dup2(devnull, STDIN_FILENO);
        dup2(devnull, STDOUT_FILENO);
        dup2(devnull, STDERR_FILENO);

        if (devnull > STDERR_FILENO) {
            close(devnull);
        }
    }

    freeaddrinfo(servinfo);
    servinfo = NULL;

    // Acquire Connections
    if (listen(server_fd, BACKLOG) == -1) {
        syslog(LOG_ERR, "listen failed: %s", strerror(errno));
        close(server_fd);
        closelog();
        return -1;
    }


    // Infinity
    while (!exit_requested) {

        struct sockaddr_storage client_addr;
        socklen_t client_addr_len =
            sizeof(client_addr);

        int client_fd =
            accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len);

        if (client_fd == -1) {
            if (errno == EINTR) {
                if (exit_requested) {
                    break;
                }
                continue;
            }

            syslog(LOG_ERR, "accept failed: %s", strerror(errno));
            break;
        }

        // Client IP
        char client_ip[INET6_ADDRSTRLEN];

        if (get_client_ip(&client_addr, client_ip, sizeof(client_ip)) == -1) {
            strncpy(client_ip, "unknown", sizeof(client_ip));
            client_ip[sizeof(client_ip) - 1] = '\0';
        }

        syslog(LOG_INFO, "Accepted connection from %s", client_ip);

        handle_client(client_fd);


        close(client_fd);
        syslog(LOG_INFO, "Closed connection from %s", client_ip);
    }


    syslog(LOG_INFO, "Caught signal, exiting");
    close(server_fd);

    if (unlink(DATA_FILE) == -1) {
        if (errno != ENOENT) {
            syslog(LOG_ERR, "Failed to delete %s: %s", DATA_FILE, strerror(errno));
        }
    }

    closelog();
    return 0;
}

static int handle_client(int client_fd)
{
    char *packet_buffer = NULL;
    size_t packet_length = 0;
    size_t packet_capacity = 0;

    char recv_buffer[BUFFER_SIZE];

    while (!exit_requested) {
        ssize_t bytes_received = recv(client_fd, recv_buffer, sizeof(recv_buffer), 0);

        if (bytes_received < 0) {

            if (errno == EINTR) {
                if (exit_requested) {
                    break;
                }

                continue;
            }

            syslog(LOG_ERR, "recv failed: %s", strerror(errno));
            free(packet_buffer);
            return -1;
        }

        if (bytes_received == 0) {
            break;
        }

        for (ssize_t i = 0; i < bytes_received; i++) {
            if (packet_length + 1 > packet_capacity) {
                size_t new_capacity;

                if (packet_capacity == 0) {
                    new_capacity = 1024;
                }
                else {
                    new_capacity = packet_capacity * 2;
                }

                char *new_buffer = realloc(packet_buffer, new_capacity);

                if (new_buffer == NULL) {
                    syslog(LOG_ERR, "malloc/realloc failed while receiving packet");

                    free(packet_buffer);
                    return -1;
                }

                packet_buffer = new_buffer;
                packet_capacity = new_capacity;
            }

            packet_buffer[packet_length] = recv_buffer[i];
            packet_length++;

            if (recv_buffer[i] == '\n') {
                if (append_packet(packet_buffer, packet_length) == -1) {

                    free(packet_buffer);
                    return -1;
                }

                if (send_file_to_client(client_fd) == -1) {

                    free(packet_buffer);
                    return -1;
                }

                packet_length = 0;
            }
        }
    }

    free(packet_buffer);

    return 0;
}

static int get_client_ip(struct sockaddr_storage *client_addr, char *ip_buffer, size_t ip_buffer_size)
{
    void *address;

    if (client_addr->ss_family == AF_INET) {
        struct sockaddr_in *ipv4 = (struct sockaddr_in *)client_addr;

        address = &(ipv4->sin_addr);
    }
    else if (client_addr->ss_family == AF_INET6) {
        struct sockaddr_in6 *ipv6 = (struct sockaddr_in6 *)client_addr;

        address = &(ipv6->sin6_addr);
    }
    else {
        return -1;
    }

    if (inet_ntop(client_addr->ss_family, address, ip_buffer, ip_buffer_size) == NULL) {
        return -1;
    }

    return 0;
}

static int append_packet(const char *packet, size_t packet_length)
{
    int file_fd;

    file_fd = open(DATA_FILE, O_WRONLY | O_CREAT | O_APPEND, 0644);

    if (file_fd == -1) {
        syslog(LOG_ERR, "Failed to open %s for writing: %s", DATA_FILE, strerror(errno));
        return -1;
    }

    size_t total_written = 0;
    while (total_written < packet_length) {
        ssize_t written = write(file_fd, packet + total_written, packet_length - total_written);

        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }

            syslog(LOG_ERR, "Failed to write %s: %s", DATA_FILE, strerror(errno));

            close(file_fd);
            return -1;
        }

        total_written += written;
    }

    close(file_fd);

    return 0;
}

static int send_file_to_client(int client_fd)
{
    int file_fd;
    char buffer[BUFFER_SIZE];

    file_fd = open(DATA_FILE, O_RDONLY);

    if (file_fd == -1) {
        syslog(LOG_ERR, "Failed to open %s: %s", DATA_FILE, strerror(errno));
        return -1;
    }

    while (1) {
        ssize_t bytes_read = read(file_fd, buffer, sizeof(buffer));

        if (bytes_read < 0) {
            if (errno == EINTR) {
                continue;
            }

            syslog(LOG_ERR, "Failed to read %s: %s", DATA_FILE, strerror(errno));

            close(file_fd);
            return -1;
        }

        if (bytes_read == 0)
            break;

        if (send_all(client_fd, buffer, bytes_read) == -1) {
            syslog(LOG_ERR, "Failed to send file to client: %s", strerror(errno));

            close(file_fd);
            return -1;
        }
    }

    close(file_fd);

    return 0;
}


// SIGINT/SIGTERM
static void signal_handler(int signo)
{
    if (signo == SIGINT || signo == SIGTERM) {
        exit_requested = 1;
    }
}

static int send_all(int fd, const char *buffer, size_t length)
{
    size_t total_sent = 0;

    while (total_sent < length) {
        ssize_t sent = send(fd, buffer + total_sent, length - total_sent, 0);

        if (sent < 0) {
            if (errno == EINTR) {
                if (exit_requested) {
                    return -1;
                }

                continue;
            }

            return -1;
        }

        if (sent == 0)
            return -1;

        total_sent += sent;
    }

    return 0;
}
