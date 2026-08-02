function analyzeResults(simOut, params)
% 分析仿真结果并可视化
    fprintf('分析仿真结果...\n');
    
    % 获取输出数据
    if isfield(simOut, 'yout')
        yout = simOut.yout;
    else
        fprintf('警告: 未找到输出数据\n');
        return;
    end
    
    % 创建图形窗口
    figure('Name', '电能质量仿真结果', 'Position', [100 100 1200 800]);
    
    % 提取时间向量
    t = simOut.tout;
    
    % 绘制电压波形
    subplot(3, 2, 1);
    if isfield(yout, 'Vabc')
        plot(t, yout.Vabc.Data);
        title('三相电压波形');
        xlabel('时间 (s)');
        ylabel('电压 (V)');
        legend('Va', 'Vb', 'Vc');
        grid on;
    end
    
    % 绘制电流波形
    subplot(3, 2, 2);
    if isfield(yout, 'Iabc')
        plot(t, yout.Iabc.Data);
        title('三相电流波形');
        xlabel('时间 (s)');
        ylabel('电流 (A)');
        legend('Ia', 'Ib', 'Ic');
        grid on;
    end
    
    % 绘制有功功率
    subplot(3, 2, 3);
    if isfield(yout, 'P')
        plot(t, yout.P.Data/1000);
        title('有功功率');
        xlabel('时间 (s)');
        ylabel('功率 (kW)');
        grid on;
    end
    
    % 绘制无功功率
    subplot(3, 2, 4);
    if isfield(yout, 'Q')
        plot(t, yout.Q.Data/1000);
        title('无功功率');
        xlabel('时间 (s)');
        ylabel('功率 (kVAR)');
        grid on;
    end
    
    % 绘制电压偏差
    subplot(3, 2, 5);
    if isfield(yout, 'Vabc')
        V_nom = params.grid.V_phase;
        V_deviation = (yout.Vabc.Data - V_nom) / V_nom * 100;
        plot(t, V_deviation);
        title('电压偏差 (%)');
        xlabel('时间 (s)');
        ylabel('偏差 (%)');
        yline(params.std.voltage_deviation*100, 'r--', '上限');
        yline(-params.std.voltage_deviation*100, 'r--', '下限');
        grid on;
    end
    
    % 绘制频率
    subplot(3, 2, 6);
    if isfield(yout, 'Frequency')
        plot(t, yout.Frequency.Data);
        title('系统频率');
        xlabel('时间 (s)');
        ylabel('频率 (Hz)');
        yline(params.grid.f_nominal + params.std.frequency_deviation, 'r--', '上限');
        yline(params.grid.f_nominal - params.std.frequency_deviation, 'r--', '下限');
        grid on;
    end
    
    sgtitle('电能质量仿真结果分析');
    fprintf('结果可视化完成\n\n');
end
