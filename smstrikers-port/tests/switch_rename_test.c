// The Switch's rename wrapper, against a rename that refuses to replace a file as Horizon's does.
#undef NDEBUG
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../src/platform/switch/rename_replace.c"

static int real_calls;
static int fail_real_call; // the nth call fails outright, counting from 1; 0 for none

// Horizon: the target must not exist. libnx then retries as a directory and returns that error.
int __real_rename(const char* from, const char* to)
{
    struct stat info;
    real_calls++;
    if (real_calls == fail_real_call)
    {
        errno = EIO;
        return -1;
    }
    if (stat(to, &info) == 0)
    {
        errno = ENOENT;
        return -1;
    }
    if (link(from, to) != 0)
        return -1;
    return unlink(from);
}

static void put(const char* path, const char* text)
{
    FILE* file = fopen(path, "wb");
    assert(file != NULL);
    assert(fputs(text, file) >= 0);
    assert(fclose(file) == 0);
}

static void expect(const char* path, const char* text)
{
    char buffer[64] = { 0 };
    FILE* file = fopen(path, "rb");
    assert(file != NULL);
    fread(buffer, 1, sizeof buffer - 1, file);
    fclose(file);
    assert(strcmp(buffer, text) == 0);
}

static int exists(const char* path) { return access(path, F_OK) == 0; }

int main(void)
{
    char dir[] = "/tmp/switch-rename-XXXXXX";
    assert(mkdtemp(dir) != NULL);
    assert(chdir(dir) == 0);

    // A new name: one call, as before.
    put("tmp", "first");
    real_calls = 0;
    assert(__wrap_rename("tmp", "save.gci") == 0);
    assert(real_calls == 1);
    expect("save.gci", "first");
    assert(!exists("tmp"));

    // The memory card's case: the temporary file replaces the save.
    put("tmp", "second");
    assert(__wrap_rename("tmp", "save.gci") == 0);
    expect("save.gci", "second");
    assert(!exists("tmp") && !exists("save.gci.old"));

    // A '.old' left by an interrupted replace does not block the next one.
    put("save.gci.old", "stale");
    put("tmp", "third");
    assert(__wrap_rename("tmp", "save.gci") == 0);
    expect("save.gci", "third");
    assert(!exists("save.gci.old"));

    // The new file fails to move in: the old save comes back and the error is reported.
    put("tmp", "fourth");
    real_calls = 0;
    fail_real_call = 3; // refused, moved aside, then this one
    errno = 0;
    assert(__wrap_rename("tmp", "save.gci") == -1);
    assert(errno == EIO);
    fail_real_call = 0;
    expect("save.gci", "third");
    expect("tmp", "fourth");
    assert(!exists("save.gci.old"));

    // The old file can't be moved aside: it is deleted instead, and the replace still happens.
    real_calls = 0;
    fail_real_call = 2;
    assert(__wrap_rename("tmp", "save.gci") == 0);
    fail_real_call = 0;
    expect("save.gci", "fourth");
    assert(!exists("tmp"));

    // A missing source is still an error, and the target is untouched.
    assert(__wrap_rename("missing", "save.gci") == -1);
    expect("save.gci", "fourth");

    // Directories are left to Horizon: a file is never deleted to make room for one.
    assert(mkdir("folder", 0777) == 0);
    assert(__wrap_rename("folder", "save.gci") == -1);
    expect("save.gci", "fourth");
    assert(exists("folder"));

    // Renaming a file onto itself changes nothing.
    assert(__wrap_rename("save.gci", "save.gci") == -1);
    expect("save.gci", "fourth");

    unlink("save.gci");
    rmdir("folder");
    assert(chdir("/") == 0);
    rmdir(dir);
    puts("switch_rename: ok");
    return 0;
}
