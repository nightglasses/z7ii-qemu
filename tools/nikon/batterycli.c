/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "battery.h"
#include "batterypeer.h"

static volatile sig_atomic_t interrupted;

typedef struct SearchRun {
    const uint8_t *challenge;
    gint stop;
} SearchRun;

typedef struct SearchJob {
    SearchRun *run;
    uint64_t start, count;
    NikonBatterySearchStatus status;
    NikonBatterySearchResult result;
} SearchJob;

static void interrupt_search(int signo)
{
    (void)signo;
    interrupted = 1;
}

static bool cancelled(void *opaque)
{
    SearchRun *run = opaque;

    return interrupted || (run && g_atomic_int_get(&run->stop));
}

static gpointer search_worker(gpointer opaque)
{
    SearchJob *job = opaque;

    job->status = nikon_battery_search(job->run->challenge,
                                      job->start, job->count,
                                      cancelled, job->run, &job->result);
    if (job->status == NIKON_BATTERY_FOUND ||
        job->status == NIKON_BATTERY_VERIFY_FAILED) {
        g_atomic_int_set(&job->run->stop, 1);
    }
    return NULL;
}

/* Decimal only: strtoull would also accept whitespace, signs and prefixes. */
static bool decimal(const char *text, uint64_t maximum, uint64_t *out)
{
    uint64_t value = 0;

    if (!text || !*text) {
        return false;
    }
    for (const char *p = text; *p; p++) {
        if (*p < '0' || *p > '9' || value > maximum / 10 ||
            (value == maximum / 10 && (unsigned)(*p - '0') > maximum % 10)) {
            return false;
        }
        value = value * 10 + (*p - '0');
    }
    *out = value;
    return true;
}

static void print_hex(const uint8_t *bytes, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        printf("%02x", bytes[i]);
    }
    putchar('\n');
}

static int finish_output(int status)
{
    if (fflush(stdout) || ferror(stdout)) {
        fprintf(stderr, "cannot write result\n");
        return 3;
    }
    return status;
}

static int usage(const char *program)
{
    fprintf(stderr,
            "usage: %s search CHALLENGE_HEX START COUNT THREADS\n"
            "       %s reply CHALLENGE_HEX REPRESENTATIVE\n"
            "       %s challenge SEED\n"
            "       %s selftest\n"
            "       %s peer SOCKET SEED PROFILE_HEX\n"
            "integers are decimal; challenge is exactly 17 canonical wire bytes\n"
            "peer profile is exactly 26 bytes (52 hex digits); see batterypeer.md\n",
            program, program, program, program, program);
    return 2;
}

static int search(const uint8_t challenge[17], uint64_t start, uint64_t count,
                  unsigned thread_count)
{
    SearchRun run = { .challenge = challenge };
    SearchJob *jobs = g_try_new0(SearchJob, thread_count);
    GThread **threads = g_try_new0(GThread *, thread_count);
    unsigned created = 0;
    uint64_t tried = 0;
    uint32_t representative = UINT32_MAX;
    bool failed = false;
    int64_t begin = g_get_monotonic_time();

    if (!jobs || !threads) {
        fprintf(stderr, "search allocation failed\n");
        g_free(jobs);
        g_free(threads);
        return 3;
    }
    /* Avoid empty partitions and keep the count products within 64 bits. */
    thread_count = MIN(thread_count, count);
    for (unsigned i = 0; i < thread_count; i++) {
        GError *error = NULL;
        uint64_t offset = count * i / thread_count;

        jobs[i] = (SearchJob) {
            .run = &run,
            .start = start + offset,
            .count = count * (i + 1) / thread_count - offset,
        };
        threads[i] = g_thread_try_new("battery-search", search_worker,
                                      &jobs[i], &error);
        if (!threads[i]) {
            fprintf(stderr, "cannot create search thread: %s\n",
                    error->message);
            g_clear_error(&error);
            failed = true;
            g_atomic_int_set(&run.stop, 1);
            break;
        }
        created++;
    }
    for (unsigned i = 0; i < created; i++) {
        g_thread_join(threads[i]);
        tried += jobs[i].result.tried;
        if (jobs[i].status == NIKON_BATTERY_FOUND) {
            representative = MIN(representative, jobs[i].result.representative);
        } else if (jobs[i].status != NIKON_BATTERY_NOT_FOUND &&
                   jobs[i].status != NIKON_BATTERY_CANCELLED) {
            failed = true;
        }
    }
    g_free(threads);
    g_free(jobs);
    if (interrupted) {
        fprintf(stderr, "search cancelled\n");
        return 130;
    }
    if (failed) {
        fprintf(stderr, "search failed; no verified result\n");
        return 3;
    }
    double seconds = (g_get_monotonic_time() - begin) / 1000000.0;

    printf("{\"status\":\"%s\",\"backend\":\"%s\",\"threads\":%u,"
           "\"start\":%" PRIu64 ",\"count\":%" PRIu64 ",\"tried\":%" PRIu64 ","
           "\"seconds\":%.6f,\"representative\":",
           representative == UINT32_MAX ? "not-found" : "found",
           nikon_battery_backend(), thread_count, start, count, tried, seconds);
    if (representative == UINT32_MAX) {
        puts("null}");
        return finish_output(1);
    }
    printf("%" PRIu32 "}\n", representative);
    return finish_output(0);
}

int main(int argc, char **argv)
{
    uint8_t challenge[17], reply[29];
    uint64_t start, count, threads, seed;
    struct sigaction action = { .sa_handler = interrupt_search };

    sigemptyset(&action.sa_mask);
    if (sigaction(SIGINT, &action, NULL) ||
        sigaction(SIGTERM, &action, NULL)) {
        fprintf(stderr, "cannot install cancellation handlers\n");
        return 3;
    }
    if (argc == 2 && !strcmp(argv[1], "selftest")) {
        if (!nikon_battery_selftest()) {
            fprintf(stderr, "predefined vector verification failed\n");
            return 3;
        }
        if (interrupted) {
            fprintf(stderr, "selftest cancelled\n");
            return 130;
        }
        printf("{\"status\":\"pass\",\"backend\":\"%s\",\"vectors\":8}\n",
               nikon_battery_backend());
        return finish_output(0);
    }
    if (argc == 5 && !strcmp(argv[1], "peer") && *argv[2] &&
        decimal(argv[3], UINT32_MAX, &seed)) {
        NikonBatteryProfile profile;
        NikonBatteryPeer peer;

        if (!nikon_battery_parse_profile(argv[4], &profile)) {
            return usage(argv[0]);
        }
        NikonBatterySearchStatus status = nikon_battery_peer_init(
            &peer, seed, &profile, cancelled, NULL);

        if (status == NIKON_BATTERY_CANCELLED) {
            fprintf(stderr, "peer precomputation cancelled\n");
            return 130;
        }
        if (status != NIKON_BATTERY_FOUND) {
            fprintf(stderr, "peer challenge/reply precomputation failed\n");
            return 3;
        }
        int result = nikon_battery_peer_run(&peer, argv[2], cancelled, NULL);

        if (result) {
            return result;
        }
        printf("{\"status\":\"eof\",\"phase\":\"%s\","
               "\"scope\":\"finite-battery-peer\"}\n",
               nikon_battery_peer_phase(&peer));
        return finish_output(0);
    }
    if (argc == 3 && !strcmp(argv[1], "challenge") &&
        decimal(argv[2], UINT32_MAX, &seed)) {
        if (!nikon_battery_challenge(seed, challenge)) {
            fprintf(stderr, "challenge has no finite field coordinate\n");
            return 3;
        }
        if (interrupted) {
            fprintf(stderr, "challenge cancelled\n");
            return 130;
        }
        print_hex(challenge, sizeof(challenge));
        return finish_output(0);
    }
    if (argc == 4 && !strcmp(argv[1], "reply") &&
        nikon_battery_parse_challenge(argv[2], challenge) &&
        decimal(argv[3], NIKON_BATTERY_SEED_LIMIT - 1, &seed)) {
        NikonBatterySearchStatus status = nikon_battery_first_auth(
            challenge, seed, cancelled, NULL, reply);

        if (status == NIKON_BATTERY_CANCELLED) {
            fprintf(stderr, "reply cancelled\n");
            return 130;
        }
        if (status != NIKON_BATTERY_FOUND) {
            fprintf(stderr,
                    "representative does not verify against all 17 bytes\n");
            return 3;
        }
        print_hex(reply, sizeof(reply));
        return finish_output(0);
    }
    if (argc == 6 && !strcmp(argv[1], "search") &&
        nikon_battery_parse_challenge(argv[2], challenge) &&
        decimal(argv[3], NIKON_BATTERY_SEED_LIMIT - 1, &start) &&
        decimal(argv[4], NIKON_BATTERY_SEED_LIMIT, &count) && count &&
        count <= NIKON_BATTERY_SEED_LIMIT - start &&
        decimal(argv[5], 256, &threads) && threads) {
        return search(challenge, start, count, threads);
    }
    return usage(argv[0]);
}
