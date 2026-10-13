/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file fuzz_driver.c
 *     A small coverage-guided mutation fuzzer for the targets in this directory. See fuzz_driver.h and README.md.
 *
 *     kfx_fuzz list
 *     kfx_fuzz <target> fuzz <corpus-dir> [-max_total_time=S] [-runs=N] [-jobs=N] [-seed=N] [-timeout=S] [-crashes=DIR]
 *     kfx_fuzz <target> repro <file>...     run the inputs in this process (the sanitizer report goes to the terminal)
 *     kfx_fuzz <target> run <file>...       run each in a child, as the fuzzer does; says how it ended
 */
/******************************************************************************/
#define _GNU_SOURCE
#include "fuzz_driver.h"

#include <dirent.h>
#include <errno.h>
#include <execinfo.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define MAX_TARGETS 64
#define MAP_BITS 16
#define MAP_SIZE (1u << MAP_BITS)
#define SANITIZER_EXIT 77
#define TIMEOUT_EXIT 124

static const struct FuzzTarget *targets[MAX_TARGETS];
static int targets_num;

void fuzz_register(const struct FuzzTarget *target)
{
    if (targets_num < MAX_TARGETS)
        targets[targets_num++] = target;
}

/* The sanitizers: no leak report (a child exits without freeing, on purpose), a distinct exit code. */
const char *__asan_default_options(void)
{
    return "detect_leaks=0:exitcode=77:abort_on_error=0:handle_abort=1:allocator_may_return_null=1:max_allocation_size_mb=1024";
}
const char *__ubsan_default_options(void)
{
    return "exitcode=77:print_stacktrace=1";
}

/* ---- coverage: edges between the basic blocks of the instrumented libraries ---- */

static uint8_t *cov_map;      /* shared with the children */
static uint32_t cov_prev;

/* Called at every basic block of an instrumented object (-fsanitize-coverage=trace-pc). */
void __sanitizer_cov_trace_pc(void)
{
    const uintptr_t pc = (uintptr_t)__builtin_return_address(0);
    const uint32_t h = (uint32_t)((pc * 0x9E3779B1u) >> (32 - MAP_BITS));
    const uint32_t idx = (h ^ cov_prev) & (MAP_SIZE - 1);
    if (cov_map != NULL && cov_map[idx] != 255)
        cov_map[idx]++;
    cov_prev = h >> 1;
}

static uint8_t count_class[256];
static void init_count_class(void)
{
    for (int i = 0; i < 256; i++)
        count_class[i] = (i == 0) ? 0 : (i == 1) ? 1 : (i == 2) ? 2 : (i == 3) ? 4 : (i < 8) ? 8 : (i < 16) ? 16 : (i < 32) ? 32 : (i < 128) ? 64 : 128;
}

static uint8_t virgin[MAP_SIZE];  /* bits not seen yet (1 = never) */

/** Classifies the run's map and folds it into `virgin`. @return 2 if a new edge, 1 if only a new hit count, 0 if nothing. */
static int cov_new_bits(void)
{
    int ret = 0;
    for (uint32_t i = 0; i < MAP_SIZE; i++)
    {
        if (cov_map[i] == 0)
            continue;
        const uint8_t c = count_class[cov_map[i]];
        if (c & virgin[i])
        {
            ret = (ret == 2 || virgin[i] == 0xff) ? 2 : 1;
            virgin[i] &= (uint8_t)~c;
        }
    }
    return ret;
}

static size_t cov_edges(void)
{
    size_t n = 0;
    for (uint32_t i = 0; i < MAP_SIZE; i++)
        n += (virgin[i] != 0xff);
    return n;
}

/* ---- random numbers ---- */

static uint64_t rng_state;
static uint64_t rnd(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return rng_state;
}
static size_t rnd_below(size_t n) { return n == 0 ? 0 : (size_t)(rnd() % n); }

/* ---- corpus ---- */

struct Input {
    uint8_t *data;
    size_t size;
};
static struct Input *corpus;
static size_t corpus_num, corpus_cap;

static void corpus_add(const uint8_t *data, size_t size)
{
    if (corpus_num == corpus_cap)
    {
        corpus_cap = corpus_cap ? corpus_cap * 2 : 256;
        corpus = (struct Input *)realloc(corpus, corpus_cap * sizeof(*corpus));
    }
    corpus[corpus_num].data = (uint8_t *)malloc(size ? size : 1);
    memcpy(corpus[corpus_num].data, data, size);
    corpus[corpus_num].size = size;
    corpus_num++;
}

static uint64_t hash_bytes(const uint8_t *d, size_t n)
{
    uint64_t h = 1469598103934665603ULL;
    for (size_t i = 0; i < n; i++)
        h = (h ^ d[i]) * 1099511628211ULL;
    return h;
}

static uint8_t *read_file(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL)
        return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0 || n > (64 << 20))
    {
        fclose(f);
        return NULL;
    }
    uint8_t *buf = (uint8_t *)malloc((size_t)n + 1);
    if (fread(buf, 1, (size_t)n, f) != (size_t)n)
    {
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *size = (size_t)n;
    return buf;
}

static int write_file(const char *path, const uint8_t *d, size_t n)
{
    FILE *f = fopen(path, "wb");
    if (f == NULL)
        return -1;
    fwrite(d, 1, n, f);
    fclose(f);
    return 0;
}

/* ---- mutation ---- */

static const struct FuzzTarget *cur;
static size_t max_len;

static const int8_t interesting8[] = { -128, -1, 0, 1, 16, 32, 64, 100, 127 };
static const int16_t interesting16[] = { -32768, -129, 128, 255, 256, 512, 1000, 1024, 4096, 32767 };
static const int32_t interesting32[] = { INT32_MIN, -100663046, -32769, 32768, 65535, 65536, 100663045, INT32_MAX };

static void put_le(uint8_t *p, int width, uint64_t v) { for (int i = 0; i < width; i++) p[i] = (uint8_t)(v >> (8 * i)); }
static uint64_t get_le(const uint8_t *p, int width) { uint64_t v = 0; for (int i = 0; i < width; i++) v |= (uint64_t)p[i] << (8 * i); return v; }

/** One random edit of buf (size *n, capacity max_len + 64). */
static void mutate_once(uint8_t *buf, size_t *n_ptr)
{
    size_t n = *n_ptr;
    const int kinds = 14;
    switch (rnd_below(kinds))
    {
    case 0: if (n) buf[rnd_below(n)] ^= (uint8_t)(1u << rnd_below(8)); break;
    case 1: if (n) buf[rnd_below(n)] = (uint8_t)rnd(); break;
    case 2: if (n) buf[rnd_below(n)] = (uint8_t)interesting8[rnd_below(sizeof(interesting8))]; break;
    case 3: if (n >= 2) put_le(buf + rnd_below(n - 1), 2, (uint16_t)interesting16[rnd_below(sizeof(interesting16) / 2)]); break;
    case 4: if (n >= 4) put_le(buf + rnd_below(n - 3), 4, (uint32_t)interesting32[rnd_below(sizeof(interesting32) / 4)]); break;
    case 5: /* add to a value of 1, 2, 4 or 8 bytes */
    {
        static const int w[] = { 1, 2, 4, 8 };
        int width = w[rnd_below(4)];
        if (n >= (size_t)width)
        {
            uint8_t *p = buf + rnd_below(n - (size_t)width + 1);
            int64_t d = (int64_t)rnd_below(35) + 1;
            put_le(p, width, get_le(p, width) + (uint64_t)(rnd() & 1 ? d : -d));
        }
        break;
    }
    case 6: /* an 8-byte word set to a large value */
        if (n >= 8)
        {
            static const uint64_t big[] = { 0xffffffffffffffffULL, 0x7fffffffffffffffULL, 0x8000000000000000ULL, 0x100000000ULL, 0xffffffffULL, 0x10000ULL };
            put_le(buf + rnd_below(n - 7), 8, big[rnd_below(6)]);
        }
        break;
    case 7: /* delete a block */
        if (n > 1)
        {
            size_t len = 1 + rnd_below(n < 32 ? n - 1 : 32);
            size_t at = rnd_below(n - len + 1);
            memmove(buf + at, buf + at + len, n - at - len);
            n -= len;
        }
        break;
    case 8: /* duplicate a block */
        if (n > 0 && n < max_len)
        {
            size_t len = 1 + rnd_below(n < 32 ? n : 32);
            if (n + len > max_len) len = max_len - n;
            size_t from = rnd_below(n - (len < n ? len : n) + 1);
            size_t at = rnd_below(n + 1);
            memmove(buf + at + len, buf + at, n - at);
            memmove(buf + at, buf + (from >= at ? from + len : from), len);
            n += len;
        }
        break;
    case 9: /* insert random bytes */
        if (n < max_len)
        {
            size_t len = 1 + rnd_below(8);
            if (n + len > max_len) len = max_len - n;
            size_t at = rnd_below(n + 1);
            memmove(buf + at + len, buf + at, n - at);
            for (size_t i = 0; i < len; i++) buf[at + i] = (uint8_t)rnd();
            n += len;
        }
        break;
    case 10: /* a dictionary token, inserted or overwritten */
        if (cur->dictionary != NULL)
        {
            size_t count = 0;
            while (cur->dictionary[count] != NULL) count++;
            if (count > 0)
            {
                const char *tok = cur->dictionary[rnd_below(count)];
                size_t len = strlen(tok);
                if (rnd() & 1 && n + len <= max_len)
                {
                    size_t at = rnd_below(n + 1);
                    memmove(buf + at + len, buf + at, n - at);
                    memcpy(buf + at, tok, len);
                    n += len;
                }
                else if (n >= len && len > 0)
                    memcpy(buf + rnd_below(n - len + 1), tok, len);
            }
        }
        break;
    case 11: /* splice a piece of another corpus input in */
        if (corpus_num > 1)
        {
            const struct Input *o = &corpus[rnd_below(corpus_num)];
            if (o->size > 0)
            {
                size_t len = 1 + rnd_below(o->size < 64 ? o->size : 64);
                size_t from = rnd_below(o->size - len + 1);
                size_t at = rnd_below(n + 1);
                if (n + len > max_len) len = max_len - n;
                if (len > 0)
                {
                    memmove(buf + at + len, buf + at, n - at);
                    memcpy(buf + at, o->data + from, len);
                    n += len;
                }
            }
        }
        break;
    case 12: /* overwrite a range with a repeated byte */
        if (n > 0)
        {
            size_t len = 1 + rnd_below(n < 16 ? n : 16);
            memset(buf + rnd_below(n - len + 1), (int)(rnd() & 1 ? 0 : (uint8_t)rnd()), len);
        }
        break;
    case 13: /* swap two bytes */
        if (n > 1)
        {
            size_t a = rnd_below(n), b = rnd_below(n);
            uint8_t t = buf[a]; buf[a] = buf[b]; buf[b] = t;
        }
        break;
    }
    *n_ptr = n;
}

static size_t mutate(const struct Input *src, uint8_t *out)
{
    size_t n = src->size > max_len ? max_len : src->size;
    memcpy(out, src->data, n);
    int stack = 1 + (int)rnd_below(6);
    for (int i = 0; i < stack; i++)
        mutate_once(out, &n);
    return n;
}

/* ---- running an input in a child ---- */

static int timeout_seconds = 10;
static const char *crash_dir = "crashes";
static volatile sig_atomic_t in_child;

static void alarm_handler(int sig)
{
    (void)sig;
    static const char msg[] = "FUZZ: timeout\n";
    if (write(2, msg, sizeof(msg) - 1) < 0) {}
    void *frames[40];
    int n = backtrace(frames, 40);
    backtrace_symbols_fd(frames, n, 2);
    _exit(TIMEOUT_EXIT);
}

enum Outcome { Out_Ok, Out_Crash, Out_Timeout, Out_Sanitizer };

static enum Outcome run_child(const uint8_t *data, size_t size, int stderr_fd, int *signal_out)
{
    memset(cov_map, 0, MAP_SIZE);
    fflush(NULL);
    pid_t pid = fork();
    if (pid < 0)
    {
        perror("fork");
        exit(1);
    }
    if (pid == 0)
    {
        in_child = 1;
        cov_prev = 0;
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) dup2(devnull, 1);
        dup2(stderr_fd >= 0 ? stderr_fd : devnull, 2);
        signal(SIGALRM, alarm_handler);
        alarm((unsigned)timeout_seconds);
        cur->one(data, size);
        _exit(0);
    }
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    if (WIFSIGNALED(status))
    {
        *signal_out = WTERMSIG(status);
        return Out_Crash;
    }
    if (WEXITSTATUS(status) == TIMEOUT_EXIT)
        return Out_Timeout;
    if (WEXITSTATUS(status) == SANITIZER_EXIT)
        return Out_Sanitizer;
    return Out_Ok;
}

static void mkdir_p(const char *path);

static void save_crash(const uint8_t *data, size_t size, enum Outcome o, int sig)
{
    static const char *const kind[] = { "ok", "crash", "timeout", "sanitizer" };
    mkdir_p(crash_dir);
    char path[512];
    snprintf(path, sizeof(path), "%s/%s-%016llx", crash_dir, kind[o], (unsigned long long)hash_bytes(data, size));
    if (access(path, F_OK) == 0)
        return;
    if (write_file(path, data, size) != 0)
        fprintf(stderr, "FUZZ: cannot write %s\n", path);
    /* once more with the report kept */
    char log[600];
    snprintf(log, sizeof(log), "%s.txt", path);
    int fd = open(log, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    int s2 = 0;
    run_child(data, size, fd, &s2);
    if (fd >= 0) close(fd);
    fprintf(stderr, "FUZZ: %s (signal %d) -> %s\n", kind[o], sig, path);
}

/* ---- the fuzz loop ---- */

static void mkdir_p(const char *path)
{
    char buf[1024];
    snprintf(buf, sizeof(buf), "%s", path);
    for (char *p = buf + 1; *p != '\0'; p++)
        if (*p == '/')
        {
            *p = '\0';
            mkdir(buf, 0755);
            *p = '/';
        }
    mkdir(buf, 0755);
}

static void sync_dir(const char *dir, char **seen, size_t *seen_num, size_t seen_cap)
{
    DIR *d = opendir(dir);
    if (d == NULL)
        return;
    struct dirent *e;
    while ((e = readdir(d)) != NULL)
    {
        if (e->d_name[0] == '.')
            continue;
        int known = 0;
        for (size_t i = 0; i < *seen_num; i++)
            if (strcmp(seen[i], e->d_name) == 0) { known = 1; break; }
        if (known || *seen_num >= seen_cap)
            continue;
        seen[(*seen_num)++] = strdup(e->d_name);
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
        size_t size = 0;
        uint8_t *data = read_file(path, &size);
        if (data == NULL)
            continue;
        int sig = 0;
        if (size <= max_len && run_child(data, size, -1, &sig) == Out_Ok)
        {
            cov_new_bits();
            corpus_add(data, size);
        }
        free(data);
    }
    closedir(d);
}

static double now_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static int fuzz_worker(const char *corpus_dir, double max_time, long long max_runs, int worker)
{
    char **seen = (char **)calloc(1 << 16, sizeof(char *));
    size_t seen_num = 0;
    sync_dir(corpus_dir, seen, &seen_num, 1 << 16);
    if (corpus_num == 0)
    {
        uint8_t zero[1] = { 0 };
        corpus_add(zero, 1);
    }
    uint8_t *buf = (uint8_t *)malloc(max_len + 128);
    const double start = now_s();
    double last_report = start, last_sync = start;
    long long runs = 0, found = 0, crashes = 0;
    for (;;)
    {
        const double t = now_s();
        if ((max_time > 0 && t - start >= max_time) || (max_runs > 0 && runs >= max_runs))
            break;
        if (t - last_sync > 3)
        {
            sync_dir(corpus_dir, seen, &seen_num, 1 << 16);
            last_sync = t;
        }
        if (t - last_report > 10)
        {
            fprintf(stderr, "[%d] %.0fs execs=%lld (%.0f/s) corpus=%zu edges=%zu new=%lld crashes=%lld\n", worker, t - start, runs,
                (double)runs / (t - start), corpus_num, cov_edges(), found, crashes);
            last_report = t;
        }
        /* recent finds are likelier picks */
        size_t pick = (rnd() & 1) ? rnd_below(corpus_num) : corpus_num - 1 - rnd_below(corpus_num < 16 ? corpus_num : 16);
        size_t n = mutate(&corpus[pick], buf);
        int sig = 0;
        enum Outcome o = run_child(buf, n, -1, &sig);
        runs++;
        if (o == Out_Timeout)
        {
            // a stall under load (28 workers copying big address spaces) is not a hang: only one that happens again counts
            int sig2 = 0;
            if (run_child(buf, n, -1, &sig2) == Out_Ok)
                o = Out_Ok;
        }
        if (o != Out_Ok)
        {
            crashes++;
            save_crash(buf, n, o, sig);
            continue;
        }
        if (cov_new_bits() == 2)
        {
            found++;
            corpus_add(buf, n);
            char path[1024];
            snprintf(path, sizeof(path), "%s/%d-%016llx", corpus_dir, worker, (unsigned long long)hash_bytes(buf, n));
            write_file(path, buf, n);
            if (seen_num < (1 << 16))
                seen[seen_num++] = strdup(strrchr(path, '/') + 1);
        }
    }
    fprintf(stderr, "[%d] done: execs=%lld corpus=%zu edges=%zu new=%lld crashes=%lld\n", worker, runs, corpus_num, cov_edges(), found, crashes);
    return crashes > 0 ? 3 : 0;
}

static const struct FuzzTarget *find_target(const char *name)
{
    for (int i = 0; i < targets_num; i++)
        if (strcmp(targets[i]->name, name) == 0)
            return targets[i];
    return NULL;
}

int main(int argc, char **argv)
{
    if (argc >= 2 && strcmp(argv[1], "list") == 0)
    {
        for (int i = 0; i < targets_num; i++)
            printf("%-22s %s\n", targets[i]->name, targets[i]->what);
        return 0;
    }
    if (argc < 3)
    {
        fprintf(stderr, "usage: %s list | <target> fuzz <corpus-dir> [options] | <target> repro|run <file>...\n", argv[0]);
        return 2;
    }
    cur = find_target(argv[1]);
    if (cur == NULL)
    {
        fprintf(stderr, "no target '%s' (kfx_fuzz list)\n", argv[1]);
        return 2;
    }
    max_len = cur->max_len ? cur->max_len : 65536;
    init_count_class();
    memset(virgin, 0xff, sizeof(virgin));
    cov_map = (uint8_t *)mmap(NULL, MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    rng_state = (uint64_t)time(NULL) ^ ((uint64_t)getpid() << 32) ^ 0x9E3779B97F4A7C15ULL;

    const char *mode = argv[2];
    for (int i = 3; i < argc; i++)
        if (strncmp(argv[i], "-timeout=", 9) == 0)
            timeout_seconds = atoi(argv[i] + 9);
    if (strcmp(mode, "seeds") == 0)
    {
        if (argc < 4 || cur->seeds == NULL)
        {
            fprintf(stderr, "usage: %s <target> seeds <dir> (the target %s)\n", argv[0], cur->seeds ? "takes one" : "writes none: scripts/fuzz_seeds.py makes its seeds");
            return 2;
        }
        mkdir(argv[3], 0755);
        return cur->seeds(argv[3]);
    }
    double max_time = 0;
    long long max_runs = 0;
    int jobs = 1;
    const char *corpus_dir = NULL;
    int first_file = 3;
    if (strcmp(mode, "fuzz") == 0)
    {
        if (argc < 4)
        {
            fprintf(stderr, "fuzz needs a corpus directory\n");
            return 2;
        }
        corpus_dir = argv[3];
        first_file = 4;
        for (int i = 4; i < argc; i++)
        {
            if (strncmp(argv[i], "-max_total_time=", 16) == 0) max_time = atof(argv[i] + 16);
            else if (strncmp(argv[i], "-runs=", 6) == 0) max_runs = atoll(argv[i] + 6);
            else if (strncmp(argv[i], "-jobs=", 6) == 0) jobs = atoi(argv[i] + 6);
            else if (strncmp(argv[i], "-seed=", 6) == 0) rng_state = (uint64_t)atoll(argv[i] + 6) * 0x9E3779B97F4A7C15ULL + 1;
            else if (strncmp(argv[i], "-timeout=", 9) == 0) timeout_seconds = atoi(argv[i] + 9);
            else if (strncmp(argv[i], "-crashes=", 9) == 0) crash_dir = argv[i] + 9;
        }
        (void)first_file;
        mkdir_p(corpus_dir);
    }

    if (cur->init != NULL && cur->init() != 0)
    {
        fprintf(stderr, "target '%s' did not initialise\n", cur->name);
        return 2;
    }
    if (rng_state == 0) rng_state = 1;

    if (strcmp(mode, "repro") == 0)
    {
        for (int i = 3; i < argc; i++)
        {
            if (argv[i][0] == '-') continue;
            size_t size = 0;
            uint8_t *data = read_file(argv[i], &size);
            if (data == NULL) { fprintf(stderr, "cannot read %s\n", argv[i]); return 2; }
            fprintf(stderr, "running %s (%zu bytes)\n", argv[i], size);
            cur->one(data, size);
            free(data);
        }
        return 0;
    }
    if (strcmp(mode, "run") == 0)
    {
        int bad = 0;
        for (int i = 3; i < argc; i++)
        {
            if (argv[i][0] == '-') continue;
            size_t size = 0;
            uint8_t *data = read_file(argv[i], &size);
            if (data == NULL) { fprintf(stderr, "cannot read %s\n", argv[i]); return 2; }
            int sig = 0;
            enum Outcome o = run_child(data, size, 2, &sig);
            printf("%s: %s%s\n", argv[i], o == Out_Ok ? "ok" : o == Out_Crash ? "CRASH" : o == Out_Timeout ? "TIMEOUT" : "SANITIZER", o == Out_Crash ? " (signal)" : "");
            bad += (o != Out_Ok);
            free(data);
        }
        return bad ? 3 : 0;
    }
    if (strcmp(mode, "fuzz") != 0)
    {
        fprintf(stderr, "unknown mode '%s'\n", mode);
        return 2;
    }

    int rc = 0;
    if (jobs <= 1)
        return fuzz_worker(corpus_dir, max_time, max_runs, 0);
    for (int j = 0; j < jobs; j++)
    {
        pid_t pid = fork();
        if (pid == 0)
        {
            rng_state ^= (uint64_t)(j + 1) * 0xD1B54A32D192ED03ULL;
            cov_map = (uint8_t *)mmap(NULL, MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
            _exit(fuzz_worker(corpus_dir, max_time, max_runs, j));
        }
    }
    int status;
    while (wait(&status) > 0)
        if (WIFEXITED(status) && WEXITSTATUS(status) != 0)
            rc = WEXITSTATUS(status);
    return rc;
}
