/*
 * PC side of the toy's MIDI over WiFi (protocol: main/midi_link.h).
 * Creates the ALSA sequencer port "Chicco Piano" (it stays while this runs,
 * whether the toy is on or not), says HELLO to the toy once a second and
 * passes what comes back to the port, dropping the duplicate copies. A message
 * with a delay waits until its time on the toy's clock, mapped to ours from
 * the fastest packets of the last few seconds (the slowest WiFi trip then only
 * eats into the toy's lead instead of shifting the beat).
 *
 * Build: gcc -O2 -Wall -o build-host/piano_bridge tools/piano_bridge.c -lasound
 * Usage: piano_bridge [host]   (default chicco-piano.local)
 */
#define _GNU_SOURCE /* ppoll */
#include <alsa/asoundlib.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define PORT        21928
#define RECENT      16      /* sequence numbers remembered for dropping copies */
#define HEADER      8       /* sequence, send time, delay */
#define PENDING     512     /* delayed messages waiting */
#define SAMPLES     128     /* clock samples kept */
#define WINDOW_US   5000000 /* clock offset: the fastest packet of the last 5 s (clocks drift) */
#define RESET_US    1000000 /* an offset this far off: the toy restarted */
#define SLOW_KEY_US 30000   /* a live key this much slower than the fastest packets is logged */

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

int main(int argc, char **argv)
{
    const char *host = argc > 1 ? argv[1] : "chicco-piano.local";
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
    uint16_t recent[RECENT];
    int n_recent = 0, recent_at = 0;

    printf("bridge up, looking for %s\n", host);
    while (true) {
        double t = now_s();
        if (t >= next_hello) {
            next_hello = t + 1.0;
            struct sockaddr_in found;
            if (resolve(host, &found)) { /* each time: the toy's IP may change */
                toy = found;
                have_toy = true;
            }
            if (have_toy) {
                sendto(sock, "HELLO", 5, 0, (struct sockaddr *)&toy, sizeof(toy));
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
        struct pollfd pfd = {.fd = sock, .events = POLLIN};
        int ready = ppoll(&pfd, 1, &timeout, NULL);
        play_due(seq, port, parser);
        if (ready <= 0) {
            continue;
        }
        uint8_t buf[16];
        struct sockaddr_in from;
        socklen_t from_len = sizeof(from);
        ssize_t n = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr *)&from, &from_len);
        if (n < HEADER + 1) {
            continue;
        }
        int64_t arrived = now_us();
        if (now_s() - last_data > 5.0) { /* first data, or after a quiet spell */
            printf("toy at %s\n", inet_ntoa(from.sin_addr));
        }
        last_data = now_s();
        /* Every copy is a clock sample. */
        int64_t sent = clock_map((uint32_t)buf[2] << 24 | buf[3] << 16 | buf[4] << 8 | buf[5], arrived);

        uint16_t s = (uint16_t)(buf[0] << 8 | buf[1]);
        bool dup = false;
        for (int i = 0; i < n_recent; i++) {
            dup |= recent[i] == s;
        }
        if (dup) {
            continue;
        }
        recent[recent_at] = s;
        recent_at = (recent_at + 1) % RECENT;
        n_recent = n_recent < RECENT ? n_recent + 1 : RECENT;

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
            printf("late: %lld ms after its time (ch %d %s %d)\n", (long long)(arrived - at) / 1000,
                   (msg[0] & 15) + 1, note_on ? "note" : "msg", len > 1 ? msg[1] : 0);
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
