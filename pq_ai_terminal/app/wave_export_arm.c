/**
 * @file wave_export_arm.c
 * @brief 波形数据导出工具 - 直接使用HAL接口采集T536实时波形并导出为CSV
 * 
 * 参考项目: /share/apps/zdh_CL818C50_DTAnalyzer
 * 接口参考: GZLB.cpp, hal_device_waveform_sampler.h
 * 
 * 用法: wave_export_arm [options]
 *   --cycles N     采集周期数（默认5）
 *   --output FILE  输出CSV文件名（默认wave_data.csv）
 *   --log FILE     日志文件名（默认wave_export.log）
 *
 * 运行: /lib32/ld-linux-armhf.so.3 --library-path /lib32:/custom/sys/lib/hal_lib/lib32 ./wave_export_arm --cycles 5
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>
#include <unistd.h>
#include <errno.h>

/* 按照参考项目的类型定义 */
typedef long int32;
typedef unsigned long uint32;

/* HAL函数声明 */
struct tag_HW_DEVICE;
int32 hal_init(void);
int32 hal_exit(void);
struct tag_HW_DEVICE *hal_device_get(const char *device_id);
int32 hal_device_release(struct tag_HW_DEVICE *dev);

/* 设备ID */
#define WAVEFORM_SAMPLER_HARDWARE_MODULE_ID "waveform_sampler"

/* 设备结构体 - 按照参考项目的定义 */
struct tag_HW_MODULE;
typedef struct tag_HW_DEVICE {
    struct tag_HW_MODULE *pModule;
    int32 nVer;
    const char *szDeviceID;
} HW_DEVICE;

typedef struct tag_WAVEFORM_SAMPLER_DEVICE {
    HW_DEVICE base;
    int32 (*read_waveform)(struct tag_WAVEFORM_SAMPLER_DEVICE *dev,
                           void *data, int32 dsize, uint32 lastn);
    int32 (*read_real_monitor_data)(struct tag_WAVEFORM_SAMPLER_DEVICE *dev,
                                    void *data, int32 dsize, uint32 lastn);
} WAVEFORM_SAMPLER_DEVICE_T;

/* 波形帧头结构 - 参考GZLB.cpp中的格式 */
typedef struct {
    uint8_t header[2];      /* 0x68 0x36 */
    uint32_t frame_len;     /* 数据段长度 */
    uint32_t frame_seq;     /* 帧序号 */
    uint16_t year;          /* 年 */
    uint8_t month;          /* 月 */
    uint8_t day;            /* 日 */
    uint8_t hour;           /* 时 */
    uint8_t minute;         /* 分 */
    uint8_t second;         /* 秒 */
    uint16_t microsecond;   /* 微秒 */
} wave_frame_header_t;

/* 实时监测量结构 - 参考DTAnalyzerAcStat.cpp */
typedef struct {
    uint32_t mon_seq;       /* 监测量序号 */
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint16_t millisecond;
    float val[14];          /* UA,UB,UC,IA,IB,IC,IZ,频率,有功功率等 */
} real_monitor_t;

/* 通道名称 */
static const char *channel_names[] = {"UA", "UB", "UC", "IA", "IB", "IC", "IZ"};

/* ========== 日志系统 ========== */
static FILE *g_log_fp = NULL;
static int g_log_level = 2;  /* 0=ERROR, 1=WARN, 2=INFO, 3=DEBUG */

#define LOG_ERROR(fmt, ...) do { \
    time_t now = time(NULL); \
    char time_str[64]; \
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", localtime(&now)); \
    printf("[%s] [ERROR] " fmt "\n", time_str, ##__VA_ARGS__); \
    if (g_log_fp) { \
        fprintf(g_log_fp, "[%s] [ERROR] " fmt "\n", time_str, ##__VA_ARGS__); \
        fflush(g_log_fp); \
    } \
} while(0)

#define LOG_WARN(fmt, ...) do { \
    if (g_log_level >= 1) { \
        time_t now = time(NULL); \
        char time_str[64]; \
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", localtime(&now)); \
        printf("[%s] [WARN] " fmt "\n", time_str, ##__VA_ARGS__); \
        if (g_log_fp) { \
            fprintf(g_log_fp, "[%s] [WARN] " fmt "\n", time_str, ##__VA_ARGS__); \
            fflush(g_log_fp); \
        } \
    } \
} while(0)

#define LOG_INFO(fmt, ...) do { \
    if (g_log_level >= 2) { \
        time_t now = time(NULL); \
        char time_str[64]; \
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", localtime(&now)); \
        printf("[%s] [INFO] " fmt "\n", time_str, ##__VA_ARGS__); \
        if (g_log_fp) { \
            fprintf(g_log_fp, "[%s] [INFO] " fmt "\n", time_str, ##__VA_ARGS__); \
            fflush(g_log_fp); \
        } \
    } \
} while(0)

#define LOG_DEBUG(fmt, ...) do { \
    if (g_log_level >= 3) { \
        time_t now = time(NULL); \
        char time_str[64]; \
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", localtime(&now)); \
        printf("[%s] [DEBUG] " fmt "\n", time_str, ##__VA_ARGS__); \
        if (g_log_fp) { \
            fprintf(g_log_fp, "[%s] [DEBUG] " fmt "\n", time_str, ##__VA_ARGS__); \
            fflush(g_log_fp); \
        } \
    } \
} while(0)

/* 小端字节序转换（与wave_sampler_hal.c保持一致）*/
static uint32_t read_le32(const uint8_t *p) {
    return ((uint32_t)p[0]) |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

/* ========== 波形参数常量 ========== */
#define WS_SINGLE_WAVEFORM_SIZE 7182
#define WS_POINTS_PER_CYCLE 256
#define WS_CHANNEL_COUNT 7

/* 解析波形数据帧 */
static int parse_wave_frame(uint8_t *data, int32 data_len, float *channels[7], int *frame_seq) {
    int idx = 0;
    int frame_count = 0;
    
    LOG_DEBUG("parse_wave_frame: data_len=%d", data_len);
    LOG_DEBUG("First 32 bytes of raw data:");
    for (int i = 0; i < 32 && i < data_len; i++) {
        printf("%02X ", data[i]);
        if ((i + 1) % 8 == 0) printf(" ");
    }
    printf("\n");
    
    while (idx < data_len - 1) {
        if (data[idx] == 0x68 && data[idx + 1] == 0x36) {
            LOG_DEBUG("Found frame header at offset %d", idx);
            
            uint32_t frame_len = read_le32(&data[idx + 2]);
            uint8_t frame_seq_byte = data[idx + 6];
            LOG_DEBUG("Frame: len=%u, seq_byte=%u", frame_len, frame_seq_byte);
            
            if (idx + WS_SINGLE_WAVEFORM_SIZE + 7 > data_len) {
                LOG_WARN("Not enough data! Need %d, have %d", idx + WS_SINGLE_WAVEFORM_SIZE + 7, data_len);
                break;
            }
            
            int wave_start = idx + 7;
            *frame_seq = read_le32(&data[wave_start]);
            LOG_DEBUG("Wave start: cycle_seq=%u", *frame_seq);
            
            /* 解析时间戳 */
            uint8_t year_byte = data[wave_start + 4];
            uint8_t month = data[wave_start + 5];
            uint8_t day = data[wave_start + 6];
            uint8_t hour = data[wave_start + 7];
            uint8_t minute = data[wave_start + 8];
            uint8_t second = data[wave_start + 9];
            uint32_t microsecond = read_le32(&data[wave_start + 10]);
            LOG_INFO("Timestamp: %04d-%02d-%02d %02d:%02d:%02d.%04u (cycle_seq=%u)",
                   year_byte + 2000, month, day, hour, minute, second, microsecond / 1000,
                   *frame_seq);
            
            int data_offset = wave_start + 14;
            
            /* 提取7通道数据 */
            for (int ch = 0; ch < WS_CHANNEL_COUNT; ch++) {
                float sample_min = 999999.0f, sample_max = -999999.0f;
                float first_val = 0.0f, mid_val = 0.0f, last_val = 0.0f;
                float sum_sq = 0.0f;
                
                for (int pt = 0; pt < WS_POINTS_PER_CYCLE; pt++) {
                    uint32_t raw_val = read_le32(&data[data_offset]);
                    float float_val;
                    memcpy(&float_val, &raw_val, sizeof(float));
                    channels[ch][pt] = float_val;
                    data_offset += 4;
                    
                    if (pt == 0) first_val = float_val;
                    if (pt == 128) mid_val = float_val;
                    if (pt == 255) last_val = float_val;
                    if (float_val < sample_min) sample_min = float_val;
                    if (float_val > sample_max) sample_max = float_val;
                    sum_sq += float_val * float_val;
                }
                
                float rms = sqrtf(sum_sq / WS_POINTS_PER_CYCLE);
                LOG_INFO("  %s: min=%10.3f max=%10.3f rms=%10.3f range=%10.3f",
                        channel_names[ch], sample_min, sample_max, rms, sample_max - sample_min);
                LOG_DEBUG("    [0]=%.6f [128]=%.6f [255]=%.6f", first_val, mid_val, last_val);
            }
            
            frame_count++;
            idx += WS_SINGLE_WAVEFORM_SIZE + 7;
        } else {
            idx++;
        }
    }
    
    LOG_DEBUG("parse_wave_frame complete, found %d frame(s)", frame_count);
    return frame_count;
}

/* 导出单周波数据到CSV */
static int export_wave_to_csv(FILE *fp, float *channels[7], int cycle_idx, int frame_seq) {
    int pt, ch;
    
    fprintf(fp, "# Cycle %d: seq=%d\n", cycle_idx, frame_seq);
    fprintf(fp, "point,UA,UB,UC,IA,IB,IC,IZ\n");
    
    for (pt = 0; pt < 256; pt++) {
        fprintf(fp, "%d", pt);
        for (ch = 0; ch < 7; ch++) {
            fprintf(fp, ",%.6f", channels[ch][pt]);
        }
        fprintf(fp, "\n");
    }
    fprintf(fp, "\n");
    
    return 0;
}

/* 打印波形统计信息 */
static void print_wave_stats(float *channels[7], int frame_seq) {
    int ch, pt;
    float min_val, max_val, rms, sum_sq;
    
    LOG_INFO("Wave Stats (Seq=%d):", frame_seq);
    for (ch = 0; ch < 7; ch++) {
        min_val = channels[ch][0];
        max_val = channels[ch][0];
        sum_sq = 0;
        
        for (pt = 0; pt < 256; pt++) {
            if (channels[ch][pt] < min_val) min_val = channels[ch][pt];
            if (channels[ch][pt] > max_val) max_val = channels[ch][pt];
            sum_sq += channels[ch][pt] * channels[ch][pt];
        }
        rms = sqrtf(sum_sq / 256.0f);
        
        LOG_INFO("  %s: min=%10.3f  max=%10.3f  rms=%10.3f  range=%10.3f",
               channel_names[ch], min_val, max_val, rms, max_val - min_val);
    }
}

/* 实时监测量读取 */
static int read_real_monitor(WAVEFORM_SAMPLER_DEVICE_T *wave_dev) {
    real_monitor_t monitor_data;
    int32_t ret;
    
    LOG_INFO("Reading real-time monitor data...");
    
    memset(&monitor_data, 0, sizeof(monitor_data));
    ret = wave_dev->read_real_monitor_data(wave_dev, &monitor_data, sizeof(monitor_data), 1);
    
    if (ret <= 0) {
        LOG_WARN("read_real_monitor_data returned %ld", ret);
        return -1;
    }
    
    LOG_INFO("Monitor data: seq=%u, time=%04d-%02d-%02d %02d:%02d:%02d.%03d",
            monitor_data.mon_seq, monitor_data.year, monitor_data.month, monitor_data.day,
            monitor_data.hour, monitor_data.minute, monitor_data.second, monitor_data.millisecond);
    LOG_INFO("  UA=%.3f V, UB=%.3f V, UC=%.3f V", monitor_data.val[0], monitor_data.val[1], monitor_data.val[2]);
    LOG_INFO("  IA=%.3f A, IB=%.3f A, IC=%.3f A", monitor_data.val[3], monitor_data.val[4], monitor_data.val[5]);
    LOG_INFO("  Frequency=%.3f Hz", monitor_data.val[7]);
    
    return 0;
}

int main(int argc, char *argv[]) {
    int max_cycles = 5;
    const char *output_file = "wave_data.csv";
    const char *log_file = "wave_export.log";
    int i, ret;
    uint32_t actual_cycles;
    
    WAVEFORM_SAMPLER_DEVICE_T *wave_dev = NULL;
    HW_DEVICE *dev = NULL;
    
    /* 波形数据缓冲区 */
    uint8_t *wave_buf = NULL;
    int32_t wave_buf_size = 7182 * 20;
    
    /* 解析命令行参数 */
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--cycles") == 0 && i + 1 < argc) {
            max_cycles = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--output") == 0 && i + 1 < argc) {
            output_file = argv[++i];
        } else if (strcmp(argv[i], "--log") == 0 && i + 1 < argc) {
            log_file = argv[++i];
        } else if (strcmp(argv[i], "--debug") == 0) {
            g_log_level = 3;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: wave_export_arm [options]\n");
            printf("Options:\n");
            printf("  --cycles N     Number of cycles to capture (default: 5)\n");
            printf("  --output FILE  Output CSV file (default: wave_data.csv)\n");
            printf("  --log FILE     Log file name (default: wave_export.log)\n");
            printf("  --debug        Enable debug level logging\n");
            printf("  --help         Show this help\n");
            return 0;
        }
    }
    
    /* 打开日志文件 */
    g_log_fp = fopen(log_file, "w");
    if (g_log_fp) {
        setvbuf(g_log_fp, NULL, _IOLBF, 0);  /* 行缓冲 */
    }
    
    LOG_INFO("============================================================");
    LOG_INFO("Wave Export Tool (ARM32) - Direct HAL Interface");
    LOG_INFO("============================================================");
    LOG_INFO("Cycles to capture: %d", max_cycles);
    LOG_INFO("Output file: %s", output_file);
    LOG_INFO("Log file: %s", log_file);
    LOG_INFO("Log level: %s", g_log_level >= 3 ? "DEBUG" : g_log_level >= 2 ? "INFO" : g_log_level >= 1 ? "WARN" : "ERROR");
    
    /* 记录系统信息 */
    LOG_INFO("System info:");
    LOG_INFO("  T536 Terminal (ARM64, 兼容32位)");
    LOG_INFO("  Waveform Sampler Device");
    
    /* ========== 初始化HAL ========== */
    LOG_INFO("Initializing HAL...");
    ret = hal_init();
    if (ret != 0) {
        LOG_WARN("hal_init() returned %d, continuing...", ret);
    } else {
        LOG_INFO("HAL initialized successfully");
    }
    
    /* ========== 获取波形采样设备 ========== */
    LOG_INFO("Getting waveform sampler device (id='%s')...", WAVEFORM_SAMPLER_HARDWARE_MODULE_ID);
    dev = hal_device_get(WAVEFORM_SAMPLER_HARDWARE_MODULE_ID);
    if (dev == NULL) {
        LOG_ERROR("Failed to get waveform sampler device!");
        LOG_ERROR("hal_device_get returned NULL - device not found or HAL not properly initialized");
        LOG_ERROR("Troubleshooting:");
        LOG_ERROR("  1. Ensure device is connected and powered on");
        LOG_ERROR("  2. Check /custom/sys/lib/hal_lib/lib32/ for required libraries");
        LOG_ERROR("  3. Try: LD_PRELOAD=/custom/sys/lib/hal_lib/lib32/libdrivers.so");
        hal_exit();
        if (g_log_fp) fclose(g_log_fp);
        return 1;
    }
    
    wave_dev = (WAVEFORM_SAMPLER_DEVICE_T *)dev;
    LOG_INFO("Device obtained successfully!");
    LOG_INFO("  pModule: %p", (void*)wave_dev->base.pModule);
    LOG_INFO("  nVer: %ld", wave_dev->base.nVer);
    LOG_INFO("  szDeviceID: %s", wave_dev->base.szDeviceID ? wave_dev->base.szDeviceID : "(null)");
    LOG_INFO("  read_waveform: %p", (void*)wave_dev->read_waveform);
    LOG_INFO("  read_real_monitor_data: %p", (void*)wave_dev->read_real_monitor_data);
    
    if (wave_dev->read_waveform == NULL) {
        LOG_ERROR("read_waveform function pointer is NULL!");
        hal_device_release(dev);
        hal_exit();
        if (g_log_fp) fclose(g_log_fp);
        return 1;
    }
    
    /* ========== 分配波形数据缓冲区 ========== */
    wave_buf = (uint8_t *)malloc(wave_buf_size);
    if (wave_buf == NULL) {
        LOG_ERROR("Failed to allocate waveform buffer (size=%d)", wave_buf_size);
        hal_device_release(dev);
        hal_exit();
        if (g_log_fp) fclose(g_log_fp);
        return 1;
    }
    memset(wave_buf, 0, wave_buf_size);
    LOG_INFO("Waveform buffer allocated: %d bytes", wave_buf_size);
    
    /* ========== 读取一次实时监测量验证设备 ========== */
    LOG_INFO("Verifying device with real-time monitor read...");
    read_real_monitor(wave_dev);
    
    /* ========== 打开输出文件 ========== */
    FILE *fp = fopen(output_file, "w");
    if (fp == NULL) {
        LOG_ERROR("Failed to open output file: %s (errno=%d: %s)", output_file, errno, strerror(errno));
        free(wave_buf);
        hal_device_release(dev);
        hal_exit();
        if (g_log_fp) fclose(g_log_fp);
        return 1;
    }
    
    /* 写入文件头 */
    time_t now = time(NULL);
    char date_str[64];
    strftime(date_str, sizeof(date_str), "%Y-%m-%d %H:%M:%S", localtime(&now));
    
    fprintf(fp, "# T536 Waveform Data Export (Direct HAL)\n");
    fprintf(fp, "# Channels: UA, UB, UC, IA, IB, IC, IZ\n");
    fprintf(fp, "# Points per cycle: 256\n");
    fprintf(fp, "# Sample rate: 12800 Hz\n");
    fprintf(fp, "# Export time: %s\n", date_str);
    fprintf(fp, "#\n\n");
    
    LOG_INFO("Output file opened: %s", output_file);
    
    /* ========== 采集循环 ========== */
    LOG_INFO("Starting wave acquisition...");
    LOG_INFO("Buffer size: %d bytes, Wave size: %d bytes/cycle", wave_buf_size, WS_SINGLE_WAVEFORM_SIZE);
    
    actual_cycles = 0;
    int retries = 0;
    const int MAX_RETRIES = 3;
    
    for (i = 0; i < max_cycles; i++) {
        LOG_INFO("--- Cycle %d/%d ---", i + 1, max_cycles);
        
        /* 读取波形数据 */
        memset(wave_buf, 0, wave_buf_size);
        
        struct timeval tv_start, tv_end;
        gettimeofday(&tv_start, NULL);
        
        ret = wave_dev->read_waveform(wave_dev, wave_buf, wave_buf_size, 1);
        
        gettimeofday(&tv_end, NULL);
        int read_time_ms = (tv_end.tv_sec - tv_start.tv_sec) * 1000 + 
                          (tv_end.tv_usec - tv_start.tv_usec) / 1000;
        
        if (ret <= 0) {
            LOG_ERROR("read_waveform failed: returned %ld (time: %dms)", ret, read_time_ms);
            retries++;
            if (retries >= MAX_RETRIES) {
                LOG_ERROR("Max retries reached, skipping this cycle");
                retries = 0;
                i++;
                continue;
            }
            LOG_WARN("Retrying... (attempt %d/%d)", retries, MAX_RETRIES);
            usleep(100000);
            i--;
            continue;
        }
        
        retries = 0;  /* 重置重试计数 */
        LOG_INFO("Waveform read: %ld bytes in %dms (expected ~%d)", ret, read_time_ms, WS_SINGLE_WAVEFORM_SIZE);
        
        /* 检查数据有效性 */
        int header_found = 0;
        for (int bi = 0; bi < ret - 1; bi++) {
            if (wave_buf[bi] == 0x68 && wave_buf[bi+1] == 0x36) {
                header_found = 1;
                uint32_t check_len = read_le32(&wave_buf[bi + 2]);
                LOG_DEBUG("Frame header found at offset %d, frame_len=%u", bi, check_len);
                break;
            }
        }
        if (!header_found) {
            LOG_WARN("No valid frame header (0x68 0x36) found in received data!");
            LOG_WARN("First 32 bytes of data:");
            for (int bi = 0; bi < 32 && bi < ret; bi++) {
                printf("  [%02d]: %02X (%d)", bi, wave_buf[bi], wave_buf[bi]);
                if ((bi + 1) % 4 == 0) printf(" |");
                printf("\n");
            }
        }
        
        /* 解析波形数据 */
        float *channels[7];
        int frame_seq = 0;
        int frame_count;
        int ch;
        
        for (ch = 0; ch < 7; ch++) {
            channels[ch] = (float *)malloc(256 * sizeof(float));
            memset(channels[ch], 0, 256 * sizeof(float));
        }
        
        frame_count = parse_wave_frame(wave_buf, ret, channels, &frame_seq);
        LOG_INFO("Parsed %d frame(s), seq=%d", frame_count, frame_seq);
        
        if (frame_count > 0) {
            /* 导出到CSV */
            export_wave_to_csv(fp, channels, actual_cycles, frame_seq);
            fflush(fp);
            
            /* 打印统计 */
            print_wave_stats(channels, frame_seq);
            LOG_INFO("Cycle %d exported to CSV (frame_seq=%d)", actual_cycles + 1, frame_seq);
            actual_cycles++;
        } else {
            LOG_WARN("No valid frames parsed from waveform data!");
        }
        
        /* 释放通道数据 */
        for (ch = 0; ch < 7; ch++) {
            free(channels[ch]);
        }
        
        /* 读取间隔 */
        if (i < max_cycles - 1) {
            LOG_DEBUG("Waiting 20ms before next cycle...");
            usleep(20000);
        }
    }
    
    fclose(fp);
    LOG_INFO("Output file closed: %s", output_file);
    
    /* ========== 清理 ========== */
    free(wave_buf);
    hal_device_release(dev);
    hal_exit();
    
    LOG_INFO("============================================================");
    LOG_INFO("Export completed!");
    LOG_INFO("  Total cycles captured: %d", actual_cycles);
    LOG_INFO("  Output file: %s", output_file);
    LOG_INFO("  Data points: %d × 7 × 256 = %d floats", actual_cycles, actual_cycles * 7 * 256);
    LOG_INFO("  Log file: %s", log_file);
    LOG_INFO("============================================================");
    
    if (g_log_fp) fclose(g_log_fp);
    
    return 0;
}
