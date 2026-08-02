const { Document, Packer, Paragraph, TextRun, Table, TableRow, TableCell,
        Header, Footer, AlignmentType, PageOrientation, LevelFormat,
        HeadingLevel, BorderStyle, WidthType, ShadingType,
        VerticalAlign, PageNumber, PageBreak } = require('docx');
const fs = require('fs');

const border = { style: BorderStyle.SINGLE, size: 1, color: "000000" };
const borders = { top: border, bottom: border, left: border, right: border };

function cell(text, width, opts = {}) {
    return new TableCell({
        borders,
        width: { size: width, type: WidthType.DXA },
        shading: opts.shading ? { fill: opts.shading, type: ShadingType.CLEAR } : undefined,
        margins: { top: 60, bottom: 60, left: 80, right: 80 },
        verticalAlign: VerticalAlign.CENTER,
        children: [new Paragraph({
            alignment: opts.align || AlignmentType.LEFT,
            children: [new TextRun({ text, bold: opts.bold || false, size: 21, font: "宋体" })]
        })]
    });
}

function h1(text) {
    return new Paragraph({
        heading: HeadingLevel.HEADING_1,
        spacing: { before: 300, after: 200 },
        children: [new TextRun({ text, bold: true, size: 32, font: "黑体" })]
    });
}

function h2(text) {
    return new Paragraph({
        heading: HeadingLevel.HEADING_2,
        spacing: { before: 240, after: 160 },
        children: [new TextRun({ text, bold: true, size: 28, font: "黑体" })]
    });
}

function h3(text) {
    return new Paragraph({
        heading: HeadingLevel.HEADING_3,
        spacing: { before: 180, after: 120 },
        children: [new TextRun({ text, bold: true, size: 24, font: "黑体" })]
    });
}

function p(text, opts = {}) {
    return new Paragraph({
        spacing: { before: opts.before || 80, after: opts.after || 80, line: 360 },
        indent: opts.indent ? { firstLine: 420 } : undefined,
        alignment: opts.align || AlignmentType.LEFT,
        children: [new TextRun({ text, size: 21, font: "宋体" })]
    });
}

function pb() {
    return new Paragraph({ children: [new PageBreak()] });
}

const doc = new Document({
    styles: {
        default: { document: { run: { font: "宋体", size: 21 } } },
        paragraphStyles: [
            { id: "Heading1", name: "Heading 1", basedOn: "Normal", next: "Normal", quickFormat: true,
              run: { size: 32, bold: true, font: "黑体" },
              paragraph: { spacing: { before: 300, after: 200 }, outlineLevel: 0 } },
            { id: "Heading2", name: "Heading 2", basedOn: "Normal", next: "Normal", quickFormat: true,
              run: { size: 28, bold: true, font: "黑体" },
              paragraph: { spacing: { before: 240, after: 160 }, outlineLevel: 1 } },
            { id: "Heading3", name: "Heading 3", basedOn: "Normal", next: "Normal", quickFormat: true,
              run: { size: 24, bold: true, font: "黑体" },
              paragraph: { spacing: { before: 180, after: 120 }, outlineLevel: 2 } },
        ]
    },
    sections: [{
        properties: {
            page: {
                size: { width: 11906, height: 16838 },
                margin: { top: 1440, right: 1440, bottom: 1440, left: 1800 }
            }
        },
        headers: {
            default: new Header({
                children: [new Paragraph({
                    alignment: AlignmentType.CENTER,
                    children: [new TextRun({ text: "新能源与充电桩接入影响评估技术方案", size: 18, font: "宋体", color: "666666" })]
                })]
            })
        },
        footers: {
            default: new Footer({
                children: [new Paragraph({
                    alignment: AlignmentType.CENTER,
                    children: [
                        new TextRun({ text: "第 ", size: 18, font: "宋体" }),
                        new TextRun({ children: [PageNumber.CURRENT], size: 18, font: "宋体" }),
                        new TextRun({ text: " 页", size: 18, font: "宋体" })
                    ]
                })]
            })
        },
        children: [
            // 封面标题
            new Paragraph({ spacing: { before: 1200, after: 400 }, alignment: AlignmentType.CENTER,
                children: [new TextRun({ text: "新能源与充电桩接入影响评估", bold: true, size: 44, font: "黑体" })]
            }),
            new Paragraph({ spacing: { before: 200, after: 800 }, alignment: AlignmentType.CENTER,
                children: [new TextRun({ text: "技术方案", bold: true, size: 44, font: "黑体" })]
            }),
            new Paragraph({ spacing: { before: 600, after: 200 }, alignment: AlignmentType.CENTER,
                children: [new TextRun({ text: "——基于终端交流采样波形数据的电能质量AI应用", size: 28, font: "楷体" })]
            }),
            new Paragraph({ spacing: { before: 1600, after: 200 }, alignment: AlignmentType.CENTER,
                children: [new TextRun({ text: "编制日期：2026年8月", size: 24, font: "宋体" })]
            }),
            new Paragraph({ spacing: { before: 200, after: 200 }, alignment: AlignmentType.CENTER,
                children: [new TextRun({ text: "版本：V1.0", size: 24, font: "宋体" })]
            }),
            pb(),

            // 一、项目背景
            h1("一、项目背景"),
            h2("1.1 政策与行业背景"),
            p("在“双碳”目标驱动下，我国新型电力系统建设进入加速期。分布式光伏、分散式风电及电动汽车充电桩呈现大规模、高密度接入低压配电网的态势，彻底改变了传统配电网单向潮流、负荷稳定的运行特性。据国家能源局统计，截至2025年底，全国分布式光伏累计装机已突破3亿千瓦，电动汽车保有量超过3500万辆，配电台区正面临前所未有的电能质量挑战。", { indent: true }),
            p("新能源发电具有间歇性、随机性与波动性特征，充电桩则属于典型的非线性冲击性负荷。两类设备在运行过程中均会产生谐波污染、电压偏差、三相不平衡、电压闪变及频率扰动等电能质量问题，严重影响配电网的供电可靠性与供电质量，同时制约新能源消纳与充电桩的规模化推广应用。", { indent: true }),

            h2("1.2 技术现状与痛点"),
            p("当前电力系统电能质量监测与评估体系存在明显短板。传统监测方式依托定点监测装置，仅基于电压、电流有效值、总谐波畸变率（THD）等统计指标开展阈值判别，存在以下突出问题：", { indent: true }),
            p("（1）数据维度不足：忽略了设备投切、工况波动过程中的原始波形时域、频域细节信息，无法精准区分新能源逆变器、充电桩整流装置及常规负荷产生的扰动源。", { indent: true }),
            p("（2）评估精度受限：难以量化多设备耦合接入下的电能质量非线性叠加影响，无法精准预判台区设备最大接入容量边界。", { indent: true }),
            p("（3）响应机制滞后：以被动统计告警为主，缺乏面向规划与运行的前瞻性风险评估与分级预警能力。", { indent: true }),

            h2("1.3 终端数据基础"),
            p("现阶段配电台区智能融合终端、智能测控终端已全面普及，具备高频交流采样能力。智能融合终端支持每周波256点采样，专变/ECU终端支持每周波512点采样，可实时采集电压、电流原始波形数据，为精细化电能质量分析与人工智能建模提供了海量、高精度的数据源。基于终端原始采样波形，结合AI算法挖掘扰动波形特征，开展新能源与充电桩接入配网的电能质量影响评估，成为解决当前配网电能质量管控难题的关键技术路径。", { indent: true }),

            // 二、评估目标
            h1("二、评估目标"),
            p("本技术方案旨在建立一套覆盖“数据采集—仿真建模—影响量化—风险预警—治理建议”全链条的新能源与充电桩接入影响评估体系，具体目标如下：", { indent: true }),
            p("（1）精准刻画影响机理：系统分析分布式光伏、风电及充电桩接入对配电网电压、频率、谐波、三相不平衡等电能质量指标的作用机理与耦合效应。", { indent: true }),
            p("（2）量化接入冲击程度：评估充电桩大规模接入对变压器负载率、线路容量及峰谷负荷特性的冲击，建立变压器过载、线路载流越限等风险评估模型。", { indent: true }),
            p("（3）构建仿真评估平台：基于潮流计算、暂态分析及蒙特卡洛模拟，建立配电台区新能源与充电桩接入的量化评估方法，支撑离线规划与在线运行双场景应用。", { indent: true }),
            p("（4）实现分级预警管控：建立安全、预警、超标三级评估区间，提出储能配置、有序充电、无功补偿、网络重构等分级治理措施建议。", { indent: true }),
            p("（5）形成可落地应用模块：研发适配边缘智能终端的轻量化AI评估模块，实现就地数据处理、就地AI推理与就地风险预警，适配配网分布式架构。", { indent: true }),

            // 三、技术路线
            h1("三、技术路线"),
            h2("3.1 总体架构"),
            p("采用“云—边—端”协同三层架构：", { indent: true }),
            p("终端侧（边缘）：智能融合终端与专变/ECU终端负责高频波形采集、标准电能质量指标计算、事件触发与波形冻结、边缘特征提取及轻量化AI推理。", { indent: true }),
            p("边缘侧（台区）：部署轻量化评估模型，实现台区承载能力动态评价、越限风险实时预判及治理方案本地推荐。", { indent: true }),
            p("平台侧（云端/训练平台）：承担数据治理、模型训练、大样本仿真、跨台区关联分析及报告生成，支持离线规划评估与全域态势感知。", { indent: true }),

            h2("3.2 核心技术路径"),
            p("本方案核心技术路径包括以下五个环节：", { indent: true }),
            p("环节一：多源数据融合采集。整合终端高频波形、负荷曲线、新能源出力特性、气象数据及台区档案，建立统一数据规范与标签体系。", { indent: true }),
            p("环节二：仿真建模与数据集构建。基于Matlab/Simulink、PSCAD搭建低压配电台区仿真模型，批量生成光伏、充电桩及光充耦合场景下的扰动波形数据集，结合现场实测数据完善样本库。", { indent: true }),
            p("环节三：时频特征融合与AI建模。采用CNN提取波形局部时域特征，Transformer捕捉长时序规律，融合FFT频域谐波特征与小波包分解细节特征，构建多特征融合轻量化混合深度学习模型。", { indent: true }),
            p("环节四：量化评估与容量测算。依据GB/T 14549、GB/T 12325等国家电能质量标准，建立波形特征—设备接入容量—电能质量指标的非线性映射关系，划分三级评估区间，实现台区承载能力动态评估。", { indent: true }),
            p("环节五：分级预警与治理决策。基于风险评估结果，自动生成安全/预警/超标三级告警，联动储能配置、有序充电策略、无功补偿及网络重构等治理措施建议。", { indent: true }),

            pb(),

            // 四、关键评估指标与方法
            h1("四、关键评估指标与方法"),
            h2("4.1 新能源接入对电能质量的影响分析"),
            h3("4.1.1 电压偏差与波动"),
            p("分布式光伏在午间出力高峰时段向配电网反送功率，导致并网点及馈线末端电压抬升；傍晚出力骤降或云层遮挡则引起电压快速跌落。风电出力受风速波动影响，具有更强的随机性，易引发持续性电压波动与闪变。", { indent: true }),
            p("评估方法：采用前推回代潮流计算，量化不同光伏渗透率、接入位置及功率因数下的节点电压分布；基于实测波形提取电压偏差ΔV与电压变动频度，结合GB/T 12325判定越限风险。", { indent: true }),

            h3("4.1.2 谐波污染"),
            p("光伏逆变器采用PWM调制技术，产生以2次、4次及开关频率附近为中心的高频谐波电流；风电变流器则因机侧/网侧变流器协调控制引入宽频谐波。谐波电流在配电网阻抗上产生谐波压降，导致电压总谐波畸变率（THD）超标。", { indent: true }),
            p("评估方法：依据GB/T 14549，计算2~31次谐波含有率及THD；基于终端波形做高分辨率FFT（8192点）精细分析；建立谐波阻抗模型，评估谐波放大与并联谐振风险。", { indent: true }),

            h3("4.1.3 三相不平衡"),
            p("单相光伏逆变器或单相充电桩的不均衡接入，导致三相电流差异显著，引发负序电流。负序电流使变压器附加损耗增加、电动机转矩脉动、保护装置误动。", { indent: true }),
            p("评估方法：计算三相电压、电流不平衡度（负序分量/正序分量），依据GB/T 15543评估越限程度；结合相别负荷分布与接入容量，建立不平衡度与单相接入规模的量化关系。", { indent: true }),

            h3("4.1.4 频率影响"),
            p("分布式新能源通过逆变器并网，与传统同步机相比缺乏转动惯量。在配电网层面，分布式光伏/风电出力骤变会通过上级电网耦合影响系统频率；在高渗透率场景下，局部孤岛运行风险增加。", { indent: true }),
            p("评估方法：分析新能源出力变化率（RoCoF）与频率偏差的关系；在仿真中模拟光伏脱网、风电切出等故障，评估频率跌落深度与恢复时间。", { indent: true }),

            h2("4.2 充电桩接入对配电网的冲击评估"),
            h3("4.2.1 变压器负载率"),
            p("充电桩属于大功率、短时冲击负荷。单台7kW交流充电桩工作电流约32A，多台同时充电易使配变负载率迅速攀升。傍晚下班时段为充电高峰，与居民用电高峰叠加，形成“双峰叠加”效应，变压器过载风险显著增加。", { indent: true }),
            p("评估方法：基于日负荷曲线叠加充电负荷，计算变压器日最大负载率与持续时间；建立变压器热模型，评估绕组热点温度与绝缘老化加速因子。", { indent: true }),

            h3("4.2.2 线路容量"),
            p("充电桩集中接入区域，低压出线电流密度增大，线路压降上升，末端电压降低。谐波电流导致线路附加发热，有效载流量下降。", { indent: true }),
            p("评估方法：采用潮流计算量化不同充电同时率下的线路电流分布与节点电压；依据导线载流量与温升模型，校核线路热稳定容量。", { indent: true }),

            h3("4.2.3 峰谷负荷特性"),
            p("无序充电行为使原有负荷峰谷差进一步扩大，配电网设备利用效率降低，峰时段购电成本上升。在光伏高发、充电高峰重叠场景下，潮流方向频繁反转，传统保护配合关系被破坏。", { indent: true }),
            p("评估方法：基于聚类分析识别典型充电行为模式；采用LSTM/GRU时序预测模型，预测未来1~24小时负荷曲线；评估峰谷差率、负荷率及反向潮流时段占比。", { indent: true }),

            h3("4.2.4 谐波与功率因数"),
            p("充电桩整流装置产生以5次、7次、11次为主的特征谐波电流，多台充电桩谐波电流矢量叠加，可能导致THD超标。同时，整流型充电桩功率因数较低（0.6~0.8），增加无功需求。", { indent: true }),
            p("评估方法：建立充电桩谐波电流源模型，叠加不同台数与类型的谐波频谱；计算公共连接点THD与功率因数，评估无功补偿需求。", { indent: true }),

            h2("4.3 光充耦合场景的综合影响"),
            p("光伏与充电桩在同一台区耦合接入时，存在显著的时空互补与冲突特性。午间光伏出力可部分抵消充电负荷，降低净负荷峰值；但若光伏容量远大于充电负荷，则出现大量反送功率，导致电压越上限。傍晚光伏出力骤降而充电负荷激增，净负荷曲线陡升，电压快速跌落风险突出。", { indent: true }),
            p("评估方法：建立光充协同仿真模型，设定典型日场景（晴天、多云、雨天）与不同充电行为模式，量化耦合场景下的电压越限概率、谐波叠加系数及变压器过载概率。", { indent: true }),

            pb(),

            // 五、仿真建模与量化评估方法
            h1("五、仿真建模与量化评估方法"),
            h2("5.1 仿真平台与模型构建"),
            p("本方案采用Matlab/Simulink与PSCAD/EMTDC双平台协同仿真策略：", { indent: true }),
            p("Matlab/Simulink：主要用于稳态潮流计算、谐波潮流分析、数据批量生成与AI模型训练数据准备。", { indent: true }),
            p("PSCAD/EMTDC：主要用于电磁暂态分析，模拟光伏逆变器并离网、充电桩投切、短路故障等暂态过程。", { indent: true }),

            h3("5.1.1 配电台区仿真模型"),
            p("搭建包含10kV/0.4kV配电变压器、低压馈线、负荷节点、光伏并网节点、充电桩接入节点的典型低压台区模型。模型参数包括：", { indent: true }),
            p("变压器：容量100~800kVA，阻抗电压4%~6%，联结组Dyn11。", { indent: true }),
            p("馈线：采用LGJ-70/LGJ-120型导线，长度100~800m，考虑线路电阻、电抗及对地电容。", { indent: true }),
            p("负荷：采用恒功率+恒阻抗混合模型，涵盖居民、商业及工业负荷，功率因数0.85~0.95。", { indent: true }),
            p("光伏：采用单相/三相逆变器模型，MPPT控制，功率等级3~30kW，可设置出力曲线或随机波动。", { indent: true }),
            p("充电桩：采用交流充电桩（7kW）与直流充电桩（30~120kW）模型，含PWM整流器及谐波电流源特征。", { indent: true }),

            h3("5.1.2 多场景仿真工况设计"),
            p("设计以下五类核心仿真场景，覆盖典型运行工况：", { indent: true }),
            new Table({
                width: { size: 8306, type: WidthType.DXA },
                columnWidths: [1500, 3406, 3400],
                rows: [
                    new TableRow({ children: [
                        cell("场景编号", 1500, { bold: true, shading: "D9E2F3", align: AlignmentType.CENTER }),
                        cell("场景描述", 3406, { bold: true, shading: "D9E2F3", align: AlignmentType.CENTER }),
                        cell("评估重点", 3400, { bold: true, shading: "D9E2F3", align: AlignmentType.CENTER })
                    ]}),
                    new TableRow({ children: [
                        cell("S1", 1500, { align: AlignmentType.CENTER }),
                        cell("纯常规负荷基准场景", 3406),
                        cell("建立基准电压、电流、功率分布", 3400)
                    ]}),
                    new TableRow({ children: [
                        cell("S2", 1500, { align: AlignmentType.CENTER }),
                        cell("单/多台充电桩接入场景", 3406),
                        cell("评估谐波、负载率、电压偏差", 3400)
                    ]}),
                    new TableRow({ children: [
                        cell("S3", 1500, { align: AlignmentType.CENTER }),
                        cell("分布式光伏接入场景", 3406),
                        cell("评估电压抬升、反向潮流、谐波", 3400)
                    ]}),
                    new TableRow({ children: [
                        cell("S4", 1500, { align: AlignmentType.CENTER }),
                        cell("光充耦合接入场景", 3406),
                        cell("评估耦合效应、峰谷特性、越限风险", 3400)
                    ]}),
                    new TableRow({ children: [
                        cell("S5", 1500, { align: AlignmentType.CENTER }),
                        cell("极端天气/高渗透率场景", 3406),
                        cell("评估暂态冲击、承载边界", 3400)
                    ]}),
                ]
            }),

            h2("5.2 潮流计算方法"),
            p("采用改进前推回代法进行三相不平衡潮流计算，适应配电网辐射状结构与多相不平衡特性。", { indent: true }),
            p("数学模型：对于节点i，功率方程为：", { indent: true }),
            p("Si = Pi + jQi = Vi · Σ(Yij · Vj)*,  j∈Ωi", { indent: true, align: AlignmentType.CENTER }),
            p("其中Si为节点注入功率，Vi为节点电压，Yij为节点导纳矩阵元素，Ωi为节点i的邻接节点集合。", { indent: true }),
            p("计算流程：", { indent: true }),
            p("（1）初始化节点电压为额定值，设置光伏出力与充电负荷；", { indent: true }),
            p("（2）前推过程：从末端负荷节点向变压器根节点计算支路功率；", { indent: true }),
            p("（3）回代过程：从根节点向末端节点更新节点电压；", { indent: true }),
            p("（4）迭代收敛判定：max|ΔV| < 0.0001 p.u.，否则返回步骤（2）。", { indent: true }),

            h2("5.3 暂态分析方法"),
            p("针对设备投切、故障及极端天气引发的暂态过程，采用电磁暂态仿真与时域分析相结合的方法：", { indent: true }),
            p("（1）光伏逆变器并离网暂态：模拟MPPT切换、低电压穿越、孤岛检测动作过程，记录并网点电压、电流波形，评估暂降/暂升深度与持续时间。", { indent: true }),
            p("（2）充电桩群投切冲击：模拟多台充电桩同时启动的励磁涌流与谐波冲击，分析变压器磁通饱和与保护动作特性。", { indent: true }),
            p("（3）短路故障分析：模拟三相短路、单相接地等故障类型，计算短路电流分布，校验开关设备开断能力与保护配合。", { indent: true }),

            h2("5.4 蒙特卡洛风险评估"),
            p("为量化不确定性因素（光伏出力波动、充电行为随机性、负荷预测误差）对电能质量的影响，引入蒙特卡洛模拟方法。", { indent: true }),
            p("模拟流程：", { indent: true }),
            p("（1）建立光伏出力概率模型（Beta分布）、充电行为模型（蒙特卡洛抽样充电时段与时长）、负荷模型（正态分布）；", { indent: true }),
            p("（2）每次抽样生成一组运行场景，执行潮流计算；", { indent: true }),
            p("（3）重复N次（N≥1000），统计各节点电压越限概率、THD超标概率、变压器过载概率；", { indent: true }),
            p("（4）输出风险概率分布曲线与置信区间。", { indent: true }),
            p("风险概率计算公式：", { indent: true }),
            p("P_risk = (1/N) · Σ I[ V_rms^(i) > V_max 或 V_rms^(i) < V_min ]", { indent: true, align: AlignmentType.CENTER }),
            p("其中I[·]为指示函数，当条件满足时取1，否则取0。", { indent: true }),

            pb(),

            // 六、数据采集需求
            h1("六、数据采集需求"),
            h2("6.1 终端波形数据采集"),
            p("终端波形数据是AI评估模型的核心输入，采集要求如下：", { indent: true }),
            new Table({
                width: { size: 8306, type: WidthType.DXA },
                columnWidths: [2400, 2953, 2953],
                rows: [
                    new TableRow({ children: [
                        cell("数据类型", 2400, { bold: true, shading: "D9E2F3", align: AlignmentType.CENTER }),
                        cell("采集要求", 2953, { bold: true, shading: "D9E2F3", align: AlignmentType.CENTER }),
                        cell("用途", 2953, { bold: true, shading: "D9E2F3", align: AlignmentType.CENTER })
                    ]}),
                    new TableRow({ children: [
                        cell("三相电压波形", 2400),
                        cell("256/512点/周波，采样率12.8k/25.6kHz", 2953),
                        cell("电压偏差、波动、谐波分析", 2953)
                    ]}),
                    new TableRow({ children: [
                        cell("三相电流波形", 2400),
                        cell("256/512点/周波，同步采样", 2953),
                        cell("谐波源识别、负载率评估", 2953)
                    ]}),
                    new TableRow({ children: [
                        cell("零序电压/电流", 2400),
                        cell("与三相波形同步采集", 2953),
                        cell("接地故障、不平衡分析", 2953)
                    ]}),
                    new TableRow({ children: [
                        cell("事件冻结波形", 2400),
                        cell("事件前后10~20周波完整波形", 2953),
                        cell("暂态分析、事件特征提取", 2953)
                    ]}),
                ]
            }),

            h2("6.2 负荷曲线数据"),
            p("（1）台区总负荷曲线：15分钟或1分钟粒度，包含有功功率、无功功率、功率因数，用于建立基准负荷模型。", { indent: true }),
            p("（2）用户分项负荷曲线：通过智能电表或终端采集重点用户（大用户、专变用户）的分时用电数据，用于负荷特性分析与预测。", { indent: true }),
            p("（3）充电负荷专项数据：充电桩编号、充电起止时间、充电电量、充电功率曲线、充电桩类型（交流/直流、功率等级），用于充电行为建模。", { indent: true }),

            h2("6.3 新能源出力特性数据"),
            p("（1）光伏发电数据：光伏装机容量、逆变器型号、并网方式（单相/三相）、历史出力曲线（有功功率、无功功率、功率因数）、气象数据（辐照度、温度、云层遮挡信息）。", { indent: true }),
            p("（2）风电出力数据：风机容量、切入/切出风速、轮毂高度、历史出力曲线、风速数据。", { indent: true }),
            p("（3）储能运行数据：储能容量、充放电功率、SOC状态、控制策略，用于评估储能对电能质量的调节效果。", { indent: true }),

            h2("6.4 台区档案与拓扑数据"),
            p("（1）变压器参数：容量、阻抗电压、联结组别、额定电流、负载率历史数据。", { indent: true }),
            p("（2）线路参数：导线型号、长度、电阻、电抗、载流量、拓扑连接关系。", { indent: true }),
            p("（3）用户档案：用户类型、重要等级、历史投诉记录、治理措施记录。", { indent: true }),
            p("（4）新能源与充电设施档案：接入容量、接入位置、接入相别、设备型号、并网时间。", { indent: true }),

            h2("6.5 数据质量要求"),
            p("（1）时间同步：终端间时钟偏差小于1ms，采用PTP（IEEE 1588）或NTP协议实现时间对齐。", { indent: true }),
            p("（2）数据完整率：波形数据完整率≥99.5%，缺失数据需标记并插值补偿。", { indent: true }),
            p("（3）采样一致性：三相电压、电流同步采样，通道对应关系准确。", { indent: true }),
            p("（4）标签可追溯：样本标签来源清晰，专家标注需复核确认。", { indent: true }),

            pb(),

            // 七、分级预警指标与治理措施
            h1("七、分级预警指标与治理措施"),
            h2("7.1 分级预警指标体系"),
            p("依据GB/T 12325、GB/T 14549、GB/T 15543等国家电能质量标准，结合配电网运行实际，建立安全、预警、超标三级评估区间。", { indent: true }),
            new Table({
                width: { size: 8306, type: WidthType.DXA },
                columnWidths: [1800, 2200, 2153, 2153],
                rows: [
                    new TableRow({ children: [
                        cell("评估指标", 1800, { bold: true, shading: "D9E2F3", align: AlignmentType.CENTER }),
                        cell("安全区间（绿色）", 2200, { bold: true, shading: "C6EFCE", align: AlignmentType.CENTER }),
                        cell("预警区间（黄色）", 2153, { bold: true, shading: "FFEB9C", align: AlignmentType.CENTER }),
                        cell("超标区间（红色）", 2153, { bold: true, shading: "FFC7CE", align: AlignmentType.CENTER })
                    ]}),
                    new TableRow({ children: [
                        cell("电压偏差", 1800),
                        cell("≤ ±5%", 2200, { align: AlignmentType.CENTER }),
                        cell("±5% ~ ±7%", 2153, { align: AlignmentType.CENTER }),
                        cell("> ±7%", 2153, { align: AlignmentType.CENTER })
                    ]}),
                    new TableRow({ children: [
                        cell("电压THD", 1800),
                        cell("≤ 3%（低压）", 2200, { align: AlignmentType.CENTER }),
                        cell("3% ~ 5%", 2153, { align: AlignmentType.CENTER }),
                        cell("> 5%", 2153, { align: AlignmentType.CENTER })
                    ]}),
                    new TableRow({ children: [
                        cell("三相不平衡度", 1800),
                        cell("≤ 1.3%", 2200, { align: AlignmentType.CENTER }),
                        cell("1.3% ~ 2%", 2153, { align: AlignmentType.CENTER }),
                        cell("> 2%", 2153, { align: AlignmentType.CENTER })
                    ]}),
                    new TableRow({ children: [
                        cell("变压器负载率", 1800),
                        cell("≤ 80%", 2200, { align: AlignmentType.CENTER }),
                        cell("80% ~ 100%", 2153, { align: AlignmentType.CENTER }),
                        cell("> 100%", 2153, { align: AlignmentType.CENTER })
                    ]}),
                    new TableRow({ children: [
                        cell("频率偏差", 1800),
                        cell("≤ ±0.2Hz", 2200, { align: AlignmentType.CENTER }),
                        cell("±0.2 ~ ±0.5Hz", 2153, { align: AlignmentType.CENTER }),
                        cell("> ±0.5Hz", 2153, { align: AlignmentType.CENTER })
                    ]}),
                    new TableRow({ children: [
                        cell("线路电流载流量", 1800),
                        cell("≤ 80%", 2200, { align: AlignmentType.CENTER }),
                        cell("80% ~ 100%", 2153, { align: AlignmentType.CENTER }),
                        cell("> 100%", 2153, { align: AlignmentType.CENTER })
                    ]}),
                ]
            }),

            h2("7.2 治理措施建议"),
            h3("7.2.1 储能配置策略"),
            p("（1）光伏消纳型储能：在光伏渗透率高的台区配置储能，午间吸收多余光伏出力，降低反送功率与电压抬升风险。推荐储能容量为光伏装机容量的10%~20%，充放电时长2~4小时。", { indent: true }),
            p("（2）削峰填谷型储能：在充电负荷集中区域配置储能，傍晚放电缓解变压器过载与线路载流压力，夜间低谷充电降低购电成本。", { indent: true }),
            p("（3）电能质量调节型储能：采用具有四象限运行能力的储能变流器，提供无功补偿与谐波治理功能，改善功率因数与THD指标。", { indent: true }),

            h3("7.2.2 有序充电策略"),
            p("（1）分时电价引导：设置峰谷分时电价，引导用户在光伏高发时段（10:00~14:00）或电网低谷时段（23:00~07:00）充电，降低峰时段负荷压力。", { indent: true }),
            p("（2）智能调度控制：基于台区实时负载率与电压状态，通过充电桩聚合平台动态调整各充电桩输出功率，实现“台区不超载、电压不越限”的协同控制。", { indent: true }),
            p("（3）V2G（车网互动）：在具备条件的场景下，利用电动汽车电池向电网反向放电，在负荷高峰时段提供功率支撑，提升台区弹性。", { indent: true }),

            h3("7.2.3 无功补偿与调压措施"),
            p("（1）分布式无功补偿：在光伏并网点、充电桩集中接入点配置SVG或智能电容器，动态补偿无功功率，维持电压在合格范围内。", { indent: true }),
            p("（2）有载调压变压器：对电压波动剧烈的台区，更换为有载调压配电变压器（OLTC），根据负荷与新能源出力变化自动调节变比。", { indent: true }),
            p("（3）线路改造升级：对长距离、大压降的低压馈线进行导线截面升级或增设配电点，降低线路阻抗与电压损耗。", { indent: true }),

            h3("7.2.4 网络重构与相序优化"),
            p("（1）三相负荷均衡调整：通过调整单相光伏逆变器、单相充电桩的接入相别，使三相负荷分布趋于均衡，降低不平衡度。", { indent: true }),
            p("（2）台区网络重构：在具备多回馈线的台区，通过联络开关切换，优化潮流分布，缓解重载线路与变压器压力。", { indent: true }),
            p("（3）微电网运行模式：在高渗透率场景下，探索台区微电网模式，通过能量管理系统实现源网荷储协调优化运行。", { indent: true }),

            h2("7.3 预警响应流程"),
            p("建立“监测—评估—预警—处置—反馈”闭环响应流程：", { indent: true }),
            p("Step 1 实时监测：终端持续采集波形与指标，标准算法层计算12项基础电能质量指标。", { indent: true }),
            p("Step 2 智能评估：AI模型分析波形特征，量化新能源/充电桩接入影响，计算风险得分。", { indent: true }),
            p("Step 3 分级预警：根据预警指标区间，自动生成绿色（正常）、黄色（预警）、红色（超标）三级告警，推送至运维平台。", { indent: true }),
            p("Step 4 治理处置：依据告警等级与问题类型，自动推荐治理措施（储能调度/有序充电/无功补偿/相序调整），生成运维工单。", { indent: true }),
            p("Step 5 效果反馈：治理措施实施后，持续跟踪指标变化，评估治理效果，迭代优化评估模型与治理策略。", { indent: true }),

            pb(),

            // 八、实施步骤
            h1("八、实施步骤"),
            h2("8.1 第一阶段：需求收敛与数据规范（第1~4周）"),
            p("（1）完成文献调研与现场需求调研，梳理技术难点与研究框架；", { indent: true }),
            p("（2）制定统一数据规范、标签规范与事件定义；", { indent: true }),
            p("（3）选择典型台区与专变用户作为试点场景；", { indent: true }),
            p("（4）明确数据安全、脱敏与合规要求。", { indent: true }),
            p("阶段成果：需求手册、数据规范、标签规范、试点方案。", { indent: true }),

            h2("8.2 第二阶段：数据采集与样本库建设（第5~10周）"),
            p("（1）部署智能融合终端与专变/ECU终端，开通高频波形采集；", { indent: true }),
            p("（2）基于Matlab搭建仿真模型，批量生成五类场景扰动波形数据集；", { indent: true }),
            p("（3）建立事件触发机制，完成数据清洗、波形切片与样本标注；", { indent: true }),
            p("（4）构建原始波形库、事件库、样本库与标签库。", { indent: true }),
            p("阶段成果：原始波形库、仿真数据集、样本库、数据质量报告。", { indent: true }),

            h2("8.3 第三阶段：模型研发与端侧适配（第11~18周）"),
            p("（1）完成特征工程设计，提取标准特征、波形特征、时序特征与业务特征；", { indent: true }),
            p("（2）训练异常检测模型（孤立森林、自编码器）、事件分类模型（1D-CNN）、扰动源识别模型（LightGBM）及风险预测模型（LSTM/GRU）；", { indent: true }),
            p("（3）构建量化评估与容量测算模型，划分三级评估区间；", { indent: true }),
            p("（4）完成模型INT8量化、剪枝压缩与端侧NPU适配。", { indent: true }),
            p("阶段成果：模型文件、模型评估报告、端侧部署包、量化评估体系。", { indent: true }),

            h2("8.4 第四阶段：平台集成与业务应用（第19~24周）"),
            p("（1）建设终端厂家AI训练平台，实现数据接入、模型训练、模型发布与在线推理；", { indent: true }),
            p("（2）开发台区电能质量看板、事件诊断页面、健康画像与自动报告功能；", { indent: true }),
            p("（3）建设大模型专家助手，实现自然语言查询、事件解释与治理建议生成；", { indent: true }),
            p("（4）打通工单系统，实现告警—工单—治理—反馈业务闭环。", { indent: true }),
            p("阶段成果：平台原型、专家助手原型、自动报告模板、业务流程说明。", { indent: true }),

            h2("8.5 第五阶段：现场试点与成果固化（第25~32周）"),
            p("（1）在典型台区与专变用户场景部署终端与平台；", { indent: true }),
            p("（2）开展在线运行验证，人工核查模型输出准确性；", { indent: true }),
            p("（3）根据现场反馈迭代优化模型与算法；", { indent: true }),
            p("（4）评估业务成效，总结推广条件，形成标准化技术方案。", { indent: true }),
            p("阶段成果：试点验证报告、模型优化报告、业务成效报告、标准化接口文档、推广应用建议。", { indent: true }),

            pb(),

            // 九、预期成果
            h1("九、预期成果"),
            h2("9.1 技术成果"),
            p("（1）新能源与充电桩电能质量扰动波形标注数据集1套，包含仿真数据与现场实测数据，覆盖5类核心场景，样本量不少于10万条。", { indent: true }),
            p("（2）轻量化AI评估算法1套，包括扰动辨识模型、影响量化评估模型、台区承载能力测算模型，端侧推理时延小于10ms，模型总大小小于100MB。", { indent: true }),
            p("（3）分级预警与治理决策规则库1套，涵盖6项核心指标的三级阈值区间与4类治理措施匹配策略。", { indent: true }),

            h2("9.2 平台成果"),
            p("（1）电能质量AI训练平台1套，支持256/512点双采样率数据统一接入、模型训练、在线推理与模型版本管理。", { indent: true }),
            p("（2）台区电能质量健康画像系统1套，支持台区/专变用户分级评分、趋势分析、风险预测与自动报告生成。", { indent: true }),
            p("（3）大模型电能质量专家助手原型1套，支持自然语言查询、事件归因解释、治理建议生成与工单辅助。", { indent: true }),

            h2("9.3 工程成果"),
            p("（1）可部署于智能融合终端与专变/ECU终端的电能质量AI应用模块方案1套，具备事件识别、影响评估、容量测算与风险预警功能。", { indent: true }),
            p("（2）典型台区试点验证报告1份，验证模型准确性、业务可用性与工程落地价值。", { indent: true }),
            p("（3）技术方案与标准化接口文档1套，支撑后续规模化推广与多厂家适配。", { indent: true }),

            h2("9.4 应用效益"),
            p("（1）提升配电网电能质量管控水平：实现从“被动统计告警”到“主动风险预警”的转变，降低电能质量超标事件发生率30%以上。", { indent: true }),
            p("（2）支撑新能源与充电桩有序接入：精准测算台区承载能力，为分布式新能源并网审批、充电桩规划布点提供数据支撑，缩短审批周期50%以上。", { indent: true }),
            p("（3）降低运维成本：通过AI自动诊断与分级预警，减少人工波形分析工作量60%以上，提升故障排查效率。", { indent: true }),
            p("（4）促进新能源消纳：通过储能配置、有序充电等治理措施优化，提升台区新能源就地消纳率10%以上。", { indent: true }),

            pb(),

            // 十、附录
            h1("十、附录"),
            h2("附录A 参考标准与规范"),
            p("[1] GB/T 12325-2008 电能质量 供电电压偏差", { indent: true }),
            p("[2] GB/T 14549-1993 公用电网谐波", { indent: true }),
            p("[3] GB/T 15543-2008 电能质量 三相电压不平衡", { indent: true }),
            p("[4] GB/T 15945-2008 电能质量 电力系统频率偏差", { indent: true }),
            p("[5] GB/T 12326-2008 电能质量 电压波动和闪变", { indent: true }),
            p("[6] NB/T 32004-2018 光伏发电并网逆变器技术规范", { indent: true }),
            p("[7] GB/T 34657-2017 电动汽车传导充电互操作性测试规范", { indent: true }),

            h2("附录B 术语与缩略语"),
            p("THD：总谐波畸变率（Total Harmonic Distortion）", { indent: true }),
            p("MPPT：最大功率点跟踪（Maximum Power Point Tracking）", { indent: true }),
            p("V2G：车辆到电网（Vehicle to Grid）", { indent: true }),
            p("SVG：静止无功发生器（Static Var Generator）", { indent: true }),
            p("OLTC：有载调压变压器（On-Load Tap Changer）", { indent: true }),
            p("RoCoF：频率变化率（Rate of Change of Frequency）", { indent: true }),
            p("PTP：精确时间协议（Precision Time Protocol）", { indent: true }),
            p("RAG：检索增强生成（Retrieval-Augmented Generation）", { indent: true }),
        ]
    }]
});

Packer.toBuffer(doc).then(buffer => {
    fs.writeFileSync("d:\\ai\\prj\\cb\\pq_ai\\新能源与充电桩接入影响评估技术方案.docx", buffer);
    console.log("文档已生成：新能源与充电桩接入影响评估技术方案.docx");
});
