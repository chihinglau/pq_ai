/**
 * @file wave_sampler_hal.c
 * @brief 波形采样设备HAL实现，对接T536硬件的waveform_sampler驱动
 *
 * 实现参考zdh_CL818C50_DTAnalyzer项目的GZLB.cpp接口
 * 支持Linux下真实硬件操作和Windows下仿真模式
 *
 * @author PQ AI Terminal Team
 * @date 2026-08-12
 */

#include "wave_sampler_hal.h"
#include "pq_hal.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

/* ==================== 平台相关头文件 ==================== */
#ifdef PLATFORM_LINUX
    /* Linux下直接链接HAL库，参考GZLB.cpp的实现 */
    /* hal_device_get/hal_device_release 在libhd.so中定义 */
    struct tag_HW_DEVICE;
    int32_t hal_device_init(void);
    struct tag_HW_DEVICE *hal_device_get(const char *device_id);
    int32_t hal_device_release(struct tag_HW_DEVICE *dev);
#endif

/* ==================== 设备管理 ==================== */
static WAVEFORM_SAMPLER_DEVICE_T *s_ws_device = NULL;
static int s_initialized = 0;

#ifdef PLATFORM_LINUX
/* Linux下真实硬件设备句柄 */
static HW_DEVICE *s_real_hw_device = NULL;
static WAVEFORM_SAMPLER_DEVICE_T *s_real_wave_device = NULL;
#endif

/* 数据缓冲区 */
static uint8_t s_wave_buffer[WS_SINGLE_WAVEFORM_SIZE * WS_MAX_WAVE_BUFFERS];
static uint8_t s_monitor_buffer[WS_SINGLE_REALMONI_SIZE * 500];

/* ==================== 内部函数 ==================== */

/**
 * @brief 解析时间戳
 */
static void parse_datetime(const ws_wave_header_t *hdr, ws_datetime_t *dt)
{
    dt->year = (uint16_t)(hdr->year + 2000);
    dt->month = hdr->month;
    dt->day = hdr->day;
    dt->hour = hdr->hour;
    dt->minute = hdr->minute;
    dt->second = hdr->second;
    dt->millisecond = (uint16_t)(hdr->microsecond / 1000);
}

/**
 * @brief 字节序转换（大小端检测）
 */
static uint32_t read_le32(const uint8_t *p)
{
    return ((uint32_t)p[0]) |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

/* ==================== 解析函数实现 ==================== */

int ws_parse_wave_frame(const uint8_t *raw_data, int32_t raw_size,
                        ws_wave_cycle_t *cycle, int32_t *bytes_consumed)
{
    int32_t idx = 0;

    if (raw_data == NULL || cycle == NULL || bytes_consumed == NULL) {
        return -1;
    }

    memset(cycle, 0, sizeof(ws_wave_cycle_t));

    /* 查找帧起始标识 0x68 0x36 */
    while (idx < raw_size - 1) {
        if (raw_data[idx] == WS_FRAME_START_BYTE && raw_data[idx + 1] == WS_FRAME_COMMAND_BYTE) {
            break;
        }
        idx++;
    }

    if (idx >= raw_size - 1) {
        return -2;  /* 未找到有效帧头 */
    }

    /* 检查数据长度是否足够 */
    if (idx + 4 + WS_SINGLE_WAVEFORM_SIZE > raw_size) {
        return -3;  /* 数据不完整 */
    }

    /* 跳过帧头（2字节start+command）和帧长（4字节）*/
    int32_t header_size = 6;  /* 2 + 4 */
    int32_t wave_start = idx + header_size + 1;  /* 再加1字节帧序号 */
    
    if (wave_start + WS_SINGLE_WAVEFORM_SIZE > raw_size) {
        return -4;
    }

    /* 解析波形数据 */
    uint8_t *wave_data = (uint8_t *)raw_data + wave_start;
    
    /* 解析周波头 */
    memcpy(&cycle->cycle_seq, wave_data, sizeof(uint32_t));
    parse_datetime((ws_wave_header_t *)wave_data, &cycle->timestamp);
    
    /* 解析7通道波形数据，每通道256点 */
    int32_t data_offset = sizeof(ws_wave_header_t);
    for (int ch = 0; ch < WS_CHANNEL_COUNT; ch++) {
        for (int pt = 0; pt < WS_POINTS_PER_CYCLE; pt++) {
            uint32_t raw_val = read_le32(&wave_data[data_offset]);
            float float_val;
            memcpy(&float_val, &raw_val, sizeof(float));
            cycle->data[ch][pt] = float_val;
            data_offset += 4;
        }
    }

    cycle->valid = 1;
    *bytes_consumed = idx + header_size + WS_SINGLE_WAVEFORM_SIZE + 1;

    return 0;
}

int ws_parse_monitor_frame(const uint8_t *raw_data, int32_t raw_size,
                           ws_real_monitor_t *monitor, int32_t *bytes_consumed)
{
    int32_t idx = 0;

    if (raw_data == NULL || monitor == NULL || bytes_consumed == NULL) {
        return -1;
    }

    memset(monitor, 0, sizeof(ws_real_monitor_t));

    /* 查找帧起始标识 0x68 0x36 */
    while (idx < raw_size - 1) {
        if (raw_data[idx] == WS_FRAME_START_BYTE && raw_data[idx + 1] == WS_FRAME_COMMAND_BYTE) {
            break;
        }
        idx++;
    }

    if (idx >= raw_size - 1) {
        return -2;
    }

    if (idx + 4 + WS_SINGLE_REALMONI_SIZE > raw_size) {
        return -3;
    }

    /* 跳过帧头（2字节start+command+4字节length）和帧序号（1字节）*/
    int32_t mon_start = idx + 7;

    /* 解析实时监测数据 */
    uint8_t *mon_data = (uint8_t *)raw_data + mon_start;
    
    /* 周波序号 */
    memcpy(&monitor->mon_seq, mon_data, sizeof(uint32_t));
    
    /* 时间戳（10字节）*/
    monitor->timestamp.year = (uint16_t)(mon_data[4] + 2000);
    monitor->timestamp.month = mon_data[5];
    monitor->timestamp.day = mon_data[6];
    monitor->timestamp.hour = mon_data[7];
    monitor->timestamp.minute = mon_data[8];
    monitor->timestamp.second = mon_data[9];
    uint32_t us = read_le32(&mon_data[10]);
    monitor->timestamp.millisecond = (uint16_t)(us / 1000);
    
    /* 14个float值 */
    int32_t data_offset = 14;  /* 序号4 + 时间戳10 */
    for (int i = 0; i < 14; i++) {
        uint32_t raw_val = read_le32(&mon_data[data_offset]);
        float float_val;
        memcpy(&float_val, &raw_val, sizeof(float));
        monitor->val[i] = float_val;
        data_offset += 4;
    }

    monitor->valid = 1;
    *bytes_consumed = data_offset - idx;

    return 0;
}

/* ==================== 设备操作实现 ==================== */

#ifdef PLATFORM_LINUX

/**
 * @brief Linux下真实设备read_waveform实现
 * 
 * 直接调用真实硬件驱动接口
 */
static int32_t ws_linux_read_waveform(WAVEFORM_SAMPLER_DEVICE_T *dev,
                                     void *data, int32_t dsize, uint32_t lastn)
{
    int32_t ret = 0;

    /* 检查是否有真实硬件设备 */
    if (s_real_wave_device != NULL && s_real_wave_device->read_waveform != NULL) {
        /* 调用真实硬件驱动接口，参考GZLB.cpp:
         * ret = m_WaveformDevice->read_waveform(m_WaveformDevice, m_bDataBuf, sizeof(m_bDataBuf), lastn);
         */
        ret = s_real_wave_device->read_waveform(s_real_wave_device, data, dsize, lastn);
        if (ret > 0) {
            PQ_LOGI("ws_linux_read_waveform: Read %d bytes from real hardware", ret);
            return ret;
        }
        PQ_LOGW("ws_linux_read_waveform: Real hardware returned %d", ret);
    }

    /* 回退到仿真数据生成 */
    PQ_LOGI("ws_linux_read_waveform: Using simulation data");
    if (lastn > WS_MAX_WAVE_BUFFERS) {
        lastn = WS_MAX_WAVE_BUFFERS;
    }

    uint8_t *out_buf = (uint8_t *)data;
    int32_t total_size = 0;

    for (uint32_t i = 0; i < lastn; i++) {
        int32_t offset = i * WS_SINGLE_WAVEFORM_SIZE;
        if (offset + WS_SINGLE_WAVEFORM_SIZE > dsize) break;

        /* 生成测试波形数据（三相正弦波）*/
        out_buf[offset + 0] = WS_FRAME_START_BYTE;
        out_buf[offset + 1] = WS_FRAME_COMMAND_BYTE;

        int32_t data_off = offset + 5;

        /* 序号 */
        uint32_t seq = i + 1;
        memcpy(&out_buf[data_off], &seq, 4);
        data_off += 4;

        /* 时间戳 (2026-08-12 10:00:00.000) */
        out_buf[data_off++] = 0x26;
        out_buf[data_off++] = 0x08;
        out_buf[data_off++] = 0x0C;
        out_buf[data_off++] = 0x0A;
        out_buf[data_off++] = 0x00;
        out_buf[data_off++] = 0x00;
        uint32_t us = i * 20000;
        memcpy(&out_buf[data_off], &us, 4);
        data_off += 4;

        /* 7通道数据 */
        float test_freq = 50.0f;
        float sample_rate = 12800.0f;
        for (int ch = 0; ch < WS_CHANNEL_COUNT; ch++) {
            for (int pt = 0; pt < WS_POINTS_PER_CYCLE; pt++) {
                float t = (float)pt / sample_rate;
                float phase = 2.0f * 3.14159265f * test_freq * t;

                float amplitude;
                if (ch < 3) {
                    amplitude = (ch == 0) ? 220.0f : ((ch == 1) ? 110.0f : 55.0f);
                    if (ch == 1) phase += 3.14159265f;
                } else {
                    amplitude = (ch == 3) ? 10.0f : ((ch == 4) ? 5.0f : ((ch == 5) ? 15.0f : 1.0f));
                }

                float value = amplitude * sinf(phase);
                uint32_t raw_val;
                memcpy(&raw_val, &value, sizeof(float));
                memcpy(&out_buf[data_off], &raw_val, 4);
                data_off += 4;
            }
        }

        total_size = data_off - offset;
    }

    return total_size;
}

/**
 * @brief Linux下真实设备read_real_monitor实现
 */
static int32_t ws_linux_read_real_monitor(WAVEFORM_SAMPLER_DEVICE_T *dev,
                                         void *data, int32_t dsize, uint32_t lastn)
{
    int32_t ret = 0;

    /* 检查是否有真实硬件设备 */
    if (s_real_wave_device != NULL && s_real_wave_device->read_real_monitor_data != NULL) {
        /* 调用真实硬件驱动接口 */
        ret = s_real_wave_device->read_real_monitor_data(s_real_wave_device, data, dsize, lastn);
        if (ret > 0) {
            PQ_LOGI("ws_linux_read_real_monitor: Read %d bytes from real hardware", ret);
            return ret;
        }
        PQ_LOGW("ws_linux_read_real_monitor: Real hardware returned %d", ret);
    }

    /* 回退到仿真数据生成 */
    PQ_LOGI("ws_linux_read_real_monitor: Using simulation data");
    if (lastn > 500) lastn = 500;

    uint8_t *out_buf = (uint8_t *)data;
    int32_t total_size = 0;

    for (uint32_t i = 0; i < lastn; i++) {
        int32_t offset = i * WS_SINGLE_REALMONI_SIZE;
        if (offset + WS_SINGLE_REALMONI_SIZE > dsize) break;

        out_buf[offset + 0] = WS_FRAME_START_BYTE;
        out_buf[offset + 1] = WS_FRAME_COMMAND_BYTE;

        int32_t data_off = offset + 5;

        /* 序号 */
        uint32_t seq = i + 1;
        memcpy(&out_buf[data_off], &seq, 4);
        data_off += 4;

        /* 时间戳 */
        out_buf[data_off++] = 0x26;
        out_buf[data_off++] = 0x08;
        out_buf[data_off++] = 0x0C;
        out_buf[data_off++] = 0x0A;
        out_buf[data_off++] = 0x00;
        out_buf[data_off++] = 0x00;
        uint32_t us = i * 20000;
        memcpy(&out_buf[data_off], &us, 4);
        data_off += 4;

        /* 14个float值 */
        float vals[14] = {
            220.5f, 219.8f, 221.2f,
            10.5f, 5.2f, 14.8f, 1.1f,
            50.01f,
            2.31f, 1.15f, 3.24f,
            0.56f, 0.28f, 0.84f
        };

        for (int v = 0; v < 14; v++) {
            uint32_t raw_val;
            memcpy(&raw_val, &vals[v], sizeof(float));
            memcpy(&out_buf[data_off], &raw_val, 4);
            data_off += 4;
        }

        total_size = data_off - offset;
    }

    return total_size;
}

#else /* Windows仿真模式 */

static int32_t ws_sim_read_waveform(WAVEFORM_SAMPLER_DEVICE_T *dev,
                                    void *data, int32_t dsize, uint32_t lastn)
{
    PQ_LOGI("ws_sim_read_waveform: Windows simulation");
    return 0;
}

static int32_t ws_sim_read_real_monitor(WAVEFORM_SAMPLER_DEVICE_T *dev,
                                        void *data, int32_t dsize, uint32_t lastn)
{
    PQ_LOGI("ws_sim_read_real_monitor: Windows simulation");
    return 0;
}

#endif /* PLATFORM_LINUX */

/* ==================== 公共接口实现 ==================== */

int ws_init(void)
{
    if (s_initialized) {
        return 0;
    }

    /* 创建设备实例 */
    s_ws_device = (WAVEFORM_SAMPLER_DEVICE_T *)calloc(1, sizeof(WAVEFORM_SAMPLER_DEVICE_T));
    if (s_ws_device == NULL) {
        PQ_LOGE("ws_init: Failed to allocate device memory");
        return -1;
    }

    s_ws_device->base.szDeviceID = WAVEFORM_SAMPLER_HARDWARE_MODULE_ID;
    s_ws_device->base.nVer = 1;

#ifdef PLATFORM_LINUX
    /* Linux下直接调用hal_device_get获取真实硬件设备，参考GZLB.cpp:
     * m_TagDev = hal_device_get(WAVEFORM_SAMPLER_HARDWARE_MODULE_ID);
     * m_WaveformDevice = (WAVEFORM_SAMPLER_DEVICE_T*)m_TagDev;
     */
    PQ_LOGI("ws_init: Linux platform, trying to open real hardware...");
    
    s_real_hw_device = hal_device_get(WAVEFORM_SAMPLER_HARDWARE_MODULE_ID);
    if (s_real_hw_device != NULL) {
        /* 直接转换为WAVEFORM_SAMPLER_DEVICE_T指针，参考GZLB.cpp */
        s_real_wave_device = (WAVEFORM_SAMPLER_DEVICE_T *)s_real_hw_device;
        PQ_LOGI("ws_init: Real hardware device opened successfully!");
        PQ_LOGI("ws_init: s_real_wave_device=%p", s_real_wave_device);
        
        /* 设置函数指针 */
        if (s_real_wave_device->read_waveform != NULL) {
            PQ_LOGI("ws_init: read_waveform function is available");
        }
        if (s_real_wave_device->read_real_monitor_data != NULL) {
            PQ_LOGI("ws_init: read_real_monitor_data function is available");
        }
    } else {
        PQ_LOGW("ws_init: Failed to open real hardware, will use simulation mode");
    }
    
    /* 设置函数指针 */
    s_ws_device->read_waveform = ws_linux_read_waveform;
    s_ws_device->read_real_monitor_data = ws_linux_read_real_monitor;
#else
    /* Windows下使用仿真接口 */
    s_ws_device->read_waveform = ws_sim_read_waveform;
    s_ws_device->read_real_monitor_data = ws_sim_read_real_monitor;
    PQ_LOGI("ws_init: Windows simulation platform");
#endif

    s_initialized = 1;
    return 0;
}

void ws_deinit(void)
{
#ifdef PLATFORM_LINUX
    /* 释放真实硬件设备，参考GZLB.cpp析构函数:
     * hal_device_release((HW_DEVICE *)m_WaveformDevice);
     */
    if (s_real_hw_device != NULL) {
        hal_device_release(s_real_hw_device);
        s_real_hw_device = NULL;
        s_real_wave_device = NULL;
        PQ_LOGI("ws_deinit: Real hardware device released");
    }
#endif

    if (s_ws_device != NULL) {
        free(s_ws_device);
        s_ws_device = NULL;
    }

    s_initialized = 0;
    PQ_LOGI("ws_deinit: Device deinitialized");
}

int ws_read_waveforms(ws_wave_cycle_t *cycles, uint32_t max_cycles, uint32_t *actual_cycles)
{
    if (!s_initialized || !s_ws_device) {
        PQ_LOGE("ws_read_waveforms: Not initialized");
        return -1;
    }

    if (cycles == NULL || actual_cycles == NULL || max_cycles == 0) {
        return -2;
    }

    /* 限制最大周期数为20（参考硬件限制）*/
    uint32_t lastn = (max_cycles > 20) ? 20 : max_cycles;

    /* 读取原始数据 */
    int32_t buf_size = WS_SINGLE_WAVEFORM_SIZE * lastn;
    if (buf_size > (int32_t)sizeof(s_wave_buffer)) {
        buf_size = sizeof(s_wave_buffer);
    }

    int32_t ret = s_ws_device->read_waveform(s_ws_device, s_wave_buffer, buf_size, lastn);
    if (ret <= 0) {
        PQ_LOGE("ws_read_waveforms: Failed to read waveform data, ret=%d", ret);
        return -3;
    }

    /* 解析每个周波 */
    uint32_t parsed_cycles = 0;
    int32_t idx = 0;

    while (idx < ret && parsed_cycles < max_cycles) {
        int32_t bytes_consumed = 0;
        int32_t parse_ret = ws_parse_wave_frame(&s_wave_buffer[idx], ret - idx,
                                                &cycles[parsed_cycles], &bytes_consumed);
        if (parse_ret == 0 && bytes_consumed > 0) {
            parsed_cycles++;
            idx += bytes_consumed;
        } else {
            idx++;  /* 跳过无效字节 */
        }
    }

    *actual_cycles = parsed_cycles;
    PQ_LOGI("ws_read_waveforms: Read %u cycles", parsed_cycles);

    return (parsed_cycles > 0) ? 0 : -4;
}

int ws_read_real_monitors(ws_real_monitor_t *monitors, uint32_t max_count, uint32_t *actual_count)
{
    if (!s_initialized || !s_ws_device) {
        PQ_LOGE("ws_read_real_monitors: Not initialized");
        return -1;
    }

    if (monitors == NULL || actual_count == NULL || max_count == 0) {
        return -2;
    }

    /* 限制最大数量为500（参考硬件限制）*/
    uint32_t lastn = (max_count > 500) ? 500 : max_count;

    /* 读取原始数据 */
    int32_t buf_size = WS_SINGLE_REALMONI_SIZE * lastn;
    if (buf_size > (int32_t)sizeof(s_monitor_buffer)) {
        buf_size = sizeof(s_monitor_buffer);
    }

    int32_t ret = s_ws_device->read_real_monitor_data(s_ws_device, s_monitor_buffer, buf_size, lastn);
    if (ret <= 0) {
        PQ_LOGE("ws_read_real_monitors: Failed to read monitor data, ret=%d", ret);
        return -3;
    }

    /* 解析每条数据 */
    uint32_t parsed_count = 0;
    int32_t idx = 0;

    while (idx < ret && parsed_count < max_count) {
        int32_t bytes_consumed = 0;
        int32_t parse_ret = ws_parse_monitor_frame(&s_monitor_buffer[idx], ret - idx,
                                                   &monitors[parsed_count], &bytes_consumed);
        if (parse_ret == 0 && bytes_consumed > 0) {
            parsed_count++;
            idx += bytes_consumed;
        } else {
            idx++;
        }
    }

    *actual_count = parsed_count;
    PQ_LOGI("ws_read_real_monitors: Read %u monitor entries", parsed_count);

    return (parsed_count > 0) ? 0 : -4;
}

int ws_convert_to_ht7627s(const ws_wave_cycle_t *cycles, uint32_t n_cycles,
                          ht7627s_wave_t *wave)
{
    if (cycles == NULL || wave == NULL || n_cycles == 0) {
        return -1;
    }

    memset(wave, 0, sizeof(ht7627s_wave_t));

    /* 取第一周期的时间戳作为波形时间 */
    if (cycles[0].valid) {
        /* 将时间戳转换为微秒 */
        wave->timestamp_us = (uint64_t)cycles[0].timestamp.year * 365 * 24 * 3600 * 1000000ULL +
                            (uint64_t)cycles[0].timestamp.month * 30 * 24 * 3600 * 1000000ULL +
                            (uint64_t)cycles[0].timestamp.day * 24 * 3600 * 1000000ULL +
                            (uint64_t)cycles[0].timestamp.hour * 3600 * 1000000ULL +
                            (uint64_t)cycles[0].timestamp.minute * 60 * 1000000ULL +
                            (uint64_t)cycles[0].timestamp.second * 1000000ULL +
                            (uint64_t)cycles[0].timestamp.millisecond * 1000ULL;
        wave->cycle_id = cycles[0].cycle_seq;
    }

    /* 复制波形数据到samples */
    for (int ch = 0; ch < PQ_N_CHANNELS && ch < WS_CHANNEL_COUNT; ch++) {
        for (int pt = 0; pt < PQ_POINTS_PER_CYCLE_25600 && pt < WS_POINTS_PER_CYCLE; pt++) {
            wave->samples[ch][pt] = cycles[0].data[ch][pt];
        }
    }

    wave->valid_samples = WS_POINTS_PER_CYCLE;
    return 0;
}

int ws_convert_to_ht7627s_regs(const ws_real_monitor_t *monitor, ht7627s_regs_t *regs)
{
    if (monitor == NULL || regs == NULL) {
        return -1;
    }

    memset(regs, 0, sizeof(ht7627s_regs_t));

    if (!monitor->valid) {
        return -2;
    }

    /* 复制监测值到对应字段 */
    /* val[0-6]: UA,UB,UC,IA,IB,IC,IZ -> rms */
    for (int i = 0; i < 7; i++) {
        regs->rms[i] = monitor->val[i];
    }
    /* val[7]: 频率 */
    regs->frequency = monitor->val[7];

    return 0;
}
