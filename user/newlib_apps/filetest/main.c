/* filetest - newlib stdio regression suite over fsd.
 *
 * Exercises the tier-2 file path end to end: newlib's FILE layer, the
 * POSIX stubs in user/lib/posix/stubs.c, the FSD shm protocol, and the
 * FatFs backend on the SD card. Style and output format mirror zztest.
 *
 * Runs from the SD card, so /home/zuzu.txt is present for the read-side
 * tests. Everything this suite writes goes to /home/filetest.tmp and is
 * created with "w" (truncating), so the suite is re-runnable without an
 * unlink path -- which is just as well, since the stubs expose no
 * _unlink and remove() would fail here.
 *
 * Note on offsets: _lseek carries newlib's int signature and truncates
 * to 32 bits, so nothing here seeks past 2 GB.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>

#define RO_PATH  "/home/zuzu.txt"
#define TMP_PATH "/home/filetest.tmp"

/* fsd's per-client open-file cap (user/services/fsd/client_table.h).
 * Kept as a literal: client_table.h is fsd-private and not on the
 * tier-2 include path. */
#define FSD_MAX_FILES_PER_CLIENT 16

/* ---------------- harness ---------------- */

#define MAX_SECTIONS 10
static struct { const char *name; int pass, fail; } sections[MAX_SECTIONS];
static int cur_sec = -1;

static void section(const char *name)
{
    cur_sec++;
    sections[cur_sec].name = name;
    sections[cur_sec].pass = 0;
    sections[cur_sec].fail = 0;
    printf("\n--- %s ---\n", name);
}

#define CHECK(c, m) do {                                              \
    if (c) { sections[cur_sec].pass++; printf("ok:   %s\n", m); }     \
    else   { sections[cur_sec].fail++; printf("FAIL: %s\n", m); }     \
} while (0)

/* CHECK with the observed value printed on failure */
#define CHECK_EQ(got, want, m) do {                                   \
    long g_ = (long)(got);                                            \
    if (g_ == (long)(want)) {                                         \
        sections[cur_sec].pass++; printf("ok:   %s\n", m);            \
    } else {                                                          \
        sections[cur_sec].fail++;                                     \
        printf("FAIL: %s (got %ld, want %ld)\n", m, g_, (long)(want));\
    }                                                                 \
} while (0)

/* ---------------- helpers ---------------- */

/* Byte length of RO_PATH, established once by the open section and
 * reused by seek/eof/stat. -1 until measured. */
static long ro_size = -1;

/* Write `text` to TMP_PATH with the given mode; returns 0 on success. */
static int put_file(const char *mode, const char *text)
{
    FILE *f = fopen(TMP_PATH, mode);
    if (!f) return -1;
    size_t n = strlen(text);
    size_t w = fwrite(text, 1, n, f);
    if (fclose(f) != 0) return -1;
    return w == n ? 0 : -1;
}

/* Slurp TMP_PATH into buf (NUL-terminated); returns bytes read, -1 on error. */
static long get_file(char *buf, size_t cap)
{
    FILE *f = fopen(TMP_PATH, "r");
    if (!f) return -1;
    size_t n = fread(buf, 1, cap - 1, f);
    buf[n] = '\0';
    int bad = ferror(f);
    fclose(f);
    return bad ? -1 : (long)n;
}

/* ---------------- sections ---------------- */

/* fopen/fread/fclose against the file the SD image ships with. */
static void sec_open(void)
{
    section("open");

    FILE *f = fopen(RO_PATH, "r");
    CHECK(f != NULL, "fopen(" RO_PATH ", r) succeeds");
    if (!f) return;

    char buf[64];
    size_t n = fread(buf, 1, sizeof(buf), f);
    CHECK(n > 0, "fread returns data from a non-empty file");
    CHECK_EQ(ferror(f), 0, "fread leaves the error flag clear");

    /* The file opens with the greeting; check a prefix rather than the
     * whole thing so editing zuzu.txt's body doesn't break the suite. */
    CHECK(n >= 6 && memcmp(buf, "Hello,", 6) == 0,
          "first bytes match the file's known contents");

    /* Measure the size once, via seek, for the sections that follow. */
    CHECK_EQ(fseek(f, 0, SEEK_END), 0, "fseek(END) on the open file");
    ro_size = ftell(f);
    CHECK(ro_size > 0, "ftell(END) reports a positive size");

    CHECK_EQ(fclose(f), 0, "fclose succeeds");
}

/* fseek SET/CUR/END and ftell must agree with each other. */
static void sec_seek(void)
{
    section("seek");

    FILE *f = fopen(RO_PATH, "r");
    CHECK(f != NULL, "reopen for seek tests");
    if (!f) return;

    CHECK_EQ(ftell(f), 0, "ftell is 0 on a freshly opened file");

    CHECK_EQ(fseek(f, 10, SEEK_SET), 0, "fseek(10, SET)");
    CHECK_EQ(ftell(f), 10, "ftell agrees after SEEK_SET");

    CHECK_EQ(fseek(f, 5, SEEK_CUR), 0, "fseek(+5, CUR)");
    CHECK_EQ(ftell(f), 15, "ftell agrees after a forward SEEK_CUR");

    CHECK_EQ(fseek(f, -5, SEEK_CUR), 0, "fseek(-5, CUR)");
    CHECK_EQ(ftell(f), 10, "ftell agrees after a backward SEEK_CUR");

    CHECK_EQ(fseek(f, 0, SEEK_END), 0, "fseek(0, END)");
    CHECK_EQ(ftell(f), ro_size, "ftell(END) matches the size seen in open");

    CHECK_EQ(fseek(f, -1, SEEK_END), 0, "fseek(-1, END)");
    CHECK_EQ(ftell(f), ro_size - 1, "ftell agrees after a negative SEEK_END");

    /* Seek back to a known offset and confirm the byte read there is the
     * same one a fresh read of that offset produces -- i.e. the seek
     * actually moved the server-side cursor, not just newlib's buffer. */
    char a, b;
    CHECK_EQ(fseek(f, 20, SEEK_SET), 0, "fseek(20, SET) before the read check");
    CHECK_EQ(fread(&a, 1, 1, f), 1, "read one byte at offset 20");
    CHECK_EQ(ftell(f), 21, "ftell advanced by the byte read");
    CHECK_EQ(fseek(f, 20, SEEK_SET), 0, "re-seek to offset 20");
    CHECK_EQ(fread(&b, 1, 1, f), 1, "re-read the byte at offset 20");
    CHECK_EQ(a, b, "the same offset yields the same byte");

    fclose(f);
}

/* Reads past the end return 0 and raise EOF, not an error. */
static void sec_eof(void)
{
    section("eof");

    FILE *f = fopen(RO_PATH, "r");
    CHECK(f != NULL, "reopen for EOF tests");
    if (!f) return;

    char buf[32];

    CHECK_EQ(fseek(f, 0, SEEK_END), 0, "seek to end");
    CHECK_EQ(feof(f), 0, "seeking to end alone does not set feof");

    CHECK_EQ(fread(buf, 1, sizeof(buf), f), 0, "fread at EOF returns 0");
    CHECK(feof(f) != 0, "feof set after a read at EOF");
    CHECK_EQ(ferror(f), 0, "EOF is not reported as an error");
    CHECK_EQ(fgetc(f), EOF, "fgetc at EOF returns EOF");

    /* rewind clears the flag and reading works again. */
    rewind(f);
    CHECK_EQ(feof(f), 0, "rewind clears feof");
    CHECK_EQ(fread(buf, 1, 1, f), 1, "reads resume after rewind");

    /* A seek beyond the end is legal; the read there still returns 0. */
    CHECK_EQ(fseek(f, ro_size + 100, SEEK_SET), 0, "seek past the end is accepted");
    CHECK_EQ(fread(buf, 1, sizeof(buf), f), 0, "read past the end returns 0");
    CHECK(feof(f) != 0, "feof set after reading past the end");

    fclose(f);
}

/* "w" creates a file, and truncates it on the second open. */
static void sec_write(void)
{
    section("write");

    const char *first = "first generation payload";

    CHECK_EQ(put_file("w", first), 0, "fopen(w) creates and writes " TMP_PATH);

    char buf[128];
    long n = get_file(buf, sizeof(buf));
    CHECK_EQ(n, (long)strlen(first), "read-back length matches what was written");
    CHECK(n >= 0 && strcmp(buf, first) == 0, "read-back contents match");

    /* Reopening "w" must truncate: the shorter payload may not leave a
     * tail of the longer one behind. */
    const char *second = "short";
    CHECK_EQ(put_file("w", second), 0, "second fopen(w) succeeds");

    n = get_file(buf, sizeof(buf));
    CHECK_EQ(n, (long)strlen(second), "fopen(w) truncated the existing file");
    CHECK(n >= 0 && strcmp(buf, second) == 0, "no tail of the old contents survives");

    /* Zero-length write leaves the file empty rather than failing. */
    CHECK_EQ(put_file("w", ""), 0, "fopen(w) with no data succeeds");
    CHECK_EQ(get_file(buf, sizeof(buf)), 0, "truncate-to-empty leaves 0 bytes");
}

/* "a" appends rather than truncating, and creates when absent. */
static void sec_append(void)
{
    section("append");

    CHECK_EQ(put_file("w", "base"), 0, "seed the file with fopen(w)");
    CHECK_EQ(put_file("a", "+more"), 0, "fopen(a) succeeds");

    char buf[128];
    long n = get_file(buf, sizeof(buf));
    CHECK_EQ(n, 9, "appended file is base+more long");
    CHECK(n >= 0 && strcmp(buf, "base+more") == 0, "append landed at the end");

    CHECK_EQ(put_file("a", "!"), 0, "second fopen(a) succeeds");
    n = get_file(buf, sizeof(buf));
    CHECK(n >= 0 && strcmp(buf, "base+more!") == 0, "appends accumulate");

    /* An append to an empty file behaves like a plain write. */
    CHECK_EQ(put_file("w", ""), 0, "truncate before the empty-append case");
    CHECK_EQ(put_file("a", "fresh"), 0, "fopen(a) on an empty file");
    n = get_file(buf, sizeof(buf));
    CHECK(n >= 0 && strcmp(buf, "fresh") == 0, "append to an empty file writes at 0");
}

/* fstat: file type and size, for both a regular file and the console. */
static void sec_stat(void)
{
    section("stat");

    const char *payload = "0123456789";   /* 10 bytes, size is easy to assert */
    CHECK_EQ(put_file("w", payload), 0, "write a known-length file");

    FILE *f = fopen(TMP_PATH, "r");
    CHECK(f != NULL, "reopen the known-length file");
    if (!f) return;

    struct stat st;
    memset(&st, 0xAA, sizeof(st));
    CHECK_EQ(fstat(fileno(f), &st), 0, "fstat on an open file descriptor");
    CHECK(S_ISREG(st.st_mode), "fstat reports S_IFREG");
    CHECK(!S_ISDIR(st.st_mode), "fstat does not report S_IFDIR");
    CHECK_EQ(st.st_size, (long)strlen(payload), "st_size matches the bytes written");

    fclose(f);

    /* stdout is a character device -- that is what makes newlib
     * line-buffer it, so a regression here changes console behaviour. */
    memset(&st, 0xAA, sizeof(st));
    CHECK_EQ(fstat(1, &st), 0, "fstat on stdout");
    CHECK(S_ISCHR(st.st_mode), "stdout reports S_IFCHR");
    CHECK_EQ(isatty(1), 1, "isatty(stdout) is true");

    /* Size tracks growth: append and re-stat. */
    CHECK_EQ(put_file("a", "abcde"), 0, "append 5 more bytes");
    f = fopen(TMP_PATH, "r");
    CHECK(f != NULL, "reopen after the append");
    if (!f) return;
    CHECK_EQ(fstat(fileno(f), &st), 0, "fstat after the append");
    CHECK_EQ(st.st_size, 15, "st_size grew by the appended bytes");
    fclose(f);
}

/* fprintf out, fscanf back in. */
static void sec_stdio(void)
{
    section("stdio");

    FILE *f = fopen(TMP_PATH, "w");
    CHECK(f != NULL, "fopen(w) for the fprintf round-trip");
    if (!f) return;

    int wrote = fprintf(f, "%d %s %x\n", 4242, "olives", 0xbeef);
    CHECK(wrote > 0, "fprintf reports bytes written");
    CHECK_EQ(fclose(f), 0, "fclose flushes the stream");

    f = fopen(TMP_PATH, "r");
    CHECK(f != NULL, "reopen for fscanf");
    if (!f) return;

    int n = 0;
    unsigned x = 0;
    char word[32] = { 0 };
    int got = fscanf(f, "%d %31s %x", &n, word, &x);
    CHECK_EQ(got, 3, "fscanf converts all three fields");
    CHECK_EQ(n, 4242, "int field round-trips");
    CHECK(strcmp(word, "olives") == 0, "string field round-trips");
    CHECK_EQ(x, 0xbeef, "hex field round-trips");

    /* Line-oriented path: fgets should see the trailing newline. */
    rewind(f);
    char line[64];
    CHECK(fgets(line, sizeof(line), f) == line, "fgets reads the line back");
    CHECK(strchr(line, '\n') != NULL, "fgets kept the trailing newline");
    CHECK(fgets(line, sizeof(line), f) == NULL, "fgets returns NULL at EOF");

    fclose(f);
}

/* Error reporting: missing files, bad descriptors. */
static void sec_errors(void)
{
    section("errors");

    errno = 0;
    FILE *f = fopen("/home/definitely_not_here.txt", "r");
    CHECK(f == NULL, "fopen of a nonexistent file fails");
    CHECK_EQ(errno, ENOENT, "errno is ENOENT");
    if (f) fclose(f);

    errno = 0;
    f = fopen("/no_such_dir/file.txt", "r");
    CHECK(f == NULL, "fopen under a nonexistent directory fails");
    if (f) fclose(f);

    /* Reading a write-only stream and writing a read-only one must fail
     * rather than silently doing the wrong thing. */
    CHECK_EQ(put_file("w", "content"), 0, "seed a file for the mode checks");

    f = fopen(TMP_PATH, "r");
    CHECK(f != NULL, "open read-only");
    if (f) {
        CHECK(fwrite("x", 1, 1, f) == 0 || ferror(f) != 0,
              "write to a read-only stream fails");
        fclose(f);
    }

    errno = 0;
    CHECK_EQ(close(999), -1, "close of an out-of-range fd fails");
    CHECK_EQ(errno, EBADF, "errno is EBADF for a bad descriptor");

    struct stat st;
    errno = 0;
    CHECK_EQ(fstat(999, &st), -1, "fstat of an out-of-range fd fails");
    CHECK_EQ(errno, EBADF, "errno is EBADF for a bad fstat");
}

/* The server-side per-client cap must refuse cleanly, not corrupt state. */
static void sec_fdcap(void)
{
    section("fdcap");

    /* Try well past the cap; every open beyond it should fail the same way. */
    const int attempts = FSD_MAX_FILES_PER_CLIENT + 4;
    FILE *fs[FSD_MAX_FILES_PER_CLIENT + 4];
    int opened = 0, first_fail = -1, first_errno = 0;

    for (int i = 0; i < attempts; i++) {
        errno = 0;
        fs[i] = fopen(RO_PATH, "r");
        if (fs[i]) {
            opened++;
        } else {
            if (first_fail < 0) { first_fail = i; first_errno = errno; }
        }
    }

    CHECK_EQ(opened, FSD_MAX_FILES_PER_CLIENT,
             "exactly FSD_MAX_FILES_PER_CLIENT opens succeed");
    CHECK_EQ(first_fail, FSD_MAX_FILES_PER_CLIENT,
             "the first refusal is the one past the cap");
    CHECK_EQ(first_errno, EMFILE, "the cap surfaces as EMFILE");

    /* The handles handed out before the cap must still be usable -- a
     * refused open may not disturb the descriptors already open. */
    int readable = 0;
    for (int i = 0; i < attempts; i++) {
        if (!fs[i]) continue;
        char c;
        if (fread(&c, 1, 1, fs[i]) == 1 && c == 'H') readable++;
    }
    CHECK_EQ(readable, opened, "every descriptor open before the cap still reads");

    /* Closing releases server-side slots. */
    int closed_ok = 1;
    for (int i = 0; i < attempts; i++)
        if (fs[i] && fclose(fs[i]) != 0) closed_ok = 0;
    CHECK(closed_ok, "all descriptors close cleanly");

    FILE *again = fopen(RO_PATH, "r");
    CHECK(again != NULL, "a fresh open succeeds once the slots are released");
    if (again) fclose(again);
}

/* ---------------- main ---------------- */

int main(void)
{
    printf("filetest: newlib stdio over fsd\n");

    /* Everything downstream assumes the SD card is mounted and the
     * shipped read fixture is reachable; fail loudly rather than
     * reporting a wall of unrelated assertion failures. */
    FILE *probe = fopen(RO_PATH, "r");
    if (!probe) {
        printf("FATAL: cannot open %s (errno %d) -- is the SD card mounted?\n",
               RO_PATH, errno);
        return 127;
    }
    fclose(probe);

    sec_open();
    sec_seek();
    sec_eof();
    sec_write();
    sec_append();
    sec_stat();
    sec_stdio();
    sec_errors();
    sec_fdcap();

    int total_pass = 0, total_fail = 0;
    printf("\n===== filetest summary =====\n");
    printf("%-10s %6s %6s\n", "section", "pass", "fail");
    for (int i = 0; i <= cur_sec; i++) {
        printf("%-10s %6d %6d\n", sections[i].name, sections[i].pass, sections[i].fail);
        total_pass += sections[i].pass;
        total_fail += sections[i].fail;
    }
    printf("%-10s %6d %6d\n", "TOTAL", total_pass, total_fail);
    printf("%s (%d failures)\n", total_fail ? "FAILED" : "ALL PASS", total_fail);
    return total_fail;
}
