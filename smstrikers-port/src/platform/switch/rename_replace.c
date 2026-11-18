// POSIX rename() replaces an existing file; Horizon's refuses to. Linked with -Wl,--wrap=rename, so
// every caller gets the POSIX behaviour: the port, SDL's SDL_RenamePath, std::filesystem and SQLite.
//
// Aurora saves the memory card by writing a temporary file and renaming it over the .gci. Without
// this, that rename fails on the Switch, each card write returns CARD_RESULT_IOERROR, and the .gci
// keeps the zeroes it was created with, which the next load reports as a corrupted file.
//
// No libnx headers, so tests/switch_rename_test.c can build this on the host.

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int __real_rename(const char* from, const char* to);

static int is_regular_file(const char* path)
{
    struct stat info;
    return stat(path, &info) == 0 && S_ISREG(info.st_mode);
}

int __wrap_rename(const char* from, const char* to)
{
    if (__real_rename(from, to) == 0)
        return 0;

    // libnx reports the refusal as whatever its directory retry returned, so the error code does
    // not say why; look at the paths instead. Only a file replacing a file is handled here.
    const int refused = errno;
    if (from == NULL || to == NULL || strcmp(from, to) == 0 || !is_regular_file(from) ||
        !is_regular_file(to))
    {
        errno = refused;
        return -1;
    }

    // Move the old file aside instead of deleting it first, so that power lost between the two
    // steps leaves the previous contents on the card as '<name>.old' rather than nothing.
    char aside[1024];
    const int length = snprintf(aside, sizeof aside, "%s.old", to);
    const int have_aside = length > 0 && (size_t)length < sizeof aside;
    int moved_aside = 0;
    if (have_aside)
    {
        unlink(aside);
        moved_aside = __real_rename(to, aside) == 0;
    }
    if (!moved_aside && unlink(to) != 0)
    {
        errno = refused;
        return -1;
    }

    if (__real_rename(from, to) != 0)
    {
        const int failed = errno;
        if (moved_aside)
            __real_rename(aside, to);
        errno = failed;
        return -1;
    }

    if (moved_aside)
        unlink(aside);
    return 0;
}
