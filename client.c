#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define BUFFER_SIZE 512

static void die_with_error(const char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

static int send_all(int sock, const char *msg) {
    size_t len = strlen(msg);
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(sock, msg + sent, len - sent, 0);
        if (n <= 0) return -1;
        sent += (size_t)n;
    }
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <hostname> <port>\n", argv[0]);
        return EXIT_FAILURE;
    }

    int port_no = atoi(argv[2]);
    int client_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (client_sock < 0) die_with_error("socket");

    struct hostent *server = gethostbyname(argv[1]);
    if (server == NULL) {
        fprintf(stderr, "Error: no such host\n");
        close(client_sock);
        return EXIT_FAILURE;
    }

    struct sockaddr_in server_addr;
    bzero((char *)&server_addr, sizeof(server_addr));
    bcopy((char *)server->h_addr_list[0],
          (char *)&server_addr.sin_addr.s_addr,
          (size_t)server->h_length);
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons((uint16_t)port_no);

    if (connect(client_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        die_with_error("connect");
    }

    printf("\nConnected to Mixed Signals server.\n");
    printf("Answer the server prompts, then press Enter. Live updates will appear here.\n\n");
    printf("Typing before your turn is safe, but the server will ignore early input.\n\n");
    fflush(stdout);

    char buffer[BUFFER_SIZE];
    char input[BUFFER_SIZE];

    while (1) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(client_sock, &readfds);
        FD_SET(STDIN_FILENO, &readfds);

        int maxfd = (client_sock > STDIN_FILENO) ? client_sock : STDIN_FILENO;
        int ready = select(maxfd + 1, &readfds, NULL, NULL, NULL);
        if (ready < 0) die_with_error("select");

        if (FD_ISSET(client_sock, &readfds)) {
            ssize_t n = recv(client_sock, buffer, sizeof(buffer) - 1, 0);
            if (n < 0) die_with_error("recv");
            if (n == 0) {
                printf("\nServer closed the connection.\n");
                break;
            }
            buffer[n] = '\0';
            printf("%s", buffer);
            fflush(stdout);
        }

        if (FD_ISSET(STDIN_FILENO, &readfds)) {
            if (fgets(input, sizeof(input), stdin) == NULL) break;
            if (send_all(client_sock, input) < 0) die_with_error("send");
        }
    }

    close(client_sock);
    return EXIT_SUCCESS;
}
