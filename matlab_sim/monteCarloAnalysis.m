function mcResults = monteCarloAnalysis(params, nSamples)
% 蒙特卡洛风险评估
% 量化不确定性因素对电能质量的影响
    
    if nargin < 2
        nSamples = 1000;  % 默认1000次采样
    end
    
    fprintf('========================================\n');
    fprintf('蒙特卡洛风险评估\n');
    fprintf('采样次数: %d\n', nSamples);
    fprintf('========================================\n\n');
    
    % 初始化结果存储
    mcResults.voltage_deviation = zeros(nSamples, 1);
    mcResults.voltage_thd = zeros(nSamples, 1);
    mcResults.transformer_loading = zeros(nSamples, 1);
    mcResults.line_loading = zeros(nSamples, 1);
    mcResults.voltage_unbalance = zeros(nSamples, 1);
    
    % 进度显示
    fprintf('开始蒙特卡洛模拟...\n');
    
    for i = 1:nSamples
        % 随机抽样 - 光伏出力 (Beta分布)
        alpha_pv = 2; beta_pv = 5;
        pv_factor = betarnd(alpha_pv, beta_pv);
        
        % 随机抽样 - 充电行为
        n_chargers = poissrnd(5);  % 泊松分布，平均5台
        n_chargers = max(1, min(n_chargers, 20));  % 限制范围
        
        % 随机抽样 - 负荷水平 (正态分布)
        load_factor = normrnd(0.8, 0.15);
        load_factor = max(0.3, min(load_factor, 1.2));
        
        % 更新参数
        simParams = params;
        simParams.pv.P_rated = params.pv.P_rated * pv_factor;
        simParams.ev.P_ac = params.ev.P_ac * n_chargers;
        simParams.load.P_rated = params.load.P_rated * load_factor;
        
        % 快速计算 (简化版潮流计算)
        [V_dev, V_thd, T_load, L_load, V_unbal] = quickPowerFlow(simParams);
        
        mcResults.voltage_deviation(i) = V_dev;
        mcResults.voltage_thd(i) = V_thd;
        mcResults.transformer_loading(i) = T_load;
        mcResults.line_loading(i) = L_load;
        mcResults.voltage_unbalance(i) = V_unbal;
        
        % 显示进度
        if mod(i, 100) == 0
            fprintf('  进度: %d/%d (%.1f%%)\n', i, nSamples, i/nSamples*100);
        end
    end
    
    fprintf('\n蒙特卡洛模拟完成\n\n');
    
    % 统计分析
    analyzeMonteCarloResults(mcResults, params);
end

function [V_dev, V_thd, T_load, L_load, V_unbal] = quickPowerFlow(params)
% 简化版潮流计算用于蒙特卡洛模拟
    
    % 计算总功率
    P_total = params.load.P_rated + params.ev.P_ac - params.pv.P_rated;
    Q_total = params.load.Q_rated;
    S_total = sqrt(P_total^2 + Q_total^2);
    
    % 变压器负载率
    T_load = S_total / params.transformer.S_rated * 100;
    
    % 线路电流
    I_line = S_total / (sqrt(3) * params.grid.V_nominal);
    L_load = I_line / params.line.I_max * 100;
    
    % 电压偏差 (简化计算)
    dV = I_line * (params.line.R * cos(atan2(Q_total, P_total)) + ...
                   params.line.X * sin(atan2(Q_total, P_total)));
    V_dev = abs(dV) / params.grid.V_phase * 100;
    
    % 谐波计算 (简化)
    if params.pv.P_rated > 0
        pv_harmonics = params.pv.harmonics * (params.pv.P_rated / params.transformer.S_rated);
    else
        pv_harmonics = 0;
    end
    
    if params.ev.P_ac > 0
        ev_harmonics = params.ev.harmonics * (params.ev.P_ac / params.transformer.S_rated);
    else
        ev_harmonics = 0;
    end
    
    V_thd = sqrt(sum((pv_harmonics + ev_harmonics).^2)) * 100;
    
    % 不平衡度 (简化)
    V_unbal = 0.5 * (params.ev.P_ac / params.transformer.S_rated) * 100;
end

function analyzeMonteCarloResults(mcResults, params)
% 分析蒙特卡洛结果
    fprintf('风险评估结果:\n');
    
    % 电压偏差越限概率
    V_dev_limit = params.std.voltage_deviation * 100;
    P_vdev = sum(abs(mcResults.voltage_deviation) > V_dev_limit) / length(mcResults.voltage_deviation);
    fprintf('  电压偏差越限概率: %.2f%% (限值: %.1f%%)\n', P_vdev*100, V_dev_limit);
    
    % 电压THD超标概率
    V_thd_limit = params.std.thd_voltage * 100;
    P_vthd = sum(mcResults.voltage_thd > V_thd_limit) / length(mcResults.voltage_thd);
    fprintf('  电压THD超标概率: %.2f%% (限值: %.1f%%)\n', P_vthd*100, V_thd_limit);
    
    % 变压器过载概率
    P_tload = sum(mcResults.transformer_loading > 100) / length(mcResults.transformer_loading);
    fprintf('  变压器过载概率: %.2f%%\n', P_tload*100);
    
    % 线路载流越限概率
    P_lload = sum(mcResults.line_loading > 100) / length(mcResults.line_loading);
    fprintf('  线路载流越限概率: %.2f%%\n', P_lload*100);
    
    % 不平衡度越限概率
    V_unbal_limit = params.std.unbalance * 100;
    P_unbal = sum(mcResults.voltage_unbalance > V_unbal_limit) / length(mcResults.voltage_unbalance);
    fprintf('  不平衡度越限概率: %.2f%% (限值: %.1f%%)\n', P_unbal*100, V_unbal_limit);
    
    fprintf('\n');
    
    % 绘制概率分布
    figure('Name', '蒙特卡洛风险评估结果', 'Position', [100 100 1200 800]);
    
    subplot(2, 3, 1);
    histogram(mcResults.voltage_deviation, 50);
    title('电压偏差分布');
    xlabel('电压偏差 (%)');
    ylabel('频数');
    xline(V_dev_limit, 'r--', '限值');
    xline(-V_dev_limit, 'r--', '限值');
    grid on;
    
    subplot(2, 3, 2);
    histogram(mcResults.voltage_thd, 50);
    title('电压THD分布');
    xlabel('THD (%)');
    ylabel('频数');
    xline(V_thd_limit, 'r--', '限值');
    grid on;
    
    subplot(2, 3, 3);
    histogram(mcResults.transformer_loading, 50);
    title('变压器负载率分布');
    xlabel('负载率 (%)');
    ylabel('频数');
    xline(100, 'r--', '额定');
    xline(80, 'g--', '预警');
    grid on;
    
    subplot(2, 3, 4);
    histogram(mcResults.line_loading, 50);
    title('线路载流量分布');
    xlabel('载流量 (%)');
    ylabel('频数');
    xline(100, 'r--', '额定');
    xline(80, 'g--', '预警');
    grid on;
    
    subplot(2, 3, 5);
    histogram(mcResults.voltage_unbalance, 50);
    title('电压不平衡度分布');
    xlabel('不平衡度 (%)');
    ylabel('频数');
    xline(V_unbal_limit, 'r--', '限值');
    grid on;
    
    subplot(2, 3, 6);
    % 风险等级矩阵
    risk_matrix = [P_vdev, P_vthd, P_tload, P_lload, P_unbal];
    bar(risk_matrix * 100);
    title('各项风险越限概率');
    xlabel('指标');
    ylabel('越限概率 (%)');
    set(gca, 'XTickLabel', {'电压偏差', '电压THD', '变压器负载', '线路载流', '不平衡度'});
    xtickangle(45);
    grid on;
    
    sgtitle('蒙特卡洛风险评估结果');
    
    % 保存结果
    resultDir = fullfile(pwd, 'results', 'monte_carlo');
    if ~exist(resultDir, 'dir')
        mkdir(resultDir);
    end
    
    timestamp = datestr(now, 'yyyymmdd_HHMMSS');
    save(fullfile(resultDir, sprintf('mc_results_%s.mat', timestamp)), 'mcResults');
    saveas(gcf, fullfile(resultDir, sprintf('mc_distribution_%s.png', timestamp)));
    
    fprintf('蒙特卡洛结果已保存到: %s\n', resultDir);
end
