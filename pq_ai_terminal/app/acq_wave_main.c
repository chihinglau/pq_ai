/**
 * @file acq_wave_main.c
 * @brief 实时波形采集与AI推理主程序
 *
 * 程序流程：
 * 1. 初始化波形采样设备（HAL层）
 * 2. 周期性读取实时波形数据
 * 3. 从波形计算PQ指标
 * 4. 特征提取
 * 5. AI推理（本地或远程RK3576）
 * 6. 场景识别
 * 7. 结果输出与上报
 *
 * 用法: pq_acq_wave [options]
 *   --cycles N     运行周期数（默认100）
 *   --interval MS  采集间隔毫秒（默认20ms）
 *   --report N     每隔N周期打印一次报告（默认50）
 *   --duration S   运行持续时间秒（0=不限）
 *   --config FILE  配置文件路径
 *   --help         显示帮助
 *
 * @author PQ AI Terminal Team
 * @date 2026-08-12
 */

#include "pq_common.h"
#include "pq_hal.h"
#include "pq_metrics.h"
#include "feature_extract.h"
#include "scenario_detect.h"
#include "ai_rpc.h"
#include "wave_sampler_hal.h"
#include "wave_freeze.h"
#include "event_trigger.h"
#include "usb_ecm.h"
#include "proto_mqtt.h"
#include "json_builder.h"
#include "sqlite_wrapper.h"
#include "pq_config.h"

#include <math.h>

#ifdef PLATFORM_WINDOWS
    #include "hal_sim.h"
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

/* ==================== 配置结构体 ==================== */
typedef struct {
    uint32_t max_cycles;       /* 运行周期数 */
    uint32_t interval_ms;      /* 采集间隔 */
    uint32_t report_interval;  /* 报告间隔 */
    uint32_t duration_sec;     /* 持续时间 */
    char config_file[256];     /* 配置文件路径 */
    int module_enabled;        /* AI模组启用 */
    char module_ip[64];        /* AI模组IP */
    int module_port;           /* AI模组端口 */
} acq_config_t;

/* ==================== 全局变量 ==================== */
static volatile int s_running = 1;
static uint32_t s_total_events = 0;

/* ==================== 帮助函数 ==================== */

static void print_banner(void)
{
    printf("\n");
    printf("============================================================\n");
    printf("  PQ AI Terminal - Real-Time Wave Acquisition\n");
    printf("  Target Platform: T536 + HT7627S\n");
    printf("  AI Compute: RK3576 via USB ECM\n");
    printf("============================================================\n");
    printf("\n");
}

static void print_usage(void)
{
    printf("Usage: pq_acq_wave [options]\n");
    printf("Options:\n");
    printf("  --cycles N       Number of acquisition cycles (default: 0=infinite)\n");
    printf("  --interval MS    Acquisition interval in ms (default: 20)\n");
    printf("  --report N       Report interval in cycles (default: 50)\n");
    printf("  --duration S     Run duration in seconds (default: 0=infinite)\n");
    printf("  --config FILE    Config file path (default: config.ini)\n");
    printf("  -h, --help       Show this help message\n");
    printf("\n");
    printf("Example:\n");
    printf("  pq_acq_wave --cycles 1000 --report 100\n");
    printf("  pq_acq_wave --duration 3600 --interval 20\n");
    printf("\n");
}

static void print_header_info(const acq_config_t *cfg)
{
    char buf[32];
    printf("  Configuration:\n");
    if (cfg->max_cycles == 0) {
        printf("    Max Cycles    : Unlimited\n");
    } else {
        snprintf(buf, sizeof(buf), "%u", cfg->max_cycles);
        printf("    Max Cycles    : %s\n", buf);
    }
    printf("    Interval      : %u ms\n", cfg->interval_ms);
    printf("    Report Every  : %u cycles\n", cfg->report_interval);
    if (cfg->duration_sec == 0) {
        printf("    Duration      : Unlimited\n");
    } else {
        snprintf(buf, sizeof(buf), "%u", cfg->duration_sec);
        printf("    Duration      : %s\n", buf);
    }
    printf("    AI Module     : %s", cfg->module_enabled ? "Enabled" : "Disabled");
    if (cfg->module_enabled) {
        printf(" (%s:%d)", cfg->module_ip, cfg->module_port);
    }
    printf("\n\n");
}

static void print_wave_data(const ws_wave_cycle_t *cycle)
{
    /* 计算各通道的简单统计 */
    float min_val, max_val, sum_val;
    int i;

    printf("  Wave Data (Seq=%u, Time=%04d-%02d-%02d %02d:%02d:%02d.%03d):\n",
           cycle->cycle_seq,
           cycle->timestamp.year, cycle->timestamp.month, cycle->timestamp.day,
           cycle->timestamp.hour, cycle->timestamp.minute, cycle->timestamp.second,
           cycle->timestamp.millisecond);

    const char *channel_names[] = {"UA", "UB", "UC", "IA", "IB", "IC", "IZ"};

    for (int ch = 0; ch < WS_CHANNEL_COUNT; ch++) {
        min_val = cycle->data[ch][0];
        max_val = cycle->data[ch][0];
        sum_val = 0;

        for (i = 1; i < WS_POINTS_PER_CYCLE; i++) {
            if (cycle->data[ch][i] < min_val) min_val = cycle->data[ch][i];
            if (cycle->data[ch][i] > max_val) max_val = cycle->data[ch][i];
            sum_val += cycle->data[ch][i];
        }

        float avg = sum_val / WS_POINTS_PER_CYCLE;
        float rms = 0;
        for (i = 0; i < WS_POINTS_PER_CYCLE; i++) {
            rms += cycle->data[ch][i] * cycle->data[ch][i];
        }
        rms = sqrtf(rms / WS_POINTS_PER_CYCLE);

        printf("    %s: min=%8.3f  max=%8.3f  avg=%8.3f  rms=%8.3f\n",
               channel_names[ch], min_val, max_val, avg, rms);
    }
    printf("\n");
}

static void print_ai_result(const ai_result_t *result)
{
    printf("  AI Inference Results:\n");
    printf("    iForest Score  : %.4f\n", result->if_score);
    printf("    AE Score       : %.4f\n", result->ae_score);
    printf("    CNN Class      : %d (confidence: %.3f)\n",
           result->cnn_class, result->cnn_confidence);
    printf("    Module Status  : %s (latency: %d ms)\n",
           result->module_available ? "ONLINE" : "OFFLINE (fallback)",
           result->latency_ms);
    printf("\n");
}

static void print_status_report(uint32_t cycle, const pq_metrics_t *metrics,
                                 const ai_result_t *ai_res,
                                 scenario_type_t scen,
                                 uint32_t events)
{
    printf("\n============================================================\n");
    printf("  STATUS REPORT - Cycle %u\n", cycle);
    printf("============================================================\n");
    printf("  Scenario    : %s\n", scenario_name(scen));
    printf("  Events      : %u\n", events);
    printf("\n  PQ Metrics:\n");
    printf("    Voltage Deviation : %8.3f%%  (%s)\n",
           metrics->voltage_deviation.value,
           pq_metric_status_str(metrics->voltage_deviation.status));
    printf("    Voltage THD       : %8.3f%%  (%s)\n",
           metrics->voltage_thd.value,
           pq_metric_status_str(metrics->voltage_thd.status));
    printf("    Current THD       : %8.3f%%  (%s)\n",
           metrics->current_thd.value,
           pq_metric_status_str(metrics->current_thd.status));
    printf("    Unbalance         : %8.3f%%  (%s)\n",
           metrics->voltage_unbalance.value,
           pq_metric_status_str(metrics->voltage_unbalance.status));
    printf("    Freq Deviation    : %8.3f Hz (%s)\n",
           metrics->frequency_deviation.value,
           pq_metric_status_str(metrics->frequency_deviation.status));
    printf("    Active Power      : %8.3f kW\n", metrics->active_power.value);
    printf("    Reactive Power    : %8.3f kvar\n", metrics->reactive_power.value);
    printf("    Power Factor      : %8.3f\n", metrics->power_factor.value);
    printf("\n  AI Results:\n");
    printf("    IF Score    : %.4f\n", ai_res->if_score);
    printf("    AE Score    : %.4f\n", ai_res->ae_score);
    printf("    CNN Class   : %d (%.3f)\n", ai_res->cnn_class, ai_res->cnn_confidence);
    printf("    Module      : %s\n", ai_res->module_available ? "ONLINE" : "OFFLINE");
    printf("============================================================\n\n");
}

/* ==================== 主采集循环 ==================== */

static int run_acquisition(const acq_config_t *cfg)
{
    uint32_t cycle = 0;
    uint32_t start_time, current_time;
    uint32_t actual_cycles;
    int ret;

    /* 数据结构 */
    ws_wave_cycle_t wave_cycles[WS_MAX_WAVE_BUFFERS];
    ws_real_monitor_t monitor;
    ht7627s_wave_t ht_wave;
    ht7627s_regs_t ht_regs;
    pq_metrics_t metrics;
    wave_freeze_buffer_t freeze_buf;
    feature_vector_t feat;
    ai_result_t ai_res;
    pq_event_t event;
    scenario_type_t scen;

    char json_buf[2048];
    pq_config_t sys_cfg;

    printf("[INIT] Initializing acquisition system...\n");

    /* 加载配置 */
    config_load(&sys_cfg, cfg->config_file);

    /* 初始化各模块 */
    ws_init();
    hal_ht7627s_init(NULL);
    pq_metrics_init(PQ_NOMINAL_VOLTAGE, PQ_NOMINAL_FREQ);
    pq_metrics_set_limits(
        config_get_float(&sys_cfg, "thresholds.voltage_deviation_limit", 7.0f),
        config_get_float(&sys_cfg, "thresholds.voltage_thd_limit", 5.0f),
        config_get_float(&sys_cfg, "thresholds.current_thd_limit", 8.0f),
        config_get_float(&sys_cfg, "thresholds.unbalance_limit", 2.0f),
        config_get_float(&sys_cfg, "thresholds.frequency_deviation_limit", 0.5f),
        config_get_float(&sys_cfg, "thresholds.transformer_load_limit", 100.0f),
        config_get_float(&sys_cfg, "thresholds.line_load_limit", 100.0f),
        config_get_float(&sys_cfg, "thresholds.power_factor_limit", 0.85f)
    );
    event_trigger_init();
    wave_freeze_init(&freeze_buf);
    feature_extract_init();
    scenario_detect_init();

    /* 初始化AI推理模块 */
    if (cfg->module_enabled) {
        ai_rpc_init(cfg->module_ip, cfg->module_port);
        printf("[INIT] AI RPC module initialized at %s:%d\n",
               cfg->module_ip, cfg->module_port);
    }

    /* 初始化数据库和MQTT（可选）*/
    db_init("pq_data");

#ifdef PLATFORM_WINDOWS
    /* Windows下启动MQTT和仿真模式 */
    mqtt_init("127.0.0.1", 1883, "pq_terminal_acq");
    mqtt_connect();
    hal_set_sim_scenario("S4");
    printf("[INIT] Windows simulation mode, scenario S4\n");
#else
    printf("[INIT] Linux real hardware mode\n");
#endif

    printf("[INIT] System initialized successfully!\n");
    printf("[INIT] Starting acquisition loop...\n\n");

    start_time = (uint32_t)time(NULL);

    /* 主循环 */
    while (s_running) {
        current_time = (uint32_t)time(NULL);

        /* 检查退出条件 */
        if (cfg->max_cycles > 0 && cycle >= cfg->max_cycles) {
            printf("[INFO] Reached max cycles (%u), stopping.\n", cfg->max_cycles);
            break;
        }
        if (cfg->duration_sec > 0 &&
            (current_time - start_time) >= cfg->duration_sec) {
            printf("[INFO] Reached duration limit (%u seconds), stopping.\n",
                   cfg->duration_sec);
            break;
        }

        /* 1. 读取实时波形数据 */
        ret = ws_read_waveforms(wave_cycles, 20, &actual_cycles);
        if (ret != 0 || actual_cycles == 0) {
            PQ_LOGW("Failed to read waveforms, retrying...");
            hal_sleep_ms(cfg->interval_ms);
            continue;
        }

        /* 2. 读取实时监测量（可选，用于快速获取RMS等）*/
        ws_read_real_monitors(&monitor, 1, &actual_cycles);

        /* 3. 转换为内部格式 */
        ws_convert_to_ht7627s(wave_cycles, actual_cycles, &ht_wave);
        ws_convert_to_ht7627s_regs(&monitor, &ht_regs);

        /* 4. 计算PQ指标 */
        pq_metrics_calculate(&ht_regs, &ht_wave, &metrics);
        pq_metrics_evaluate(&metrics);

        /* 5. 波形冻结（用于事件检测和后分析）*/
        wave_freeze_push(&freeze_buf, &ht_wave);

        /* 6. 事件检测 */
        if (event_trigger_check(&metrics, &event) == 1) {
            s_total_events++;
            wave_freeze_trigger(&freeze_buf, ht_wave.cycle_id);

            printf("\n[EVENT] Event detected: %s (severity: %.2f)\n",
                   event_type_str(event.type), event.severity);
            printf("[EVENT] Description: %s\n", event.description);

            /* 保存事件 */
            db_save_event(&event);

            /* MQTT上报 */
            json_begin(json_buf, sizeof(json_buf));
            json_add_string(json_buf, sizeof(json_buf), "type", event_type_str(event.type), 0);
            json_add_float(json_buf, sizeof(json_buf), "severity", event.severity, 0);
            json_add_string(json_buf, sizeof(json_buf), "desc", event.description, 1);
            json_add_float(json_buf, sizeof(json_buf), "timestamp", (float)current_time, 0);
            json_end(json_buf, sizeof(json_buf));

#ifdef PLATFORM_WINDOWS
            mqtt_publish("pq/terminal/event", json_buf, 1);
#endif
            printf("[EVENT] Event published via MQTT\n");
        }

        /* 7. 特征提取 */
        feature_extract_from_wave(freeze_buf.cycles, freeze_buf.count,
                                  &metrics, &feat);

        /* 8. AI推理 */
        memset(&ai_res, 0, sizeof(ai_res));
        if (cfg->module_enabled) {
            ai_rpc_infer(&feat, &metrics, &ai_res);
        }

        /* 9. 场景识别 */
        scen = scenario_detect_classify(&metrics, &feat);

        /* 10. 周期性打印报告 */
        if (cycle % cfg->report_interval == 0 || s_total_events > 0) {
            if (cycle % cfg->report_interval == 0) {
                print_status_report(cycle, &metrics, &ai_res, scen, s_total_events);
            }

            /* 每100周期打印一次详细波形数据 */
            if (cycle % 100 == 0 && cycle > 0) {
                print_wave_data(&wave_cycles[actual_cycles - 1]);
                print_ai_result(&ai_res);
            }
        }

        /* 11. 保存数据 */
        if (cycle % cfg->report_interval == 0) {
            db_save_metrics(&metrics);

            /* 状态上报 */
            json_begin(json_buf, sizeof(json_buf));
            json_add_string(json_buf, sizeof(json_buf), "scenario", scenario_name(scen), 0);
            json_add_float(json_buf, sizeof(json_buf), "if_score", ai_res.if_score, 0);
            json_add_float(json_buf, sizeof(json_buf), "ae_score", ai_res.ae_score, 0);
            json_add_int(json_buf, sizeof(json_buf), "cnn_class", ai_res.cnn_class, 0);
            json_add_int(json_buf, sizeof(json_buf), "module", ai_res.module_available, 0);
            json_add_int(json_buf, sizeof(json_buf), "cycle", (int)cycle, 0);
            json_end(json_buf, sizeof(json_buf));

#ifdef PLATFORM_WINDOWS
            mqtt_publish("pq/terminal/status", json_buf, 0);
#endif
        }

        cycle++;
        hal_sleep_ms(cfg->interval_ms);
    }

    /* 清理 */
    printf("[INFO] Acquisition stopped after %u cycles.\n", cycle);
    printf("[INFO] Total events detected: %u\n", s_total_events);

    db_close();
    ai_rpc_deinit();
    ws_deinit();

#ifdef PLATFORM_WINDOWS
    mqtt_disconnect();
#endif

    return 0;
}

/* ==================== 信号处理 ==================== */

#ifdef PLATFORM_LINUX
#include <signal.h>
static void signal_handler(int sig)
{
    if (sig == SIGINT || sig == SIGTERM) {
        printf("\n[INFO] Received signal %d, shutting down...\n", sig);
        s_running = 0;
    }
}
#endif

/* ==================== 主入口 ==================== */

int main(int argc, char *argv[])
{
    acq_config_t cfg;
    int i;

    /* 默认配置 */
    memset(&cfg, 0, sizeof(cfg));
    cfg.max_cycles = 0;          /* 不限（除非指定）*/
    cfg.interval_ms = 20;        /* 50Hz对应20ms */
    cfg.report_interval = 50;    /* 每50周期报告一次 */
    cfg.duration_sec = 0;        /* 不限 */
    strcpy(cfg.config_file, "config.ini");
    cfg.module_enabled = 1;
    strcpy(cfg.module_ip, "192.168.100.1");  /* RK3576 USB ECM地址 */
    cfg.module_port = 9090;

    /* 解析命令行参数 */
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--cycles") == 0 && i + 1 < argc) {
            cfg.max_cycles = strtoul(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--interval") == 0 && i + 1 < argc) {
            cfg.interval_ms = strtoul(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--report") == 0 && i + 1 < argc) {
            cfg.report_interval = strtoul(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--duration") == 0 && i + 1 < argc) {
            cfg.duration_sec = strtoul(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
            strncpy(cfg.config_file, argv[++i], sizeof(cfg.config_file) - 1);
        } else if (strcmp(argv[i], "--no-ai") == 0) {
            cfg.module_enabled = 0;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage();
            return 0;
        }
    }

    /* 打印信息 */
    print_banner();
    print_header_info(&cfg);

    /* 信号处理 */
#ifdef PLATFORM_LINUX
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
#endif

    /* 运行采集 */
    return run_acquisition(&cfg);
}
