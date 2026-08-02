function params = loadSystemParams()
% 加载配电台区系统参数
% 基于技术方案中的典型低压台区模型

    fprintf('加载系统参数...\n');
    
    %% 电网参数 (10kV/0.4kV)
    params.grid.V_nominal = 400;           % 额定线电压 (V)
    params.grid.V_phase = 400/sqrt(3);     % 额定相电压 (V)
    params.grid.f_nominal = 50;            % 额定频率 (Hz)
    params.grid.S_base = 1e6;              % 基准容量 (VA)
    
    %% 变压器参数 (Dyn11, 400kVA)
    params.transformer.S_rated = 400e3;    % 额定容量 (VA)
    params.transformer.V1_rated = 10e3;    % 高压侧额定电压 (V)
    params.transformer.V2_rated = 400;     % 低压侧额定电压 (V)
    params.transformer.Z_percent = 4;      % 阻抗电压 (%)
    params.transformer.P_cu = 4000;        % 铜损 (W)
    params.transformer.P_fe = 800;         % 铁损 (W)
    params.transformer.connection = 'Dyn11';
    
    %% 线路参数 (LGJ-120, 典型城市配电台区低压出线)
    params.line.length = 100;              % 线路长度 (m)
    params.line.R_per_km = 0.27;           % 单位电阻 (Ω/km)
    params.line.X_per_km = 0.35;           % 单位电抗 (Ω/km)
    params.line.R = params.line.R_per_km * params.line.length / 1000;
    params.line.X = params.line.X_per_km * params.line.length / 1000;
    params.line.I_max = 380;               % 最大载流量 (A)
    
    %% 负荷参数 (混合负荷)
    params.load.P_rated = 150e3;           % 额定有功功率 (W)
    params.load.Q_rated = 80e3;            % 额定无功功率 (VAR)
    params.load.pf = 0.88;                 % 功率因数
    params.load.type = 'mixed';            % 负荷类型
    
    %% 光伏参数
    params.pv.P_rated = 100e3;             % 光伏额定功率 (W)
    params.pv.V_mppt = 320;                % MPPT电压 (V)
    params.pv.I_mppt = 312.5;              % MPPT电流 (A)
    params.pv.efficiency = 0.97;           % 逆变器效率
    params.pv.harmonics = [0.05, 0.03, 0.02, 0.01];  % 2,4,6,8次谐波含量
    
    %% 充电桩参数
    params.ev.P_ac = 7e3;                  % 交流充电桩功率 (W)
    params.ev.P_dc = 60e3;                 % 直流充电桩功率 (W)
    params.ev.efficiency = 0.95;           % 充电效率
    params.ev.pf = 0.95;                   % 功率因数
    params.ev.harmonics = [0.15, 0.10, 0.08, 0.05];  % 5,7,11,13次谐波含量
    params.ev.simultaneity = 0.7;          % 同时率
    
    %% 仿真参数
    params.sim.Ts = 1e-4;                  % 采样时间 (s)
    params.sim.Tstop = 10;                 % 仿真停止时间 (s)
    params.sim.solver = 'ode23tb';         % 求解器
    
    %% 电能质量标准 (GB/T)
    params.std.voltage_deviation = 0.07;   % 电压偏差限值 (7%)
    params.std.thd_voltage = 0.05;         % 电压THD限值 (5%)
    params.std.thd_current = 0.08;         % 电流THD限值 (8%)
    params.std.unbalance = 0.02;           % 不平衡度限值 (2%)
    params.std.frequency_deviation = 0.5;  % 频率偏差限值 (Hz)
    
    fprintf('  变压器: %dkVA, %s\n', params.transformer.S_rated/1000, params.transformer.connection);
    fprintf('  线路: %.0fm, R=%.4fΩ, X=%.4fΩ\n', params.line.length, params.line.R, params.line.X);
    fprintf('  负荷: P=%.0fkW, pf=%.2f\n', params.load.P_rated/1000, params.load.pf);
    fprintf('  光伏: %.0fkW\n', params.pv.P_rated/1000);
    fprintf('  充电桩: AC=%.1fkW, DC=%.0fkW\n', params.ev.P_ac/1000, params.ev.P_dc/1000);
    fprintf('\n');
end
