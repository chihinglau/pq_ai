function saveResults(simOut, pqMetrics, scenario)
% 保存仿真结果
    fprintf('保存仿真结果...\n');
    
    % 创建结果目录
    resultDir = fullfile(pwd, 'results', scenario);
    if ~exist(resultDir, 'dir')
        mkdir(resultDir);
    end
    
    % 保存工作空间变量
    timestamp = datestr(now, 'yyyymmdd_HHMMSS');
    filename = fullfile(resultDir, sprintf('sim_result_%s.mat', timestamp));
    save(filename, 'simOut', 'pqMetrics', 'scenario');
    fprintf('  结果已保存: %s\n', filename);
    
    % 保存指标到文本文件
    txtFile = fullfile(resultDir, sprintf('pq_metrics_%s.txt', timestamp));
    fid = fopen(txtFile, 'w');
    fprintf(fid, '电能质量指标评估结果\n');
    fprintf(fid, '场景: %s\n', scenario);
    fprintf(fid, '时间: %s\n', datestr(now));
    fprintf(fid, '========================================\n');
    
    fields = fieldnames(pqMetrics);
    for i = 1:length(fields)
        field = fields{i};
        value = pqMetrics.(field);
        fprintf(fid, '%s: %.4f\n', field, value);
    end
    
    fclose(fid);
    fprintf('  指标已保存: %s\n', txtFile);
    
    % 保存图形
    fig = gcf;
    if ~isempty(fig)
        imgFile = fullfile(resultDir, sprintf('waveform_%s.png', timestamp));
        saveas(fig, imgFile);
        fprintf('  图形已保存: %s\n', imgFile);
    end
    
    fprintf('\n');
end
