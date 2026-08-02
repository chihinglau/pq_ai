function displayPQMetrics(pqMetrics)
% 显示电能质量指标评估结果
    fprintf('========================================\n');
    fprintf('电能质量指标评估结果\n');
    fprintf('========================================\n');
    
    % 评估标准
    std_voltage_dev = 7;      % 电压偏差限值 (%)
    std_thd_v = 5;            % 电压THD限值 (%)
    std_thd_i = 8;            % 电流THD限值 (%)
    std_unbalance = 2;        % 不平衡度限值 (%)
    std_freq_dev = 0.5;       % 频率偏差限值 (Hz)
    std_loading = 100;        % 负载率限值 (%)
    
    % 评估函数
    assess = @(value, limit) iif(value <= limit, ' [PASS]', ' [FAIL]');
    
    fields = fieldnames(pqMetrics);
    for i = 1:length(fields)
        field = fields{i};
        value = pqMetrics.(field);
        
        switch field
            case 'voltage_deviation'
                status = assess(max(abs(value)), std_voltage_dev);
                fprintf('电压偏差:      %8.2f%% %s\n', max(abs(value)), status);
                
            case 'voltage_deviation_max'
                % 已在voltage_deviation中显示
                
            case 'voltage_thd'
                status = assess(value, std_thd_v);
                fprintf('电压THD:       %8.2f%% %s\n', value, status);
                
            case 'current_thd'
                status = assess(value, std_thd_i);
                fprintf('电流THD:       %8.2f%% %s\n', value, status);
                
            case 'voltage_unbalance'
                status = assess(value, std_unbalance);
                fprintf('电压不平衡度:  %8.2f%% %s\n', value, status);
                
            case 'frequency_deviation'
                status = assess(value, std_freq_dev);
                fprintf('频率偏差:      %8.3f Hz %s\n', value, status);
                
            case 'transformer_loading'
                status = assess(value, std_loading);
                fprintf('变压器负载率:  %8.1f%% %s\n', value, status);
                
            case 'line_loading'
                status = assess(value, std_loading);
                fprintf('线路载流量:    %8.1f%% %s\n', value, status);
                
            case 'power_factor'
                fprintf('功率因数:      %8.3f\n', value);
        end
    end
    
    fprintf('========================================\n');
end

function out = iif(cond, a, b)
    if cond
        out = a;
    else
        out = b;
    end
end
