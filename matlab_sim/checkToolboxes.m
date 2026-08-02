function checkToolboxes()
% 检查必需的Simulink工具箱
    requiredToolboxes = {
        'Simulink'
        'Simscape'
        'Simscape Electrical'
    };
    
    v = ver;
    installedNames = {v.Name};
    
    fprintf('检查工具箱:\n');
    allAvailable = true;
    for i = 1:length(requiredToolboxes)
        tbName = requiredToolboxes{i};
        isInstalled = any(contains(installedNames, tbName));
        if isInstalled
            fprintf('  [OK] %s\n', tbName);
        else
            fprintf('  [MISSING] %s - 请安装此工具箱\n', tbName);
            allAvailable = false;
        end
    end
    
    if ~allAvailable
        error('部分必需工具箱未安装，请先安装缺失的工具箱');
    end
    fprintf('\n');
end
