// 323071043

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <signal.h>

#define MAX_BUFFER 1024
#define MAX_PLAYERS 100

typedef struct {
    int socket;
    int id;
    int active;
} Player;

int server_socket;
Player players[MAX_PLAYERS];
int max_number_of_players;
int target_number;
fd_set read_fds, write_fds;

void usage() {
    fprintf(stderr, "Usage: ./server <port> <seed> <max-number-of-players>\n");
    exit(EXIT_FAILURE);
}

void cleanup() {
    printf("Shutting down server...\n");
    close(server_socket);
    for (int i = 0; i < max_number_of_players; ++i) {
        if (players[i].active) {
            close(players[i].socket);
        }
    }
    exit(EXIT_SUCCESS);
}

void handle_signal(int signal) {
    cleanup();
}

void init_server(int port, int seed, int max_players) {
    struct sockaddr_in server_addr;

    // Seed the random number generator
    srand(seed);
    target_number = rand() % 100 + 1;

    // Initialize players array
    memset(players, 0, sizeof(players));

    // Create server socket
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    // Bind server socket
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    // Start listening
    if (listen(server_socket, max_players) < 0) {
        perror("listen");
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    printf("Server initialized on port %d\n", port);
}

void broadcast_message(const char *message, int exclude_id) {
    for (int i = 0; i < max_number_of_players; ++i) {
        if (players[i].active && players[i].id != exclude_id) {
            send(players[i].socket, message, strlen(message), 0);
        }
    }
}

void handle_new_connection() {
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    int client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &addr_len);

    if (client_socket < 0) {
        perror("accept");
        return;
    }

    // Assign a player ID and add to players array
    int player_id = -1;
    for (int i = 0; i < max_number_of_players; ++i) {
        if (!players[i].active) {
            players[i].socket = client_socket;
            players[i].id = i + 1;
            players[i].active = 1;
            player_id = players[i].id;
            break;
        }
    }

    if (player_id == -1) {
        // Max players reached
        const char *msg = "Server full. Try again later.\n";
        send(client_socket, msg, strlen(msg), 0);
        close(client_socket);
        return;
    }

    // Send welcome message to the new player
    char welcome_msg[MAX_BUFFER];
    snprintf(welcome_msg, sizeof(welcome_msg), "Welcome to the game, your id is %d\n", player_id);
    send(client_socket, welcome_msg, strlen(welcome_msg), 0);

    // Notify other players
    snprintf(welcome_msg, sizeof(welcome_msg), "Player %d joined the game\n", player_id);
    broadcast_message(welcome_msg, player_id);

    printf("Player %d connected.\n", player_id);
}

void handle_player_input(int player_index) {
    char buffer[MAX_BUFFER];
    int bytes_read = recv(players[player_index].socket, buffer, sizeof(buffer) - 1, 0);

    if (bytes_read <= 0) {
        // Player disconnected
        printf("Player %d disconnected.\n", players[player_index].id);
        char disconnect_msg[MAX_BUFFER];
        snprintf(disconnect_msg, sizeof(disconnect_msg), "Player %d disconnected\n", players[player_index].id);
        broadcast_message(disconnect_msg, players[player_index].id);

        close(players[player_index].socket);
        players[player_index].active = 0;
        return;
    }

    buffer[bytes_read] = '\0';
    int guess = atoi(buffer);
    printf("Player %d guessed %d\n", players[player_index].id, guess);

    char response[MAX_BUFFER];
    snprintf(response, sizeof(response), "Player %d guessed %d\n", players[player_index].id, guess);
    broadcast_message(response, players[player_index].id);

    if (guess > target_number) {
        snprintf(response, sizeof(response), "The guess %d is too high\n", guess);
        send(players[player_index].socket, response, strlen(response), 0);
    } else if (guess < target_number) {
        snprintf(response, sizeof(response), "The guess %d is too low\n", guess);
        send(players[player_index].socket, response, strlen(response), 0);
    } else {
        snprintf(response, sizeof(response), "Player %d wins\n", players[player_index].id);
        broadcast_message(response, -1);

        snprintf(response, sizeof(response), "The correct guess is %d\n", target_number);
        broadcast_message(response, -1);

        // Reset game
        target_number = rand() % 100 + 1;
        printf("New target number: %d\n", target_number);
    }
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        usage();
    }

    int port = atoi(argv[1]);
    int seed = atoi(argv[2]);
    max_number_of_players = atoi(argv[3]);

    if (port <= 0 || port > 65535 || max_number_of_players <= 1) {
        usage();
    }

    signal(SIGINT, handle_signal);

    init_server(port, seed, max_number_of_players);

    FD_ZERO(&read_fds);
    FD_ZERO(&write_fds);

    while (1) {
        FD_SET(server_socket, &read_fds);
        int max_sd = server_socket;

        for (int i = 0; i < max_number_of_players; ++i) {
            if (players[i].active) {
                FD_SET(players[i].socket, &read_fds);
                if (players[i].socket > max_sd) {
                    max_sd = players[i].socket;
                }
            }
        }

        int activity = select(max_sd + 1, &read_fds, NULL, NULL, NULL);
        if (activity < 0 && errno != EINTR) {
            perror("select");
            cleanup();
        }

        if (FD_ISSET(server_socket, &read_fds)) {
            printf("Server is ready to read from welcome socket %d\n", server_socket);
            handle_new_connection();
        }

        for (int i = 0; i < max_number_of_players; ++i) {
            if (players[i].active && FD_ISSET(players[i].socket, &read_fds)) {
                printf("Server is ready to read from player %d on socket %d\n", players[i].id, players[i].socket);
                handle_player_input(i);
            }
        }
    }

    cleanup();
    return 0;
}
