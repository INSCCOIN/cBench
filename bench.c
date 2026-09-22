#define _GNU_SOURCE
#include "fb.h"
#include <math.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define NTEST 6

static const char *names[NTEST] = {
    "int    add/mul  (higher better)",
    "float  mul/sin  (higher better)",
    "memcpy MB/s",
    "walk   ns/hit   (lower better)",
    "fb fill frames/s",
    "fb px   Mpx/s",
};

static double score[NTEST];
static int have[NTEST];
static int sel, running, want_quit;
static char msg[96];

static uint16_t Cbg, Cfg, Cbar, Cacc, Cdim, Csel;
static struct termios oldt;
static int rawon;

static double now_s(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}

static void colors(void)
{
    Cbg = rgb565(8, 12, 16);
    Cfg = rgb565(210, 220, 230);
    Cbar = rgb565(20, 32, 44);
    Cacc = rgb565(70, 180, 140);
    Cdim = rgb565(100, 120, 130);
    Csel = rgb565(24, 48, 40);
}

static void io_open(void)
{
    struct termios t;
    tcgetattr(0, &oldt);
    t = oldt;
    t.c_lflag &= ~(ICANON | ECHO);
    t.c_cc[VMIN] = 0;
    t.c_cc[VTIME] = 0;
    tcsetattr(0, TCSANOW, &t);
    rawon = 1;
}

static void io_close(void)
{
    if (rawon)
        tcsetattr(0, TCSANOW, &oldt);
}

static double bench_int(void)
{
    volatile unsigned a = 1, b = 3, c = 7;
    unsigned long n = 0;
    double t0 = now_s(), t;
    do {
        int i;
        for (i = 0; i < 10000; i++) {
            a = a * 1664525u + 1013904223u;
            b = b + a * 3u + c;
            c = (c ^ b) + a;
        }
        n += 10000;
        t = now_s() - t0;
    } while (t < 0.6);
    (void)a;
    (void)b;
    (void)c;
    return n / t / 1e6; /* Mops */
}

static double bench_float(void)
{
    volatile double x = 1.0001, y = 0.5;
    unsigned long n = 0;
    double t0 = now_s(), t;
    do {
        int i;
        for (i = 0; i < 4000; i++) {
            x = x * 1.0000001 + 0.000001;
            y = y + sin(x) * 0.0001;
        }
        n += 4000;
        t = now_s() - t0;
    } while (t < 0.6);
    (void)y;
    return n / t / 1e6;
}

static double bench_memcpy(void)
{
    const size_t n = 2 * 1024 * 1024;
    char *a = malloc(n), *b = malloc(n);
    double t0, t, mb = 0;
    if (!a || !b) {
        free(a);
        free(b);
        return 0;
    }
    memset(a, 0x5a, n);
    t0 = now_s();
    do {
        memcpy(b, a, n);
        mb += n / (1024.0 * 1024.0);
        t = now_s() - t0;
    } while (t < 0.6);
    free(a);
    free(b);
    return mb / t;
}

static double bench_walk(void)
{
    const int n = 256 * 1024;
    int *p = malloc((size_t)n * sizeof *p);
    int i, idx = 0;
    unsigned long hits = 0;
    double t0, t;
    if (!p)
        return 0;
    for (i = 0; i < n; i++)
        p[i] = (i * 1103515245u + 12345) % n;
    t0 = now_s();
    do {
        for (i = 0; i < 20000; i++) {
            idx = p[idx];
            hits++;
        }
        t = now_s() - t0;
    } while (t < 0.6);
    free(p);
    return (t / (double)hits) * 1e9;
}

static double bench_fill(void)
{
    int frames = 0;
    double t0 = now_s(), t;
    uint16_t c = rgb565(20, 40, 30);
    do {
        fb_clear(c);
        fb_flip();
        frames++;
        t = now_s() - t0;
        c = (uint16_t)(c + 17);
    } while (t < 0.6);
    return frames / t;
}

static double bench_px(void)
{
    unsigned long n = 0;
    double t0 = now_s(), t;
    int x, y;
    do {
        for (y = 0; y < 80; y++)
            for (x = 0; x < 160; x++)
                px(x + 40, y + 40, rgb565(x, y, 80));
        n += 80ul * 160ul;
        t = now_s() - t0;
    } while (t < 0.5);
    fb_flip();
    return n / t / 1e6;
}

static double (*fn[NTEST])(void) = {
    bench_int, bench_float, bench_memcpy, bench_walk, bench_fill, bench_px
};

static void log_line(const char *s)
{
    char p[512];
    FILE *f;
    const char *h = getenv("HOME");
    struct passwd *pw;
    if (!h || !h[0]) {
        pw = getpwuid(getuid());
        h = pw ? pw->pw_dir : ".";
    }
    snprintf(p, sizeof p, "%s/cbench.log", h);
    f = fopen(p, "a");
    if (!f)
        return;
    fprintf(f, "%s\n", s);
    fclose(f);
}

static void run_one(int i)
{
    char line[160];
    time_t tt = time(NULL);
    struct tm *tm = localtime(&tt);
    snprintf(msg, sizeof msg, "running %s", names[i]);
    running = 1;
    /* paint once so user sees it */
    {
        extern void draw(void);
        draw();
    }
    score[i] = fn[i]();
    have[i] = 1;
    running = 0;
    snprintf(msg, sizeof msg, "done");
    snprintf(line, sizeof line, "%04d-%02d-%02d %02d:%02d  %s  %.3f",
             tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
             tm->tm_hour, tm->tm_min, names[i], score[i]);
    log_line(line);
}

static void run_all(void)
{
    int i;
    for (i = 0; i < NTEST; i++)
        run_one(i);
}

void draw(void)
{
    int i;
    char buf[80];
    fb_clear(Cbg);
    fill(0, 0, (int)FB_W, 16, Cbar);
    text(6, 4, "cBench", rgb565(180, 230, 200));
    text(80, 4, "SharkDeck", Cdim);
    for (i = 0; i < NTEST; i++) {
        int y = 28 + i * 28;
        if (i == sel)
            fill(4, y - 4, (int)FB_W - 8, 26, Csel);
        text(10, y, names[i], i == sel ? Cacc : Cfg);
        if (have[i]) {
            snprintf(buf, sizeof buf, "%.3f", score[i]);
            text((int)FB_W - 8 - (int)strlen(buf) * 6, y, buf, Cacc);
        } else
            text((int)FB_W - 50, y, "--", Cdim);
    }
    fill(0, (int)FB_H - 28, (int)FB_W, 28, Cbar);
    text(6, (int)FB_H - 22, running ? msg : "Enter run   A all   S save-line   X quit", Cdim);
    text(6, (int)FB_H - 12, msg, Cfg);
    fb_flip();
}

int main(void)
{
    colors();
    if (fb_open() < 0) {
        fprintf(stderr, "cBench needs /dev/fb0\n");
        return 1;
    }
    io_open();
    snprintf(msg, sizeof msg, "pick a test");
    while (!want_quit) {
        unsigned char b[8];
        int n;
        draw();
        n = (int)read(0, b, sizeof b);
        if (n <= 0) {
            usleep(20000);
            continue;
        }
        if (b[0] == 'x' || b[0] == 'X' || b[0] == 24)
            want_quit = 1;
        else if (b[0] == 'a' || b[0] == 'A')
            run_all();
        else if (b[0] == 13 || b[0] == 10 || b[0] == ' ')
            run_one(sel);
        else if (b[0] == 's' || b[0] == 'S') {
            snprintf(msg, sizeof msg, "logged ~/cbench.log");
            log_line("# snapshot");
        } else if (b[0] == 27 && n >= 3 && b[2] == 'A' && sel > 0)
            sel--;
        else if (b[0] == 27 && n >= 3 && b[2] == 'B' && sel + 1 < NTEST)
            sel++;
    }
    io_close();
    fb_close();
    return 0;
}
