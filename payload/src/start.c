/* OnionStation5: the payload's start code, run before Tor's own main().
 *
 * It makes the data folder, writes a default torrc the first time, applies
 * optional environment settings, checks the clock, then starts Tor with that
 * torrc. On the PS5 it also shows notifications when Tor starts and when it
 * has bootstrapped.
 *
 * Copyright (c) 2026, ThatRetiredDude. BSD-3-Clause, see LICENSE. */

#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#define __BSD_VISIBLE 1

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "start.h"

#ifndef ONIONSTATION_TOR_VERSION
#define ONIONSTATION_TOR_VERSION "unknown"
#endif
#ifndef ONIONSTATION_BUILD_TIME
#define ONIONSTATION_BUILD_TIME 0
#endif

/* Where everything lives. /data survives reboots and is writable from a
 * payload; the title (build step 3) moves this to /download0. */
#define DEFAULT_DIR "/data/onionstation"

int __real_main(int argc, char **argv);

static char base_dir[256];
static char tor_dir[300];
static char torrc_path[300];
static char log_path[300];
static char defaults_path[300];
static long log_start;

static void say(const char *fmt, const char *arg)
{
    fprintf(stderr, "onionstation: ");
    fprintf(stderr, fmt, arg);
    fputc('\n', stderr);
}

static int make_dir(const char *path)
{
    if (mkdir(path, 0700) == 0 || errno == EEXIST)
        return chmod(path, 0700);
    return -1;
}

/* The default settings: a Tor client for this console and the home network.
 * The SOCKS port answers LAN addresses only; the HTTP proxy port (for the PS5
 * browser) and the control port answer this console only. */
static int write_default_torrc(void)
{
    FILE *f = fopen(torrc_path, "wx");
    if (!f)
        return errno == EEXIST ? 0 : -1;
    fprintf(f,
            "## OnionStation5 Tor settings. Edit to change them; delete this file to get\n"
            "## the defaults back on the next start.\n"
            "DataDirectory %s\n"
            "Log notice file %s\n"
            "Log notice stderr\n"
            "\n"
            "## SOCKS for devices on the home network (e.g. 192.168.1.20:9050).\n"
            "SocksPort 0.0.0.0:9050\n"
            "SocksPolicy accept 127.0.0.0/8\n"
            "SocksPolicy accept 10.0.0.0/8\n"
            "SocksPolicy accept 172.16.0.0/12\n"
            "SocksPolicy accept 192.168.0.0/16\n"
            "SocksPolicy reject *\n"
            "\n"
            "## HTTP CONNECT proxy for the PS5 browser (Settings > Network > proxy\n"
            "## server 127.0.0.1, port 9080). This console only.\n"
            "HTTPTunnelPort 127.0.0.1:9080\n"
            "\n"
            "## For the OnionStation5 app. This console only.\n"
            "ControlPort 127.0.0.1:9051\n"
            "CookieAuthentication 1\n"
            "\n"
            "AvoidDiskWrites 1\n",
            tor_dir, log_path);
    return fclose(f);
}

/* Optional KEY=VALUE lines in <dir>/env, read before Tor starts. This is how
 * the console tests pick libevent's backend without a rebuild, e.g.
 * EVENT_NOKQUEUE=1 or EVENT_NOPOLL=1 (docs/PAYLOAD.md). */
static void load_env(void)
{
    char path[300], line[512];
    snprintf(path, sizeof path, "%s/env", base_dir);
    FILE *f = fopen(path, "r");
    if (!f)
        return;
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = '\0';
        char *eq = strchr(line, '=');
        if (line[0] == '#' || !eq)
            continue;
        *eq = '\0';
        setenv(line, eq + 1, 1);
        say("env %s set", line);
    }
    fclose(f);
}

/* Tor refuses directory documents that look out of date, so a console clock
 * that is far off stops it from bootstrapping. Anything before this build
 * was made is certainly wrong. */
static void check_clock(void)
{
    time_t now = time(NULL);
    if (ONIONSTATION_BUILD_TIME && now < (time_t)ONIONSTATION_BUILD_TIME) {
        say("%s", "the console clock is earlier than this build; set the date and time or Tor won't connect");
        os_notify("OnionStation5: the clock looks wrong. Set the date and time, or Tor can't connect.");
    }
}

/* Watches Tor's log for the bootstrap lines and tells the user once each. */
static void *watch_log(void *unused)
{
    (void)unused;
    long offset = log_start;
    int reported_50 = 0;
    for (;;) {
        sleep(2);
        FILE *f = fopen(log_path, "r");
        if (!f)
            continue;
        if (fseek(f, offset, SEEK_SET) != 0)
            offset = 0;
        char line[1024];
        while (fgets(line, sizeof line, f)) {
            if (strstr(line, "Bootstrapped 100%")) {
                os_notify("OnionStation5: Tor is connected.");
                fclose(f);
                return NULL;
            }
            if (!reported_50 && strstr(line, "Bootstrapped 50%")) {
                reported_50 = 1;
                os_notify("OnionStation5: Tor is halfway connected...");
            }
        }
        offset = ftell(f);
        fclose(f);
    }
}

int __wrap_main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    const char *dir = getenv("ONIONSTATION_DIR");
    snprintf(base_dir, sizeof base_dir, "%s", dir && *dir ? dir : DEFAULT_DIR);
    snprintf(tor_dir, sizeof tor_dir, "%s/tor", base_dir);
    snprintf(torrc_path, sizeof torrc_path, "%s/torrc", base_dir);
    snprintf(log_path, sizeof log_path, "%s/tor.log", base_dir);
    snprintf(defaults_path, sizeof defaults_path, "%s/torrc-defaults", base_dir);

    if (make_dir(base_dir) != 0 || make_dir(tor_dir) != 0) {
        say("can't make %s", tor_dir);
        os_notify("OnionStation5: can't create its data folder.");
        return 1;
    }
    if (write_default_torrc() != 0) {
        say("can't write %s", torrc_path);
        os_notify("OnionStation5: can't write its settings file.");
        return 1;
    }
    /* An empty defaults file, so no system-wide torrc-defaults is read. */
    FILE *defaults = fopen(defaults_path, "a");
    if (defaults)
        fclose(defaults);
    /* Only this run's log lines count for the notifications. */
    struct stat st;
    log_start = stat(log_path, &st) == 0 ? (long)st.st_size : 0;
    load_env();
    check_clock();
    os_platform_init();

    say("starting Tor %s", ONIONSTATION_TOR_VERSION);
    os_notify("OnionStation5: starting Tor " ONIONSTATION_TOR_VERSION);
    pthread_t watcher;
    if (pthread_create(&watcher, NULL, watch_log, NULL) == 0)
        pthread_detach(watcher);

    /* Tor's own command line, whatever the loader passed. */
    char *tor_argv[] = {"tor", "-f", torrc_path, "--defaults-torrc", defaults_path, NULL};
    int rc = __real_main(5, tor_argv);
    if (rc != 0)
        os_notify("OnionStation5: Tor stopped with an error. See tor.log.");
    return rc;
}

#ifndef ONIONSTATION_PS5
void os_notify(const char *message)
{
    say("notify: %s", message);
}

void os_platform_init(void)
{
}
#endif
