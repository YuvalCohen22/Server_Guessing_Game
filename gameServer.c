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

typedef struct Msg_q {
    char message[MAX_BUFFER];
    struct Msg_q* next;
} Msg_q;

typedef struct {
    int socket;
    int id;
    int active;
    Msg_q* head;
} Player;

int server_socket;
Player* players;
int max_number_of_players;
int target_number;
int player_count = 0;
int max_fd = 0;
int game_over = 0;
fd_set temp_read_fds, temp_write_fds;

void free_player(int index) {
    FD_CLR(players[index].socket, &temp_read_fds);
    FD_CLR(players[index].socket, &temp_write_fds);
    players[index].active = 0;
    close(players[index].socket);
    Msg_q* ptr = players[index].head;
    while (players[index].head != NULL) {
        ptr = ptr->next;
        free(players[index].head);
        players[index].head = ptr;
    }
}

void usage() {
    fprintf(stderr, "Usage: ./server <port> <seed> <max-number-of-players>\n");
    exit(EXIT_FAILURE);
}

void cleanup() {
    close(server_socket);
    for (int i = 0; i < max_number_of_players; ++i) {
        if (players[i].active) {
            free_player(i);
        }
    }
    free(players);
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
    memset(players, 0, max_number_of_players * sizeof(Player));

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

}

void enqueue_message(const char *message, int exclude_id) {
    for (int i = 0; i < max_number_of_players; ++i) {
        if (players[i].active && players[i].id != exclude_id) {
            Msg_q* ptr = players[i].head;
            if (ptr != NULL) {
                while (ptr->next != NULL)
                    ptr = ptr->next;
                ptr->next = (Msg_q*)malloc(sizeof(Msg_q));
                ptr->next->next = NULL;
                strcpy(ptr->next->message, message);
            }
            else {
                players[i].head = (Msg_q*)malloc(sizeof(Msg_q));
                players[i].head->next = NULL;
                strcpy(players[i].head->message, message);
            }
            FD_SET(players[i].socket, &temp_write_fds);
        }
    }
}

void enqueue(const char *message, int id) {
    Msg_q* ptr = players[id].head;
    if (ptr != NULL) {
        while (ptr->next != NULL)
            ptr = ptr->next;
        ptr->next = (Msg_q*)malloc(sizeof(Msg_q));
        ptr->next->next = NULL;
        strcpy(ptr->next->message, message);
    }
    else {
        players[id].head = (Msg_q*)malloc(sizeof(Msg_q));
        players[id].head->next = NULL;
        strcpy(players[id].head->message, message);
    }
    FD_SET(players[id].socket, &temp_write_fds);
}

void send_message_to_player(int index) {
        if (players[index].active) {
            Msg_q* message = players[index].head;
            players[index].head = players[index].head->next;
            send(players[index].socket, message->message, strlen(message->message), 0);
            free(message);
        }
        if (players[index].head == NULL) {
            FD_CLR(players[index].socket, &temp_write_fds);
            if (game_over) {
                free_player(index);
                player_count--;
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
    int ind = -1;
    for (int i = 0; i < max_number_of_players; ++i) {
        if (!players[i].active) {
            player_count++;
            ind = i;
            players[i].socket = client_socket;
            players[i].id = i + 1;
            players[i].active = 1;
            player_id = players[i].id;
            players[i].head = NULL;
            if (max_fd < players[i].socket)
                max_fd = players[i].socket;
            break;
        }
    }

    FD_SET(players[ind].socket, &temp_read_fds);

    // Send welcome message to the new player
    char welcome_msg[MAX_BUFFER];
    snprintf(welcome_msg, sizeof(welcome_msg), "Welcome to the game, your id is %d\n", player_id);
    enqueue(welcome_msg, ind);

    // Notify other players
    snprintf(welcome_msg, sizeof(welcome_msg), "Player %d joined the game\n", player_id);
    enqueue_message(welcome_msg, player_id);

}

void handle_player_input(int player_index) {
    char buffer[MAX_BUFFER];
    int bytes_read = read(players[player_index].socket, buffer, sizeof(buffer) - 1);

    if (bytes_read <= 0) {
        // Player disconnected
        player_count--;
        char disconnect_msg[MAX_BUFFER];
        snprintf(disconnect_msg, sizeof(disconnect_msg), "Player %d disconnected\n", players[player_index].id);
        enqueue_message(disconnect_msg, players[player_index].id);
        if (max_fd == players[player_index].socket) {
            for (int i = 0; i < max_number_of_players; ++i) {
                if (players[i].active) {
                    if (players[i].socket > max_fd) {
                        max_fd = players[i].socket;
                    }
                }
            }
        }
        free_player(player_index);

        return;
    }

    buffer[bytes_read] = '\0';
    int guess = atoi(buffer);

    char response[MAX_BUFFER];
    snprintf(response, sizeof(response), "Player %d guessed %d\n", players[player_index].id, guess);
    enqueue_message(response, -1);

    if (guess > target_number) {
        snprintf(response, sizeof(response), "The guess %d is too high\n", guess);
        enqueue_message(response, -1);
    } else if (guess < target_number) {
        snprintf(response, sizeof(response), "The guess %d is too low\n", guess);
        enqueue_message(response, -1);
    } else {
        game_over = 1;
        snprintf(response, sizeof(response), "Player %d wins\n", players[player_index].id);
        enqueue_message(response, -1);

        snprintf(response, sizeof(response), "The correct guessing is %d\n", target_number);
        enqueue_message(response, -1);
    }
}

int main(int argc, char *argv[]) {
    if (argc != 4)
        usage();

    int port = atoi(argv[1]);
    int seed = atoi(argv[2]);
    max_number_of_players = atoi(argv[3]);

    if (port <= 0 || port > 65535 || max_number_of_players <= 0) {
        usage();
    }

    players = (Player*)malloc(sizeof(Player) * max_number_of_players);
    if (players == NULL) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }

    signal(SIGINT, handle_signal);

    init_server(port, seed, max_number_of_players);

    fd_set read_fds, write_fds;

    FD_ZERO(&read_fds);
    FD_ZERO(&write_fds);


    FD_ZERO(&temp_read_fds);
    FD_ZERO(&temp_write_fds);

    max_fd = server_socket;

    while (1) {

        if (player_count == max_number_of_players)
            FD_CLR(server_socket, &temp_read_fds);
        else
            FD_SET(server_socket, &temp_read_fds);

        read_fds = temp_read_fds;
        write_fds = temp_write_fds;

        int activity = select(max_fd + 1, &read_fds, &write_fds, NULL, NULL);
        if (activity < 0 && errno != EINTR) {
            perror("select");
            cleanup();
        }

        if (FD_ISSET(server_socket, &read_fds)) {
            printf("Server is ready to read from welcome socket %d\n", server_socket);
            handle_new_connection(&temp_write_fds, &temp_read_fds);
            activity--;
        }

        for (int i = 0; i < max_number_of_players && activity > 0; ++i) {
            if (players[i].active && FD_ISSET(players[i].socket, &read_fds)) {
                printf("Server is ready to read from player %d on socket %d\n", players[i].id, players[i].socket);
                handle_player_input(i);
                activity--;
            }
            if (players[i].active && FD_ISSET(players[i].socket, &write_fds)) {
                printf("Server is ready to write to player %d on socket %d\n", players[i].id, players[i].socket);
                send_message_to_player(i);
                activity--;
            }
        }

        // Reset to new game
        if (game_over && player_count == 0) {
            // Generate a new target number
            target_number = rand() % 100 + 1;

            max_fd = server_socket;
            game_over = 0;
        }

    }
}
