%% =========================================================================
%  新能源与充电桩接入影响评估 - MATLAB仿真项目主脚本
%  基于终端交流采样波形数据的电能质量AI应用
%  版本: V1.1
%  日期: 2026-08-01
% =========================================================================
%
%  使用方式:
%    main_setup                    - 运行默认场景(S4)的完整仿真
%    main_setup('S1')              - 运行指定场景仿真
%    main_setup('all')             - 批量运行全部5个场景
%    main_setup('monte_carlo')     - 运行蒙特卡洛风险评估
%    main_setup('dataset')         - 生成AI训练数据集
%    main_setup('full')            - 运行全部: 所有场景 + 蒙特卡洛 + 数据集
%
% =========================================================================

function main_setup(mode)
    if nargin < 1
        mode = 'S4';  % 默认场景: 光充耦合
    end

    clearvars -except mode;
    clc; close all;

    %% 项目路径设置
    projectRoot = fileparts(mfilename('fullpath'));
    addpath(genpath(projectRoot));

    fprintf('========================================\n');
    fprintf('新能源与充电桩接入影响评估仿真平台\n');
    fprintf('========================================\n\n');

    %% 检查工具箱
    checkToolboxes();

    %% 加载系统参数
    params = loadSystemParams();

    %% 根据模式执行
    switch lower(mode)
        case {'s1', 's2', 's3', 's4', 's5'}
            runSingleSimulation(mode, params);

        case 'all'
            runAllScenarios(params);

        case 'monte_carlo'
            runMonteCarlo(params);

        case 'dataset'
            runDatasetGeneration(params);

        case 'full'
            runAllScenarios(params);
            runMonteCarlo(params);
            runDatasetGeneration(params);

        otherwise
            fprintf('未知模式: %s\n', mode);
            fprintf('支持模式: S1~S5, all, monte_carlo, dataset, full\n');
    end

    fprintf('\n全部任务完成!\n');
end

%% =========================================================================
function runSingleSimulation(scenario, params)
    fprintf('当前仿真场景: %s\n', getScenarioName(scenario));

    %% 运行仿真
    simOut = runSimulation(scenario, params);

    %% 结果分析与可视化
    analyzeResults(simOut, params);

    %% 电能质量指标计算
    pqMetrics = calculatePQMetrics(simOut, params);
    displayPQMetrics(pqMetrics);

    %% 保存结果
    saveResults(simOut, pqMetrics, scenario);
end

%% =========================================================================
function runAllScenarios(params)
    fprintf('\n========================================\n');
    fprintf('批量运行全部5个仿真场景\n');
    fprintf('========================================\n\n');

    scenarios = {'S1', 'S2', 'S3', 'S4', 'S5'};
    allMetrics = struct();

    for i = 1:length(scenarios)
        scenario = scenarios{i};
        fprintf('\n--- 场景 %d/%d: %s ---\n', i, length(scenarios), getScenarioName(scenario));

        simOut = runSimulation(scenario, params);
        pqMetrics = calculatePQMetrics(simOut, params);
        saveResults(simOut, pqMetrics, scenario);

        % 收集关键指标用于对比
        allMetrics.(scenario) = pqMetrics;
    end

    %% 生成场景对比报告
    generateComparisonReport(allMetrics, params);
end

%% =========================================================================
function runMonteCarlo(params)
    fprintf('\n');
    mcResults = monteCarloAnalysis(params, 1000);
end

%% =========================================================================
function runDatasetGeneration(params)
    fprintf('\n');
    dataset = generateDataset(params, 10000);
end

%% =========================================================================
function generateComparisonReport(allMetrics, params)
    fprintf('\n========================================\n');
    fprintf('场景对比汇总报告\n');
    fprintf('========================================\n');

    scenarios = {'S1', 'S2', 'S3', 'S4', 'S5'};
    scenarioNames = {'基准负荷', '充电桩', '分布式光伏', '光充耦合', '极端场景'};

    % 表格头
    fprintf('\n%-12s', '指标');
    for i = 1:length(scenarios)
        fprintf('%12s', scenarioNames{i});
    end
    fprintf('\n');
    fprintf('%s\n', repmat('-', 1, 12*6));

    % 电压偏差
    fprintf('%-12s', '电压偏差(%)');
    for i = 1:length(scenarios)
        s = scenarios{i};
        if isfield(allMetrics.(s), 'voltage_deviation_max')
            fprintf('%12.2f', allMetrics.(s).voltage_deviation_max);
        else
            fprintf('%12s', '-');
        end
    end
    fprintf('\n');

    % 电压THD
    fprintf('%-12s', '电压THD(%)');
    for i = 1:length(scenarios)
        s = scenarios{i};
        if isfield(allMetrics.(s), 'voltage_thd')
            fprintf('%12.2f', allMetrics.(s).voltage_thd);
        else
            fprintf('%12s', '-');
        end
    end
    fprintf('\n');

    % 电流THD
    fprintf('%-12s', '电流THD(%)');
    for i = 1:length(scenarios)
        s = scenarios{i};
        if isfield(allMetrics.(s), 'current_thd')
            fprintf('%12.2f', allMetrics.(s).current_thd);
        else
            fprintf('%12s', '-');
        end
    end
    fprintf('\n');

    % 不平衡度
    fprintf('%-12s', '不平衡度(%)');
    for i = 1:length(scenarios)
        s = scenarios{i};
        if isfield(allMetrics.(s), 'voltage_unbalance')
            fprintf('%12.2f', allMetrics.(s).voltage_unbalance);
        else
            fprintf('%12s', '-');
        end
    end
    fprintf('\n');

    % 变压器负载率
    fprintf('%-12s', '变压器负载(%)');
    for i = 1:length(scenarios)
        s = scenarios{i};
        if isfield(allMetrics.(s), 'transformer_loading')
            fprintf('%12.1f', allMetrics.(s).transformer_loading);
        else
            fprintf('%12s', '-');
        end
    end
    fprintf('\n');

    % 线路载流量
    fprintf('%-12s', '线路载流(%)');
    for i = 1:length(scenarios)
        s = scenarios{i};
        if isfield(allMetrics.(s), 'line_loading')
            fprintf('%12.1f', allMetrics.(s).line_loading);
        else
            fprintf('%12s', '-');
        end
    end
    fprintf('\n');

    % 功率因数
    fprintf('%-12s', '功率因数');
    for i = 1:length(scenarios)
        s = scenarios{i};
        if isfield(allMetrics.(s), 'power_factor')
            fprintf('%12.3f', allMetrics.(s).power_factor);
        else
            fprintf('%12s', '-');
        end
    end
    fprintf('\n');

    fprintf('%s\n', repmat('-', 1, 12*6));

    % 越限统计
    fprintf('\n越限指标统计:\n');
    limits = struct('voltage_deviation_max', 7, 'voltage_thd', 5, ...
                    'current_thd', 8, 'voltage_unbalance', 2, ...
                    'transformer_loading', 100, 'line_loading', 100);
    limitNames = {'电压偏差', '电压THD', '电流THD', '不平衡度', '变压器负载', '线路载流'};
    limitFields = {'voltage_deviation_max', 'voltage_thd', 'current_thd', ...
                   'voltage_unbalance', 'transformer_loading', 'line_loading'};

    for j = 1:length(limitFields)
        field = limitFields{j};
        violations = 0;
        for i = 1:length(scenarios)
            s = scenarios{i};
            if isfield(allMetrics.(s), field)
                val = allMetrics.(s).(field);
                lim = limits.(field);
                if val > lim
                    violations = violations + 1;
                end
            end
        end
        fprintf('  %s: %d/%d 场景越限\n', limitNames{j}, violations, length(scenarios));
    end

    fprintf('\n');

    % 保存对比报告
    resultDir = fullfile(pwd, 'results', 'comparison');
    if ~exist(resultDir, 'dir')
        mkdir(resultDir);
    end
    timestamp = datestr(now, 'yyyymmdd_HHMMSS');
    save(fullfile(resultDir, sprintf('comparison_%s.mat', timestamp)), 'allMetrics', 'params');
    fprintf('对比报告已保存: %s\n', resultDir);
end
