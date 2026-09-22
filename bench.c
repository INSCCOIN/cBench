#define _GNU_SOURCE
#include "fb.h"
#include <math.h>
#include <pthread.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define NTEST 11

struct Test {
    const char *id;
    const char *label;
    const char *unit;
    int lower_better;
    int on;
    double score;
    int have;
};

static struct Test T[NTEST] = {
    {"int64", "int64 mul/add", "Mops", 0, 1, 0, 0},
    {"float", "float mul+exp", "Mops", 0, 1, 0, 0},
    {"memcpy", "memcpy", "MB/s", 0, 1, 0, 0},
    {"walk", "ptr chase 2MB", "ns", 1, 1, 0, 0},
    {"gemm", "gemm 80x80", "Mflop", 0, 1, 0, 0},
    {"mandel", "mandelbrot", "kpix", 0, 1, 0, 0},
    {"sieve", "sieve 2e6", "Mnum/s", 0, 1, 0, 0},
    {"qsort", "qsort 200k", "kitem/s", 0, 1, 0, 0},
    {"thr4", "4-thread int", "Mops", 0, 1, 0, 0},
    {"fbfill", "fb clear+flip", "fps", 0, 1, 0, 0},
    {"fbpx", "fb px()", "Mpx/s", 0, 0, 0, 0},
};

static double duration = 1.0;
static int threads = 4;
static int sel, want_quit;
static char msg[96] = "space on/off  enter run  a all  w write cfg  x quit";

static uint16_t Cbg, Cfg, Cacc, Cdim;
static struct termios oldt;
static int rawon;

static char *home_dir(void)
{
    const char *h = getenv("HOME");
    struct passwd *pw;
    if (h && *h)
        return (char *)h;
    pw = getpwuid(getuid());
    return pw ? pw->pw_dir : ".";
}

static void cfg_path(char *p, size_t n)
{
    snprintf(p, n, "%s/.cbench.cfg", home_dir());
}

static void cfg_save(void)
{
    char p[512];
    FILE *f;
    int i;
    cfg_path(p, sizeof p);
    f = fopen(p, "w");
    if (!f) {
        snprintf(msg, sizeof msg, "can't write %s", p);
        return;
    }
    fprintf(f, "# cBench — 1=on 0=off\n");
    fprintf(f, "duration=%.2f\n", duration);
    fprintf(f, "threads=%d\n", threads);
    for (i = 0; i < NTEST; i++)
        fprintf(f, "%s=%d\n", T[i].id, T[i].on);
    fclose(f);
    snprintf(msg, sizeof msg, "wrote %s", p);
}

static void cfg_load(void)
{
    char p[512], line[80];
    FILE *f;
    int i;
    cfg_path(p, sizeof p);
    f = fopen(p, "r");
    if (!f) {
        cfg_save();
        return;
    }
    while (fgets(line, sizeof line, f)) {
        char *eq;
        if (line[0] == '#' || line[0] == '\n')
            continue;
        eq = strchr(line, '=');
        if (!eq)
            continue;
        *eq++ = 0;
        if (!strcmp(line, "duration")) {
            duration = atof(eq);
            if (duration < 0.2)
                duration = 0.2;
            if (duration > 8)
                duration = 8;
        } else if (!strcmp(line, "threads")) {
            threads = atoi(eq);
            if (threads < 1)
                threads = 1;
            if (threads > 4)
                threads = 4;
        } else {
            for (i = 0; i < NTEST; i++)
                if (!strcmp(line, T[i].id))
                    T[i].on = atoi(eq) != 0;
        }
    }
    fclose(f);
}

static double now_s(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
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

static void log_line(const char *s)
{
    char p[512];
    FILE *f;
    snprintf(p, sizeof p, "%s/cbench.log", home_dir());
    f = fopen(p, "a");
    if (!f)
        return;
    fprintf(f, "%s\n", s);
    fclose(f);
}

static double bench_int64(void)
{
    volatile unsigned long long a = 1, b = 3, c = 7;
    unsigned long n = 0;
    double t0 = now_s(), t;
    do {
        int i;
        for (i = 0; i < 20000; i++) {
            a = a * 6364136223846793005ull + 1;
            b = b * a + c;
            c ^= b >> 17;
        }
        n += 20000;
        t = now_s() - t0;
    } while (t < duration);
    (void)a;
    (void)b;
    (void)c;
    return n / t / 1e6;
}

static double bench_float(void)
{
    volatile double x = 1.0000001, y = 0.1;
    unsigned long n = 0;
    double t0 = now_s(), t;
    do {
        int i;
        for (i = 0; i < 3000; i++) {
            x = x * 1.0000003 + 1e-9;
            y = y + exp(-x * 0.0001) * sin(x);
        }
        n += 3000;
        t = now_s() - t0;
    } while (t < duration);
    (void)y;
    return n / t / 1e6;
}

static double bench_memcpy(void)
{
    const size_t n = 4 * 1024 * 1024;
    char *a = malloc(n), *b = malloc(n);
    double t0, t, mb = 0;
    if (!a || !b) {
        free(a);
        free(b);
        return 0;
    }
    memset(a, 0xa5, n);
    t0 = now_s();
    do {
        memcpy(b, a, n);
        mb += n / (1024.0 * 1024.0);
        t = now_s() - t0;
    } while (t < duration);
    free(a);
    free(b);
    return mb / t;
}

static double bench_walk(void)
{
    const int n = 512 * 1024; /* 2MB of ints */
    int *p = malloc((size_t)n * sizeof *p);
    int i, idx = 0;
    unsigned long hits = 0;
    double t0, t;
    if (!p)
        return 0;
    for (i = 0; i < n; i++)
        p[i] = (int)(((unsigned)i * 1103515245u + 12345u) % (unsigned)n);
    t0 = now_s();
    do {
        for (i = 0; i < 30000; i++) {
            idx = p[idx];
            hits++;
        }
        t = now_s() - t0;
    } while (t < duration);
    free(p);
    return (t / (double)hits) * 1e9;
}

static double bench_gemm(void)
{
    const int N = 80;
    float *A, *B, *C;
    int i, j, k, reps = 0;
    double t0, t, flop;
    A = malloc((size_t)N * N * sizeof(float));
    B = malloc((size_t)N * N * sizeof(float));
    C = malloc((size_t)N * N * sizeof(float));
    if (!A || !B || !C) {
        free(A);
        free(B);
        free(C);
        return 0;
    }
    for (i = 0; i < N * N; i++) {
        A[i] = (float)(i % 17) * 0.01f;
        B[i] = (float)(i % 13) * 0.02f;
        C[i] = 0;
    }
    t0 = now_s();
    do {
        for (i = 0; i < N; i++)
            for (j = 0; j < N; j++) {
                float s = 0;
                for (k = 0; k < N; k++)
                    s += A[i * N + k] * B[k * N + j];
                C[i * N + j] = s;
            }
        reps++;
        t = now_s() - t0;
    } while (t < duration);
    flop = (double)reps * 2.0 * N * N * N;
    free(A);
    free(B);
    free(C);
    return flop / t / 1e6;
}

static double bench_mandel(void)
{
    const int W = 160, H = 100, MAX = 80;
    unsigned long pix = 0;
    double t0 = now_s(), t;
    do {
        int x, y;
        for (y = 0; y < H; y++)
            for (x = 0; x < W; x++) {
                double cr = (x / (double)W) * 3.0 - 2.0;
                double ci = (y / (double)H) * 2.0 - 1.0;
                double zr = 0, zi = 0;
                int n;
                for (n = 0; n < MAX; n++) {
                    double zr2 = zr * zr - zi * zi + cr;
                    zi = 2 * zr * zi + ci;
                    zr = zr2;
                    if (zr * zr + zi * zi > 4)
                        break;
                }
                pix++;
            }
        t = now_s() - t0;
    } while (t < duration);
    return pix / t / 1000.0;
}

static double bench_sieve(void)
{
    const int N = 2000000;
    char *c = malloc((size_t)N);
    double t0, t, done = 0;
    int p, i;
    if (!c)
        return 0;
    t0 = now_s();
    do {
        memset(c, 1, (size_t)N);
        c[0] = c[1] = 0;
        for (p = 2; p * p < N; p++)
            if (c[p])
                for (i = p * p; i < N; i += p)
                    c[i] = 0;
        done += N;
        t = now_s() - t0;
    } while (t < duration);
    free(c);
    return done / t / 1e6;
}

static int icmp(const void *a, const void *b)
{
    int x = *(const int *)a, y = *(const int *)b;
    return (x > y) - (x < y);
}

static double bench_qsort(void)
{
    const int N = 200000;
    int *a = malloc((size_t)N * sizeof *a);
    unsigned long items = 0;
    double t0, t;
    int i;
    if (!a)
        return 0;
    t0 = now_s();
    do {
        for (i = 0; i < N; i++)
            a[i] = (int)((unsigned)i * 1103515245u + 12345);
        qsort(a, (size_t)N, sizeof *a, icmp);
        items += N;
        t = now_s() - t0;
    } while (t < duration);
    free(a);
    return items / t / 1000.0;
}

struct Thr {
    unsigned long n;
    double t;
};

static void *thr_body(void *arg)
{
    struct Thr *th = arg;
    volatile unsigned long long a = 1, b = 9;
    unsigned long n = 0;
    double t0 = now_s(), t;
    do {
        int i;
        for (i = 0; i < 20000; i++) {
            a = a * 6364136223846793005ull + 1;
            b += a;
        }
        n += 20000;
        t = now_s() - t0;
    } while (t < duration);
    (void)b;
    th->n = n;
    th->t = t;
    return NULL;
}

static double bench_thr(void)
{
    pthread_t id[4];
    struct Thr th[4];
    int i, nt = threads;
    double sum = 0;
    if (nt > 4)
        nt = 4;
    for (i = 0; i < nt; i++)
        pthread_create(&id[i], NULL, thr_body, &th[i]);
    for (i = 0; i < nt; i++) {
        pthread_join(id[i], NULL);
        sum += th[i].n / th[i].t;
    }
    return sum / 1e6;
}

static double bench_fill(void)
{
    int frames = 0;
    double t0 = now_s(), t;
    uint16_t c = 0x03e0;
    do {
        fb_clear(c);
        fb_flip();
        frames++;
        t = now_s() - t0;
        c = (uint16_t)(c + 32);
    } while (t < duration);
    return frames / t;
}

static double bench_px(void)
{
    unsigned long n = 0;
    double t0 = now_s(), t;
    int x, y;
    do {
        for (y = 0; y < 64; y++)
            for (x = 0; x < 128; x++)
                px(x + 8, y + 20, 0x07e0);
        n += 64ul * 128ul;
        t = now_s() - t0;
    } while (t < duration * 0.8);
    fb_flip();
    return n / t / 1e6;
}

static double (*fn[NTEST])(void) = {
    bench_int64, bench_float, bench_memcpy, bench_walk, bench_gemm,
    bench_mandel, bench_sieve, bench_qsort, bench_thr, bench_fill, bench_px
};

static void draw(void);

static void run_one(int i)
{
    char line[160];
    time_t tt = time(NULL);
    struct tm *tm = localtime(&tt);
    if (!T[i].on) {
        snprintf(msg, sizeof msg, "%s off — space to enable", T[i].id);
        return;
    }
    snprintf(msg, sizeof msg, "running %s …", T[i].id);
    draw();
    T[i].score = fn[i]();
    T[i].have = 1;
    snprintf(msg, sizeof msg, "%s  %.3f %s", T[i].id, T[i].score, T[i].unit);
    snprintf(line, sizeof line, "%04d-%02d-%02d %02d:%02d  %s  %.4f  %s",
             tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
             tm->tm_hour, tm->tm_min, T[i].id, T[i].score, T[i].unit);
    log_line(line);
}

static void run_all(void)
{
    int i;
    for (i = 0; i < NTEST; i++)
        if (T[i].on)
            run_one(i);
    snprintf(msg, sizeof msg, "all on tests done  dt=%.2fs", duration);
}

static void draw(void)
{
    int i, row = 2;
    char buf[64];
    fb_clear(Cbg);
    text(2, 2, "cBench", Cacc);
    snprintf(buf, sizeof buf, "dt=%.2fs thr=%d", duration, threads);
    text(56, 2, buf, Cdim);
    text(2, 12, "id        on   score", Cdim);
    for (i = 0; i < NTEST; i++) {
        int y = 22 + i * 10;
        char mark = T[i].on ? 'x' : ' ';
        char cur = (i == sel) ? '>' : ' ';
        snprintf(buf, sizeof buf, "%c [%c] %-8s", cur, mark, T[i].id);
        text(2, y, buf, i == sel ? Cacc : Cfg);
        if (T[i].have) {
            snprintf(buf, sizeof buf, "%8.3f %s%s",
                     T[i].score, T[i].unit, T[i].lower_better ? " *" : "");
            text(120, y, buf, Cacc);
        }
        (void)row;
    }
    text(2, (int)FB_H - 20, msg, Cfg);
    text(2, (int)FB_H - 10, "space toggle  enter  a=all  w=cfg  x=quit", Cdim);
    fb_flip();
}

int main(void)
{
    Cbg = rgb565(0, 0, 0);
    Cfg = rgb565(80, 220, 90);
    Cacc = rgb565(180, 255, 140);
    Cdim = rgb565(40, 110, 50);
    cfg_load();
    if (fb_open() < 0) {
        fprintf(stderr, "cBench needs /dev/fb0\n");
        return 1;
    }
    io_open();
    while (!want_quit) {
        unsigned char b[8];
        int n;
        draw();
        n = (int)read(0, b, sizeof b);
        if (n <= 0) {
            usleep(25000);
            continue;
        }
        if (b[0] == 'x' || b[0] == 'X' || b[0] == 24)
            want_quit = 1;
        else if (b[0] == ' ') {
            T[sel].on = !T[sel].on;
            cfg_save();
        } else if (b[0] == 'w' || b[0] == 'W')
            cfg_save();
        else if (b[0] == 'a' || b[0] == 'A')
            run_all();
        else if (b[0] == 13 || b[0] == 10)
            run_one(sel);
        else if (b[0] == '+' ) {
            duration += 0.2;
            if (duration > 8)
                duration = 8;
            cfg_save();
        } else if (b[0] == '-') {
            duration -= 0.2;
            if (duration < 0.2)
                duration = 0.2;
            cfg_save();
        } else if (b[0] == 27 && n >= 3 && b[2] == 'A' && sel > 0)
            sel--;
        else if (b[0] == 27 && n >= 3 && b[2] == 'B' && sel + 1 < NTEST)
            sel++;
    }
    io_close();
    fb_close();
    return 0;
}
