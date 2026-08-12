/**
 * @file hal_adapter.h
 * @brief HAL接口适配层，对接T536真实硬件驱动
 * 
 * 此文件定义了与真实硬件HAL库交互所需的函数声明
 * 参考 zdh_CL818C50_DTAnalyzer 项目的 hal_frame_hd.h
 */

#ifndef HAL_ADAPTER_H
#define HAL_ADAPTER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== 前向声明 ==================== */
/* HW_DEVICE 结构已在 wave_sampler_hal.h 中定义 */

/* ==================== HAL框架函数接口 ==================== */

/**
 * @brief 打开驱动设备
 * @param[in] device_id: 设备名称
 * @return 成功返回设备操作函数接口; 失败返回NULL
 */
struct tag_HW_DEVICE *hal_device_get(const char *device_id);

/**
 * @brief 关闭驱动设备
 * @param[in] dev: 设备句柄
 * @return 成功返回0; 失败返回错误编号
 */
int32_t hal_device_release(struct tag_HW_DEVICE *dev);

/* ==================== 波形采样设备接口 ==================== */

/* 波形采样设备ID */
#define WAVEFORM_SAMPLER_DEVICE_ID "waveform_sampler"

#ifdef __cplusplus
}
#endif

#endif /* HAL_ADAPTER_H */
