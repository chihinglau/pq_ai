function pqMetrics = calculatePQMetrics(simOut, params)
% 计算电能质量指标 (基于GB/T标准)
    fprintf('计算电能质量指标...\n');
    
    t = simOut.tout;
    dt = mean(diff(t));
    fs = 1/dt;  % 采样频率
    
    % 获取电压电流数据
    if isfield(simOut.yout, 'Vabc')
        Vabc = simOut.yout.Vabc.Data;
    else
        Vabc = [];
    end
    
    if isfield(simOut.yout, 'Iabc')
        Iabc = simOut.yout.Iabc.Data;
    else
        Iabc = [];
    end
    
    pqMetrics = struct();
    
    %% 1. 电压偏差 (GB/T 12325)
    if ~isempty(Vabc)
        V_rms = rms(Vabc);
        V_nom = params.grid.V_phase;
        pqMetrics.voltage_deviation = (V_rms - V_nom) / V_nom * 100;  % 百分比
        pqMetrics.voltage_deviation_max = max(abs(pqMetrics.voltage_deviation));
        fprintf('  电压偏差: %.2f%% (最大: %.2f%%)\n', mean(pqMetrics.voltage_deviation), pqMetrics.voltage_deviation_max);
    end
    
    %% 2. 电压THD (GB/T 14549)
    if ~isempty(Vabc) && size(Vabc, 2) >= 3
        % 对A相电压进行FFT分析
        Va = Vabc(:, 1);
        N = length(Va);
        Va_fft = fft(Va);
        Nhalf = floor(N/2);
        Va_fft = Va_fft(1:Nhalf+1);
        Va_fft(2:end-1) = 2*Va_fft(2:end-1);
        
        % 计算谐波含量
        f = fs*(0:Nhalf)/N;
        fundamental_idx = round(params.grid.f_nominal / (fs/N)) + 1;
        fundamental_amp = abs(Va_fft(fundamental_idx));
        
        harmonics_amp = 0;
        for h = 2:31  % 2~31次谐波
            harmonic_freq = h * params.grid.f_nominal;
            [~, idx] = min(abs(f - harmonic_freq));
            harmonics_amp = harmonics_amp + abs(Va_fft(idx))^2;
        end
        
        pqMetrics.voltage_thd = sqrt(harmonics_amp) / fundamental_amp * 100;
        fprintf('  电压THD: %.2f%% (限值: %.1f%%)\n', pqMetrics.voltage_thd, params.std.thd_voltage*100);
    end
    
    %% 3. 电流THD
    if ~isempty(Iabc) && size(Iabc, 2) >= 3
        Ia = Iabc(:, 1);
        N = length(Ia);
        Ia_fft = fft(Ia);
        Nhalf = floor(N/2);
        Ia_fft = Ia_fft(1:Nhalf+1);
        Ia_fft(2:end-1) = 2*Ia_fft(2:end-1);
        
        f = fs*(0:Nhalf)/N;
        fundamental_idx = round(params.grid.f_nominal / (fs/N)) + 1;
        fundamental_amp = abs(Ia_fft(fundamental_idx));
        
        harmonics_amp = 0;
        for h = 2:31
            harmonic_freq = h * params.grid.f_nominal;
            [~, idx] = min(abs(f - harmonic_freq));
            harmonics_amp = harmonics_amp + abs(Ia_fft(idx))^2;
        end
        
        pqMetrics.current_thd = sqrt(harmonics_amp) / fundamental_amp * 100;
        fprintf('  电流THD: %.2f%% (限值: %.1f%%)\n', pqMetrics.current_thd, params.std.thd_current*100);
    end
    
    %% 4. 三相不平衡度 (GB/T 15543)
    if ~isempty(Vabc) && size(Vabc, 2) == 3
        Va = Vabc(:, 1); Vb = Vabc(:, 2); Vc = Vabc(:, 3);
        
        % 使用相干DFT精确提取基波相量 (整数个周波，消除频谱泄漏)
        f0 = params.grid.f_nominal;
        n_cycles = 500;  % 取500个完整周波
        n_samp_per_cycle = round(fs / f0);
        Ncoh = n_cycles * n_samp_per_cycle;
        Ncoh = min(Ncoh, length(Va));
        
        [V1_a, phi_a] = getCoherentPhasor(Va(1:Ncoh), fs, f0);
        [V1_b, phi_b] = getCoherentPhasor(Vb(1:Ncoh), fs, f0);
        [V1_c, phi_c] = getCoherentPhasor(Vc(1:Ncoh), fs, f0);
        
        % 构造基波复相量
        V_a_phasor = V1_a * exp(1j * phi_a);
        V_b_phasor = V1_b * exp(1j * phi_b);
        V_c_phasor = V1_c * exp(1j * phi_c);
        
        % 对称分量法
        alpha = exp(1j * 2*pi/3);
        V_pos = (V_a_phasor + alpha*V_b_phasor + alpha^2*V_c_phasor) / 3;
        V_neg = (V_a_phasor + alpha^2*V_b_phasor + alpha*V_c_phasor) / 3;
        
        if abs(V_pos) > 1e-6
            pqMetrics.voltage_unbalance = abs(V_neg) / abs(V_pos) * 100;
        else
            pqMetrics.voltage_unbalance = 0;
        end
        fprintf('  电压不平衡度: %.2f%% (限值: %.1f%%)\n', pqMetrics.voltage_unbalance, params.std.unbalance*100);
    end
    
    %% 5. 频率偏差
    if isfield(simOut.yout, 'Frequency')
        f_meas = simOut.yout.Frequency.Data;
        pqMetrics.frequency_deviation = max(abs(f_meas - params.grid.f_nominal));
        fprintf('  频率偏差: %.3f Hz (限值: %.1f Hz)\n', pqMetrics.frequency_deviation, params.std.frequency_deviation);
    end
    
    %% 6. 变压器负载率
    if isfield(simOut.yout, 'P') && isfield(simOut.yout, 'Q')
        P = simOut.yout.P.Data;
        Q = simOut.yout.Q.Data;
        S = sqrt(P.^2 + Q.^2);
        pqMetrics.transformer_loading = max(S) / params.transformer.S_rated * 100;
        fprintf('  变压器负载率: %.1f%% (额定: %.0fkVA)\n', pqMetrics.transformer_loading, params.transformer.S_rated/1000);
    end
    
    %% 7. 线路载流量
    if ~isempty(Iabc)
        I_line = max(rms(Iabc, 1));
        pqMetrics.line_loading = I_line / params.line.I_max * 100;
        fprintf('  线路载流量: %.1f%% (最大: %.0fA)\n', pqMetrics.line_loading, params.line.I_max);
    end
    
    %% 8. 功率因数
    if isfield(simOut.yout, 'P') && isfield(simOut.yout, 'Q')
        P = simOut.yout.P.Data;
        Q = simOut.yout.Q.Data;
        S = sqrt(P.^2 + Q.^2);
        pqMetrics.power_factor = mean(P ./ S);
        fprintf('  功率因数: %.3f\n', pqMetrics.power_factor);
    end
    
    fprintf('\n');
end

%% =========================================================================
function [V1_mag, V1_phase] = getCoherentPhasor(x, fs, f0)
%GETCOHERENTPHASOR 使用相干DFT提取基波相量 (无频谱泄漏)
%   要求输入信号长度为整数个基波周期
    N = length(x);
    n = (0:N-1)';
    % 相关运算提取同相/正交分量
    I = sum(x .* cos(2*pi*f0*n/fs)) * 2 / N;
    Q = sum(x .* sin(2*pi*f0*n/fs)) * 2 / N;
    % 幅值和相位
    V1_mag = sqrt(I^2 + Q^2);
    V1_phase = atan2(-Q, I);
end
