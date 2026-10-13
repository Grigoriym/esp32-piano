/*
 * PC side of the toy's MIDI over WiFi (protocol: main/midi_link.h).
 * Creates the ALSA sequencer port "Chicco Piano" (it stays while this runs,
 * whether the toy is on or not), says HELLO to the toy once a second and
 * passes what comes back to the port, dropping the duplicate copies.
 *
 * Build: gcc -O2 -Wall -o build-host/piano_bridge tools/piano_bridge.c -lasound
 * Usage: piano_bridge [host]   (default chicco-piano.local)
 */
#include <alsa/asoundlib.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define PORT   21928
#define RECENT 16 /* sequence numbers remembered for dropping copies */

static double now_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
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

        struct pollfd pfd = {.fd = sock, .events = POLLIN};
        if (poll(&pfd, 1, 200) <= 0) {
            continue;
        }
        uint8_t buf[16];
        struct sockaddr_in from;
        socklen_t from_len = sizeof(from);
        ssize_t n = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr *)&from, &from_len);
        if (n < 3) {
            continue;
        }
        if (now_s() - last_data > 5.0) { /* first data, or after a quiet spell */
            printf("toy at %s\n", inet_ntoa(from.sin_addr));
        }
        last_data = now_s();

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

        snd_seq_event_t ev;
        snd_midi_event_reset_encode(parser);
        snd_seq_ev_clear(&ev);
        if (snd_midi_event_encode(parser, &buf[2], n - 2, &ev) > 0 && ev.type != SND_SEQ_EVENT_NONE) {
            snd_seq_ev_set_source(&ev, port);
            snd_seq_ev_set_subs(&ev);
            snd_seq_ev_set_direct(&ev);
            snd_seq_event_output_direct(seq, &ev);
        }
    }
}
