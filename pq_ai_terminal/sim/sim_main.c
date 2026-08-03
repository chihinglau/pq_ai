/**
 * @file sim_main.c
 * @brief 软模拟仿真主程序（Windows/Linux通用）
 *
 * 用法: pq_sim.exe --scenario S1|S2|S3|S4|S5 [--cycles N]
 */

#include "pq_common.h"
#include "pq_hal.h"
#include "pq_metrics.h"
#include "event_trigger.h"
#include "wave_freeze.h"
#include "feature_extract.h"
#include "scenario_detect.h"
#include "iforest_infer.h"
#include "ae_infer.h"
#include "cnn1d_infer.h"
#include "ai_rpc.h"
#include "usb_ecm.h"
#include "compute_module_sim.h"
#include "proto_mqtt.h"
#include "time_sync.h"
#include "sqlite_wrapper.h"
#include "json_builder.h"
#include "hal_sim.h"
#include "pq_config.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static void print_header(void)
{
    printf("\n");
    printf("============================================================\n");
    printf("  PQ AI Terminal - Soft Simulation Environment\n");
    printf("  Host:    T536 + HT7627S  (sampling & PQ metrics)\n");
    printf("  Compute: RK3576          (AI inference via USB ECM)\n");
    printf("============================================================\n\n");
}

static void print_metrics(const pq_metrics_t *m)
{
    printf("  %-24s %8.3f %8.3f %s\n", "Voltage Deviation(%)",
           m->voltage_deviation.value, m->voltage_deviation.limit,
           pq_metric_status_str(m->voltage_deviation.status));
    printf("  %-24s %8.3f %8.3f %s\n", "Voltage THD(%)",
           m->voltage_thd.value, m->voltage_thd.limit,
           pq_metric_status_str(m->voltage_thd.status));
    printf("  %-24s %8.3f %8.3f %s\n", "Current THD(%)",
           m->current_thd.value, m->current_thd.limit,
           pq_metric_status_str(m->current_thd.status));
    printf("  %-24s %8.3f %8.3f %s\n", "Unbalance(%)",
           m->voltage_unbalance.value, m->voltage_unbalance.limit,
           pq_metric_status_str(m->voltage_unbalance.status));
    printf("  %-24s %8.3f %8.3f %s\n", "Freq Deviation(Hz)",
           m->frequency_deviation.value, m->frequency_deviation.limit,
           pq_metric_status_str(m->frequency_deviation.status));
    printf("  %-24s %8.3f %8.3f %s\n", "Transformer Load(%)",
           m->transformer_load.value, m->transformer_load.limit,
           pq_metric_status_str(m->transformer_load.status));
    printf("  %-24s %8.3f %8.3f %s\n", "Line Load(%)",
           m->line_load.value, m->line_load.limit,
           pq_metric_status_str(m->line_load.status));
    printf("  %-24s %8.3f %8.3f %s\n", "Power Factor",
           m->power_factor.value, m->power_factor.limit,
           pq_metric_status_str(m->power_factor.status));
    printf("  %-24s %8.3f\n", "Active Power(kW)", m->active_power.value);
    printf("  %-24s %8.3f\n", "Reactive Power(kvar)", m->reactive_power.value);
    printf("  %-24s %8.3f\n", "Apparent Power(kVA)", m->apparent_power.value);
}

static void print_summary(const pq_metrics_t *m, scenario_type_t scen,
                          int total_cycles, int event_count)
{
    int i;
    int pass = 1;
    const pq_metric_t *metrics_arr[] = {
        &m->voltage_deviation, &m->voltage_thd, &m->current_thd,
        &m->voltage_unbalance, &m->frequency_deviation,
        &m->transformer_load, &m->line_load, &m->power_factor
    };

    printf("\n============================================================\n");
    printf("  SIMULATION SUMMARY\n");
    printf("============================================================\n");
    printf("  Scenario       : %s\n", scenario_name(scen));
    printf("  Total Cycles   : %d\n", total_cycles);
    printf("  Events Triggered: %d\n", event_count);
    printf("\n  --- PASS/FAIL Evaluation ---\n");

    for (i = 0; i < 8; i++) {
        const char *status = pq_metric_status_str(metrics_arr[i]->status);
        printf("  %-24s %s\n", metrics_arr[i]->name, status);
        if (metrics_arr[i]->status == 2) pass = 0;
    }

    printf("\n  %-24s %s\n", "OVERALL",
           pass ? "[PASS] ALL CLEAR" : "[FAIL] VIOLATIONS DETECTED");
    printf("\n  Recommendation : %s\n", scenario_recommendation(scen));
    printf("============================================================\n");
}

static int run_single_scenario(const char *scenario, int max_cycles)
{
    int i;
    ht7627s_cfg_t cfg = {0};
    ht7627s_regs_t regs = {0};
    ht7627s_wave_t wave = {0};
    pq_metrics_t metrics = {0};
    wave_freeze_buffer_t wbuf = {0};
    feature_vector_t feat = {0};
    ai_result_t ai_res = {0};
    pq_event_t event = {0};
    scenario_type_t scen = SCENARIO_UNKNOWN;
    int event_count = 0;
    char json_buf[1024];

    pq_config_t sys_cfg = {0};
    config_load(&sys_cfg, "config.ini");

    print_header();
    printf("  Simulation Parameters:\n");
    printf("    Scenario: %s\n", scenario);
    printf("    Cycles  : %d\n", max_cycles);
    printf("    Config  : %s\n", sys_cfg.count > 0 ? "loaded" : "default");
    printf("\n");

    /* 初始化 */
    time_sync_init(config_get_string(&sys_cfg, "mqtt.broker_host", NULL));
    db_init("data");
    mqtt_init(config_get_string(&sys_cfg, "mqtt.broker_host", "127.0.0.1"),
              config_get_int(&sys_cfg, "mqtt.broker_port", 1883),
              config_get_string(&sys_cfg, "mqtt.client_id", "pq_terminal_sim"));
    mqtt_connect();

    cfg.sample_rate = PQ_SAMPLE_RATE_12800;
    cfg.pts_per_cycle = PQ_POINTS_PER_CYCLE_12800;
    cfg.n_channels = PQ_N_CHANNELS;
    hal_ht7627s_init(&cfg);
    hal_set_sim_scenario(scenario);

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
    wave_freeze_init(&wbuf);
    feature_extract_init();
    scenario_detect_init();
    /* AI 模型加载在算力模组侧（RK3576），主机通过 ai_rpc 远程调用 */

    PQ_LOGI("All modules initialized. Starting simulation...");

    /* 主循环 */
    for (i = 0; i < max_cycles; i++) {
        /* 1. 读取HT7627S数据 */
        hal_ht7627s_read_regs(&regs);
        hal_ht7627s_read_wave(&wave);

        /* 2. 计算PQ指标 */
        pq_metrics_calculate(&regs, &wave, &metrics);
        pq_metrics_evaluate(&metrics);

        /* 3. 波形冻结 */
        wave_freeze_push(&wbuf, &wave);

        /* 4. 事件检测 */
        if (event_trigger_check(&metrics, &event) == 1) {
            event_count++;
            wave_freeze_trigger(&wbuf, wave.cycle_id);
            db_save_event(&event);

            /* 发送MQTT告警 */
            json_begin(json_buf, sizeof(json_buf));
            json_add_string(json_buf, sizeof(json_buf), "type", event_type_str(event.type), 0);
            json_add_float(json_buf, sizeof(json_buf), "severity", event.severity, 0);
            json_add_string(json_buf, sizeof(json_buf), "desc", event.description, 1);
            json_end(json_buf, sizeof(json_buf));
            mqtt_publish("pq/terminal/event", json_buf, 1);
        }

        /* 5. 特征提取与AI推理（通过USB ECM发送给RK3576算力模组） */
        feature_extract_from_wave(wbuf.cycles, wbuf.count, &metrics, &feat);

        /* AI 推理在 RK3576 算力模组上执行，主机通过 USB ECM RPC 调用 */
        ai_rpc_infer(&feat, &metrics, &ai_res);

        /* 6. 场景识别 */
        scen = scenario_detect_classify(&metrics, &feat);

        /* 7. 周期性上报 */
        if (i % 50 == 0) {
            json_begin(json_buf, sizeof(json_buf));
            json_add_string(json_buf, sizeof(json_buf), "scenario", scenario_name(scen), 0);
            json_add_float(json_buf, sizeof(json_buf), "if_score", ai_res.if_score, 0);
            json_add_float(json_buf, sizeof(json_buf), "ae_score", ai_res.ae_score, 0);
            json_add_int(json_buf, sizeof(json_buf), "cnn_class", ai_res.cnn_class, 0);
            json_add_int(json_buf, sizeof(json_buf), "ai_module", ai_res.module_available, 1);
            json_end(json_buf, sizeof(json_buf));
            mqtt_publish("pq/terminal/status", json_buf, 0);
        }

        /* 8. 保存指标 */
        db_save_metrics(&metrics);

        /* 9. 周期打印 */
        if (i % 50 == 0 || event_count > 0) {
            printf("\n--- Cycle %d (scenario=%s) ---\n", i, scenario_name(scen));
            print_metrics(&metrics);
            printf("  %-24s %8.3f\n", "IF Anomaly Score", ai_res.if_score);
            printf("  %-24s %8.3f\n", "AE Anomaly Score", ai_res.ae_score);
            printf("  %-24s %d (conf=%.3f)\n", "CNN Event Class", ai_res.cnn_class, ai_res.cnn_confidence);
            printf("  %-24s %s\n", "AI Compute Module",
                   ai_res.module_available ? "ONLINE (RK3576 via USB ECM)" : "OFFLINE (local fallback)");
        }

        /* 10. 模拟20ms周期 */
        hal_sleep_ms(20);

        /* 事件恢复后reset波形缓冲 */
        if (wbuf.frozen && (i - (int)wbuf.frozen_cycle) > WAVE_FREEZE_POST_CYCLES) {
            wave_freeze_reset(&wbuf);
        }
    }

    /* 最终摘要 */
    print_summary(&metrics, scen, max_cycles, event_count);

    db_close();
    mqtt_disconnect();
    return 0;
}

int sim_main(int argc, char *argv[])
{
    int i;
    const char *scenario = "S4";
    int max_cycles = 100;
    int run_all = 0;
    const char *all_scenarios[] = {"S1", "S2", "S3", "S4", "S5"};

    /* 加载配置 */
    pq_config_t sys_cfg = {0};
    config_load(&sys_cfg, "config.ini");

    /* 启动 RK3576 算力模组仿真器（仿真模式） */
    const char *cm_ip = config_get_string(&sys_cfg, "compute_module.ip", USB_ECM_SIM_IP);
    int cm_port = config_get_int(&sys_cfg, "compute_module.port", USB_ECM_DEFAULT_PORT);
    int cm_enabled = config_get_int(&sys_cfg, "compute_module.enabled", 1);

    if (cm_enabled) {
        if (compute_module_sim_start(cm_ip, cm_port) == 0) {
            printf("[INIT] RK3576 compute module simulator started (%s:%d)\n", cm_ip, cm_port);
        }
        /* 初始化 AI RPC 客户端（主机侧，通过 USB ECM 连接算力模组） */
        ai_rpc_init(cm_ip, cm_port);
    } else {
        printf("[INIT] Compute module disabled, AI will use local fallback\n");
        ai_rpc_init(USB_ECM_SIM_IP, cm_port);
    }

    /* 解析命令行 */
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--scenario") == 0 && i + 1 < argc) {
            scenario = argv[++i];
        } else if (strcmp(argv[i], "--cycles") == 0 && i + 1 < argc) {
            max_cycles = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--all") == 0) {
            run_all = 1;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: %s [options]\n", argv[0]);
            printf("Options:\n");
            printf("  --scenario S1|S2|S3|S4|S5  Select simulation scenario (default: S4)\n");
            printf("  --cycles N                 Number of cycles to simulate (default: 100)\n");
            printf("  --all                      Run all S1~S5 scenarios sequentially\n");
            printf("  -h, --help                 Show this help message\n");
            printf("\nScenarios:\n");
            printf("  S1  Baseline load (340kW industrial)\n");
            printf("  S2  EV charging (80kW, 5/7/11/13 harmonics)\n");
            printf("  S3  Distributed PV (200kW, voltage rise +2.93%%)\n");
            printf("  S4  EV+PV coupled (280kW, combined effects)\n");
            printf("  S5  Extreme scenario (360kW, high THD)\n");
            return 0;
        }
    }

    if (run_all) {
        for (i = 0; i < 5; i++) {
            run_single_scenario(all_scenarios[i], max_cycles);
            printf("\n\n");
        }
    } else {
        run_single_scenario(scenario, max_cycles);
    }

    /* 清理：关闭 AI RPC 和算力模组仿真器 */
    ai_rpc_deinit();
    if (cm_enabled) {
        compute_module_sim_stop();
    }

    return 0;
}

/* Windows主入口 */
int main(int argc, char *argv[])
{
    return sim_main(argc, argv);
}
