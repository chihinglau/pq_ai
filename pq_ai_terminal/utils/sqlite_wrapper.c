/**
 * @file sqlite_wrapper.c
 * @brief 数据持久化实现 (CSV模拟)
 */

#include "sqlite_wrapper.h"
#include <stdio.h>
#include <time.h>

static FILE *g_fp_metrics = NULL;
static FILE *g_fp_events = NULL;
static int g_db_init = 0;

int db_init(const char *path)
{
    char fname[256];
    if (path == NULL) path = "data";

    /* 创建目录尝试（Windows下简化为当前目录） */
    (void)path;

    snprintf(fname, sizeof(fname), "pq_metrics.csv");
    g_fp_metrics = fopen(fname, "w");
    if (g_fp_metrics) {
        fprintf(g_fp_metrics,
            "timestamp,voltage_deviation,voltage_thd,current_thd,"
            "voltage_unbalance,frequency_deviation,transformer_load,"
            "line_load,power_factor,active_power,reactive_power,apparent_power\n");
        fflush(g_fp_metrics);
    }

    snprintf(fname, sizeof(fname), "pq_events.csv");
    g_fp_events = fopen(fname, "w");
    if (g_fp_events) {
        fprintf(g_fp_events, "id,type,start_ts,severity,description\n");
        fflush(g_fp_events);
    }

    g_db_init = 1;
    PQ_LOGI("DB init: metrics=%s events=%s", g_fp_metrics ? "OK" : "FAIL", g_fp_events ? "OK" : "FAIL");
    return 0;
}

int db_save_metrics(const pq_metrics_t *metrics)
{
    if (!g_db_init || g_fp_metrics == NULL || metrics == NULL) return -1;
    fprintf(g_fp_metrics,
        "%lu,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f\n",
        (unsigned long)time(NULL),
        metrics->voltage_deviation.value,
        metrics->voltage_thd.value,
        metrics->current_thd.value,
        metrics->voltage_unbalance.value,
        metrics->frequency_deviation.value,
        metrics->transformer_load.value,
        metrics->line_load.value,
        metrics->power_factor.value,
        metrics->active_power.value,
        metrics->reactive_power.value,
        metrics->apparent_power.value);
    fflush(g_fp_metrics);
    return 0;
}

int db_save_event(const pq_event_t *event)
{
    if (!g_db_init || g_fp_events == NULL || event == NULL) return -1;
    fprintf(g_fp_events, "%lu,%d,%lu,%.2f,\"%s\"\n",
        (unsigned long)event->id, event->type,
        (unsigned long)event->start_ts, event->severity, event->description);
    fflush(g_fp_events);
    return 0;
}

int db_close(void)
{
    if (g_fp_metrics) { fclose(g_fp_metrics); g_fp_metrics = NULL; }
    if (g_fp_events) { fclose(g_fp_events); g_fp_events = NULL; }
    g_db_init = 0;
    return 0;
}
