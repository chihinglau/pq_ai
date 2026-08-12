/**
 * @file wave_sampler_hal.h
 * @brief 波形采样设备HAL接口，对接T536硬件的waveform_sampler驱动
 *
 * 参考 zdh_CL818C50_DTAnalyzer 项目的 GZLB.cpp / GZLB.h 接口实现
 * 硬件接口定义：hal_device_waveform_sampler.h
 *
 * 波形数据帧格式（与硬件驱动一致）：
 *   帧头: 0x68 0x36 (2字节)
 *   帧长: 7182 字节 (SINGLE_WAVEFORM_SIZE)
 *   组成: 4字节长度 + 1字节序号 + 10字节时间戳 + 7通道×256点×4字节(float)
 *
 * 实时监测量数据格式：
 *   大小: 70字节 (SINGLE_REALMONI_SIZE)
 *   组成: 4字节序号 + 10字节时间戳 + 14个float值
 *
 * @author PQ AI Terminal Team
 * @date 2026-08-12
 */

#ifndef WAVE_SAMPLER_HAL_H
#define WAVE_SAMPLER_HAL_H

#include "pq_common.h"
#include "pq_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 设备ID（与硬件驱动一致）*/
#define WAVEFORM_SAMPLER_HARDWARE_MODULE_ID "waveform_sampler"

/* 波形数据常量（与硬件驱动一致）*/
#define WS_SINGLE_WAVEFORM_SIZE    7182   /* 单波形数据大小 */
#define WS_SINGLE_REALMONI_SIZE    70     /* 单实时监测数据大小 */
#define WS_CHANNEL_COUNT           7      /* 模拟通道数: UA,UB,UC,IA,IB,IC,IZ */
#define WS_POINTS_PER_CYCLE        256    /* 每周期采样点数 */
#define WS_MAX_WAVE_BUFFERS        20     /* 最多缓存周波数 */

/* 帧头标识 */
#define WS_FRAME_START_BYTE        0x68   /* 帧起始字节 */
#define WS_FRAME_COMMAND_BYTE      0x36   /* 帧命令字节 */

/* 通道索引 */
#define WS_CH_UA   0   /* A相电压 */
#define WS_CH_UB   1   /* B相电压 */
#define WS_CH_UC   2   /* C相电压 */
#define WS_CH_IA   3   /* A相电流 */
#define WS_CH_IB   4   /* B相电流 */
#define WS_CH_IC   5   /* C相电流 */
#define WS_CH_IZ   6   /* 零序电流 */

/* 实时监测量索引 */
#define WS_MON_F_UA   0   /* A相电压有效值(V) */
#define WS_MON_F_UB   1   /* B相电压有效值(V) */
#define WS_MON_F_UC   2   /* C相电压有效值(V) */
#define WS_MON_F_IA   3   /* A相电流有效值(A) */
#define WS_MON_F_IB   4   /* B相电流有效值(A) */
#define WS_MON_F_IC   5   /* C相电流有效值(A) */
#define WS_MON_F_IZ   6   /* 零序电流有效值(A) */
#define WS_MON_F_FREQ 7   /* 频率(Hz) */
#define WS_MON_P_UA   8   /* A相有功功率(kW) */
#define WS_MON_P_UB   9   /* B相有功功率(kW) */
#define WS_MON_P_UC   10  /* C相有功功率(kW) */
#define WS_MON_Q_UA   11  /* A相无功功率(kvar) */
#define WS_MON_Q_UB   12  /* B相无功功率(kvar) */
#define WS_MON_Q_UC   13  /* C相无功功率(kvar) */

/* ==================== 数据结构定义 ==================== */

/* HAL基础设备结构 - 必须与真实HAL库布局兼容
 * 真实定义:
 *   struct tag_HW_MODULE *pModule;  // 指针 8字节
 *   int32 nVer;                      // 4字节
 *   const char *szDeviceID;          // 指针 8字节
 * 总大小: 24字节 (含对齐)
 */
struct tag_HW_MODULE;  /* 前向声明 */
typedef struct tag_HW_DEVICE {
    struct tag_HW_MODULE *pModule;   /* 设备所属模块对象指针 */
    int32_t nVer;                    /* 设备接口版本号 */
    const char *szDeviceID;          /* 设备ID名 */
} HW_DEVICE;

/* 波形数据帧头（用于解析）*/
#pragma pack(1)
typedef struct {
    uint8_t  start;           /* 0x68 */
    uint8_t  command;         /* 0x36 */
    uint32_t length;          /* 数据段长度 */
    uint8_t  frame_seq;       /* 帧序号 */
} ws_frame_header_t;

/* 周波头信息（波形数据起始处）*/
typedef struct {
    uint32_t cycle_seq;       /* 周波序号 */
    uint8_t  year;            /* 年份(HEX) */
    uint8_t  month;           /* 月份(HEX) */
    uint8_t  day;             /* 日期(HEX) */
    uint8_t  hour;            /* 小时(HEX) */
    uint8_t  minute;          /* 分钟(HEX) */
    uint8_t  second;          /* 秒(HEX) */
    uint32_t microsecond;     /* 微秒 */
} ws_wave_header_t;

/* 时间戳结构体 */
typedef struct {
    uint16_t year;
    uint8_t  month;
    uint8_t  day;
    uint8_t  hour;
    uint8_t  minute;
    uint8_t  second;
    uint16_t millisecond;
} ws_datetime_t;

/* 单周波波形数据（解析后）*/
typedef struct {
    float    data[WS_CHANNEL_COUNT][WS_POINTS_PER_CYCLE];  /* 7通道×256点浮点数据 */
    uint32_t cycle_seq;            /* 周波序号 */
    ws_datetime_t timestamp;       /* 时间戳 */
    uint8_t  valid;                /* 是否有效 */
} ws_wave_cycle_t;

/* 实时监测量数据（解析后）*/
typedef struct {
    float    val[WS_SINGLE_REALMONI_SIZE / 4];  /* 14个float值 */
    uint32_t mon_seq;               /* 监测量序号 */
    ws_datetime_t timestamp;         /* 时间戳 */
    uint8_t  valid;                 /* 是否有效 */
} ws_real_monitor_t;

/* 波形采样设备抽象结构（与硬件HAL接口兼容）*/
typedef struct tag_WAVEFORM_SAMPLER_DEVICE {
    HW_DEVICE base;
    int32_t (*read_waveform)(struct tag_WAVEFORM_SAMPLER_DEVICE *dev,
                             void *data, int32_t dsize, uint32_t lastn);
    int32_t (*read_real_monitor_data)(struct tag_WAVEFORM_SAMPLER_DEVICE *dev,
                                      void *data, int32_t dsize, uint32_t lastn);
} WAVEFORM_SAMPLER_DEVICE_T;

/* 兼容宏 */
#define read_real_monitor read_real_monitor_data

/* ==================== 公共接口 ==================== */

/**
 * @brief 初始化波形采样设备
 * @return 0成功, 非0失败
 */
int ws_init(void);

/**
 * @brief 反初始化波形采样设备
 */
void ws_deinit(void);

/**
 * @brief 读取指定数量的最新周波数据
 * @param cycles 输出周波数组指针（至少max_cycles个元素）
 * @param max_cycles 最大读取周期数（建议1-20）
 * @param actual_cycles 实际读取周期数输出
 * @return 0成功, 非0失败
 */
int ws_read_waveforms(ws_wave_cycle_t *cycles, uint32_t max_cycles, uint32_t *actual_cycles);

/**
 * @brief 读取指定数量的实时监测量数据
 * @param monitors 输出监测量数组指针
 * @param max_count 最大读取数量（建议1-500）
 * @param actual_count 实际读取数量输出
 * @return 0成功, 非0失败
 */
int ws_read_real_monitors(ws_real_monitor_t *monitors, uint32_t max_count, uint32_t *actual_count);

/**
 * @brief 将解析后的波形数据转换为ht7627s_wave_t格式
 * @param cycles 输入周波数组
 * @param n_cycles 周波数量
 * @param wave 输出的ht7627s_wave_t结构
 * @return 0成功, 非0失败
 */
int ws_convert_to_ht7627s(const ws_wave_cycle_t *cycles, uint32_t n_cycles,
                          ht7627s_wave_t *wave);

/**
 * @brief 将解析后的实时监测量数据转换为ht7627s_regs_t格式
 * @param monitor 输入监测量数据
 * @param regs 输出的ht7627s_regs_t结构
 * @return 0成功, 非0失败
 */
int ws_convert_to_ht7627s_regs(const ws_real_monitor_t *monitor, ht7627s_regs_t *regs);

/**
 * @brief 从原始字节数据解析单个周波
 * @param raw_data 原始字节数据指针
 * @param raw_size 原始数据大小
 * @param cycle 输出解析后的周波数据
 * @param bytes_consumed 解析消耗的字节数
 * @return 0成功, 非0失败
 */
int ws_parse_wave_frame(const uint8_t *raw_data, int32_t raw_size,
                        ws_wave_cycle_t *cycle, int32_t *bytes_consumed);

/**
 * @brief 从原始字节数据解析单个实时监测量
 * @param raw_data 原始字节数据指针
 * @param raw_size 原始数据大小
 * @param monitor 输出解析后的监测量数据
 * @param bytes_consumed 解析消耗的字节数
 * @return 0成功, 非0失败
 */
int ws_parse_monitor_frame(const uint8_t *raw_data, int32_t raw_size,
                           ws_real_monitor_t *monitor, int32_t *bytes_consumed);

#ifdef __cplusplus
}
#endif

#endif /* WAVE_SAMPLER_HAL_H */
