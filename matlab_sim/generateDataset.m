function dataset = generateDataset(params, nSamples)
% 生成AI训练数据集
% 批量生成不同场景下的扰动波形数据
    
    if nargin < 2
        nSamples = 10000;  % 默认10000条样本
    end
    
    fprintf('========================================\n');
    fprintf('生成AI训练数据集\n');
    fprintf('样本数量: %d\n', nSamples);
    fprintf('========================================\n\n');
    
    % 数据存储
    dataset.waveforms = cell(nSamples, 1);
    dataset.labels = zeros(nSamples, 1);
    dataset.scenarios = cell(nSamples, 1);
    dataset.features = zeros(nSamples, 20);  % 特征向量
    
    % 场景定义
    scenarios = {'normal', 'pv_only', 'ev_only', 'pv_ev', 'extreme'};
    
    fprintf('开始生成数据...\n');
    
    for i = 1:nSamples
        % 随机选择场景
        scenarioIdx = randi(length(scenarios));
        scenario = scenarios{scenarioIdx};
        
        % 生成随机参数
        simParams = generateRandomParams(params, scenario);
        
        % 生成波形数据 (简化版)
        [waveform, features] = generateWaveform(simParams);
        
        % 存储数据
        dataset.waveforms{i} = waveform;
        dataset.labels(i) = scenarioIdx;
        dataset.scenarios{i} = scenario;
        dataset.features(i, :) = features;
        
        % 显示进度
        if mod(i, 1000) == 0
            fprintf('  进度: %d/%d (%.1f%%)\n', i, nSamples, i/nSamples*100);
        end
    end
    
    fprintf('\n数据集生成完成\n');
    
    % 保存数据集
    saveDataset(dataset, nSamples);
end

function simParams = generateRandomParams(baseParams, scenario)
% 根据场景生成随机参数
    simParams = baseParams;
    
    switch scenario
        case 'normal'
            % 正常负荷场景
            simParams.pv.P_rated = 0;
            simParams.ev.P_ac = 0;
            simParams.load.P_rated = baseParams.load.P_rated * (0.5 + 0.5*rand());
            
        case 'pv_only'
            % 仅光伏接入
            simParams.pv.P_rated = baseParams.pv.P_rated * rand();
            simParams.ev.P_ac = 0;
            simParams.load.P_rated = baseParams.load.P_rated * (0.3 + 0.7*rand());
            
        case 'ev_only'
            % 仅充电桩接入
            simParams.pv.P_rated = 0;
            simParams.ev.P_ac = baseParams.ev.P_ac * randi([1, 10]);
            simParams.load.P_rated = baseParams.load.P_rated * (0.5 + 0.5*rand());
            
        case 'pv_ev'
            % 光充耦合
            simParams.pv.P_rated = baseParams.pv.P_rated * rand();
            simParams.ev.P_ac = baseParams.ev.P_ac * randi([1, 8]);
            simParams.load.P_rated = baseParams.load.P_rated * (0.3 + 0.7*rand());
            
        case 'extreme'
            % 极端场景
            simParams.pv.P_rated = baseParams.pv.P_rated * (0.8 + 0.4*rand());
            simParams.ev.P_ac = baseParams.ev.P_ac * randi([8, 20]);
            simParams.load.P_rated = baseParams.load.P_rated * (0.8 + 0.4*rand());
    end
end

function [waveform, features] = generateWaveform(params)
% 生成模拟波形数据
    fs = 25600;  % 采样率 25.6kHz
    T = 0.2;     % 波形时长 200ms (10个周波)
    t = 0:1/fs:T-1/fs;
    f0 = 50;     % 基波频率
    
    % 基波电压
    V_nom = params.grid.V_phase;
    Va = V_nom * sin(2*pi*f0*t);
    Vb = V_nom * sin(2*pi*f0*t - 2*pi/3);
    Vc = V_nom * sin(2*pi*f0*t + 2*pi/3);
    
    % 添加谐波
    if params.pv.P_rated > 0
        pv_ratio = params.pv.P_rated / params.transformer.S_rated;
        for h = [2, 4, 6, 8]
            harmonic_amp = params.pv.harmonics(h/2) * pv_ratio * V_nom;
            Va = Va + harmonic_amp * sin(2*pi*h*f0*t + rand()*2*pi);
            Vb = Vb + harmonic_amp * sin(2*pi*h*f0*t + rand()*2*pi);
            Vc = Vc + harmonic_amp * sin(2*pi*h*f0*t + rand()*2*pi);
        end
    end
    
    if params.ev.P_ac > 0
        ev_ratio = params.ev.P_ac / params.transformer.S_rated;
        ev_harmonic_orders = [5, 7, 11, 13];
        for idx = 1:length(ev_harmonic_orders)
            h = ev_harmonic_orders(idx);
            harmonic_amp = params.ev.harmonics(idx) * ev_ratio * V_nom;
            Va = Va + harmonic_amp * sin(2*pi*h*f0*t + rand()*2*pi);
            Vb = Vb + harmonic_amp * sin(2*pi*h*f0*t + rand()*2*pi);
            Vc = Vc + harmonic_amp * sin(2*pi*h*f0*t + rand()*2*pi);
        end
    end
    
    % 添加噪声
    noise_level = 0.01 * V_nom;
    Va = Va + noise_level * randn(size(t));
    Vb = Vb + noise_level * randn(size(t));
    Vc = Vc + noise_level * randn(size(t));
    
    % 组合波形
    waveform = [Va', Vb', Vc'];
    
    % 提取特征
    features = extractFeatures(waveform, fs, f0);
end

function features = extractFeatures(waveform, fs, f0)
% 提取波形特征
    [N, nChannels] = size(waveform);
    
    % 时域特征
    features(1) = rms(waveform(:, 1));  % A相RMS
    features(2) = rms(waveform(:, 2));  % B相RMS
    features(3) = rms(waveform(:, 3));  % C相RMS
    
    features(4) = max(abs(waveform(:, 1)));  % A相峰值
    features(5) = max(abs(waveform(:, 2)));  % B相峰值
    features(6) = max(abs(waveform(:, 3)));  % C相峰值
    
    features(7) = std(waveform(:, 1));  % A相标准差
    features(8) = std(waveform(:, 2));  % B相标准差
    features(9) = std(waveform(:, 3));  % C相标准差
    
    % 频域特征 (FFT)
    nfft = 2^nextpow2(N);
    Va_fft = abs(fft(waveform(:, 1), nfft));
    Va_fft = Va_fft(1:nfft/2+1);
    
    % 谐波含量
    harmonics = [2, 3, 5, 7, 11, 13];
    for i = 1:length(harmonics)
        h = harmonics(i);
        idx = round(h * f0 / (fs/nfft)) + 1;
        if idx <= length(Va_fft)
            features(9+i) = Va_fft(idx);
        else
            features(9+i) = 0;
        end
    end
    
    % THD近似
    fundamental = Va_fft(round(f0/(fs/nfft))+1);
    harmonics_sum = sum(Va_fft(round(2*f0/(fs/nfft))+1:round(31*f0/(fs/nfft))+1).^2);
    features(16) = sqrt(harmonics_sum) / fundamental;
    
    % 不平衡度
    features(17) = abs(features(1) - features(2)) / mean(features(1:3));
    features(18) = abs(features(2) - features(3)) / mean(features(1:3));
    features(19) = abs(features(3) - features(1)) / mean(features(1:3));
    
    % 波形因子
    features(20) = features(4) / features(1);  % 峰值/RMS
end

function saveDataset(dataset, nSamples)
% 保存数据集
    dataDir = fullfile(pwd, 'dataset');
    if ~exist(dataDir, 'dir')
        mkdir(dataDir);
    end
    
    timestamp = datestr(now, 'yyyymmdd_HHMMSS');
    filename = fullfile(dataDir, sprintf('pq_dataset_%s_%d.mat', timestamp, nSamples));
    save(filename, 'dataset', '-v7.3');
    
    fprintf('数据集已保存: %s\n', filename);
    fprintf('  样本数: %d\n', length(dataset.labels));
    fprintf('  类别分布:\n');
    unique_labels = unique(dataset.labels);
    for i = 1:length(unique_labels)
        count = sum(dataset.labels == unique_labels(i));
        fprintf('    类别 %d (%s): %d (%.1f%%)\n', ...
            unique_labels(i), dataset.scenarios{find(dataset.labels == unique_labels(i), 1)}, ...
            count, count/length(dataset.labels)*100);
    end
end
