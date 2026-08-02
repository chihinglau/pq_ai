/**
 * @file time_sync.c
 * @brief 时间同步实现 (Windows模拟)
 */

#include "time_sync.h"

#if defined(PLATFORM_WINDOWS) || defined(_WIN32)
    #include <windows.h>
#else
    #include <sys/time.h>
    #include <time.h>
#endif

static const char *g_ntp_server = "pool.ntp.org";

int time_sync_init(const char *ntp_server)
{
    if (ntp_server) {
        g_ntp_server = ntp_server;
    }
    return 0;
}

uint64_t time_sync_get_unix_ms(void)
{
#if defined(PLATFORM_WINDOWS) || defined(_WIN32)
    FILETIME ft;
    ULARGE_INTEGER ull;
    GetSystemTimeAsFileTime(&ft);
    ull.LowPart = ft.dwLowDateTime;
    ull.HighPart = ft.dwHighDateTime;
    /* 100-nanosecond intervals since Jan 1, 1601 -> ms since Jan 1, 1970 */
    return (ull.QuadPart / 10000ULL) - 11644473600000ULL;
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000ULL + (uint64_t)tv.tv_usec / 1000ULL;
#endif
}

int time_sync_ntp_sync(void)
{
    PQ_LOGI("NTP sync stub: server=%s", g_ntp_server);
    return 0;
}
