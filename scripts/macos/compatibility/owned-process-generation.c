#include <errno.h>
#include <inttypes.h>
#include <libproc.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/proc.h>
#include <sys/proc_info.h>

int main(int argc, char **argv)
{
    struct proc_bsdinfo info;
    char *end;
    long pid;
    int size;

    if (argc != 2) return 2;
    errno = 0;
    pid = strtol(argv[1], &end, 10);
    if (errno || *end || pid <= 0 || pid > 0x7fffffff) return 2;
    size = proc_pidinfo((int)pid, PROC_PIDTBSDINFO, 0, &info, sizeof(info));
    if (!size && errno == ESRCH) return 1;
    if (size != sizeof(info) || info.pbi_pid != (unsigned)pid) {
        fprintf(stderr, "generation query failed: pid=%ld bytes=%d errno=%d\n", pid, size, errno);
        return 2;
    }
    if (info.pbi_status == SZOMB) return 1;
    printf("%u:%" PRIu64 ":%" PRIu64 "\n",
           info.pbi_pid, info.pbi_start_tvsec, info.pbi_start_tvusec);
    return 0;
}
