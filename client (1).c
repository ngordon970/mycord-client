#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <netdb.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define ESC "\033"
#define COLOR_RED   "\033[31m"
#define COLOR_GRAY  "\033[90m"
#define COLOR_RESET "\033[0m"

#define MAX_MESSAGES 500
#define MAX_LINE 1200

typedef enum {
    MSG_LOGIN = 0,
    MSG_LOGOUT = 1,
    MSG_MESSAGE_SEND = 2,
    MSG_MESSAGE_RECV = 10,
    MSG_DISCONNECT = 12,
    MSG_SYSTEM = 13
} message_type_t;

typedef struct __attribute__((packed)) {
    uint32_t type;
    uint32_t timestamp;
    char username[32];
    char message[1024];
} message_t;

typedef struct {
    struct sockaddr_in server;
    int socket_fd;
    bool running;
    bool quiet;
    bool tui;
    char username[32];
} settings_t;

static settings_t settings = {0};

/* ───────────── TUI STATE ───────────── */
static char messages[MAX_MESSAGES][MAX_LINE];
static int msg_count = 0;
static int scroll_offset = 0;
static pthread_mutex_t ui_lock = PTHREAD_MUTEX_INITIALIZER;
static struct termios orig_term;

/* ───────────── TERMINAL HELPERS ───────────── */
void clear_screen() { printf(ESC "[2J"); }
void move_cursor(int r, int c) { printf(ESC "[%d;%dH", r, c); }
void hide_cursor() { printf(ESC "[?25l"); }
void show_cursor() { printf(ESC "[?25h"); }
void flush() { fflush(stdout); }

void enable_raw_mode() {
    tcgetattr(STDIN_FILENO, &orig_term);
    struct termios raw = orig_term;
    raw.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

void disable_raw_mode() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_term);
}

/* ───────────── UI FUNCTIONS ───────────── */
void add_message(const char *line) {
    pthread_mutex_lock(&ui_lock);

    if (msg_count < MAX_MESSAGES) {
        strcpy(messages[msg_count++], line);
    } else {
        for (int i = 1; i < MAX_MESSAGES; i++)
            strcpy(messages[i - 1], messages[i]);
        strcpy(messages[MAX_MESSAGES - 1], line);
    }

    pthread_mutex_unlock(&ui_lock);
}

void redraw_ui(const char *input) {
    pthread_mutex_lock(&ui_lock);

    clear_screen();
    move_cursor(1, 1);

    int rows = 24;
    int usable = rows - 2;

    int start = msg_count - usable - scroll_offset;
    if (start < 0) start = 0;

    for (int i = start; i < msg_count - scroll_offset; i++)
        printf("%s\n", messages[i]);

    move_cursor(rows, 1);
    printf("> %s", input);
    flush();

    pthread_mutex_unlock(&ui_lock);
}

/* ───────────── NETWORK HELPERS ───────────── */
ssize_t full_read(int fd, void *buf, size_t n) {
    size_t total = 0;
    while (total < n) {
        ssize_t r = read(fd, (char *)buf + total, n - total);
        if (r <= 0) return r;
        total += r;
    }
    return total;
}

void send_message(message_t *msg) {
    message_t net = *msg;
    net.type = htonl(msg->type);
    net.timestamp = htonl(msg->timestamp);
    write(settings.socket_fd, &net, sizeof(net));
}

/* ───────────── RECEIVE THREAD ───────────── */
void* receive_thread(void *arg) {
    message_t msg;

    while (settings.running) {
        if (full_read(settings.socket_fd, &msg, sizeof(msg)) <= 0)
            break;

        msg.type = ntohl(msg.type);
        msg.timestamp = ntohl(msg.timestamp);

        time_t t = msg.timestamp;
        struct tm *tm = localtime(&t);
        char ts[64];
        strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", tm);

        char line[MAX_LINE];

        if (msg.type == MSG_MESSAGE_RECV) {
            char formatted[1024] = "";
            char *p = msg.message;

            while (*p) {
                if (!settings.quiet && *p == '@' &&
                    strncmp(p + 1, settings.username, strlen(settings.username)) == 0) {
                    strcat(formatted, "\a" COLOR_RED "@");
                    strcat(formatted, settings.username);
                    strcat(formatted, COLOR_RESET);
                    p += strlen(settings.username) + 1;
                } else {
                    strncat(formatted, p, 1);
                    p++;
                }
            }

            snprintf(line, sizeof(line), "[%s] %s: %s", ts, msg.username, formatted);
        }
        else if (msg.type == MSG_SYSTEM) {
            snprintf(line, sizeof(line), COLOR_GRAY "[SYSTEM] %s" COLOR_RESET, msg.message);
        }
        else if (msg.type == MSG_DISCONNECT) {
            snprintf(line, sizeof(line), COLOR_RED "[DISCONNECT] %s" COLOR_RESET, msg.message);
            add_message(line);
            redraw_ui("");
            settings.running = false;
            break;
        }
        else {
            continue;
        }

        add_message(line);
        redraw_ui("");
    }

    return NULL;
}

/* ───────────── SIGNAL HANDLER ───────────── */
void handle_signal(int sig) {
    settings.running = false;
}

/* ───────────── MAIN ───────────── */
int main(int argc, char *argv[]) {
    settings.running = true;
    settings.server.sin_family = AF_INET;
    settings.server.sin_port = htons(8080);
    inet_pton(AF_INET, "127.0.0.1", &settings.server.sin_addr);

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--tui")) settings.tui = true;
        else if (!strcmp(argv[i], "--quiet")) settings.quiet = true;
        else if (!strcmp(argv[i], "--port") && i + 1 < argc)
            settings.server.sin_port = htons(atoi(argv[++i]));
        else if (!strcmp(argv[i], "--domain") && i + 1 < argc) {
            struct hostent *h = gethostbyname(argv[++i]);
            memcpy(&settings.server.sin_addr, h->h_addr, 4);
        }
    }

    strcpy(settings.username, getenv("USER"));

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    settings.socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    connect(settings.socket_fd, (struct sockaddr *)&settings.server, sizeof(settings.server));

    message_t login = {0};
    login.type = MSG_LOGIN;
    strcpy(login.username, settings.username);
    send_message(&login);

    pthread_t recv;
    pthread_create(&recv, NULL, receive_thread, NULL);

    if (settings.tui) {
        enable_raw_mode();
        hide_cursor();
    }

    char input[1024] = "";
    int len = 0;

    while (settings.running) {
        char c;
        if (read(STDIN_FILENO, &c, 1) != 1) break;

        if (c == '\n') {
            if (len > 0) {
                message_t out = {0};
                out.type = MSG_MESSAGE_SEND;
                strcpy(out.message, input);
                send_message(&out);
                len = 0;
                input[0] = '\0';
            }
        }
        else if (c == 127 && len > 0) {
            input[--len] = '\0';
        }
        else if (c == '\033') {
            char seq[2];
            read(STDIN_FILENO, &seq[0], 1);
            read(STDIN_FILENO, &seq[1], 1);
            if (seq[1] == 'A') scroll_offset++;
            else if (seq[1] == 'B' && scroll_offset > 0) scroll_offset--;
        }
        else if (isprint(c) && len < 1023) {
            input[len++] = c;
            input[len] = '\0';
        }

        if (settings.tui)
            redraw_ui(input);
    }

    if (settings.tui) {
        show_cursor();
        disable_raw_mode();
        clear_screen();
    }

    message_t logout = {0};
    logout.type = MSG_LOGOUT;
    send_message(&logout);

    close(settings.socket_fd);
    pthread_join(recv, NULL);

    return 0;
}
