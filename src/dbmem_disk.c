/* DBYTEMEM R5 - read-only fixed drive capacity probe. CC0-1.0 */
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include "disk_policy.h"

static const char *rank_name(int rank)
{
    if (rank == 2) return "CRITICAL";
    if (rank == 1) return "WARN";
    return "OK";
}

int main(int argc, char **argv)
{
    dbm_disk_opts o;
    DWORD present;
    uint32_t selected;
    unsigned int index;
    int worst = 0, scanned = 0;
    if (argc == 2 && !strcmp(argv[1], "--help")) {
        puts("usage: dbmem_disk.exe [--drive C] [--drive D] [--warn-percent 15] [--critical-percent 10] [--critical-free-gib 5] [--csv]");
        puts("read only. by default inspect every drive letter with DRIVE_FIXED. mounted folders without drive letters are not enumerated.");
        puts("exit codes: 0 OK, 1 invalid options, 2 system/drive error, 3 WARN, 4 CRITICAL");
        return 0;
    }
    if (!dbm_disk_parse(argc, argv, &o)) {
        fputs("invalid_options\n", stderr);
        return 1;
    }
    present = GetLogicalDrives();
    if (present == 0u) {
        fprintf(stderr, "GetLogicalDrives_error=%lu\n",
                (unsigned long)GetLastError());
        return 2;
    }
    selected = o.drives ? o.drives : (uint32_t)present;
    if (o.drives && (o.drives & ~(uint32_t)present)) {
        fputs("selected_drive_not_mounted\n", stderr);
        return 2;
    }
    if (o.csv) {
        puts("drive,status,total_bytes_for_caller,free_bytes_for_caller,volume_free_bytes,free_percent");
    }
    for (index = 0; index < 26u; ++index) {
        wchar_t root[] = L"A:\\";
        ULARGE_INTEGER caller_free, caller_total, volume_free;
        UINT kind;
        int rank;
        double percent;
        if (!(selected & (UINT32_C(1) << index))) continue;
        root[0] = (wchar_t)(L'A' + index);
        kind = GetDriveTypeW(root);
        if (kind != DRIVE_FIXED) {
            if (o.drives) {
                fprintf(stderr, "drive_not_fixed=%c:\n", (char)('A' + index));
                return 2;
            }
            continue;
        }
        if (!GetDiskFreeSpaceExW(root, &caller_free, &caller_total,
                                 &volume_free)) {
            fprintf(stderr, "GetDiskFreeSpaceExW_error=%lu drive=%c:\n",
                    (unsigned long)GetLastError(), (char)('A' + index));
            return 2;
        }
        rank = dbm_disk_rank((uint64_t)caller_free.QuadPart,
                             (uint64_t)caller_total.QuadPart, &o);
        if (rank < 0) {
            fprintf(stderr, "invalid_disk_metrics=%c:\n",
                    (char)('A' + index));
            return 2;
        }
        percent = 100.0 * (double)caller_free.QuadPart /
                  (double)caller_total.QuadPart;
        if (o.csv) {
            printf("%c:,%s,%llu,%llu,%llu,%.2f\n",
                   (char)('A' + index), rank_name(rank),
                   (unsigned long long)caller_total.QuadPart,
                   (unsigned long long)caller_free.QuadPart,
                   (unsigned long long)volume_free.QuadPart, percent);
        } else {
            printf("drive=%c: status=%s total_bytes_for_caller=%llu free_bytes_for_caller=%llu volume_free_bytes=%llu free_percent=%.2f\n",
                   (char)('A' + index), rank_name(rank),
                   (unsigned long long)caller_total.QuadPart,
                   (unsigned long long)caller_free.QuadPart,
                   (unsigned long long)volume_free.QuadPart, percent);
        }
        if (rank > worst) worst = rank;
        ++scanned;
    }
    if (fflush(stdout) != 0) {
        fputs("output_error\n", stderr);
        return 2;
    }
    if (!scanned) {
        fputs("no_fixed_drives\n", stderr);
        return 2;
    }
    return worst == 2 ? 4 : worst == 1 ? 3 : 0;
}
