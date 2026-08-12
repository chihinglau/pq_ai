# -*- coding: utf-8 -*-
"""
T536 波形数据绘图工具
用于读取 wave_data.csv 并生成波形图
"""

import numpy as np
import matplotlib.pyplot as plt
import matplotlib
matplotlib.rcParams['font.sans-serif'] = ['SimHei', 'DejaVu Sans']
matplotlib.rcParams['axes.unicode_minus'] = False

# 配置参数
SAMPLE_RATE = 12800  # 采样率 Hz
POINTS_PER_CYCLE = 256  # 每周期采样点数
N_CHANNELS = 7  # 通道数
CHANNEL_NAMES = ['UA', 'UB', 'UC', 'IA', 'IB', 'IC', 'IZ']
UNITS = ['V', 'V', 'V', 'A', 'A', 'A', 'A']


def parse_csv(filepath):
    """解析波形数据CSV文件"""
    cycles = []
    monitors = []
    current_cycle = None
    current_monitor = None

    with open(filepath, 'r') as f:
        for line in f:
            line = line.strip()
            if not line:
                if current_cycle is not None:
                    cycles.append(current_cycle)
                    current_cycle = None
                if current_monitor is not None:
                    monitors.append(current_monitor)
                    current_monitor = None
                continue

            # 跳过注释行
            if line.startswith('#'):
                if '# Cycle' in line:
                    if current_cycle is not None:
                        cycles.append(current_cycle)
                    current_cycle = {'data': []}
                elif '# Monitor' in line:
                    if current_monitor is not None:
                        monitors.append(current_monitor)
                    current_monitor = {}
                continue

            # 跳过表头
            if line.startswith('point'):
                continue

            # 检查是否是监测量数据
            if '_RMS' in line or '_Hz' in line or '_kW' in line or '_kvar' in line:
                parts = line.split(',')
                if len(parts) >= 2 and current_monitor is not None:
                    key = parts[0]
                    try:
                        val = float(parts[1])
                        current_monitor[key] = val
                    except ValueError:
                        pass
                continue

            # 解析波形数据点
            parts = line.split(',')
            if len(parts) >= 8 and current_cycle is not None:
                try:
                    point = float(parts[0])
                    values = [float(parts[i+1]) for i in range(N_CHANNELS)]
                    current_cycle['data'].append([point] + values)
                except ValueError:
                    pass

    # 添加最后一个周期
    if current_cycle is not None:
        cycles.append(current_cycle)
    if current_monitor is not None:
        monitors.append(current_monitor)

    return cycles, monitors


def plot_waveforms(cycles, monitors, output_file='waveform_plot.png'):
    """绘制波形图"""
    if not cycles:
        print("无数据可绘制！")
        return

    n_cycles = len(cycles)
    n_points = len(cycles[0]['data'])
    time_ms = np.linspace(0, n_points / SAMPLE_RATE * 1000, n_points)

    # 提取数据矩阵
    wave_matrix = np.zeros((n_cycles, N_CHANNELS, n_points))
    for c, cycle in enumerate(cycles):
        data = np.array(cycle['data'])
        for ch in range(N_CHANNELS):
            wave_matrix[c, ch, :] = data[:, ch + 1]

    latest = n_cycles - 1

    # 创建图形
    fig = plt.figure(figsize=(16, 12))
    fig.suptitle('T536 实时波形数据采集结果', fontsize=16, fontweight='bold')

    # 子图1: 所有通道叠加
    for ch in range(N_CHANNELS):
        ax = fig.add_subplot(3, 3, ch + 1)
        for c in range(min(n_cycles, 5)):
            ax.plot(time_ms, wave_matrix[c, ch, :], 'b-', linewidth=0.5, alpha=0.7)

        data = wave_matrix[latest, ch, :]
        rms = np.sqrt(np.mean(data**2))
        ax.set_title(f'{CHANNEL_NAMES[ch]} (RMS={rms:.2f}{UNITS[ch]})')
        ax.set_xlabel('时间 (ms)')
        ax.set_ylabel(f'数值 ({UNITS[ch]})')
        ax.grid(True, alpha=0.3)

    plt.tight_layout()

    # 保存图片
    fig.savefig(output_file, dpi=150, bbox_inches='tight')
    print(f"波形图已保存: {output_file}")

    # 创建第二个图形 - 详细波形
    fig2, axes = plt.subplots(2, 2, figsize=(16, 10))
    fig2.suptitle('T536 最新周期波形详细分析', fontsize=14)

    # 电压通道
    ax1 = axes[0, 0]
    colors = ['r', 'g', 'b']
    for ch in range(3):
        ax1.plot(time_ms, wave_matrix[latest, ch, :], colors[ch], linewidth=1.5,
                label=CHANNEL_NAMES[ch])
    ax1.set_title('三相电压波形')
    ax1.set_xlabel('时间 (ms)')
    ax1.set_ylabel('电压 (V)')
    ax1.legend()
    ax1.grid(True, alpha=0.3)

    # 电流通道
    ax2 = axes[0, 1]
    for ch in range(3, 6):
        ax2.plot(time_ms, wave_matrix[latest, ch, :], colors[ch - 3], linewidth=1.5,
                label=CHANNEL_NAMES[ch])
    ax2.plot(time_ms, wave_matrix[latest, 6, :], 'k-', linewidth=1.5, label='IZ')
    ax2.set_title('三相电流 + 零序波形')
    ax2.set_xlabel('时间 (ms)')
    ax2.set_ylabel('电流 (A)')
    ax2.legend()
    ax2.grid(True, alpha=0.3)

    # 电压矢量图
    ax3 = axes[1, 0]
    angles = np.linspace(0, 2 * np.pi, 100)
    for ch in range(3):
        data = wave_matrix[latest, ch, :]
        rms = np.sqrt(np.mean(data**2))
        angle = np.angle(np.mean(data))
        ax3.plot([0, rms * np.cos(angle)], [0, rms * np.sin(angle)],
                colors[ch], linewidth=2, label=f'{CHANNEL_NAMES[ch]} (RMS={rms:.1f}V)')
    ax3.set_title('三相电压矢量图')
    ax3.set_xlabel('实部 (V)')
    ax3.set_ylabel('虚部 (V)')
    ax3.legend()
    ax3.grid(True, alpha=0.3)
    ax3.set_aspect('equal')
    circle = plt.Circle((0, 0), 220, fill=False, linestyle='--', alpha=0.5)
    ax3.add_patch(circle)

    # 功率分析
    ax4 = axes[1, 1]
    p_a = np.mean(wave_matrix[latest, 0, :] * wave_matrix[latest, 3, :])
    p_b = np.mean(wave_matrix[latest, 1, :] * wave_matrix[latest, 4, :])
    p_c = np.mean(wave_matrix[latest, 2, :] * wave_matrix[latest, 5, :])
    total_p = p_a + p_b + p_c

    bars = ax4.bar(['Pa', 'Pb', 'Pc', 'Total'], [p_a, p_b, p_c, total_p],
                   color=['red', 'green', 'blue', 'gray'])
    ax4.set_title('有功功率分析')
    ax4.set_ylabel('功率 (W)')
    ax4.grid(True, alpha=0.3, axis='y')

    for bar in bars:
        height = bar.get_height()
        ax4.text(bar.get_x() + bar.get_width() / 2., height,
                f'{height:.1f}W', ha='center', va='bottom')

    plt.tight_layout()
    fig2.savefig(output_file.replace('.png', '_detail.png'), dpi=150, bbox_inches='tight')
    print(f"详细波形图已保存: {output_file.replace('.png', '_detail.png')}")

    # 打印统计信息
    print("\n========== 波形统计信息 ==========")
    print(f"采样率: {SAMPLE_RATE} Hz")
    print(f"周期数: {n_cycles}")
    print(f"每周期点数: {n_points}")
    print(f"\n通道统计 (周期 {latest + 1}):")

    total_power = 0
    for ch in range(N_CHANNELS):
        data = wave_matrix[latest, ch, :]
        rms = np.sqrt(np.mean(data**2))
        print(f"  {CHANNEL_NAMES[ch]:4s}: RMS = {rms:8.3f} {UNITS[ch]}, "
              f"Min = {np.min(data):8.3f}, Max = {np.max(data):8.3f}")

    print(f"\n  有功功率: P = {total_p:.3f} W (Pa={p_a:.2f}, Pb={p_b:.2f}, Pc={p_c:.2f})")

    if monitors:
        print(f"\n========== 实时监测量 (最新) ==========")
        latest_monitor = monitors[-1]
        for key, val in latest_monitor.items():
            print(f"  {key}: {val:.3f}")

    return wave_matrix, time_ms


def main():
    """主函数"""
    filepath = 'wave_data.csv'

    print(f"读取文件: {filepath}")
    cycles, monitors = parse_csv(filepath)
    print(f"解析完成: {len(cycles)} 个周期, {len(monitors)} 个监测量快照")

    if cycles:
        wave_matrix, time_ms = plot_waveforms(cycles, monitors, 't536_waveform.png')
        print("\n波形分析完成！")
    else:
        print("错误: 未解析到有效数据！")


if __name__ == '__main__':
    main()
