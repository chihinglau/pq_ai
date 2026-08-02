function simOut = runSimulation(scenario, params)
%RUNSIMULATION 运行电能质量时域仿真 (纯MATLAB实现)
%   基于电路方程和功率流分析生成电压/电流波形，无需Simulink模型
%
%   输入:
%       scenario - 场景代码 'S1'~'S5'
%       params   - 系统参数结构体
%   输出:
%       simOut   - 仿真结果结构体，包含 tout, yout 等字段

    fprintf('运行仿真场景: %s...\n', getScenarioName(scenario));

    %% 1. 根据场景配置功率参数
    [P_load, Q_load, P_pv, P_ev, harmonics_pv, harmonics_ev] = configureScenarioPower(scenario, params);

    %% 2. 稳态潮流计算 (前推回代法简化版)
    [V_pcc, I_line, S_trafo, pf] = powerFlowCalc(P_load, Q_load, P_pv, P_ev, params);

    %% 3. 时域波形生成
    fs = 1 / params.sim.Ts;
    t = 0:params.sim.Ts:params.sim.Tstop;
    N = length(t);
    f0 = params.grid.f_nominal;
    w0 = 2*pi*f0;

    % 额定相电压幅值
    V_nom_amp = params.grid.V_phase * sqrt(2);

    % 基波电压 (PCC点，考虑电压偏差)
    V_pcc_amp = V_pcc * sqrt(2);
    Va = V_pcc_amp * sin(w0*t);
    Vb = V_pcc_amp * sin(w0*t - 2*pi/3);
    Vc = V_pcc_amp * sin(w0*t + 2*pi/3);

    % 基波电流
    I_line_amp = I_line * sqrt(2);
    % 电流相位角 (滞后电压)
    phi_i = acos(pf);
    Ia = I_line_amp * sin(w0*t - phi_i);
    Ib = I_line_amp * sin(w0*t - 2*pi/3 - phi_i);
    Ic = I_line_amp * sin(w0*t + 2*pi/3 - phi_i);

    %% 4. 叠加谐波 (光伏逆变器谐波: 2,4,6,8次)
    if ~isempty(harmonics_pv) && sum(harmonics_pv) > 0
        for h = [2, 4, 6, 8]
            h_idx = h/2;
            if h_idx <= length(harmonics_pv)
                % 谐波电流注入引起的谐波电压 = I_h * Z_h
                I_h = I_line * harmonics_pv(h_idx);  % 谐波电流有效值
                Z_h = sqrt(params.line.R^2 + (h * params.line.X)^2);  % 谐波阻抗
                V_h = I_h * Z_h * sqrt(2);  % 谐波电压幅值
                phase_h = rand()*2*pi;  % 随机相位
                Va = Va + V_h * sin(h*w0*t + phase_h);
                Vb = Vb + V_h * sin(h*w0*t + phase_h - 2*pi/3);
                Vc = Vc + V_h * sin(h*w0*t + phase_h + 2*pi/3);
                % 谐波电流
                Ia = Ia + I_h*sqrt(2) * sin(h*w0*t + phase_h);
                Ib = Ib + I_h*sqrt(2) * sin(h*w0*t + phase_h - 2*pi/3);
                Ic = Ic + I_h*sqrt(2) * sin(h*w0*t + phase_h + 2*pi/3);
            end
        end
    end

    %% 5. 叠加谐波 (充电桩谐波: 5,7,11,13次)
    if ~isempty(harmonics_ev) && sum(harmonics_ev) > 0
        harmonic_orders = [5, 7, 11, 13];
        for idx = 1:length(harmonic_orders)
            h = harmonic_orders(idx);
            if idx <= length(harmonics_ev)
                I_h = I_line * harmonics_ev(idx);
                Z_h = sqrt(params.line.R^2 + (h * params.line.X)^2);
                V_h = I_h * Z_h * sqrt(2);
                phase_h = rand()*2*pi;
                Va = Va + V_h * sin(h*w0*t + phase_h);
                Vb = Vb + V_h * sin(h*w0*t + phase_h - 2*pi/3);
                Vc = Vc + V_h * sin(h*w0*t + phase_h + 2*pi/3);
                Ia = Ia + I_h*sqrt(2) * sin(h*w0*t + phase_h);
                Ib = Ib + I_h*sqrt(2) * sin(h*w0*t + phase_h - 2*pi/3);
                Ic = Ic + I_h*sqrt(2) * sin(h*w0*t + phase_h + 2*pi/3);
            end
        end
    end

    %% 6. 叠加三相不平衡 (充电桩单相接入效应)
    if P_ev > 0
        unbalance_factor = 0.01 * (P_ev / params.transformer.S_rated);
        Va = Va * (1 + unbalance_factor);
        Vb = Vb * (1 - unbalance_factor*0.5);
        Vc = Vc * (1 - unbalance_factor*0.5);
        Ia = Ia * (1 + unbalance_factor);
        Ib = Ib * (1 - unbalance_factor*0.5);
        Ic = Ic * (1 - unbalance_factor*0.5);
    end

    %% 7. 叠加噪声
    noise_v = 0.005 * V_nom_amp;
    noise_i = 0.005 * I_line_amp;
    Va = Va + noise_v * randn(1, N);
    Vb = Vb + noise_v * randn(1, N);
    Vc = Vc + noise_v * randn(1, N);
    Ia = Ia + noise_i * randn(1, N);
    Ib = Ib + noise_i * randn(1, N);
    Ic = Ic + noise_i * randn(1, N);

    %% 8. 计算功率信号
    % 瞬时功率
    p_inst = Va.*Ia + Vb.*Ib + Vc.*Ic;
    % 通过移动平均提取有功功率 (一个周波)
    n_cycle = round(fs / f0);
    P = movmean(p_inst, n_cycle);
    % 无功功率近似
    Q = sqrt(max(S_trafo^2 - P.^2, 0));

    %% 9. 计算频率 (简化：基于PLL概念，实际为额定值加微小扰动)
    freq_dev = 0.01 * (P_pv - P_ev) / params.transformer.S_rated * f0;
    Frequency = f0 + freq_dev * sin(2*pi*0.1*t) + 0.001*randn(1,N);

    %% 10. 构建输出结构
    simOut.tout = t';

    % 电压电流 (转置为列向量，符合Simulink风格)
    Vabc_data = [Va', Vb', Vc'];
    Iabc_data = [Ia', Ib', Ic'];

    % 使用timeseries格式兼容原有分析代码
    simOut.yout.Vabc = struct('Data', Vabc_data, 'Time', t');
    simOut.yout.Iabc = struct('Data', Iabc_data, 'Time', t');
    simOut.yout.P = struct('Data', P', 'Time', t');
    simOut.yout.Q = struct('Data', Q', 'Time', t');
    simOut.yout.Frequency = struct('Data', Frequency', 'Time', t');

    % 附加稳态结果
    simOut.steady.V_pcc = V_pcc;
    simOut.steady.I_line = I_line;
    simOut.steady.S_trafo = S_trafo;
    simOut.steady.pf = pf;
    simOut.steady.P_load = P_load;
    simOut.steady.P_pv = P_pv;
    simOut.steady.P_ev = P_ev;

    fprintf('  仿真完成: %d 采样点, 时长 %.1fs\n\n', N, params.sim.Tstop);
end

%% =========================================================================
function [P_load, Q_load, P_pv, P_ev, harmonics_pv, harmonics_ev] = configureScenarioPower(scenario, params)
% 根据场景配置功率参数
    switch scenario
        case 'S1'
            P_load = params.load.P_rated;
            Q_load = params.load.Q_rated;
            P_pv = 0;
            P_ev = 0;
            harmonics_pv = [];
            harmonics_ev = [];
        case 'S2'
            P_load = params.load.P_rated;
            Q_load = params.load.Q_rated;
            P_pv = 0;
            P_ev = params.ev.P_ac * 5;  % 5台交流桩
            harmonics_pv = [];
            harmonics_ev = params.ev.harmonics;
        case 'S3'
            P_load = params.load.P_rated * 0.8;  % 白天负荷略低
            Q_load = params.load.Q_rated * 0.8;
            P_pv = params.pv.P_rated * 0.8;  % 中等光照
            P_ev = 0;
            harmonics_pv = params.pv.harmonics;
            harmonics_ev = [];
        case 'S4'
            P_load = params.load.P_rated * 0.9;
            Q_load = params.load.Q_rated * 0.9;
            P_pv = params.pv.P_rated * 0.6;
            P_ev = params.ev.P_ac * 3;  % 3台桩
            harmonics_pv = params.pv.harmonics;
            harmonics_ev = params.ev.harmonics;
        case 'S5'
            P_load = params.load.P_rated * 1.2;  % 高负荷
            Q_load = params.load.Q_rated * 1.2;
            P_pv = params.pv.P_rated * 1.0;  % 满发
            P_ev = params.ev.P_ac * 10;  % 10台桩
            harmonics_pv = params.pv.harmonics * 1.2;
            harmonics_ev = params.ev.harmonics * 1.2;
        otherwise
            error('未知场景: %s', scenario);
    end
end

%% =========================================================================
function [V_pcc, I_line, S_trafo, pf] = powerFlowCalc(P_load, Q_load, P_pv, P_ev, params)
% 简化潮流计算 (单节点等值)
%   计算PCC点电压、线路电流、变压器视在功率

    % 净功率 (负荷为正，发电为负)
    P_net = P_load + P_ev - P_pv;
    Q_net = Q_load;
    S_net = sqrt(P_net^2 + Q_net^2);

    % 变压器负载率
    S_trafo = S_net;

    % 功率因数
    if S_net > 0
        pf = P_net / S_net;
    else
        pf = 1.0;
    end

    % 线路电流 (低压侧)
    V_nom = params.grid.V_nominal;
    I_line = S_net / (sqrt(3) * V_nom);

    % 电压降计算 (相电压降)
    % dV_phase = I_line * (R*cos(phi) + X*sin(phi))
    if I_line > 0
        phi = atan2(Q_net, P_net);
        dV = I_line * (params.line.R * cos(phi) + params.line.X * sin(phi));
    else
        dV = 0;
    end

    % PCC点电压 (相电压)
    V_pcc = params.grid.V_phase - dV;

    % 确保电压在合理范围
    V_pcc = max(V_pcc, params.grid.V_phase * 0.85);
    V_pcc = min(V_pcc, params.grid.V_phase * 1.15);
end
