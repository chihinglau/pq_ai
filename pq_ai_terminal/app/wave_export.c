/**
 * @file wave_export.c
 * @brief 波形数据导出工具 - 采集T536实时波形并导出为CSV
 *
 * 用法: wave_export [options]
 *   --cycles N     采集周期数（默认5）
 *   --output FILE  输出CSV文件名（默认wave_data.csv）
 *   --interval MS  采集间隔毫秒（默认20）
 *   --help         显示帮助
 */

#include "pq_common.h"
#include "pq_hal.h"
#include "wave_sampler_hal.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>

/* 通道名称 */
static const char *channel_names[] = {"UA", "UB", "UC", "IA", "IB", "IC", "IZ"};

/* 导出单周波数据到CSV */
static int export_wave_to_csv(FILE *fp, const ws_wave_cycle_t *cycle, int cycle_idx)
{
    int ch, pt;
    
    /* 写入周波头信息 */
    fprintf(fp, "# Cycle %d: seq=%u, time=%04d-%02d-%02d %02d:%02d:%02d.%03d\n",
            cycle_idx,
            cycle->cycle_seq,
            cycle->timestamp.year, cycle->timestamp.month, cycle->timestamp.day,
            cycle->timestamp.hour, cycle->timestamp.minute, cycle->timestamp.second,
            cycle->timestamp.millisecond);
    
    /* 写入表头 */
    fprintf(fp, "point,UA,UB,UC,IA,IB,IC,IZ\n");
    
    /* 写入每个采样点的数据 */
    for (pt = 0; pt < WS_POINTS_PER_CYCLE; pt++) {
        fprintf(fp, "%d", pt);
        for (ch = 0; ch < WS_CHANNEL_COUNT; ch++) {
            fprintf(fp, ",%.6f", cycle->data[ch][pt]);
        }
        fprintf(fp, "\n");
    }
    fprintf(fp, "\n");
    
    return 0;
}

/* 导出监测量数据 */
static int export_monitor_to_csv(FILE *fp, const ws_real_monitor_t *monitor, int cycle_idx)
{
    int i;
    
    fprintf(fp, "# Monitor %d: seq=%u, time=%04d-%02d-%02d %02d:%02d:%02d.%03d\n",
            cycle_idx,
            monitor->mon_seq,
            monitor->timestamp.year, monitor->timestamp.month, monitor->timestamp.day,
            monitor->timestamp.hour, monitor->timestamp.minute, monitor->timestamp.second,
            monitor->timestamp.millisecond);
    
    /* 电压/电流RMS */
    fprintf(fp, "UA_RMS,%.3f\n", monitor->val[WS_MON_F_UA]);
    fprintf(fp, "UB_RMS,%.3f\n", monitor->val[WS_MON_F_UB]);
    fprintf(fp, "UC_RMS,%.3f\n", monitor->val[WS_MON_F_UC]);
    fprintf(fp, "IA_RMS,%.3f\n", monitor->val[WS_MON_F_IA]);
    fprintf(fp, "IB_RMS,%.3f\n", monitor->val[WS_MON_F_IB]);
    fprintf(fp, "IC_RMS,%.3f\n", monitor->val[WS_MON_F_IC]);
    fprintf(fp, "IZ_RMS,%.3f\n", monitor->val[WS_MON_F_IZ]);
    
    /* 频率 */
    fprintf(fp, "Frequency_Hz,%.3f\n", monitor->val[WS_MON_F_FREQ]);
    
    /* 功率 */
    fprintf(fp, "PA_A_kW,%.3f\n", monitor->val[WS_MON_P_UA]);
    fprintf(fp, "PA_B_kW,%.3f\n", monitor->val[WS_MON_P_UB]);
    fprintf(fp, "PA_C_kW,%.3f\n", monitor->val[WS_MON_P_UC]);
    fprintf(fp, "QA_A_kvar,%.3f\n", monitor->val[WS_MON_Q_UA]);
    fprintf(fp, "QA_B_kvar,%.3f\n", monitor->val[WS_MON_Q_UB]);
    fprintf(fp, "QA_C_kvar,%.3f\n", monitor->val[WS_MON_Q_UC]);
    fprintf(fp, "\n");
    
    return 0;
}

/* 打印波形统计信息 */
static void print_wave_stats(const ws_wave_cycle_t *cycle)
{
    int ch, pt;
    float min_val, max_val, rms, sum_sq;
    
    printf("  Wave Stats (Seq=%u):\n", cycle->cycle_seq);
    for (ch = 0; ch < WS_CHANNEL_COUNT; ch++) {
        min_val = cycle->data[ch][0];
        max_val = cycle->data[ch][0];
        sum_sq = 0;
        
        for (pt = 0; pt < WS_POINTS_PER_CYCLE; pt++) {
            if (cycle->data[ch][pt] < min_val) min_val = cycle->data[ch][pt];
            if (cycle->data[ch][pt] > max_val) max_val = cycle->data[ch][pt];
            sum_sq += cycle->data[ch][pt] * cycle->data[ch][pt];
        }
        rms = sqrtf(sum_sq / WS_POINTS_PER_CYCLE);
        
        printf("    %s: min=%10.3f  max=%10.3f  rms=%10.3f\n",
               channel_names[ch], min_val, max_val, rms);
    }
}

int main(int argc, char *argv[])
{
    int max_cycles = 5;
    int interval_ms = 20;
    const char *output_file = "wave_data.csv";
    int i, ret;
    uint32_t actual_cycles;
    
    ws_wave_cycle_t wave_cycles[20];
    ws_real_monitor_t monitor;
    
    FILE *fp;
    
    /* 解析命令行参数 */
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--cycles") == 0 && i + 1 < argc) {
            max_cycles = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--output") == 0 && i + 1 < argc) {
            output_file = argv[++i];
        } else if (strcmp(argv[i], "--interval") == 0 && i + 1 < argc) {
            interval_ms = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: wave_export [options]\n");
            printf("Options:\n");
            printf("  --cycles N     Number of cycles to capture (default: 5)\n");
            printf("  --output FILE  Output CSV file (default: wave_data.csv)\n");
            printf("  --interval MS  Interval between captures in ms (default: 20)\n");
            printf("  --help         Show this help\n");
            return 0;
        }
    }
    
    printf("\n");
    printf("============================================================\n");
    printf("  T536 Wave Data Export Tool\n");
    printf("  Target: HT7627S Real Hardware\n");
    printf("============================================================\n");
    printf("\n");
    printf("  Cycles to capture : %d\n", max_cycles);
    printf("  Output file       : %s\n", output_file);
    printf("  Interval          : %d ms\n", interval_ms);
    printf("\n");
    
    /* 初始化 */
    printf("[INIT] Initializing waveform sampler...\n");
    ret = ws_init();
    if (ret != 0) {
        printf("[ERROR] Failed to initialize waveform sampler\n");
        return 1;
    }
    printf("[INIT] Initialized successfully!\n\n");
    
    /* 打开输出文件 */
    fp = fopen(output_file, "w");
    if (fp == NULL) {
        printf("[ERROR] Failed to open output file: %s\n", output_file);
        ws_deinit();
        return 1;
    }
    
    /* 写入文件头 */
    fprintf(fp, "# T536 Waveform Data Export\n");
    fprintf(fp, "# Channels: UA, UB, UC, IA, IB, IC, IZ\n");
    fprintf(fp, "# Points per cycle: %d\n", WS_POINTS_PER_CYCLE);
    fprintf(fp, "# Sample rate: 12800 Hz\n");
    fprintf(fp, "# Export time: %s\n", __DATE__ " " __TIME__);
    fprintf(fp, "#\n\n");
    
    /* 采集循环 */
    printf("[CAPTURE] Starting wave acquisition...\n\n");
    
    for (i = 0; i < max_cycles; i++) {
        printf("[CAPTURE] Cycle %d/%d...\n", i + 1, max_cycles);
        
        /* 读取波形数据 */
        ret = ws_read_waveforms(wave_cycles, 1, &actual_cycles);
        if (ret != 0 || actual_cycles == 0) {
            printf("  [WARN] Failed to read waveform, retrying...\n");
            hal_sleep_ms(100);
            i--;
            continue;
        }
        
        /* 读取监测量 */
        ws_read_real_monitors(&monitor, 1, &actual_cycles);
        
        /* 导出到CSV */
        export_wave_to_csv(fp, &wave_cycles[actual_cycles - 1], i);
        export_monitor_to_csv(fp, &monitor, i);
        
        /* 打印统计 */
        print_wave_stats(&wave_cycles[actual_cycles - 1]);
        printf("\n");
        
        hal_sleep_ms(interval_ms);
    }
    
    fclose(fp);
    
    /* 清理 */
    ws_deinit();
    
    printf("[DONE] Export completed!\n");
    printf("[DONE] Data saved to: %s\n", output_file);
    printf("[DONE] Total cycles: %d\n", max_cycles);
    printf("[DONE] Total data points: %d × %d × %d = %d floats\n",
           max_cycles, WS_CHANNEL_COUNT, WS_POINTS_PER_CYCLE,
           max_cycles * WS_CHANNEL_COUNT * WS_POINTS_PER_CYCLE);
    printf("\n");
    
    return 0;
}
