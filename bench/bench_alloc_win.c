/* DBYTEMEM R3 - fixed batch lifetime allocator measurement. CC0-1.0 */
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include "../src/arena_win.h"

#define DBM_BLOCKS 2048u
#define DBM_BYTES 64u
#define DBM_BATCHES 128u
#define DBM_REPEATS 5u

typedef struct measurement {
    double elapsed_seconds;
    uint64_t checksum;
} measurement;

static void *slots[DBM_BLOCKS];

static int timer_start(LARGE_INTEGER *start, LARGE_INTEGER *frequency)
{
    return QueryPerformanceFrequency(frequency) && frequency->QuadPart > 0 &&
           QueryPerformanceCounter(start);
}

static int timer_stop(LARGE_INTEGER start, LARGE_INTEGER frequency,
                      double *seconds)
{
    LARGE_INTEGER end;
    if (!QueryPerformanceCounter(&end) || end.QuadPart < start.QuadPart)
        return 0;
    *seconds = (double)(end.QuadPart - start.QuadPart) /
               (double)frequency.QuadPart;
    return 1;
}

static int bench_heap(measurement *out)
{
    LARGE_INTEGER start, freq;
    HANDLE heap = GetProcessHeap();
    uint64_t sum = 0;
    unsigned int batch, i;
    if (!heap || !timer_start(&start, &freq)) return 0;
    for (batch = 0; batch < DBM_BATCHES; ++batch) {
        for (i = 0; i < DBM_BLOCKS; ++i) {
            unsigned char *p = (unsigned char *)HeapAlloc(heap, 0, DBM_BYTES);
            if (!p) return 0;
            slots[i] = p;
            p[0] = (unsigned char)(i & 255u);
            p[DBM_BYTES - 1u] = (unsigned char)(batch & 255u);
            sum += (uint64_t)p[0] + (uint64_t)p[DBM_BYTES - 1u];
        }
        for (i = DBM_BLOCKS; i-- > 0;) {
            if (!HeapFree(heap, 0, slots[i])) return 0;
        }
    }
    if (!timer_stop(start, freq, &out->elapsed_seconds)) return 0;
    out->checksum = sum;
    return 1;
}

static int bench_vm(measurement *out)
{
    dbm_vm_arena a;
    LARGE_INTEGER start, freq;
    uint64_t sum = 0;
    unsigned int batch, i;
    int ok = 0;
    if (!dbm_vm_init(&a, (size_t)DBM_BLOCKS * DBM_BYTES)) return 0;
    if (!timer_start(&start, &freq)) goto done;
    for (batch = 0; batch < DBM_BATCHES; ++batch) {
        for (i = 0; i < DBM_BLOCKS; ++i) {
            unsigned char *p = (unsigned char *)dbm_vm_alloc(&a, DBM_BYTES, 16);
            if (!p) goto done;
            p[0] = (unsigned char)(i & 255u);
            p[DBM_BYTES - 1u] = (unsigned char)(batch & 255u);
            sum += (uint64_t)p[0] + (uint64_t)p[DBM_BYTES - 1u];
        }
        if (!dbm_vm_reset(&a)) goto done;
    }
    if (!timer_stop(start, freq, &out->elapsed_seconds)) goto done;
    out->checksum = sum;
    ok = 1;
done:
    if (!dbm_vm_destroy(&a)) return 0;
    return ok;
}

static double median(double *items)
{
    unsigned int i, j;
    for (i = 1; i < DBM_REPEATS; ++i) {
        double key = items[i];
        j = i;
        while (j && items[j - 1u] > key) {
            items[j] = items[j - 1u];
            --j;
        }
        items[j] = key;
    }
    return items[DBM_REPEATS / 2u];
}

int main(void)
{
    double heap_times[DBM_REPEATS], vm_times[DBM_REPEATS];
    const double operations = (double)DBM_BLOCKS * DBM_BATCHES;
    uint64_t expected = 0;
    unsigned int round, batch, i;
    measurement m;
    /* independent expected checksum for two workloads with identical writes */
    for (batch = 0; batch < DBM_BATCHES; ++batch) {
        for (i = 0; i < DBM_BLOCKS; ++i) {
            expected += (uint64_t)(i & 255u) + (uint64_t)(batch & 255u);
        }
    }
    for (round = 0; round < DBM_REPEATS; ++round) {
        /* swap order each round to reduce first-run bias */
        if (!(round & 1u)) {
            if (!bench_heap(&m) || m.checksum != expected) return 2;
            heap_times[round] = m.elapsed_seconds;
            if (!bench_vm(&m) || m.checksum != expected) return 3;
            vm_times[round] = m.elapsed_seconds;
        } else {
            if (!bench_vm(&m) || m.checksum != expected) return 3;
            vm_times[round] = m.elapsed_seconds;
            if (!bench_heap(&m) || m.checksum != expected) return 2;
            heap_times[round] = m.elapsed_seconds;
        }
    }
    puts("benchmark=batch-lifetime");
    printf("block_bytes=%u\nblocks_per_batch=%u\nbatches_per_run=%u\nrepeats=%u\n",
           DBM_BYTES, DBM_BLOCKS, DBM_BATCHES, DBM_REPEATS);
    printf("max_live_payload_bytes=%u\n", DBM_BYTES * DBM_BLOCKS);
    printf("operations_per_run=%.0f\n", operations);
    printf("heap_median_ns_per_block=%.3f\n", median(heap_times) * 1e9 / operations);
    printf("arena_vm_median_ns_per_block=%.3f\n", median(vm_times) * 1e9 / operations);
    printf("expected_checksum=%llu\n", (unsigned long long)expected);
    puts("result=measured_no_general_speed_claim");
    return 0;
}
