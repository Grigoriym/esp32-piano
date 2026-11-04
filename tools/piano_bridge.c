/*
 * PC side of the toy's MIDI over WiFi (protocol: main/midi_link.h).
 * Creates the ALSA sequencer port "Chicco Piano" (it stays while this runs,
 * whether the toy is on or not), says HELLO to the toy once a second and
 * passes what comes back to the port, dropping the duplicate copies. A message
 * with a delay waits until its time on the toy's clock, mapped to ours from
 * the fastest packets of the last few seconds (the slowest WiFi trip then only
 * eats into the toy's lead instead of shifting the beat).
 *
 * The same packets come over the USB cable too (main/midi_link.h): the serial
 * port is opened when given, HELLO goes there as well, and whichever copy of a
 * message comes first plays. The toy's log lines on the cable go to
 * build-host/toy-serial.log. The port is held exclusively: stop this to flash.
 *
 * Build: gcc -O2 -Wall -o build-host/piano_bridge tools/piano_bridge.c -lasound -lpthread
 * Usage: piano_bridge [host [serial port]]   (default chicco-piano.local, no cable)
 */
#define _GNU_SOURCE /* ppoll */
#include <alsa/asoundlib.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define PORT           21928
#define BEHIND         2000 /* a sequence number further back than this: the toy restarted */
#define SERIAL_LOG     "build-host/toy-serial.log"
#define SERIAL_LOG_MAX (20 * 1024 * 1024)
#define HEADER         8       /* sequence, send time, delay */
#define PENDING        512     /* delayed messages waiting */
#define SAMPLES        128     /* clock samples kept */
#define WINDOW_US      5000000 /* clock offset: the fastest packet of the last 5 s (clocks drift) */
#define RESET_US       1000000 /* an offset this far off: the toy restarted */
#define SLOW_KEY_US    30000   /* a live key this much slower than the fastest packets is logged */
#define LATE_KEPT      16      /* late messages whose other copy is watched for */
#define STALL_US       20000   /* the bridge's own loop held up this long is logged */

static double now_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

static int64_t now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

/* The toy's clock in ours: offset = min(arrival - send time) over the window. */
static struct {
    int64_t at[SAMPLES], d[SAMPLES];
    int n, next;
    uint32_t last_raw;
    int64_t last_sent; /* the toy's send time, unwrapped */
} clk;

static int64_t clock_offset(int64_t now)
{
    int64_t best = INT64_MAX;
    for (int i = 0; i < clk.n; i++) {
        if (now - clk.at[i] <= WINDOW_US && clk.d[i] < best) best = clk.d[i];
    }
    return best;
}

/* A packet sent at raw (toy us) arrived now: returns its send time in our clock. */
static int64_t clock_map(uint32_t raw, int64_t now)
{
    int64_t sent = clk.n ? clk.last_sent + (int32_t)(raw - clk.last_raw) : raw;
    int64_t off = clock_offset(now);
    if (clk.n && off != INT64_MAX && llabs(now - sent - off) > RESET_US) {
        printf("toy clock jumped: restarted?\n");
        clk.n = 0;
        sent = raw;
    }
    clk.last_raw = raw;
    clk.last_sent = sent;
    clk.at[clk.next] = now;
    clk.d[clk.next] = now - sent;
    clk.next = (clk.next + 1) % SAMPLES;
    clk.n = clk.n < SAMPLES ? clk.n + 1 : SAMPLES;
    return sent + clock_offset(now);
}

typedef struct {
    int64_t at;
    uint32_t order; /* same time: first come first */
    uint8_t msg[3];
    int len;
} pending_t;

static pending_t pending[PENDING];
static int n_pending;
static uint32_t order;

static void play(snd_seq_t *seq, int port, snd_midi_event_t *parser, const uint8_t *msg, int len)
{
    snd_seq_event_t ev;
    snd_midi_event_reset_encode(parser);
    snd_seq_ev_clear(&ev);
    if (snd_midi_event_encode(parser, msg, len, &ev) > 0 && ev.type != SND_SEQ_EVENT_NONE) {
        snd_seq_ev_set_source(&ev, port);
        snd_seq_ev_set_subs(&ev);
        snd_seq_ev_set_direct(&ev);
        snd_seq_event_output_direct(seq, &ev);
    }
}

/* Plays every waiting message that is due, oldest first. */
static void play_due(snd_seq_t *seq, int port, snd_midi_event_t *parser)
{
    int64_t now = now_us();
    while (true) {
        int first = -1;
        for (int i = 0; i < n_pending; i++) {
            if (pending[i].at > now) continue;
            if (first < 0 || pending[i].at < pending[first].at
                || (pending[i].at == pending[first].at && pending[i].order < pending[first].order)) {
                first = i;
            }
        }
        if (first < 0) return;
        play(seq, port, parser, pending[first].msg, pending[first].len);
        pending[first] = pending[--n_pending];
    }
}

/* The toy's address, looked up in a thread of its own: when the mDNS cache entry
 * runs out (~every 2 min) a lookup waits ~250-290 ms on the network, and in the
 * main loop that held up everything, cable included (measured 2026-11-04). */
static pthread_mutex_t found_lock = PTHREAD_MUTEX_INITIALIZER;
static struct sockaddr_in found_addr;
static bool found_any;

static bool resolve(const char *host, struct sockaddr_in *out);

static void *resolve_thread(void *arg)
{
    const char *host = arg;
    while (true) {
        struct sockaddr_in a;
        if (resolve(host, &a)) { /* each time: the toy's IP may change */
            pthread_mutex_lock(&found_lock);
            found_addr = a;
            found_any = true;
            pthread_mutex_unlock(&found_lock);
        }
        sleep(1);
    }
    return NULL;
}

static bool resolve(const char *host, struct sockaddr_in *out)
{
    struct addrinfo hints = {.ai_family = AF_INET, .ai_socktype = SOCK_DGRAM};
    struct addrinfo *res;
    if (getaddrinfo(host, NULL, &hints, &res) != 0) {
        return false;
    }
    *out = *(struct sockaddr_in *)res->ai_addr;
    out->sin_port = htons(PORT);
    freeaddrinfo(res);
    return true;
}

/* Sequence numbers seen: a copy (the second WiFi copy, or the WiFi one after
 * the cable's) is dropped, however late it comes. */
static uint8_t seen[65536 / 8];
static uint16_t highest;
static bool any_seen;

static bool seen_bit(uint16_t s)
{
    return seen[s / 8] & (1u << (s % 8));
}

static void mark(uint16_t s, bool on)
{
    if (on) {
        seen[s / 8] |= (uint8_t)(1u << (s % 8));
    } else {
        seen[s / 8] &= (uint8_t)~(1u << (s % 8));
    }
}

static bool is_copy(uint16_t s)
{
    int diff = (int16_t)(s - highest);
    if (!any_seen || diff < -BEHIND) { /* first, or the toy restarted */
        memset(seen, 0, sizeof(seen));
        any_seen = true;
        highest = s;
        mark(s, true);
        return false;
    }
    if (diff > 0) {
        for (uint16_t i = highest + 1; i != (uint16_t)(s + 1); i++) mark(i, false);
        highest = s;
        mark(s, true);
        return false;
    }
    bool copy = seen_bit(s);
    mark(s, true);
    return copy;
}

/* The cable, raw at 921600. Opening raises DTR and RTS together, which doesn't
 * reset the board; dropping RTS before DTR keeps EN up (dropping DTR first
 * would reset it, as pyserial does). */
static int serial_open(const char *path)
{
    int fd = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) return -1;
    int bits = TIOCM_RTS;
    ioctl(fd, TIOCMBIC, &bits);
    bits = TIOCM_DTR;
    ioctl(fd, TIOCMBIC, &bits);
    if (ioctl(fd, TIOCEXCL) != 0) { /* esptool then says the port is busy */
        close(fd);
        return -1;
    }
    struct termios tio;
    tcgetattr(fd, &tio);
    cfmakeraw(&tio);
    tio.c_cflag &= ~(HUPCL | CRTSCTS);
    tio.c_cflag |= CLOCAL | CREAD;
    cfsetispeed(&tio, B921600);
    cfsetospeed(&tio, B921600);
    tcsetattr(fd, TCSANOW, &tio);
    return fd;
}

/* Splits the cable's bytes into frames (0xF5, length, packet, sum) and log text. */
typedef struct {
    uint8_t buf[16];
    int n, want; /* want 0: in text */
    char line[256];
    int line_n;
    FILE *log;
} serial_parser_t;

/* Feeds one byte; returns the packet length when a frame completed (in p->buf). */
static int serial_feed(serial_parser_t *p, uint8_t c)
{
    if (p->want == 0) {
        if (c == 0xf5) {
            p->want = -1; /* length next */
            return 0;
        }
        if (c == '\n' || p->line_n == (int)sizeof(p->line) - 1) {
            if (p->log) {
                fprintf(p->log, "%.*s\n", p->line_n, p->line);
                fflush(p->log);
                if (ftell(p->log) > SERIAL_LOG_MAX) rewind(p->log);
            }
            p->line_n = 0;
        } else if (c != '\r') {
            p->line[p->line_n++] = (char)c;
        }
        return 0;
    }
    if (p->want == -1) {
        if (c < HEADER + 1 || c > HEADER + 3) { /* not a frame: back to text */
            p->want = 0;
            return 0;
        }
        p->want = c + 1; /* packet + sum */
        p->n = 0;
        return 0;
    }
    p->buf[p->n++] = c;
    if (p->n < p->want) return 0;
    int len = p->want - 1;
    p->want = 0;
    uint8_t sum = 0;
    for (int i = 0; i < len; i++) sum += p->buf[i];
    return sum == p->buf[len] ? len : 0;
}

typedef struct {
    uint16_t seq;
    int64_t at; /* 0: free */
} late_t;
static late_t late_seq[LATE_KEPT];
static int late_next;

int main(int argc, char **argv)
{
    const char *host = argc > 1 ? argv[1] : "chicco-piano.local";
    const char *serial_path = argc > 2 ? argv[2] : NULL;
    int ser = -1;
    double next_serial_try = 0;
    serial_parser_t sp = {.log = fopen(SERIAL_LOG, "w")};
    setvbuf(stdout, NULL, _IOLBF, 0);

    snd_seq_t *seq;
    if (snd_seq_open(&seq, "default", SND_SEQ_OPEN_OUTPUT, 0) < 0) {
        fprintf(stderr, "can't open the ALSA sequencer\n");
        return 1;
    }
    snd_seq_set_client_name(seq, "Chicco Piano");
    int port = snd_seq_create_simple_port(seq, "Chicco Piano WiFi",
                                          SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ,
                                          SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);
    snd_midi_event_t *parser;
    snd_midi_event_new(16, &parser);

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in any = {.sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_ANY)};
    bind(sock, (struct sockaddr *)&any, sizeof(any)); /* any free port: the toy replies to it */

    struct sockaddr_in toy;
    bool have_toy = false;
    double next_hello = 0, last_data = -10;
    int last_via = 0;
    int64_t polled = now_us(); /* when the last wait ended: the loop's own work is timed from it */

    pthread_t resolver;
    pthread_create(&resolver, NULL, resolve_thread, (void *)host);
    printf("bridge up, looking for %s\n", host);
    while (true) {
        double t = now_s();
        if (t >= next_hello) {
            next_hello = t + 1.0;
            pthread_mutex_lock(&found_lock);
            if (found_any) {
                toy = found_addr;
                have_toy = true;
            }
            pthread_mutex_unlock(&found_lock);
            if (have_toy) {
                sendto(sock, "HELLO", 5, 0, (struct sockaddr *)&toy, sizeof(toy));
            }
            if (serial_path && ser < 0 && t >= next_serial_try) {
                next_serial_try = t + 2.0;
                ser = serial_open(serial_path);
                if (ser >= 0) printf("cable: %s open\n", serial_path);
            }
            if (ser >= 0 && write(ser, "HELLO\n", 6) != 6) {
                printf("cable: gone\n");
                close(ser);
                ser = -1;
            }
        }

        /* Wake for the next due message, or the next HELLO. */
        int64_t wait = (int64_t)((next_hello - t) * 1e6);
        int64_t now = now_us();
        for (int i = 0; i < n_pending; i++) {
            if (pending[i].at - now < wait) wait = pending[i].at - now;
        }
        if (wait < 0) wait = 0;
        struct timespec timeout = {.tv_sec = wait / 1000000, .tv_nsec = (wait % 1000000) * 1000};
        struct pollfd pfd[2] = {{.fd = sock, .events = POLLIN}, {.fd = ser, .events = POLLIN}};
        int64_t busy = now_us() - polled; /* the whole pass since the last wait */
        if (busy > STALL_US) {            /* the bridge itself held everything up */
            printf("bridge busy %lld ms\n", (long long)busy / 1000);
        }
        int64_t wake_by = now_us() + wait;
        int ready = ppoll(pfd, ser >= 0 ? 2 : 1, &timeout, NULL);
        polled = now_us();
        if (polled - wake_by > STALL_US) { /* the PC didn't run the bridge in time */
            printf("bridge woke %lld ms late\n", (long long)(polled - wake_by) / 1000);
        }
        play_due(seq, port, parser);
        if (ready <= 0) {
            continue;
        }
        /* Packets that came in now: from the cable (any number) and one from WiFi. */
        uint8_t pkts[64][16];
        int lens[64], n_pkts = 0, n_cable = 0;
        if (ser >= 0 && (pfd[1].revents & (POLLERR | POLLHUP))) {
            printf("cable: gone\n");
            close(ser);
            ser = -1;
        } else if (ser >= 0 && (pfd[1].revents & POLLIN)) {
            uint8_t raw[512];
            ssize_t got = read(ser, raw, sizeof(raw));
            for (ssize_t i = 0; i < got; i++) {
                int len = serial_feed(&sp, raw[i]);
                if (len && n_pkts < 63) {
                    memcpy(pkts[n_pkts], sp.buf, len);
                    lens[n_pkts++] = len;
                }
            }
        }
        n_cable = n_pkts;
        if (pfd[0].revents & POLLIN) {
            struct sockaddr_in from;
            socklen_t from_len = sizeof(from);
            ssize_t got =
                recvfrom(sock, pkts[n_pkts], sizeof(pkts[0]), 0, (struct sockaddr *)&from, &from_len);
            if (got >= HEADER + 1) {
                lens[n_pkts++] = (int)got;
                if (now_s() - last_data > 5.0) { /* first data, or after a quiet spell */
                    printf("toy at %s\n", inet_ntoa(from.sin_addr));
                }
                last_data = now_s();
            }
        }
        for (int k = 0; k < n_pkts; k++) {
            const uint8_t *buf = pkts[k];
            ssize_t n = lens[k];
            int64_t arrived = now_us();
            /* Every copy is a clock sample. */
            int64_t sent = clock_map((uint32_t)buf[2] << 24 | buf[3] << 16 | buf[4] << 8 | buf[5], arrived);
            uint16_t sq = (uint16_t)(buf[0] << 8 | buf[1]);
            if (is_copy(sq)) {
                /* A late one's other copy: together = the PC held both, much later = the cable was fine */
                for (int i = 0; i < LATE_KEPT; i++) {
                    if (late_seq[i].at && late_seq[i].seq == sq) {
                        printf("  its other copy came %lld ms after (seq %u)\n",
                               (long long)(arrived - late_seq[i].at) / 1000, sq);
                        late_seq[i].at = 0;
                    }
                }
                continue;
            }
            int via = k < n_cable ? 1 : 2; /* which way this one came first */
            if (via != last_via) {
                printf("playing what comes over %s\n", via == 1 ? "the cable" : "WiFi");
                last_via = via;
            }

            int delay = (buf[6] << 8 | buf[7]) * 100;
            int len = (int)n - HEADER;
            const uint8_t *msg = &buf[HEADER];
            bool note_on = len == 3 && (msg[0] & 0xf0) == 0x90 && msg[2];
            if (delay == 0 || len > 3) {
                /* A live key: how much slower than the fastest recent packet it came. */
                if (note_on && arrived - sent > SLOW_KEY_US) {
                    printf("slow key: %lld ms (ch %d note %d)\n", (long long)(arrived - sent) / 1000,
                           (msg[0] & 15) + 1, msg[1]);
                }
                play(seq, port, parser, msg, len);
                continue;
            }
            int64_t at = sent + delay;
            if (at <= arrived) { /* the head start wasn't enough: plays now, late */
                printf("late: %lld ms after its time (ch %d %s %d, over %s)\n",
                       (long long)(arrived - at) / 1000, (msg[0] & 15) + 1, note_on ? "note" : "msg",
                       len > 1 ? msg[1] : 0, via == 1 ? "the cable" : "WiFi");
                late_seq[late_next] = (late_t){sq, arrived};
                late_next = (late_next + 1) % LATE_KEPT;
            }
            if (at <= arrived || n_pending == PENDING) {
                play(seq, port, parser, msg, len);
            } else {
                pending_t *p = &pending[n_pending++];
                p->at = at;
                p->order = order++;
                memcpy(p->msg, msg, len);
                p->len = len;
            }
        }
    }
}
