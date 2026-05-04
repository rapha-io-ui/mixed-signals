#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define MAX_WORD 128
#define MAX_NAME 32
#define BUFFER_SIZE 512
#define DEFAULT_ROUNDS 5
#define REGULAR_SCORE 10
#define SCORE_SCALE 4

static void die_with_error(const char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

static void trim_newline(char *s) {
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r')) {
        s[len - 1] = '\0';
        len--;
    }
}

static void clean_text(char *s) {
    trim_newline(s);
    size_t start = 0;
    while (s[start] && isspace((unsigned char)s[start])) start++;
    if (start > 0) memmove(s, s + start, strlen(s + start) + 1);
    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) {
        s[len - 1] = '\0';
        len--;
    }
}

static void to_lower_copy(char *dest, const char *src, size_t size) {
    size_t i;
    for (i = 0; i + 1 < size && src[i] != '\0'; ++i) {
        dest[i] = (char)tolower((unsigned char)src[i]);
    }
    dest[i] = '\0';
}

static int recv_line_available(int sock, char *pending, size_t *pending_len,
                               char *out, size_t out_size) {
    while (1) {
        char ch;
        ssize_t n = recv(sock, &ch, 1, MSG_DONTWAIT);
        if (n > 0) {
            if (ch == '\n') {
                size_t copy_len = *pending_len;
                if (copy_len >= out_size) copy_len = out_size - 1;
                memcpy(out, pending, copy_len);
                out[copy_len] = '\0';
                *pending_len = 0;
                pending[0] = '\0';
                return 1;
            }
            if (*pending_len + 1 < BUFFER_SIZE) {
                pending[*pending_len] = ch;
                (*pending_len)++;
                pending[*pending_len] = '\0';
            }
            continue;
        }
        if (n == 0) return -1;
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
        if (errno == EINTR) continue;
        return -1;
    }
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

static void enable_responsive_socket(int sock) {
    int opt = 1;
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));
}

static void send_both(int p1, int p2, const char *msg) {
    send_all(p1, msg);
    send_all(p2, msg);
}

static void format_score(char *out, size_t out_size, int units) {
    int whole = units / SCORE_SCALE;
    int quarter = units % SCORE_SCALE;
    if (quarter == 0) snprintf(out, out_size, "%d", whole);
    else if (quarter == 1) snprintf(out, out_size, "%d.25", whole);
    else if (quarter == 2) snprintf(out, out_size, "%d.5", whole);
    else snprintf(out, out_size, "%d.75", whole);
}

static void scoreboard(int p1, int p2, const char *name1, const char *name2, int score1, int score2) {
    char s1[32], s2[32], msg[BUFFER_SIZE];
    format_score(s1, sizeof(s1), score1);
    format_score(s2, sizeof(s2), score2);
    snprintf(msg, sizeof(msg), "SCOREBOARD -> %s: %s | %s: %s\n", name1, s1, name2, s2);
    send_both(p1, p2, msg);
}

static int max_int(int a, int b) {
    return a > b ? a : b;
}

static int drain_out_of_turn_input(int sock, const char *name, char *pending, size_t *pending_len) {
    char junk[BUFFER_SIZE];
    int n = recv_line_available(sock, pending, pending_len, junk, sizeof(junk));
    if (n < 0) return -1;
    if (n == 0) return 1;
    clean_text(junk);
    if (strlen(junk) > 0) {
        char msg[BUFFER_SIZE];
        snprintf(msg, sizeof(msg), "[WAIT] %s, input ignored until it is your turn.\n", name);
        send_all(sock, msg);
    }
    return 1;
}

static int prompt_expected(int expected_sock, int other_sock, const char *expected_name,
                           const char *other_name, const char *prompt,
                           char *out, size_t out_size) {
    if (send_all(expected_sock, prompt) < 0) return -1;
    (void)expected_name;
    char expected_pending[BUFFER_SIZE] = "";
    char other_pending[BUFFER_SIZE] = "";
    size_t expected_pending_len = 0;
    size_t other_pending_len = 0;

    while (1) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(expected_sock, &readfds);
        FD_SET(other_sock, &readfds);

        int ready = select(max_int(expected_sock, other_sock) + 1, &readfds, NULL, NULL, NULL);
        if (ready < 0) return -1;

        if (FD_ISSET(other_sock, &readfds)) {
            if (drain_out_of_turn_input(other_sock, other_name, other_pending, &other_pending_len) < 0) return -1;
        }

        if (FD_ISSET(expected_sock, &readfds)) {
            int n = recv_line_available(expected_sock, expected_pending, &expected_pending_len, out, out_size);
            if (n < 0) return -1;
            if (n == 1) {
                clean_text(out);
                (void)expected_name;
                return 1;
            }
        }
    }
}

static int live_countdown(int p1, int p2, int seconds, const char *label) {
    char msg[BUFFER_SIZE];
    char p1_pending[BUFFER_SIZE] = "";
    char p2_pending[BUFFER_SIZE] = "";
    size_t p1_pending_len = 0;
    size_t p2_pending_len = 0;

    for (int left = seconds; left > 0; --left) {
        snprintf(msg, sizeof(msg), "%s %d...\n", label, left);
        send_both(p1, p2, msg);

        time_t tick_start = time(NULL);
        while ((int)(time(NULL) - tick_start) < 1) {
            fd_set readfds;
            FD_ZERO(&readfds);
            FD_SET(p1, &readfds);
            FD_SET(p2, &readfds);

            struct timeval tv;
            tv.tv_sec = 0;
            tv.tv_usec = 100000;

            int ready = select(max_int(p1, p2) + 1, &readfds, NULL, NULL, &tv);
            if (ready < 0) return -1;
            if (ready > 0 && FD_ISSET(p1, &readfds)) {
                if (drain_out_of_turn_input(p1, "Player 1", p1_pending, &p1_pending_len) < 0) return -1;
            }
            if (ready > 0 && FD_ISSET(p2, &readfds)) {
                if (drain_out_of_turn_input(p2, "Player 2", p2_pending, &p2_pending_len) < 0) return -1;
            }
        }
    }

    return 1;
}

static void shuffle_chars(char *s) {
    size_t len = strlen(s);
    if (len < 2) return;
    for (size_t i = 0; i < len; ++i) {
        size_t j = (size_t)(rand() % (int)len);
        char temp = s[i];
        s[i] = s[j];
        s[j] = temp;
    }
}

static void distort_scramble(const char *word, char *out, size_t out_size) {
    strncpy(out, word, out_size - 1);
    out[out_size - 1] = '\0';
    if (strlen(out) > 1) {
        shuffle_chars(out);
        if (strcmp(out, word) == 0) shuffle_chars(out);
    }
}

static void distort_remove(const char *word, char *out, size_t out_size) {
    size_t len = strlen(word);
    size_t pos = 0;
    if (len <= 2) {
        strncpy(out, word, out_size - 1);
        out[out_size - 1] = '\0';
        return;
    }
    for (size_t i = 0; i < len && pos + 1 < out_size; ++i) {
        if ((rand() % 100) < 35 && i != 0 && i != len - 1) continue;
        out[pos++] = word[i];
    }
    out[pos] = '\0';
    if (strlen(out) < 2) snprintf(out, out_size, "%c_%c", word[0], word[len - 1]);
}

static void distort_reverse(const char *word, char *out, size_t out_size) {
    size_t len = strlen(word);
    if (len + 1 > out_size) len = out_size - 1;
    for (size_t i = 0; i < len; ++i) out[i] = word[len - 1 - i];
    out[len] = '\0';
}

static const char *apply_random_distortion(const char *word, char *out, size_t out_size) {
    int mode = rand() % 3;
    if (mode == 0) {
        distort_scramble(word, out, out_size);
        return "SCRAMBLE";
    }
    if (mode == 1) {
        distort_remove(word, out, out_size);
        return "REMOVE";
    }
    distort_reverse(word, out, out_size);
    return "REVERSE";
}

static bool words_match(const char *a, const char *b) {
    char x[MAX_WORD], y[MAX_WORD];
    to_lower_copy(x, a, sizeof(x));
    to_lower_copy(y, b, sizeof(y));
    return strcmp(x, y) == 0;
}

static bool valid_word(const char *s) {
    if (strlen(s) < 2) return false;
    for (size_t i = 0; i < strlen(s); ++i) {
        if (!isalpha((unsigned char)s[i])) return false;
    }
    return true;
}

int main(int argc, char *argv[]) {
    if (argc < 2 || argc > 3) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        return EXIT_FAILURE;
    }

    int port_no = atoi(argv[1]);
    if (port_no < 2000 || port_no > 65535) {
        fprintf(stderr, "Invalid port. Use a port from 2000 to 65535.\n");
        return EXIT_FAILURE;
    }
    if (argc == 3) {
        printf("Note: turn timers are disabled, so the extra time-limit argument is ignored.\n");
    }

    srand((unsigned int)time(NULL));

    int server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) die_with_error("socket");

    int opt = 1;
    if (setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) die_with_error("setsockopt");

    struct sockaddr_in server_addr;
    bzero((char *)&server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons((uint16_t)port_no);

    if (bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) die_with_error("bind");
    if (listen(server_sock, 5) < 0) die_with_error("listen");

    printf("Mixed Signals server listening on port %d...\n", port_no);

    struct sockaddr_in client_addr;
    socklen_t client_size = sizeof(client_addr);

    printf("Waiting for Player 1...\n");
    int p1 = accept(server_sock, (struct sockaddr *)&client_addr, &client_size);
    if (p1 < 0) die_with_error("accept player1");
    enable_responsive_socket(p1);
    send_all(p1, "WELCOME Player 1. Waiting for Player 2...\n");

    printf("Waiting for Player 2...\n");
    int p2 = accept(server_sock, (struct sockaddr *)&client_addr, &client_size);
    if (p2 < 0) die_with_error("accept player2");
    enable_responsive_socket(p2);
    send_all(p2, "WELCOME Player 2.\n");
    send_both(p1, p2, "Both players connected. Setup will start.\n");

    char name1[MAX_NAME] = "Player 1";
    char name2[MAX_NAME] = "Player 2";
    char tmp[BUFFER_SIZE];

    send_both(p1, p2, "\nSETUP STEP 1/3: Player 1 must enter a name.\n");
    while (1) {
        if (prompt_expected(p1, p2, "Player 1", "Player 2",
                            "Player 1 name: ", name1, sizeof(name1)) < 0) goto disconnected;
        if (strlen(name1) > 0) break;
        send_all(p1, "Name cannot be empty.\n");
    }

    send_both(p1, p2, "\nSETUP STEP 2/3: Player 1 must choose how many rounds to play.\n");
    int total_rounds = DEFAULT_ROUNDS;
    while (1) {
        snprintf(tmp, sizeof(tmp), "Number of rounds (1-50): ");
        if (prompt_expected(p1, p2, name1, "Player 2", tmp, tmp, sizeof(tmp)) < 0) goto disconnected;
        total_rounds = atoi(tmp);
        if (total_rounds >= 1 && total_rounds <= 50) break;
        send_all(p1, "Invalid round count. Enter a number from 1 to 50.\n");
    }

    send_both(p1, p2, "\nSETUP STEP 3/3: Player 2 must enter a name.\n");
    while (1) {
        if (prompt_expected(p2, p1, "Player 2", name1,
                            "Player 2 name: ", name2, sizeof(name2)) < 0) goto disconnected;
        if (strlen(name2) > 0) break;
        send_all(p2, "Name cannot be empty.\n");
    }

    snprintf(tmp, sizeof(tmp), "Game setup complete: %s vs %s | Rounds: %d\n",
             name1, name2, total_rounds);
    send_both(p1, p2, tmp);

    if (live_countdown(p1, p2, 3, "Game starts in") < 0) goto disconnected;
    send_both(p1, p2, "START!\n");

    int score1 = 0, score2 = 0;

    for (int round = 1; round <= total_rounds; ++round) {
        int sender = (round % 2 == 1) ? 1 : 2;
        int guesser = (sender == 1) ? 2 : 1;
        int sender_sock = (sender == 1) ? p1 : p2;
        int guesser_sock = (guesser == 1) ? p1 : p2;
        const char *sender_name = (sender == 1) ? name1 : name2;
        const char *guesser_name = (guesser == 1) ? name1 : name2;

        snprintf(tmp, sizeof(tmp), "\n=== ROUND %d/%d ===\nTurn: %s sends the word. %s guesses.\n",
                 round, total_rounds, sender_name, guesser_name);
        send_both(p1, p2, tmp);
        scoreboard(p1, p2, name1, name2, score1, score2);

        char original[MAX_WORD];
        while (1) {
            snprintf(tmp, sizeof(tmp), "%s, enter one word (letters only, no spaces): ", sender_name);
            if (prompt_expected(sender_sock, guesser_sock, sender_name, guesser_name,
                                tmp, original, sizeof(original)) < 0) goto disconnected;
            if (valid_word(original)) break;
            send_all(sender_sock, "Invalid input. Use one word with letters only.\n");
        }

        snprintf(tmp, sizeof(tmp), "%s entered a word. %s will guess now.\n", sender_name, guesser_name);
        send_both(p1, p2, tmp);

        char distorted1[MAX_WORD], distorted2[MAX_WORD];
        const char *mode1 = apply_random_distortion(original, distorted1, sizeof(distorted1));
        const char *mode2 = apply_random_distortion(original, distorted2, sizeof(distorted2));

        char guess[MAX_WORD];
        snprintf(tmp, sizeof(tmp), "Distorted word: %s\nMode: %s\n%s, type your guess or PASS: ",
                 distorted1, mode1, guesser_name);
        if (prompt_expected(guesser_sock, sender_sock, guesser_name, sender_name,
                            tmp, guess, sizeof(guess)) < 0) goto disconnected;

        if (strcasecmp(guess, "PASS") == 0) {
            snprintf(tmp, sizeof(tmp), "%s used PASS. New distorted word: %s\nMode: %s\n%s, enter your final guess: ",
                     guesser_name, distorted2, mode2, guesser_name);
            if (prompt_expected(guesser_sock, sender_sock, guesser_name, sender_name,
                                tmp, guess, sizeof(guess)) < 0) goto disconnected;
            if (words_match(guess, original)) {
                int earned = (REGULAR_SCORE / 2) * SCORE_SCALE;
                if (guesser == 1) score1 += earned;
                else score2 += earned;
                char earned_text[32];
                format_score(earned_text, sizeof(earned_text), earned);
                snprintf(tmp, sizeof(tmp), "Correct! %s earns %s points. Original word: '%s'.\n",
                         guesser_name, earned_text, original);
                send_both(p1, p2, tmp);
            } else {
                if (sender == 1) score1 += REGULAR_SCORE * SCORE_SCALE;
                else score2 += REGULAR_SCORE * SCORE_SCALE;
                snprintf(tmp, sizeof(tmp), "Wrong guess. %s earns %d points. Original word: '%s'.\n",
                         sender_name, REGULAR_SCORE, original);
                send_both(p1, p2, tmp);
            }
        } else if (words_match(guess, original)) {
            int earned = REGULAR_SCORE * SCORE_SCALE;
            if (guesser == 1) score1 += earned;
            else score2 += earned;
            char earned_text[32];
            format_score(earned_text, sizeof(earned_text), earned);
            snprintf(tmp, sizeof(tmp), "Correct! %s earns %s points. Original word: '%s'.\n",
                     guesser_name, earned_text, original);
            send_both(p1, p2, tmp);
        } else {
            if (sender == 1) score1 += REGULAR_SCORE * SCORE_SCALE;
            else score2 += REGULAR_SCORE * SCORE_SCALE;
            snprintf(tmp, sizeof(tmp), "Wrong guess. %s earns %d points. Original word: '%s'.\n",
                     sender_name, REGULAR_SCORE, original);
            send_both(p1, p2, tmp);
        }

        scoreboard(p1, p2, name1, name2, score1, score2);
    }

    char s1[32], s2[32], final_msg[BUFFER_SIZE];
    format_score(s1, sizeof(s1), score1);
    format_score(s2, sizeof(s2), score2);
    if (score1 > score2) {
        snprintf(final_msg, sizeof(final_msg), "\nGAME OVER\nWinner: %s\nFinal Score -> %s: %s | %s: %s\n",
                 name1, name1, s1, name2, s2);
    } else if (score2 > score1) {
        snprintf(final_msg, sizeof(final_msg), "\nGAME OVER\nWinner: %s\nFinal Score -> %s: %s | %s: %s\n",
                 name2, name1, s1, name2, s2);
    } else {
        snprintf(final_msg, sizeof(final_msg), "\nGAME OVER\nIt's a tie!\nFinal Score -> %s: %s | %s: %s\n",
                 name1, s1, name2, s2);
    }
    send_both(p1, p2, final_msg);

    close(p1);
    close(p2);
    close(server_sock);
    return EXIT_SUCCESS;

disconnected:
    send_both(p1, p2, "A player disconnected. Game ended.\n");
    close(p1);
    close(p2);
    close(server_sock);
    return EXIT_SUCCESS;
}
