%% T536 波形数据读取与绘图脚本
% 用于读取 wave_data.csv 并绘制实时波形图
% 
% 使用方法:
%   1. 确保 MATLAB 路径包含此脚本
%   2. 运行脚本，选择 CSV 文件
%   3. 查看生成的波形图

clear; clc; close all;

%% 配置参数
SAMPLE_RATE = 12800;  % 采样率 Hz
POINTS_PER_CYCLE = 256;  % 每周期采样点数
N_CHANNELS = 7;  % 通道数
CHANNEL_NAMES = {'UA', 'UB', 'UC', 'IA', 'IB', 'IC', 'IZ'};
UNITS = {'V', 'V', 'V', 'A', 'A', 'A', 'A'};

%% 选择数据文件
[filename, filepath] = uigetfile('*.csv', '选择波形数据文件');
if isequal(filename, 0)
    disp('未选择文件，退出。');
    return;
end
fullpath = fullfile(filepath, filename);
fprintf('读取文件: %s\n', fullpath);

%% 解析CSV文件
fid = fopen(fullpath, 'r');
if fid == -1
    error('无法打开文件');
end

cycles = {};
monitor_data = {};
cycle_idx = 0;
monitor_idx = 0;

while ~feof(fid)
    line = fgetl(fid);
    if ~ischar(line)
        break;
    end
    
    % 跳过注释行
    if line(1) == '#'
        % 检查是否是周期头
        if contains(line, '# Cycle')
            cycle_idx = cycle_idx + 1;
            fprintf('解析周期 %d...\n', cycle_idx);
        end
        % 检查是否是监测量头
        if contains(line, '# Monitor')
            monitor_idx = monitor_idx + 1;
        end
        continue;
    end
    
    % 跳过表头
    if startsWith(line, 'point')
        continue;
    end
    
    % 检查是否是监测量数据
    if contains(line, '_RMS') || contains(line, '_Hz') || contains(line, '_kW') || contains(line, '_kvar')
        % 解析监测量
        parts = strsplit(line, ',');
        if length(parts) >= 2
            key = parts{1};
            val = str2double(parts{2});
            if ~isnan(val)
                if monitor_idx > 0
                    monitor_data{monitor_idx}.(key) = val;
                end
            end
        end
        continue;
    end
    
    % 解析波形数据点
    parts = strsplit(line, ',');
    if length(parts) >= 8
        point = str2double(parts{1});
        if cycle_idx > 0 && ~isnan(point)
            data_point = struct();
            data_point.point = point;
            for ch = 1:N_CHANNELS
                data_point.(CHANNEL_NAMES{ch}) = str2double(parts{ch + 1});
            end
            if length(cycles) < cycle_idx
                cycles{cycle_idx} = [];
            end
            cycles{cycle_idx} = [cycles{cycle_idx}; data_point];
        end
    end
end

fclose(fid);

fprintf('解析完成: %d 个周期, %d 个监测量快照\n', cycle_idx, monitor_idx);

%% 提取波形数据矩阵
if cycle_idx > 0 && ~isempty(cycles{1})
    n_cycles = cycle_idx;
    n_points = size(cycles{1}, 1);
    
    % 为每个周期创建矩阵
    wave_matrix = zeros(n_cycles, N_CHANNELS, n_points);
    
    for c = 1:n_cycles
        if length(cycles) >= c && ~isempty(cycles{c})
            for ch = 1:N_CHANNELS
                wave_matrix(c, ch, :) = [cycles{c}.(CHANNEL_NAMES{ch})];
            end
        end
    end
    
    %% 创建时间轴
    time_ms = (0:n_points-1) / SAMPLE_RATE * 1000;  % 毫秒
    
    %% 绘图1: 所有周期叠加显示
    figure('Name', 'T536 实时波形 - 所有周期', 'Position', [100 100 1200 800]);
    
    for ch = 1:N_CHANNELS
        subplot(3, 3, ch);
        hold on;
        for c = 1:min(n_cycles, 5)  % 最多显示5个周期
            plot(time_ms, squeeze(wave_matrix(c, ch, :)), 'b-', 'LineWidth', 1);
        end
        
        % 计算统计值
        data = squeeze(wave_matrix(1, ch, :));
        rms = sqrt(mean(data.^2));
        min_val = min(data);
        max_val = max(data);
        
        title(sprintf('%s (RMS=%.2f%s, Min=%.2f, Max=%.2f)', ...
              CHANNEL_NAMES{ch}, rms, UNITS{ch}, min_val, max_val));
        xlabel('时间 (ms)');
        ylabel(sprintf('电压/电流 (%s)', UNITS{ch}));
        grid on;
        hold off;
    end
    
    sgtitle('T536 实时波形数据 - 多周期叠加', 'FontSize', 14);
    
    %% 绘图2: 最新周期详细显示
    figure('Name', 'T536 实时波形 - 最新周期', 'Position', [100 100 1200 800]);
    
    latest_cycle = n_cycles;
    
    % 电压通道
    subplot(2, 1, 1);
    hold on;
    colors = lines(N_CHANNELS);
    for ch = 1:3  % 电压通道 UA, UB, UC
        plot(time_ms, squeeze(wave_matrix(latest_cycle, ch, :)), ...
             'Color', colors(ch, :), 'LineWidth', 1.5, ...
             'DisplayName', CHANNEL_NAMES{ch});
    end
    title('电压通道波形 (UA, UB, UC)');
    xlabel('时间 (ms)');
    ylabel('电压 (V)');
    legend('Location', 'best');
    grid on;
    hold off;
    
    % 电流通道
    subplot(2, 1, 2);
    hold on;
    for ch = 4:N_CHANNELS  % 电流通道 IA, IB, IC, IZ
        plot(time_ms, squeeze(wave_matrix(latest_cycle, ch, :)), ...
             'Color', colors(ch, :), 'LineWidth', 1.5, ...
             'DisplayName', CHANNEL_NAMES{ch});
    end
    title('电流通道波形 (IA, IB, IC, IZ)');
    xlabel('时间 (ms)');
    ylabel('电流 (A)');
    legend('Location', 'best');
    grid on;
    hold off;
    
    sgtitle('T536 最新周期波形', 'FontSize', 14);
    
    %% 绘图3: 三相电压电流合成图
    figure('Name', 'T536 三相波形合成', 'Position', [100 100 1200 600]);
    
    % 三相电压
    subplot(1, 2, 1);
    hold on;
    phase_colors = {'r', 'g', 'b'};
    for ch = 1:3
        plot(time_ms, squeeze(wave_matrix(latest_cycle, ch, :)), ...
             phase_colors{ch}, 'LineWidth', 2);
    end
    title('三相电压波形');
    xlabel('时间 (ms)');
    ylabel('电压 (V)');
    legend({'UA', 'UB', 'UC'});
    grid on;
    hold off;
    
    % 三相电流
    subplot(1, 2, 2);
    hold on;
    for ch = 4:6
        plot(time_ms, squeeze(wave_matrix(latest_cycle, ch, :)), ...
             phase_colors{ch-3}, 'LineWidth', 2);
    end
    plot(time_ms, squeeze(wave_matrix(latest_cycle, 7, :)), 'k-', 'LineWidth', 1.5);
    title('三相电流 + 零序');
    xlabel('时间 (ms)');
    ylabel('电流 (A)');
    legend({'IA', 'IB', 'IC', 'IZ'});
    grid on;
    hold off;
    
    sgtitle('T536 三相波形分析', 'FontSize', 14);
    
    %% 打印统计信息
    fprintf('\n========== 波形统计信息 ==========\n');
    fprintf('采样率: %d Hz\n', SAMPLE_RATE);
    fprintf('周期数: %d\n', n_cycles);
    fprintf('每周期点数: %d\n', n_points);
    fprintf('\n通道统计 (周期 %d):\n', latest_cycle);
    
    total_power = 0;
    for ch = 1:N_CHANNELS
        data = squeeze(wave_matrix(latest_cycle, ch, :));
        rms = sqrt(mean(data.^2));
        fprintf('  %s: RMS = %8.3f %s, Min = %8.3f, Max = %8.3f\n', ...
                CHANNEL_NAMES{ch}, rms, UNITS{ch}, min(data), max(data));
    end
    
    % 计算总有功功率 (简化: UA*IA + UB*IB + UC*IC)
    p_a = mean(squeeze(wave_matrix(latest_cycle, 1, :)) .* squeeze(wave_matrix(latest_cycle, 4, :)));
    p_b = mean(squeeze(wave_matrix(latest_cycle, 2, :)) .* squeeze(wave_matrix(latest_cycle, 5, :)));
    p_c = mean(squeeze(wave_matrix(latest_cycle, 3, :)) .* squeeze(wave_matrix(latest_cycle, 6, :)));
    total_p = p_a + p_b + p_c;
    fprintf('\n  有功功率: P = %.3f W (Pa=%.2f, Pb=%.2f, Pc=%.2f)\n', total_p, p_a, p_b, p_c);
    
else
    disp('未解析到波形数据！');
end

%% 显示监测量数据
if monitor_idx > 0
    fprintf('\n========== 实时监测量 ==========\n');
    for m = 1:min(monitor_idx, 3)  % 显示前3组
        fprintf('\n监测量快照 %d:\n', m);
        if isfield(monitor_data{m}, 'UA_RMS')
            fprintf('  UA_RMS: %.3f V\n', monitor_data{m}.UA_RMS);
        end
        if isfield(monitor_data{m}, 'UB_RMS')
            fprintf('  UB_RMS: %.3f V\n', monitor_data{m}.UB_RMS);
        end
        if isfield(monitor_data{m}, 'UC_RMS')
            fprintf('  UC_RMS: %.3f V\n', monitor_data{m}.UC_RMS);
        end
        if isfield(monitor_data{m}, 'Frequency_Hz')
            fprintf('  频率: %.3f Hz\n', monitor_data{m}.Frequency_Hz);
        end
    end
end

fprintf('\n========== 完成 ==========\n');
