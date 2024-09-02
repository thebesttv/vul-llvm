//GEMINI-pro DATASET v1.0 Category: Email Client ; Style: portable
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#define MAX_DATA_SIZE 1024

struct email_client_state {
    int sockfd;
    struct sockaddr_in serveraddr;
    char *host;
    int port;
};

struct email_client_state *email_client_init(const char *host, int port) {
    struct email_client_state *state = malloc(sizeof(struct email_client_state));
    if (state == NULL) {
        fprintf(stderr, "Error: malloc() failed: %s\n", strerror(errno));
        return NULL;
    }

    state->host = strdup(host);
    if (state->host == NULL) {
        fprintf(stderr, "Error: strdup() failed: %s\n", strerror(errno));
        free(state);
        return NULL;
    }

    state->port = port;

    state->sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (state->sockfd == -1) {
        fprintf(stderr, "Error: socket() failed: %s\n", strerror(errno));
        free(state->host);
        free(state);
        return NULL;
    }

    struct hostent *hostent = gethostbyname(host);
    if (hostent == NULL) {
        fprintf(stderr, "Error: gethostbyname() failed: %s\n", strerror(errno));
        close(state->sockfd);
        free(state->host);
        free(state);
        return NULL;
    }

    memset(&state->serveraddr, 0, sizeof(state->serveraddr));
    state->serveraddr.sin_family = AF_INET;
    state->serveraddr.sin_port = htons(port);
    memcpy(&state->serveraddr.sin_addr, hostent->h_addr_list[0], hostent->h_length);

    return state;
}

int email_client_connect(struct email_client_state *state) {
    if (connect(state->sockfd, (struct sockaddr *)&state->serveraddr, sizeof(state->serveraddr)) == -1) {
        fprintf(stderr, "Error: connect() failed: %s\n", strerror(errno));
        close(state->sockfd);
        free(state->host);
        free(state);
        return -1;
    }

    return 0;
}

int email_client_send_email(struct email_client_state *state, const char *from, const char *to, const char *subject, const char *body) {
    char buffer[MAX_DATA_SIZE];

    snprintf(buffer, MAX_DATA_SIZE, "From: %s\r\nTo: %s\r\nSubject: %s\r\n\r\n%s\r\n", from, to, subject, body);

    if (send(state->sockfd, buffer, strlen(buffer), 0) == -1) {
        fprintf(stderr, "Error: send() failed: %s\n", strerror(errno));
        return -1;
    }

    return 0;
}

int email_client_close(struct email_client_state *state) {
    close(state->sockfd);
    free(state->host);
    free(state);

    return 0;
}

int main(int argc, char **argv) {
    if (argc != 6) {
        fprintf(stderr, "Usage: %s <host> <port> <from> <to> <subject>\n", argv[0]);
        return 1;
    }

    struct email_client_state *state = email_client_init(argv[1], atoi(argv[2]));
    if (state == NULL) {
        return 1;
    }

    if (email_client_connect(state) == -1) {
        email_client_close(state);
        return 1;
    }

    if (email_client_send_email(state, argv[3], argv[4], argv[5], "This is a test email") == -1) {
        email_client_close(state);
        return 1;
    }

    email_client_close(state);

    return 0;
}