function name = getScenarioName(scenario)
% 获取场景名称
    switch scenario
        case 'S1'
            name = 'S1 - 纯常规负荷基准场景';
        case 'S2'
            name = 'S2 - 充电桩接入场景';
        case 'S3'
            name = 'S3 - 分布式光伏接入场景';
        case 'S4'
            name = 'S4 - 光充耦合接入场景';
        case 'S5'
            name = 'S5 - 极端天气/高渗透率场景';
        otherwise
            name = '未知场景';
    end
end
