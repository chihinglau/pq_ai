/**
 * @file hal_sim.c
 * @brief Windows仿真环境下的HAL实现
 * @author PQ AI Terminal Team
 * @date 2026-08-02
 */

#include "hal_sim.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

#ifdef PLATFORM_WINDOWS
    #include <windows.h>
#else
    #include <sys/time.h>
    #include <poll.h>
    #include <pthread.h>
#endif

/* 全局仿真场景 */
uint16_t g_sim_scenario = SCEN_S1;

void hal_set_sim_scenario(const char *scenario)
{
    if (scenario && strlen(scenario) >= 2) {
        g_sim_scenario = ((uint16_t)scenario[0] << 8) | (uint16_t)scenario[1];
    }
}

/* 静态状态 */
static ht7627s_cfg_t s_cfg;
static uint32_t s_cycle_id = 0;
static int s_initialized = 0;

#ifdef PLATFORM_WINDOWS
static LARGE_INTEGER s_qpc_freq;
static CRITICAL_SECTION s_crit;
#else
static pthread_mutex_t s_crit = PTHREAD_MUTEX_INITIALIZER;
#endif

/* 场景参数表 */
static const sim_scenario_params_t s_scenarios[5] = {
    /* S1: 基准场景 - 340kW工业负荷 */
    {
        0.0f, 0.0f, 0.0f,
        {0}, {0},
        60.0f,
        340.0f, 0.85f
    },
    /* S2: 充电桩场景 - 5/7/11/13次谐波，THD_I≈20%，THD_V≈7% */
    {
        0.0f, 0.0f, 0.0f,
        {
            [4] = 5.0f,    /* 5次 */
            [6] = 3.5f,    /* 7次 */
            [10] = 2.5f,   /* 11次 */
            [12] = 2.0f,   /* 13次 */
        },
        {
            [4] = 12.0f,   /* 5次 */
            [6] = 8.0f,    /* 7次 */
            [10] = 6.0f,   /* 11次 */
            [12] = 5.0f,   /* 13次 */
        },
        55.0f,
        80.0f, 0.95f
    },
    /* S3: 光伏场景 - 电压抬升+2.93%，偶次谐波 */
    {
        2.93f, 0.0f, 0.0f,
        {
            [1] = 1.5f,    /* 2次 */
            [3] = 0.8f,    /* 4次 */
            [5] = 0.5f,    /* 6次 */
        },
        {0},
        58.0f,
        200.0f, 0.99f
    },
    /* S4: 光充耦合场景 - S2+S3叠加 */
    {
        2.93f, 0.0f, 0.0f,
        {
            [1] = 1.5f,
            [3] = 0.8f,
            [5] = 0.5f,
        },
        {
            [4] = 12.0f,
            [6] = 8.0f,
            [10] = 6.0f,
            [12] = 5.0f,
        },
        50.0f,
        280.0f, 0.92f
    },
    /* S5: 极端场景 - THD更高 */
    {
        3.5f, 0.0f, 0.0f,
        {
            [1] = 2.0f,
            [3] = 1.2f,
            [5] = 0.8f,
        },
        {
            [4] = 15.0f,
            [6] = 10.0f,
            [10] = 8.0f,
            [12] = 6.0f,
            [16] = 4.0f,
            [18] = 3.0f,
        },
        45.0f,
        360.0f, 0.88f
    }
};

const sim_scenario_params_t* sim_get_scenario_params(uint16_t scenario)
{
    switch (scenario) {
        case SCEN_S1: return &s_scenarios[0];
        case SCEN_S2: return &s_scenarios[1];
        case SCEN_S3: return &s_scenarios[2];
        case SCEN_S4: return &s_scenarios[3];
        case SCEN_S5: return &s_scenarios[4];
        default:      return &s_scenarios[0];
    }
}

/* 生成白噪声 */
static float sim_noise(float snr_db)
{
    /* Box-Muller变换生成高斯噪声 */
    static int has_spare = 0;
    static float spare;
    float u1, u2, mag;
    
    if (has_spare) {
        has_spare = 0;
        return spare;
    }
    
    u1 = (float)rand() / RAND_MAX;
    u2 = (float)rand() / RAND_MAX;
    if (u1 < 1e-6f) u1 = 1e-6f;
    
    mag = sqrtf(-2.0f * logf(u1));
    spare = mag * sinf(2.0f * PI * u2);
    has_spare = 1;
    
    /* 根据SNR调整噪声幅度 */
    float signal_amp = PQ_NOMINAL_VOLTAGE;
    float noise_amp = signal_amp / powf(10.0f, snr_db / 20.0f);
    
    return mag * cosf(2.0f * PI * u2) * noise_amp;
}

void sim_generate_wave(ht7627s_wave_t *wave, const sim_scenario_params_t *params, uint32_t sample_rate)
{
    uint16_t pts = (sample_rate == PQ_SAMPLE_RATE_25600) ? 
                   PQ_POINTS_PER_CYCLE_25600 : PQ_POINTS_PER_CYCLE_12800;
    float dt = 1.0f / (float)sample_rate;
    float v_nom = PQ_NOMINAL_VOLTAGE;
    float v_offset = v_nom * sqrtf(2.0f) * (1.0f + params->voltage_offset / 100.0f);
    /* 电流基于负载功率计算 (三相星形) */
    float i_rms = 0.0f;
    if (params->load_active_kw > 0.0f && v_nom > 0.0f && params->load_pf > 0.0f) {
        float s_kva = params->load_active_kw / params->load_pf;
        i_rms = s_kva * 1000.0f / (3.0f * v_nom); /* I_phase = S / (3*V_phase) */
    }
    float i_peak = i_rms * sqrtf(2.0f);
    
    wave->valid_samples = pts;
    wave->cycle_id = s_cycle_id++;
    hal_ht7627s_get_time_us(&wave->timestamp_us);
    
    for (uint16_t n = 0; n < pts; n++) {
        float t = n * dt;
        float omega_t = 2.0f * PI * PQ_NOMINAL_FREQ * t;
        
        /* 三相电压 */
        float va = v_offset * sinf(omega_t);
        float vb = v_offset * sinf(omega_t - 2.0f * PI / 3.0f + params->unbalance_b);
        float vc = v_offset * sinf(omega_t + 2.0f * PI / 3.0f + params->unbalance_c);
        
        /* 三相电流 (考虑功率因数角) */
        float phi = (params->load_pf > 0.0f && params->load_pf <= 1.0f) ? acosf(params->load_pf) : 0.0f;
        float ia = i_peak * sinf(omega_t - phi);
        float ib = i_peak * sinf(omega_t - 2.0f * PI / 3.0f + params->unbalance_b - phi);
        float ic = i_peak * sinf(omega_t + 2.0f * PI / 3.0f + params->unbalance_c - phi);
        
        /* 添加电压谐波 */
        for (uint16_t h = 2; h <= PQ_MAX_HARMONIC_ORDER; h++) {
            if (params->harmonic_v[h-1] > 0.0f) {
                float amp = v_offset * params->harmonic_v[h-1] / 100.0f;
                float h_omega = omega_t * h;
                va += amp * sinf(h_omega);
                vb += amp * sinf(h_omega - 2.0f * PI / 3.0f * h);
                vc += amp * sinf(h_omega + 2.0f * PI / 3.0f * h);
            }
        }
        
        /* 添加电流谐波 */
        for (uint16_t h = 2; h <= PQ_MAX_HARMONIC_ORDER; h++) {
            if (params->harmonic_i[h-1] > 0.0f) {
                float amp = i_rms * sqrtf(2.0f) * params->harmonic_i[h-1] / 100.0f;
                float h_omega = omega_t * h;
                ia += amp * sinf(h_omega);
                ib += amp * sinf(h_omega - 2.0f * PI / 3.0f * h);
                ic += amp * sinf(h_omega + 2.0f * PI / 3.0f * h);
            }
        }
        
        /* 添加噪声 */
        va += sim_noise(params->noise_snr_db);
        vb += sim_noise(params->noise_snr_db);
        vc += sim_noise(params->noise_snr_db);
        ia += sim_noise(params->noise_snr_db);
        ib += sim_noise(params->noise_snr_db);
        ic += sim_noise(params->noise_snr_db);
        
        /* 零序电压 */
        float v0 = (va + vb + vc) / 3.0f;
        
        wave->samples[0][n] = va;
        wave->samples[1][n] = vb;
        wave->samples[2][n] = vc;
        wave->samples[3][n] = ia;
        wave->samples[4][n] = ib;
        wave->samples[5][n] = ic;
        wave->samples[6][n] = v0; /* 零序电压 */
    }
}

void sim_calc_regs_from_wave(ht7627s_regs_t *regs, const ht7627s_wave_t *wave)
{
    uint16_t pts = wave->valid_samples;
    float sum_sq[PQ_N_CHANNELS] = {0};
    
    memset(regs, 0, sizeof(ht7627s_regs_t));
    
    /* 计算RMS */
    for (uint16_t n = 0; n < pts; n++) {
        for (uint16_t ch = 0; ch < PQ_N_CHANNELS; ch++) {
            float s = wave->samples[ch][n];
            sum_sq[ch] += s * s;
        }
    }
    
    for (uint16_t ch = 0; ch < PQ_N_CHANNELS; ch++) {
        regs->rms[ch] = sqrtf(sum_sq[ch] / pts);
    }
    
    /* 简化THD和谐波计算 - 使用场景参数近似 */
    const sim_scenario_params_t *params = sim_get_scenario_params(g_sim_scenario);
    
    float v_harm_sum = 0.0f;
    float i_harm_sum = 0.0f;
    for (uint16_t h = 2; h <= PQ_MAX_HARMONIC_ORDER; h++) {
        regs->harmonics[h-1] = params->harmonic_v[h-1] + params->harmonic_i[h-1];
        v_harm_sum += params->harmonic_v[h-1] * params->harmonic_v[h-1];
        i_harm_sum += params->harmonic_i[h-1] * params->harmonic_i[h-1];
    }
    
    regs->thd_v = sqrtf(v_harm_sum);
    regs->thd_i = sqrtf(i_harm_sum);
    
    /* 功率计算 */
    float p_sum = 0.0f;
    for (uint16_t n = 0; n < pts; n++) {
        /* 瞬时功率 */
        float va = wave->samples[0][n];
        float ia = wave->samples[3][n];
        float vb = wave->samples[1][n];
        float ib = wave->samples[4][n];
        float vc = wave->samples[2][n];
        float ic = wave->samples[5][n];
        
        p_sum += (va * ia + vb * ib + vc * ic) / 1000.0f; /* kW */
    }
    
    regs->active_power = p_sum / pts;
    
    /* 无功功率（90°相移法，每相独立计算后求和） */
    int shift_90 = pts / 4;
    float q_sum = 0.0f;
    for (uint16_t n = 0; n < pts; n++) {
        float va = wave->samples[0][n];
        float vb = wave->samples[1][n];
        float vc = wave->samples[2][n];
        int idx_a = (n + shift_90) % pts;
        float ia_90 = wave->samples[3][idx_a];
        float ib_90 = wave->samples[4][idx_a];
        float ic_90 = wave->samples[5][idx_a];
        q_sum += (va * ia_90 + vb * ib_90 + vc * ic_90) / 1000.0f;
    }
    regs->reactive_power = q_sum / pts;
    regs->apparent_power = sqrtf(regs->active_power * regs->active_power + 
                                  regs->reactive_power * regs->reactive_power);
    
    if (regs->apparent_power > 0.0f) {
        regs->power_factor = regs->active_power / regs->apparent_power;
    } else {
        regs->power_factor = 1.0f;
    }
    
    regs->frequency = PQ_NOMINAL_FREQ;
    
    /* 相位角 */
    for (uint16_t ch = 0; ch < PQ_N_CHANNELS; ch++) {
        regs->phase_angle[ch] = 0.0f;
    }
}

int hal_ht7627s_init(const ht7627s_cfg_t *cfg)
{
    if (s_initialized) {
        return 0;
    }
    
    memcpy(&s_cfg, cfg, sizeof(ht7627s_cfg_t));
    s_cycle_id = 0;
    
#ifdef PLATFORM_WINDOWS
    QueryPerformanceFrequency(&s_qpc_freq);
    InitializeCriticalSection(&s_crit);
    srand((unsigned int)GetTickCount());
#else
    pthread_mutex_init(&s_crit, NULL);
    srand((unsigned int)time(NULL));
#endif
    
    s_initialized = 1;
    
    PQ_LOGI("HT7627S simulator initialized, sample_rate=%u, scenario=%c%c", 
            cfg->sample_rate, (char)(g_sim_scenario >> 8), (char)g_sim_scenario);
    return 0;
}

int hal_ht7627s_read_regs(ht7627s_regs_t *regs)
{
    ht7627s_wave_t wave;
    const sim_scenario_params_t *params = sim_get_scenario_params(g_sim_scenario);
    
    sim_generate_wave(&wave, params, s_cfg.sample_rate);
    sim_calc_regs_from_wave(regs, &wave);
    
    return 0;
}

int hal_ht7627s_read_wave(ht7627s_wave_t *wave)
{
    const sim_scenario_params_t *params = sim_get_scenario_params(g_sim_scenario);
    sim_generate_wave(wave, params, s_cfg.sample_rate);
    return 0;
}

int hal_ht7627s_get_time_us(uint64_t *t)
{
#ifdef PLATFORM_WINDOWS
    LARGE_INTEGER qpc;
    QueryPerformanceCounter(&qpc);
    *t = (uint64_t)(qpc.QuadPart * 1000000LL / s_qpc_freq.QuadPart);
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    *t = (uint64_t)tv.tv_sec * 1000000ULL + tv.tv_usec;
#endif
    return 0;
}

void hal_sleep_ms(uint32_t ms)
{
#ifdef PLATFORM_WINDOWS
    Sleep(ms);
#else
    poll(NULL, 0, (int)ms);
#endif
}

void hal_critical_enter(void)
{
#ifdef PLATFORM_WINDOWS
    EnterCriticalSection(&s_crit);
#else
    pthread_mutex_lock(&s_crit);
#endif
}

void hal_critical_exit(void)
{
#ifdef PLATFORM_WINDOWS
    LeaveCriticalSection(&s_crit);
#else
    pthread_mutex_unlock(&s_crit);
#endif
}
