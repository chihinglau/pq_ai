/**
 * @file ht7627s_regs.h
 * @brief HT7627S寄存器地址定义
 */

#ifndef HT7627S_REGS_H
#define HT7627S_REGS_H

/* 电压/电流RMS寄存器 */
#define HT7627S_REG_RMS_UA     0x00
#define HT7627S_REG_RMS_UB     0x01
#define HT7627S_REG_RMS_UC     0x02
#define HT7627S_REG_RMS_IA     0x03
#define HT7627S_REG_RMS_IB     0x04
#define HT7627S_REG_RMS_IC     0x05
#define HT7627S_REG_RMS_IN     0x06

/* THD寄存器 */
#define HT7627S_REG_THD_V      0x10
#define HT7627S_REG_THD_I      0x11

/* 频率寄存器 */
#define HT7627S_REG_FREQ       0x20

/* 功率寄存器 */
#define HT7627S_REG_PA         0x30
#define HT7627S_REG_PB         0x31
#define HT7627S_REG_PC         0x32
#define HT7627S_REG_PTOTAL     0x33
#define HT7627S_REG_QA         0x34
#define HT7627S_REG_QB         0x35
#define HT7627S_REG_QC         0x36
#define HT7627S_REG_QTOTAL     0x37
#define HT7627S_REG_SA         0x38
#define HT7627S_REG_SB         0x39
#define HT7627S_REG_SC         0x3A
#define HT7627S_REG_STOTAL     0x3B
#define HT7627S_REG_PF         0x3C

/* 谐波含有率寄存器基址 (2-31次) */
#define HT7627S_REG_HARM_BASE  0x50

/* 状态/中断寄存器 */
#define HT7627S_REG_STATUS     0x80
#define HT7627S_REG_INT_MASK   0x81
#define HT7627S_REG_CTRL       0x82

/* 采样率配置 */
#define HT7627S_CTRL_RATE_12800  0x00
#define HT7627S_CTRL_RATE_25600  0x01

#endif /* HT7627S_REGS_H */
