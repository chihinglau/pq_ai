function dataset = generateDataset(params, nSamples)
% 生成AI训练数据集
% 批量生成不同场景下的扰动波形数据
% v2.0: 增加各场景特征差异，提高可区分性
    
    if nargin < 2
        nSamples = 10000;  % 默认10000条样本
    end
    
    fprintf('========================================\n');
    fprintf('生成AI训练数据集 v2.0\n');
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
        
        % 生成随机参数 (包含场景特征配置)
        simParams = generateRandomParams(params, scenario);
        
        % 生成波形数据
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
    
    % 显示特征统计
    fprintf('\n特征统计:\n');
    for s = 1:length(scenarios)
        mask = dataset.labels == s;
        feats = dataset.features(mask, :);
        fprintf('  %s (n=%d):\n', scenarios{s}, sum(mask));
        fprintf('    RMS: [%.1f, %.1f], mean=%.1f\n', min(feats(:,1)), max(feats(:,1)), mean(feats(:,1)));
        fprintf('    THD: [%.4f, %.4f], mean=%.4f\n', min(feats(:,16)), max(feats(:,16)), mean(feats(:,16)));
    end
    
    % 保存数据集
    saveDataset(dataset, nSamples);
end

function simParams = generateRandomParams(baseParams, scenario)
% 根据场景生成随机参数 (v2.0: 增加特征差异)
    simParams = baseParams;
    
    % 初始化场景特征参数
    simParams.voltage_scale = 1.0;      % 电压缩放因子
    simParams.harmonic_level = 0;       % 谐波注入等级 (0-3)
    simParams.unbalance_level = 0;      % 不平衡度等级 (0-3)
    simParams.transient_level = 0;      % 瞬态脉冲等级 (0-3)
    simParams.sag_swell = 0;            % 电压暂降/暂升 (0=无, 1=暂降, 2=暂升)
    simParams.noise_level = 0.01;      % 噪声水平
    
    switch scenario
        case 'normal'
            % 正常场景: 电压稳定，低谐波，无扰动
            simParams.voltage_scale = 0.98 + 0.04 * rand();  % 0.98-1.02
            simParams.harmonic_level = 0;                    % 无谐波
            simParams.unbalance_level = 0;                   % 三相对称
            simParams.transient_level = 0;                   % 无瞬态
            simParams.sag_swell = 0;                         % 无暂降暂升
            simParams.noise_level = 0.005 + 0.005 * rand();  % 低噪声
            
            simParams.pv.P_rated = 0;
            simParams.ev.P_ac = 0;
            simParams.load.P_rated = baseParams.load.P_rated * (0.5 + 0.5*rand());
            
        case 'pv_only'
            % 光伏场景: 电压轻微下降，2/4/6/8次谐波
            simParams.voltage_scale = 0.88 + 0.08 * rand();  % 0.88-0.96
            simParams.harmonic_level = 1 + randi(2);          % 谐波等级 1-2
            simParams.unbalance_level = 0;
            simParams.transient_level = 0;
            simParams.sag_swell = 0;
            simParams.noise_level = 0.01;
            
            simParams.pv.P_rated = baseParams.pv.P_rated * (0.3 + 0.7*rand());
            simParams.ev.P_ac = 0;
            simParams.load.P_rated = baseParams.load.P_rated * (0.3 + 0.7*rand());
            
        case 'ev_only'
            % 充电桩场景: 电压下降，5/7/11/13次谐波，可能轻微不平衡
            simParams.voltage_scale = 0.80 + 0.10 * rand();  % 0.80-0.90
            simParams.harmonic_level = 2 + randi(2);          % 谐波等级 2-3
            simParams.unbalance_level = randi(2);             % 不平衡度 0-1
            simParams.transient_level = randi(2);             % 瞬态脉冲 0-1
            simParams.sag_swell = 0;
            simParams.noise_level = 0.012;
            
            simParams.pv.P_rated = 0;
            simParams.ev.P_ac = baseParams.ev.P_ac * (3 + randi(7));
            simParams.load.P_rated = baseParams.load.P_rated * (0.4 + 0.4*rand());
            
        case 'pv_ev'
            % 光充耦合: 电压显著下降，混合谐波，可能不平衡
            simParams.voltage_scale = 0.75 + 0.12 * rand();  % 0.75-0.87
            simParams.harmonic_level = 3;                     % 谐波等级 3 (最高)
            simParams.unbalance_level = 1 + randi(2);         % 不平衡度 1-2
            simParams.transient_level = randi(2);             % 瞬态脉冲 0-1
            simParams.sag_swell = 1;                          % 电压暂降
            simParams.noise_level = 0.015;
            
            simParams.pv.P_rated = baseParams.pv.P_rated * (0.4 + 0.6*rand());
            simParams.ev.P_ac = baseParams.ev.P_ac * (2 + randi(6));
            simParams.load.P_rated = baseParams.load.P_rated * (0.3 + 0.5*rand());
            
        case 'extreme'
            % 极端场景: 电压大幅变化，高THD，瞬态脉冲，严重不平衡
            extreme_type = randi(4);
            if extreme_type == 1
                % 严重电压暂降
                simParams.voltage_scale = 0.55 + 0.15 * rand();  % 0.55-0.70
                simParams.sag_swell = 1;
            elseif extreme_type == 2
                % 电压暂升
                simParams.voltage_scale = 1.15 + 0.15 * rand();  % 1.15-1.30
                simParams.sag_swell = 2;
            elseif extreme_type == 3
                % 严重过负荷
                simParams.voltage_scale = 0.70 + 0.10 * rand();  % 0.70-0.80
                simParams.sag_swell = 1;
            else
                % 瞬态脉冲
                simParams.voltage_scale = 0.85 + 0.10 * rand();  % 0.85-0.95
                simParams.sag_swell = 0;
            end
            
            simParams.harmonic_level = 3;                     % 高THD
            simParams.unbalance_level = 2 + randi(2);         % 严重不平衡 2-3
            simParams.transient_level = 2 + randi(2);         % 瞬态脉冲 2-3
            simParams.noise_level = 0.02;
            
            simParams.pv.P_rated = baseParams.pv.P_rated * (0.8 + 0.4*rand());
            simParams.ev.P_ac = baseParams.ev.P_ac * (8 + randi(12));
            simParams.load.P_rated = baseParams.load.P_rated * (0.8 + 0.4*rand());
    end
end

function [waveform, features] = generateWaveform(params)
% 生成模拟波形数据 (v2.0: 增强特征区分度)
    fs = 25600;  % 采样率 25.6kHz
    T = 0.2;     % 波形时长 200ms (10个周波)
    t = 0:1/fs:T-1/fs;
    f0 = 50;     % 基波频率
    N = length(t);
    
    % 基波电压
    V_nom = params.grid.V_phase * params.voltage_scale;
    Va = V_nom * sin(2*pi*f0*t);
    Vb = V_nom * sin(2*pi*f0*t - 2*pi/3);
    Vc = V_nom * sin(2*pi*f0*t + 2*pi/3);
    
    % === 1. 添加谐波 (根据harmonic_level) ===
    if params.harmonic_level > 0
        % 光伏谐波: 2, 4, 6, 8次
        pv_harmonics = [2, 4, 6, 8];
        pv_amplitude = [0.02, 0.015, 0.01, 0.008] * params.harmonic_level * V_nom;
        
        for idx = 1:length(pv_harmonics)
            h = pv_harmonics(idx);
            amp = pv_amplitude(idx);
            Va = Va + amp * sin(2*pi*h*f0*t + rand()*2*pi);
            Vb = Vb + amp * sin(2*pi*h*f0*t + rand()*2*pi);
            Vc = Vc + amp * sin(2*pi*h*f0*t + rand()*2*pi);
        end
        
        % 充电桩谐波: 5, 7, 11, 13次
        ev_harmonics = [5, 7, 11, 13];
        ev_amplitude = [0.015, 0.012, 0.008, 0.005] * params.harmonic_level * V_nom;
        
        for idx = 1:length(ev_harmonics)
            h = ev_harmonics(idx);
            amp = ev_amplitude(idx);
            Va = Va + amp * sin(2*pi*h*f0*t + rand()*2*pi);
            Vb = Vb + amp * sin(2*pi*h*f0*t + rand()*2*pi);
            Vc = Vc + amp * sin(2*pi*h*f0*t + rand()*2*pi);
        end
    end
    
    % === 2. 添加三相不平衡 ===
    if params.unbalance_level > 0
        unbalance_amount = 0.05 * params.unbalance_level;
        
        % A相保持不变，B/C相偏移
        phase_shift_b = unbalance_amount * (0.5 + rand()) * pi;
        phase_shift_c = -unbalance_amount * (0.5 + rand()) * pi;
        
        Vb_orig = Vb;
        Vc_orig = Vc;
        
        % 通过调制实现不平衡
        Vb = Vb_orig * (1 - unbalance_amount * rand());
        Vc = Vc_orig * (1 + unbalance_amount * rand());
    end
    
    % === 3. 添加瞬态脉冲 ===
    if params.transient_level > 0
        % 在随机位置添加高频衰减脉冲
        n_pulses = params.transient_level;
        
        for p = 1:n_pulses
            % 随机位置
            pulse_pos = randi([round(N*0.2), round(N*0.8)]);
            pulse_width = round(0.002 * fs);  % 2ms宽
            pulse_amp = (0.3 + 0.5*rand()) * params.transient_level * V_nom;
            
            % 衰减脉冲
            t_pulse = (0:pulse_width-1) / fs;
            decay = exp(-t_pulse * 2000);  % 快速衰减
            
            % 高频振荡
            carrier_freq = 2000 + 3000 * rand();  % 2-5 kHz
            pulse = pulse_amp * decay .* sin(2*pi*carrier_freq*t_pulse);
            
            % 添加到三相
            for ch = 1:3
                start_idx = pulse_pos;
                end_idx = min(pulse_pos + pulse_width - 1, N);
                actual_width = end_idx - start_idx + 1;
                if actual_width > 0
                    if ch == 1
                        Va(start_idx:end_idx) = Va(start_idx:end_idx) + pulse(1:actual_width);
                    elseif ch == 2
                        Vb(start_idx:end_idx) = Vb(start_idx:end_idx) + pulse(1:actual_width);
                    else
                        Vc(start_idx:end_idx) = Vc(start_idx:end_idx) + pulse(1:actual_width);
                    end
                end
            end
        end
    end
    
    % === 4. 电压暂降/暂升 ===
    if params.sag_swell == 1
        % 电压暂降: 在某个时间段电压下降
        sag_start = round(N * (0.2 + 0.3*rand()));
        sag_duration = round(N * (0.1 + 0.2*rand()));
        sag_depth = 0.2 + 0.4*rand();  % 下降20-60%
        
        sag_end = min(sag_start + sag_duration, N);
        sag_window = sag_start:sag_end;
        
        Va(sag_window) = Va(sag_window) * (1 - sag_depth);
        Vb(sag_window) = Vb(sag_window) * (1 - sag_depth);
        Vc(sag_window) = Vc(sag_window) * (1 - sag_depth);
        
    elseif params.sag_swell == 2
        % 电压暂升: 在某个时间段电压上升
        swell_start = round(N * (0.2 + 0.3*rand()));
        swell_duration = round(N * (0.1 + 0.2*rand()));
        swell_height = 0.2 + 0.4*rand();  % 上升20-60%
        
        swell_end = min(swell_start + swell_duration, N);
        swell_window = swell_start:swell_end;
        
        Va(swell_window) = Va(swell_window) * (1 + swell_height);
        Vb(swell_window) = Vb(swell_window) * (1 + swell_height);
        Vc(swell_window) = Vc(swell_window) * (1 + swell_height);
    end
    
    % === 5. 添加噪声 ===
    noise_level = params.noise_level * V_nom;
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
    features(16) = sqrt(harmonics_sum) / (fundamental + 1e-10);
    
    % 不平衡度
    features(17) = abs(features(1) - features(2)) / (mean(features(1:3)) + 1e-10);
    features(18) = abs(features(2) - features(3)) / (mean(features(1:3)) + 1e-10);
    features(19) = abs(features(3) - features(1)) / (mean(features(1:3)) + 1e-10);
    
    % 波形因子
    features(20) = features(4) / (features(1) + 1e-10);  % 峰值/RMS
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
    
    fprintf('\n数据集已保存: %s\n', filename);
    fprintf('  样本数: %d\n', length(dataset.labels));
    fprintf('  类别分布:\n');
    unique_labels = unique(dataset.labels);
    scenarios = {'normal', 'pv_only', 'ev_only', 'pv_ev', 'extreme'};
    for i = 1:length(unique_labels)
        count = sum(dataset.labels == unique_labels(i));
        fprintf('    类别 %d (%s): %d (%.1f%%)\n', ...
            unique_labels(i), scenarios{unique_labels(i)}, ...
            count, count/length(dataset.labels)*100);
    end
end
