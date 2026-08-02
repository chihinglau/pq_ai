const { Document, Packer, Paragraph, TextRun, Table, TableRow, TableCell,
        HeadingLevel, AlignmentType, BorderStyle, WidthType, ShadingType,
        Header, Footer, PageNumber, PageBreak } = require('docx');
const fs = require('fs');

const border = { style: BorderStyle.SINGLE, size: 1, color: "999999" };
const borders = { top: border, bottom: border, left: border, right: border };

function cell(text, width, opts = {}) {
  return new TableCell({
    borders,
    width: { size: width, type: WidthType.DXA },
    shading: opts.shading ? { fill: opts.shading, type: ShadingType.CLEAR } : undefined,
    verticalAlign: opts.center ? "center" : undefined,
    children: [new Paragraph({
      alignment: opts.center ? AlignmentType.CENTER : AlignmentType.LEFT,
      children: [new TextRun({ text, bold: opts.bold, size: opts.size || 21, font: "SimSun" })]
    })]
  });
}

function heading1(text) {
  return new Paragraph({
    heading: HeadingLevel.HEADING_1,
    spacing: { before: 360, after: 180 },
    children: [new TextRun({ text, bold: true, size: 32, font: "SimHei" })]
  });
}

function heading2(text) {
  return new Paragraph({
    heading: HeadingLevel.HEADING_2,
    spacing: { before: 280, after: 140 },
    children: [new TextRun({ text, bold: true, size: 28, font: "SimHei" })]
  });
}

function heading3(text) {
  return new Paragraph({
    spacing: { before: 200, after: 100 },
    children: [new TextRun({ text, bold: true, size: 24, font: "SimHei" })]
  });
}

function body(text, opts = {}) {
  return new Paragraph({
    spacing: { before: 60, after: 60, line: 360 },
    alignment: opts.center ? AlignmentType.CENTER : AlignmentType.JUSTIFIED,
    indent: opts.indent ? { firstLine: 420 } : undefined,
    children: [new TextRun({ text, size: opts.size || 21, font: "SimSun" })]
  });
}

function formula(text) {
  return new Paragraph({
    spacing: { before: 120, after: 120 },
    alignment: AlignmentType.CENTER,
    children: [new TextRun({ text, italic: true, size: 21, font: "Times New Roman" })]
  });
}

function note(text) {
  return new Paragraph({
    spacing: { before: 60, after: 60 },
    indent: { left: 420 },
    children: [new TextRun({ text: "注：" + text, size: 20, font: "SimSun", color: "666666" })]
  });
}

const doc = new Document({
  styles: {
    default: { document: { run: { font: "SimSun", size: 21 } } },
    paragraphStyles: [
      { id: "Heading1", name: "Heading 1", basedOn: "Normal", next: "Normal", quickFormat: true,
        run: { size: 32, bold: true, font: "SimHei" },
        paragraph: { spacing: { before: 360, after: 180 }, outlineLevel: 0 } },
      { id: "Heading2", name: "Heading 2", basedOn: "Normal", next: "Normal", quickFormat: true,
        run: { size: 28, bold: true, font: "SimHei" },
        paragraph: { spacing: { before: 280, after: 140 }, outlineLevel: 1 } },
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
          children: [new TextRun({ text: "新能源与充电桩接入影响评估——MATLAB仿真技术方案", size: 18, font: "SimSun", color: "888888" })]
        })]
      })
    },
    footers: {
      default: new Footer({
        children: [new Paragraph({
          alignment: AlignmentType.CENTER,
          children: [
            new TextRun({ text: "第 ", size: 18, font: "SimSun" }),
            new TextRun({ children: [PageNumber.CURRENT], size: 18 }),
            new TextRun({ text: " 页", size: 18, font: "SimSun" })
          ]
        })]
      })
    },
    children: [
      // ===== 封面 =====
      new Paragraph({ spacing: { before: 1200 } }),
      new Paragraph({
        alignment: AlignmentType.CENTER,
        spacing: { after: 600 },
        children: [new TextRun({ text: "新能源与充电桩接入影响评估", bold: true, size: 44, font: "SimHei" })]
      }),
      new Paragraph({
        alignment: AlignmentType.CENTER,
        spacing: { after: 200 },
        children: [new TextRun({ text: "MATLAB仿真技术方案", bold: true, size: 44, font: "SimHei" })]
      }),
      new Paragraph({ spacing: { before: 800 } }),
      new Paragraph({
        alignment: AlignmentType.CENTER,
        children: [new TextRun({ text: "——基于终端交流采样波形数据的电能质量AI应用", size: 28, font: "SimSun" })]
      }),
      new Paragraph({ spacing: { before: 1600 } }),
      new Paragraph({
        alignment: AlignmentType.CENTER,
        children: [new TextRun({ text: "版本：V1.1", size: 24, font: "SimSun" })]
      }),
      new Paragraph({
        alignment: AlignmentType.CENTER,
        spacing: { before: 200 },
        children: [new TextRun({ text: "日期：2026年8月", size: 24, font: "SimSun" })]
      }),

      new Paragraph({ children: [new PageBreak()] }),

      // ===== 第一章 概述 =====
      heading1("第一章  项目概述与仿真目标"),

      heading2("1.1  项目背景"),
      body("随着“双碳”战略推进，分布式新能源（光伏、风电）与电动汽车充电桩在配电网中的渗透率快速提升。高比例电力电子设备接入对配电网的电压质量、谐波特性、三相平衡性及设备承载能力提出了严峻挑战。传统的配电网监测手段难以在故障前精准刻画这些动态影响，亟需建立一套可量化、可复现、可扩展的仿真评估体系。", { indent: true }),
      body("本项目基于MATLAB平台，构建了一套完整的配电台区电能质量仿真系统。该系统采用纯MATLAB时域数值仿真方法，不依赖Simulink图形建模，通过解析电路方程直接生成高精度电压/电流波形，并在此基础上计算国家标准规定的全部电能质量指标，最终形成面向AI应用的训练数据集。", { indent: true }),

      heading2("1.2  仿真目标"),
      body("本仿真系统的核心目标包括以下五个方面：", { indent: true }),
      body("（1）建立典型配电台区的等值电气模型，涵盖变压器、馈线线路、常规负荷、光伏逆变器及充电桩等关键元件；"),
      body("（2）模拟五种典型运行场景（基准负荷、充电桩接入、光伏接入、光充耦合、极端高渗透），量化各类接入方式对电能质量的独立及耦合影响；"),
      body("（3）依据GB/T系列国家标准，系统计算电压偏差、谐波总畸变率（THD）、三相不平衡度、频率偏差、变压器负载率等核心指标；"),
      body("（4）采用蒙特卡洛方法评估源荷不确定性下的电能质量越限概率，为风险管控提供量化依据；"),
      body("（5）批量生成带标签的扰动波形数据集，为后续AI诊断模型的训练与验证提供数据基础。"),

      heading2("1.3  仿真流程总览"),
      body("整个仿真流程按照以下六个阶段顺序执行，各阶段之间通过结构化数据传递衔接："),

      new Table({
        width: { size: 8306, type: WidthType.DXA },
        columnWidths: [1500, 2200, 2200, 2406],
        rows: [
          new TableRow({ children: [
            cell("阶段", 1500, { bold: true, center: true, shading: "D5E8F0" }),
            cell("任务", 2200, { bold: true, center: true, shading: "D5E8F0" }),
            cell("核心方法", 2200, { bold: true, center: true, shading: "D5E8F0" }),
            cell("输出", 2406, { bold: true, center: true, shading: "D5E8F0" }),
          ]}),
          new TableRow({ children: [
            cell("1", 1500, { center: true }),
            cell("系统建模", 2200),
            cell("等值电路参数化", 2200),
            cell("系统参数结构体", 2406),
          ]}),
          new TableRow({ children: [
            cell("2", 1500, { center: true }),
            cell("场景配置", 2200),
            cell("功率分配与谐波注入", 2200),
            cell("场景功率参数", 2406),
          ]}),
          new TableRow({ children: [
            cell("3", 1500, { center: true }),
            cell("潮流计算", 2200),
            cell("单节点等值前推回代", 2200),
            cell("PCC点电压、线路电流", 2406),
          ]}),
          new TableRow({ children: [
            cell("4", 1500, { center: true }),
            cell("时域波形生成", 2200),
            cell("解析波形叠加法", 2200),
            cell("三相电压/电流时域序列", 2406),
          ]}),
          new TableRow({ children: [
            cell("5", 1500, { center: true }),
            cell("指标计算", 2200),
            cell("FFT频谱分析、对称分量法", 2200),
            cell("电能质量指标集合", 2406),
          ]}),
          new TableRow({ children: [
            cell("6", 1500, { center: true }),
            cell("风险评估/数据集", 2200),
            cell("蒙特卡洛采样、随机参数生成", 2200),
            cell("概率分布、AI训练数据", 2406),
          ]}),
        ]
      }),

      new Paragraph({ children: [new PageBreak()] }),

      // ===== 第二章 系统建模 =====
      heading1("第二章  系统建模与参数化"),

      heading2("2.1  建模思想与等值假设"),
      body("本仿真面向配电台区（0.4kV低压侧）的电能质量评估，采用单节点等值建模策略。其核心思想是：将台区低压母线视为公共连接点（PCC, Point of Common Coupling），所有负荷、光伏、充电桩等效为直接挂接在PCC点的功率注入/消耗元件，线路阻抗集中等效为PCC与变压器低压侧之间的单一阻抗。", { indent: true }),
      body("该等值方法基于以下工程假设：", { indent: true }),
      body("（1）低压配电网呈辐射状拓扑，各支路潮流方向明确，不存在环流；"),
      body("（2）线路参数（电阻、电抗）远小于负荷阻抗，可采用集中参数模型；"),
      body("（3）新能源与负荷的功率波动相对于电磁暂态时间尺度缓慢，可采用准稳态近似；"),
      body("（4）三相系统对称运行，仅在充电桩单相接入或光伏三相不对称出力的场景下引入不平衡度。"),

      heading2("2.2  等值电路元件模型"),

      heading3("2.2.1  配电网电源模型"),
      body("将上级10kV/0.4kV配电变压器高压侧视为无穷大电源，低压侧额定相电压为："),
      formula("V_{ph} = V_{LL} / √3 = 400V / √3 ≈ 230.94V"),
      body("其中 V_{LL} 为线电压额定值。该电源模型假设上级电网的电压幅值与频率恒定，不随下级台区负荷变化而波动，从而将研究焦点集中于台区内部电能质量问题。"),

      heading3("2.2.2  变压器模型"),
      body("变压器采用额定容量等值模型，核心参数包括："),
      body("——额定容量 S_rated（kVA）：表征变压器的最大持续承载能力；"),
      body("——阻抗电压 u_k（%）：反映变压器内部漏抗，用于短路电流计算；"),
      body("——接线组别（Dyn11）：影响零序通路及谐波传递特性。"),
      body("变压器负载率定义为实际视在功率与额定容量之比："),
      formula("η_{trafo} = S_{actual} / S_{rated} × 100%"),
      body("当 η_{trafo} > 100% 时，判定变压器过载。"),

      heading3("2.2.3  线路模型"),
      body("馈线线路采用串联RL集中参数模型，等值阻抗由线路长度与单位阻抗决定："),
      formula("R_{line} = r_0 × L_{line} / 1000"),
      formula("X_{line} = x_0 × L_{line} / 1000"),
      body("其中 r_0、x_0 为单位长度电阻与电抗（Ω/km），L_{line} 为线路长度（m）。线路载流量 I_max 作为硬约束，线路负载率为："),
      formula("η_{line} = I_{actual} / I_{max} × 100%"),

      heading3("2.2.4  负荷模型"),
      body("常规负荷等效为恒定功率负荷（Constant Power Load, CPL），以额定有功功率 P_rated 和无功功率 Q_rated 表征："),
      formula("S_{load} = √(P_{load}² + Q_{load}²)"),
      formula("cos φ_{load} = P_{load} / S_{load}"),
      body("负荷模型假设功率因数恒定，负荷电流随端电压变化而自动调整以维持功率恒定。"),

      heading3("2.2.5  光伏逆变器模型"),
      body("光伏系统通过DC/AC逆变器并网，其交流侧等效为负功率注入（向电网输送有功）。逆变器采用MPPT控制，输出谐波特性由PWM开关策略决定。仿真中将其建模为："),
      body("——有功注入 P_pv（kW）：与光照强度及逆变器容量相关；"),
      body("——谐波电流源：各次谐波含量以基波电流的百分比表示，典型谐波阶次为2、4、6、8次（由单相逆变器或三相逆变器非线性调制产生）。"),

      heading3("2.2.6  充电桩模型"),
      body("充电桩根据类型分为交流充电桩（AC, ~7kW/相）与直流充电桩（DC, 60kW）。在0.4kV低压侧评估中，充电桩等效为有功负荷，其非线性整流前端产生特征谐波。典型谐波阶次为5、7、11、13次，谐波含量以基波电流百分比表示。"),

      new Paragraph({ children: [new PageBreak()] }),

      // ===== 第三章 场景配置 =====
      heading1("第三章  仿真场景配置原理"),

      heading2("3.1  场景设计思想"),
      body("场景配置的核心任务是：在统一的系统参数框架下，通过调整各类功率元件的接入状态与功率水平，构造具有明确物理意义和工程代表性的运行工况。每个场景对应一组确定的功率流边界条件，为后续的潮流计算和波形生成提供输入。", { indent: true }),

      heading2("3.2  五类核心场景定义"),

      new Table({
        width: { size: 8306, type: WidthType.DXA },
        columnWidths: [900, 2500, 2400, 2506],
        rows: [
          new TableRow({ children: [
            cell("场景", 900, { bold: true, center: true, shading: "D5E8F0" }),
            cell("功率配置", 2500, { bold: true, center: true, shading: "D5E8F0" }),
            cell("谐波注入", 2400, { bold: true, center: true, shading: "D5E8F0" }),
            cell("评估重点", 2506, { bold: true, center: true, shading: "D5E8F0" }),
          ]}),
          new TableRow({ children: [
            cell("S1", 900, { center: true }),
            cell("P_load=150kW, Q_load=81.8kvar, P_pv=0, P_ev=0", 2500),
            cell("无", 2400),
            cell("建立电能质量基准线", 2506),
          ]}),
          new TableRow({ children: [
            cell("S2", 900, { center: true }),
            cell("P_load=150kW, P_ev=35kW (5台×7kW), P_pv=0", 2500),
            cell("充电桩5/7/11/13次谐波", 2400),
            cell("充电桩谐波与负载冲击", 2506),
          ]}),
          new TableRow({ children: [
            cell("S3", 900, { center: true }),
            cell("P_load=120kW, P_pv=80kW, P_ev=0", 2500),
            cell("光伏2/4/6/8次谐波", 2400),
            cell("光伏电压抬升与反向潮流", 2506),
          ]}),
          new TableRow({ children: [
            cell("S4", 900, { center: true }),
            cell("P_load=135kW, P_pv=60kW, P_ev=21kW", 2500),
            cell("光伏+充电桩谐波叠加", 2400),
            cell("光充耦合综合效应", 2506),
          ]}),
          new TableRow({ children: [
            cell("S5", 900, { center: true }),
            cell("P_load=180kW, P_pv=100kW, P_ev=70kW", 2500),
            cell("双谐波源增强×1.2", 2400),
            cell("高渗透率承载边界", 2506),
          ]}),
        ]
      }),

      heading2("3.3  场景功率配置原理"),
      body("场景配置遵循以下工程逻辑："),
      body("（1）常规负荷 P_load 在白天场景（S3/S4/S5）中适度降低（0.8~0.9倍额定值），模拟工商业负荷的日特性；夜间或高负荷场景（S1/S2/S5）中保持额定或超载；"),
      body("（2）光伏出力 P_pv 与光照强度正相关，S3为中等光照（0.8倍额定），S4为一般光照（0.6倍），S5为满发（1.0倍）；"),
      body("（3）充电桩接入数量反映台区充电需求密度，S2为5台（约20%住户），S4为3台（光充共存），S5为10台（极端拥堵）；"),
      body("（4）谐波注入强度与设备功率成正比，S5中谐波含量额外增强20%，模拟设备老化或控制参数漂移导致的谐波放大。"),

      new Paragraph({ children: [new PageBreak()] }),

      // ===== 第四章 潮流计算 =====
      heading1("第四章  潮流计算方法"),

      heading2("4.1  问题描述"),
      body("在给定场景功率配置（P_load, Q_load, P_pv, P_ev）和系统参数（线路阻抗、变压器容量、额定电压）的条件下，求解PCC点的稳态电压 V_pcc、线路电流 I_line、变压器视在功率 S_trafo 及功率因数 pf。", { indent: true }),

      heading2("4.2  单节点等值前推回代法"),
      body("由于采用单节点等值模型，潮流计算简化为单节点功率平衡问题，无需迭代求解。计算步骤如下："),

      heading3("4.2.1  净功率计算"),
      body("定义流向PCC点的功率为正方向。净有功功率为负荷与充电消耗减去光伏发电："),
      formula("P_{net} = P_{load} + P_{ev} − P_{pv}"),
      body("净无功功率仅由负荷产生（光伏逆变器通常单位功率因数运行，充电桩近似纯阻性负荷）："),
      formula("Q_{net} = Q_{load}"),
      body("净视在功率为："),
      formula("S_{net} = √(P_{net}² + Q_{net}²)"),

      heading3("4.2.2  线路电流计算"),
      body("由三相功率方程，线路电流有效值为："),
      formula("I_{line} = S_{net} / (√3 × V_{LL})"),
      body("其中 V_{LL} = 400V 为额定线电压。"),

      heading3("4.2.3  电压降计算"),
      body("线路上的相电压降为电流在阻抗上的压降。设功率因数角 φ = arctan(Q_{net}/P_{net})，则相电压降为："),
      formula("ΔV_{ph} = I_{line} × (R_{line} × cos φ + X_{line} × sin φ)"),
      body("该公式为配电网常用的简化电压降公式，适用于功率因数角较小（cos φ > 0.8）的低压配电线路。PCC点相电压为："),
      formula("V_{pcc} = V_{ph} − ΔV_{ph}"),
      body("其中 V_{ph} = V_{LL}/√3 为变压器低压侧额定相电压。"),

      heading3("4.2.4  约束与饱和处理"),
      body("为确保仿真结果在物理合理范围内，对PCC点电压施加硬约束："),
      formula("V_{pcc} = clip(V_{pcc}, 0.85 × V_{ph}, 1.15 × V_{ph})"),
      body("当计算电压超出 ±15% 范围时，强制截断至边界值，避免极端参数配置下产生非物理结果。"),

      heading2("4.3  功率因数与负载率"),
      body("变压器负载率为净视在功率与额定容量之比："),
      formula("η_{trafo} = S_{net} / S_{rated} × 100%"),
      body("功率因数为净有功功率与净视在功率之比："),
      formula("pf = P_{net} / S_{net}"),
      body("当 S_{net} = 0（即光伏满发完全抵消负荷）时，功率因数定义为1.0。"),

      note("对于含光伏的场景，若 P_pv > P_load + P_ev，则净功率为负值（向电网反送电），此时功率因数仍按上述公式计算，但潮流方向反转，PCC点电压抬升。"),

      new Paragraph({ children: [new PageBreak()] }),

      // ===== 第五章 时域波形生成 =====
      heading1("第五章  时域波形生成算法"),

      heading2("5.1  算法概述"),
      body("时域波形生成是本仿真系统的核心环节。与Simulink模型驱动的数值积分不同，本系统采用解析波形叠加法：基于潮流计算得到的稳态电气量，通过解析公式直接构造各相电压、电流的时域采样序列。该方法计算效率高（无需求解微分方程），且便于精确控制谐波成分与不平衡度。", { indent: true }),

      heading2("5.2  采样参数设置"),
      body("仿真采用固定步长采样，参数设置如下："),
      body("——采样频率 f_s = 12.8 kHz（对应采样间隔 T_s = 78.125 μs）；"),
      body("——仿真时长 T_{stop} = 0.5 s，总采样点数 N = f_s × T_{stop} = 6400；"),
      body("——基波频率 f_0 = 50 Hz，每基波周期采样点数 n_{cycle} = f_s / f_0 = 256。"),
      body("高采样率（256点/周期）确保了谐波分析（最高50次谐波）的频谱分辨率和时域波形保真度。"),

      heading2("5.3  基波波形生成"),

      heading3("5.3.1  基波电压"),
      body("三相基波电压以PCC点相电压有效值 V_pcc 为幅值基准，构造标准对称三相正弦波："),
      formula("v_a(t) = V_{pcc} × √2 × sin(ω_0 t)"),
      formula("v_b(t) = V_{pcc} × √2 × sin(ω_0 t − 2π/3)"),
      formula("v_c(t) = V_{pcc} × √2 × sin(ω_0 t + 2π/3)"),
      body("其中 ω_0 = 2πf_0 = 100π rad/s 为基波角频率。"),

      heading3("5.3.2  基波电流"),
      body("基波电流以线路电流有效值 I_line 为幅值，相位滞后电压 φ_i = arccos(pf)："),
      formula("i_a(t) = I_{line} × √2 × sin(ω_0 t − φ_i)"),
      formula("i_b(t) = I_{line} × √2 × sin(ω_0 t − 2π/3 − φ_i)"),
      formula("i_c(t) = I_{line} × √2 × sin(ω_0 t + 2π/3 − φ_i)"),

      heading2("5.4  谐波叠加模型"),

      heading3("5.4.1  光伏逆变器谐波"),
      body("光伏逆变器的PWM调制过程产生特征谐波电流。在仿真中，将各次谐波电流在线路阻抗上产生的谐波电压降叠加至PCC点电压，同时将谐波电流直接叠加至电流波形。谐波电压幅值为："),
      formula("V_h = I_h × Z_h × √2"),
      body("其中 I_h = I_{line} × h_{pv}(h) 为h次谐波电流有效值，h_{pv}(h) 为光伏谐波含量百分比；Z_h = √(R_{line}² + (hX_{line})²) 为h次谐波阻抗。"),
      body("谐波电压/电流以随机相位叠加，模拟多逆变器非同步运行时的相位随机性："),
      formula("v_{a,h}(t) = V_h × sin(h ω_0 t + θ_h),  θ_h ~ U(0, 2π)"),
      body("光伏谐波阶次取 h ∈ {2, 4, 6, 8}，对应单相或三相逆变器的低次非特征谐波。"),

      heading3("5.4.2  充电桩谐波"),
      body("充电桩整流器产生的特征谐波阶次为 h ∈ {5, 7, 11, 13}。谐波叠加方式与光伏相同，但谐波阻抗和含量参数独立配置。"),

      heading2("5.5  三相不平衡模型"),
      body("三相不平衡主要源于充电桩的单相接入及光伏三相不对称出力。仿真中采用简化的幅值不平衡模型："),
      formula("v_a'(t) = v_a(t) × (1 + ε)"),
      formula("v_b'(t) = v_b(t) × (1 − ε/2)"),
      formula("v_c'(t) = v_c(t) × (1 − ε/2)"),
      body("其中不平衡系数 ε 与充电桩接入功率成正比："),
      formula("ε = 0.01 × P_{ev} / S_{rated}"),
      body("即充电桩功率每占变压器额定容量的1%，引入约1%的幅值不平衡。该模型为工程简化，实际不平衡度还受线路三相阻抗不对称、负荷三相分布不均等因素影响。"),

      heading2("5.6  噪声注入"),
      body("为模拟实际测量中的量化噪声和电磁干扰，在电压、电流波形上叠加高斯白噪声："),
      formula("v_{noise}(t) = 0.005 × V_{nom} × N(0, 1)"),
      formula("i_{noise}(t) = 0.005 × I_{line} × N(0, 1)"),
      body("噪声标准差为信号额定幅值的0.5%，信噪比约为46 dB，与高精度电能质量监测终端的噪声水平相当。"),

      heading2("5.7  功率与频率信号"),
      body("瞬时三相有功功率通过电压电流瞬时值计算："),
      formula("p(t) = v_a(t)i_a(t) + v_b(t)i_b(t) + v_c(t)i_c(t)"),
      body("为消除瞬时功率的脉动分量，采用移动平均滤波提取平均有功功率，窗口长度为一个基波周期（256个采样点）："),
      formula("P(t) = movmean(p(t), n_{cycle})"),
      body("无功功率由视在功率与有功功率的差值计算："),
      formula("Q(t) = √(S_{trafo}² − P(t)²)"),
      body("频率信号模拟了电网频率的微小波动，其偏差量与源荷功率不平衡相关："),
      formula("f(t) = f_0 + Δf × sin(2π × 0.1 × t) + 0.001 × N(0, 1)"),
      formula("Δf = 0.01 × (P_{pv} − P_{ev}) / S_{rated} × f_0"),
      body("其中0.1 Hz的缓慢波动模拟了电网一次调频响应过程，高斯噪声模拟频率测量的随机误差。"),

      new Paragraph({ children: [new PageBreak()] }),

      // ===== 第六章 指标计算 =====
      heading1("第六章  电能质量指标计算方法"),

      heading2("6.1  电压偏差"),
      body("电压偏差定义为实测电压有效值与额定电压有效值之差，以额定电压的百分比表示（依据GB/T 12325-2008）："),
      formula("δV = (V_{rms} − V_{nom}) / V_{nom} × 100%"),
      body("其中 V_{rms} 通过各相电压的均方根值计算。对于三相系统，取三相电压偏差的平均值或最大值（本系统报告最大值）。"),
      body("合格判据：|δV| ≤ 7%（低压配电系统，GB/T 12325）。"),

      heading2("6.2  谐波总畸变率（THD）"),

      heading3("6.2.1  算法原理"),
      body("总谐波畸变率（Total Harmonic Distortion, THD）定义为各次谐波分量有效值的平方和开方与基波分量有效值之比（依据GB/T 14549-1993）："),
      formula("THD_V = √(∑_{h=2}^{H} V_h²) / V_1 × 100%"),
      formula("THD_I = √(∑_{h=2}^{H} I_h²) / I_1 × 100%"),
      body("其中 H 为最高谐波次数（本系统取50次，即分析至2.5 kHz），V_h、I_h 为第h次谐波电压/电流有效值，V_1、I_1 为基波有效值。"),

      heading3("6.2.2  FFT频谱分析实现"),
      body("谐波分量通过离散傅里叶变换（DFT）提取。对长度为N的时域采样序列 x[n]，其DFT为："),
      formula("X[k] = ∑_{n=0}^{N−1} x[n] × e^{−j2πkn/N},  k = 0, 1, ..., N−1"),
      body("利用MATLAB的FFT算法（Cooley-Tukey快速傅里叶变换，计算复杂度O(N log N)）计算频谱。频谱频率分辨率为："),
      formula("Δf = f_s / N = 12800 / 6400 = 2 Hz"),
      body("为提取单边幅度谱，对FFT结果进行对称折叠处理："),
      formula("X_{single}[k] = 2 × X[k],  k = 1, ..., N/2"),
      formula("X_{single}[0] = X[0]"),
      body("各次谐波分量通过搜索基波频率整数倍处的频谱峰值获得。为避免频谱泄漏对THD计算的影响，采样时长设计为整数个基波周期（T_{stop} = 0.5 s = 25个周期）。"),

      heading2("6.3  三相电压不平衡度"),

      heading3("6.3.1  对称分量法原理"),
      body("三相不平衡度依据GB/T 15543-2008，采用对称分量法（Symmetrical Components Method）计算。该方法由Fortescue于1918年提出，将任意不对称三相量分解为三组对称分量："),
      formula("正序分量：V_{a1} = (V_a + α V_b + α² V_c) / 3"),
      formula("负序分量：V_{a2} = (V_a + α² V_b + α V_c) / 3"),
      formula("零序分量：V_{a0} = (V_a + V_b + V_c) / 3"),
      body("其中 α = e^{j2π/3} = −1/2 + j√3/2 为120°旋转算子。"),

      heading3("6.3.2  不平衡度计算"),
      body("电压不平衡度定义为负序电压分量与正序电压分量之比："),
      formula("ε_u = |V_{a2}| / |V_{a1}| × 100%"),
      body("合格判据：ε_u ≤ 2%（低压配电系统，GB/T 15543）。"),

      heading3("6.3.3  基波相量提取——相干DFT"),
      body("对称分量法要求输入量为基波相量（复数形式，含幅值与相位）。若直接对原始时域采样序列应用对称分量法，谐波分量将严重干扰计算结果。因此，本系统采用相干DFT（Coherent DFT）精确提取基波相量。"),
      body("相干DFT的核心思想是：利用信号长度恰好为整数个基波周期的特性，消除频谱泄漏。取 n_{cycles} = 500 个完整周波，对应采样点数："),
      formula("N_{coh} = n_{cycles} × (f_s / f_0) = 500 × 256 = 128000"),
      body("基波同相分量（I）和正交分量（Q）通过相关运算提取："),
      formula("I = (2/N_{coh}) × ∑_{n=0}^{N_{coh}−1} x[n] × cos(2π f_0 n / f_s)"),
      formula("Q = (2/N_{coh}) × ∑_{n=0}^{N_{coh}−1} x[n] × sin(2π f_0 n / f_s)"),
      body("基波幅值和相位为："),
      formula("V_1 = √(I² + Q²)"),
      formula("φ_1 = atan2(−Q, I)"),
      body("基波复相量为 V̇_1 = V_1 × e^{jφ_1}。对三相电压分别提取基波相量后，代入对称分量法公式，即可得到精确的不平衡度。"),

      note("相干DFT之所以有效，是因为当N_{coh}恰好为整数个周期时，基波频率精确对应DFT的某一个频谱线，能量完全集中在该频点上，相邻频点的泄漏为零。这是THD计算中要求采样时长为整数周期，以及不平衡度计算中要求相干采样的共同原理。"),

      heading2("6.4  频率偏差"),
      body("频率偏差定义为实测频率与额定频率之差（依据GB/T 15945-2008）："),
      formula("Δf = f_{meas} − f_{nom}"),
      body("本系统中 f_{meas} 取仿真生成的频率信号在稳态时段（0.1~0.5 s，避开启动暂态）的均值。"),
      body("合格判据：|Δf| ≤ 0.5 Hz（电网正常运行条件下，GB/T 15945）。"),

      heading2("6.5  变压器与线路负载率"),
      body("变压器负载率已在第四章定义。线路负载率为实际电流与线路最大载流量之比："),
      formula("η_{line} = I_{line} / I_{max} × 100%"),
      body("两者合格判据均为 ≤ 100%，超过则判定为过载。"),

      heading2("6.6  功率因数"),
      body("功率因数为总有功功率与总视在功率之比："),
      formula("pf = P_{net} / S_{net}"),
      body("功率因数无明确越限值，但低于0.9时通常需要无功补偿。"),

      new Paragraph({ children: [new PageBreak()] }),

      // ===== 第七章 蒙特卡洛 =====
      heading1("第七章  蒙特卡洛风险评估方法"),

      heading2("7.1  方法动机"),
      body("确定性仿真（固定场景）只能评估特定工况下的电能质量，无法回答“在源荷随机波动条件下，指标越限的概率有多大”这一关键工程问题。蒙特卡洛方法通过大量随机采样，将不确定性因素引入仿真，从而统计越限概率分布。", { indent: true }),

      heading2("7.2  不确定性建模"),
      body("本系统对以下四类参数引入随机扰动："),

      new Table({
        width: { size: 8306, type: WidthType.DXA },
        columnWidths: [2200, 2200, 1906, 2000],
        rows: [
          new TableRow({ children: [
            cell("参数", 2200, { bold: true, center: true, shading: "D5E8F0" }),
            cell("分布类型", 2200, { bold: true, center: true, shading: "D5E8F0" }),
            cell("均值", 1906, { bold: true, center: true, shading: "D5E8F0" }),
            cell("变异系数", 2000, { bold: true, center: true, shading: "D5E8F0" }),
          ]}),
          new TableRow({ children: [
            cell("光伏出力 P_pv", 2200),
            cell("正态分布", 2200, { center: true }),
            cell("0.5~P_rated", 1906, { center: true }),
            cell("15%", 2000, { center: true }),
          ]}),
          new TableRow({ children: [
            cell("充电功率 P_ev", 2200),
            cell("正态分布", 2200, { center: true }),
            cell("0~70kW", 1906, { center: true }),
            cell("20%", 2000, { center: true }),
          ]}),
          new TableRow({ children: [
            cell("负荷功率 P_load", 2200),
            cell("正态分布", 2200, { center: true }),
            cell("0.7~1.3×额定", 1906, { center: true }),
            cell("10%", 2000, { center: true }),
          ]}),
          new TableRow({ children: [
            cell("谐波含量 h_dist", 2200),
            cell("正态分布", 2200, { center: true }),
            cell("基准值", 1906, { center: true }),
            cell("20%", 2000, { center: true }),
          ]}),
        ]
      }),

      heading2("7.3  采样与仿真流程"),
      body("蒙特卡洛分析的核心算法流程如下："),
      body("Step 1：设定采样次数 N_{MC}（默认1000次）；"),
      body("Step 2：对每次采样 i = 1, 2, ..., N_{MC}："),
      body("  （a）从上述分布中随机抽取一组参数 (P_pv, P_ev, P_load, h_dist)；"),
      body("  （b）运行一次完整的仿真流程（潮流计算 → 波形生成 → 指标计算）；"),
      body("  （c）记录全部电能质量指标；"),
      body("Step 3：统计分析："),
      body("  （a）计算各指标的均值 μ、标准差 σ、95%置信区间；"),
      body("  （b）计算各指标越限概率 P_{violation} = N_{violation} / N_{MC}；"),
      body("  （c）绘制概率密度直方图与累积分布函数。"),

      heading2("7.4  越限概率计算"),
      body("对于第 j 个电能质量指标 I_j（如电压THD），其越限概率为："),
      formula("P_{vio,j} = (1/N_{MC}) × ∑_{i=1}^{N_{MC}} 𝟙(I_{j,i} > I_{j,limit})"),
      body("其中 𝟙(·) 为指示函数，当条件成立时取1，否则取0。I_{j,limit} 为GB/T标准规定的限值。"),

      heading2("7.5  收敛性分析"),
      body("蒙特卡洛估计的标准误差与采样次数的平方根成反比："),
      formula("SE(P̂_{vio}) = √(P̂_{vio}(1−P̂_{vio}) / N_{MC})"),
      body("当 N_{MC} = 1000 时，对于中等越限概率（如 P ≈ 0.1），标准误差约为1%，可满足工程评估的精度要求。"),

      new Paragraph({ children: [new PageBreak()] }),

      // ===== 第八章 数据集生成 =====
      heading1("第八章  AI训练数据集生成方法"),

      heading2("8.1  设计目标"),
      body("AI训练数据集需满足以下要求：", { indent: true }),
      body("（1）样本量充足：支持深度学习模型的训练与验证；"),
      body("（2）场景覆盖全面：包含全部五类运行场景及多种中间状态；"),
      body("（3）标签完整：每条样本携带场景类别标签、指标数值标签及越限标签；"),
      body("（4）特征维度丰富：从原始波形中提取统计特征与频域特征。"),

      heading2("8.2  样本生成策略"),
      body("数据集生成采用参数随机化策略。对于每条样本，独立随机生成以下参数："),
      body("——负荷功率比 k_load ~ U(0.5, 1.3)：表征负荷水平从低谷到超载的连续变化；"),
      body("——光伏出力比 k_pv ~ U(0, 1.0)：表征从夜间零出力到日间满发的全范围；"),
      body("——充电桩数量 n_ev ~ Discrete-U(0, 10)：表征0~10台充电桩的离散接入状态；"),
      body("——光伏谐波扰动因子 f_hpv ~ N(1.0, 0.2²)：模拟逆变器控制参数漂移；"),
      body("——充电桩谐波扰动因子 f_hev ~ N(1.0, 0.2²)：模拟整流器老化。"),
      body("实际功率由比例系数与额定值相乘得到："),
      formula("P_{load} = k_{load} × P_{load,rated}"),
      formula("P_{pv} = k_{pv} × P_{pv,rated}"),
      formula("P_{ev} = n_{ev} × P_{ev,unit}"),

      heading2("8.3  场景标签编码"),
      body("每条样本根据其功率配置自动分配场景标签。标签判定逻辑如下："),
      body("——若 P_pv < 5kW 且 P_ev < 5kW：标签 = 0（纯负荷/基准）；"),
      body("——若 P_pv < 5kW 且 P_ev ≥ 5kW：标签 = 1（充电桩主导）；"),
      body("——若 P_pv ≥ 5kW 且 P_ev < 5kW：标签 = 2（光伏主导）；"),
      body("——若 P_pv ≥ 5kW 且 P_ev ≥ 5kW：标签 = 3（光充耦合）；"),
      body("——若 P_pv > 80kW 或 P_ev > 50kW：标签 = 4（极端场景）。"),
      body("该编码策略确保了数据集覆盖从单一元素到复杂耦合的全部运行状态。"),

      heading2("8.4  特征工程"),
      body("从每段三相波形中提取20维特征向量，构成AI模型的输入："),

      new Table({
        width: { size: 8306, type: WidthType.DXA },
        columnWidths: [600, 2500, 2600, 2606],
        rows: [
          new TableRow({ children: [
            cell("#", 600, { bold: true, center: true, shading: "D5E8F0" }),
            cell("特征名称", 2500, { bold: true, center: true, shading: "D5E8F0" }),
            cell("计算方法", 2600, { bold: true, center: true, shading: "D5E8F0" }),
            cell("物理意义", 2606, { bold: true, center: true, shading: "D5E8F0" }),
          ]}),
          new TableRow({ children: [cell("1", 600, { center: true }), cell("电压均值", 2500), cell("mean(Va, Vb, Vc)", 2600), cell("电压直流偏移", 2606)] }),
          new TableRow({ children: [cell("2", 600, { center: true }), cell("电压标准差", 2500), cell("std(Va, Vb, Vc)", 2600), cell("电压波动强度", 2606)] }),
          new TableRow({ children: [cell("3", 600, { center: true }), cell("电压峰峰值", 2500), cell("max−min(V)", 2600), cell("电压极值范围", 2606)] }),
          new TableRow({ children: [cell("4", 600, { center: true }), cell("电流均值", 2500), cell("mean(Ia, Ib, Ic)", 2600), cell("电流直流偏移", 2606)] }),
          new TableRow({ children: [cell("5", 600, { center: true }), cell("电流标准差", 2500), cell("std(Ia, Ib, Ic)", 2600), cell("电流波动强度", 2606)] }),
          new TableRow({ children: [cell("6", 600, { center: true }), cell("有功功率均值", 2500), cell("mean(P)", 2600), cell("平均负荷水平", 2606)] }),
          new TableRow({ children: [cell("7", 600, { center: true }), cell("无功功率均值", 2500), cell("mean(Q)", 2600), cell("无功需求水平", 2606)] }),
          new TableRow({ children: [cell("8", 600, { center: true }), cell("功率因数", 2500), cell("P / S", 2600), cell("系统功率因数", 2606)] }),
          new TableRow({ children: [cell("9", 600, { center: true }), cell("电压THD", 2500), cell("FFT谐波分析", 2600), cell("电压畸变程度", 2606)] }),
          new TableRow({ children: [cell("10", 600, { center: true }), cell("电流THD", 2500), cell("FFT谐波分析", 2600), cell("电流畸变程度", 2606)] }),
          new TableRow({ children: [cell("11", 600, { center: true }), cell("3次谐波含量", 2500), cell("V_3 / V_1", 2600), cell("三倍频谐波", 2606)] }),
          new TableRow({ children: [cell("12", 600, { center: true }), cell("5次谐波含量", 2500), cell("V_5 / V_1", 2600), cell("特征谐波(整流器)", 2606)] }),
          new TableRow({ children: [cell("13", 600, { center: true }), cell("7次谐波含量", 2500), cell("V_7 / V_1", 2600), cell("特征谐波(整流器)", 2606)] }),
          new TableRow({ children: [cell("14", 600, { center: true }), cell("电压不平衡度", 2500), cell("对称分量法", 2600), cell("三相不对称程度", 2606)] }),
          new TableRow({ children: [cell("15", 600, { center: true }), cell("电压偏差", 2500), cell("(V_rms−V_nom)/V_nom", 2600), cell("电压偏离额定值", 2606)] }),
          new TableRow({ children: [cell("16", 600, { center: true }), cell("频率偏差", 2500), cell("f_mean − f_nom", 2600), cell("频率稳定性", 2606)] }),
          new TableRow({ children: [cell("17", 600, { center: true }), cell("变压器负载率", 2500), cell("S / S_rated", 2600), cell("变压器承载状态", 2606)] }),
          new TableRow({ children: [cell("18", 600, { center: true }), cell("线路负载率", 2500), cell("I / I_max", 2600), cell("线路承载状态", 2606)] }),
          new TableRow({ children: [cell("19", 600, { center: true }), cell("电压峰值因子", 2500), cell("max|V| / V_rms", 2600), cell("波形尖峰程度", 2606)] }),
          new TableRow({ children: [cell("20", 600, { center: true }), cell("电流峰值因子", 2500), cell("max|I| / I_rms", 2600), cell("电流冲击程度", 2606)] }),
        ]
      }),

      new Paragraph({ children: [new PageBreak()] }),

      // ===== 第九章 参数 =====
      heading1("第九章  系统参数设置与边界条件"),

      heading2("9.1  电网参数"),
      new Table({
        width: { size: 8306, type: WidthType.DXA },
        columnWidths: [3500, 2400, 2406],
        rows: [
          new TableRow({ children: [
            cell("参数", 3500, { bold: true, center: true, shading: "D5E8F0" }),
            cell("数值", 2400, { bold: true, center: true, shading: "D5E8F0" }),
            cell("单位", 2406, { bold: true, center: true, shading: "D5E8F0" }),
          ]}),
          new TableRow({ children: [cell("额定线电压 V_{LL}", 3500), cell("400", 2400, { center: true }), cell("V", 2406, { center: true })] }),
          new TableRow({ children: [cell("额定相电压 V_{ph}", 3500), cell("230.94", 2400, { center: true }), cell("V", 2406, { center: true })] }),
          new TableRow({ children: [cell("额定频率 f_0", 3500), cell("50", 2400, { center: true }), cell("Hz", 2406, { center: true })] }),
        ]
      }),

      heading2("9.2  变压器参数"),
      new Table({
        width: { size: 8306, type: WidthType.DXA },
        columnWidths: [3500, 2400, 2406],
        rows: [
          new TableRow({ children: [
            cell("参数", 3500, { bold: true, center: true, shading: "D5E8F0" }),
            cell("数值", 2400, { bold: true, center: true, shading: "D5E8F0" }),
            cell("单位", 2406, { bold: true, center: true, shading: "D5E8F0" }),
          ]}),
          new TableRow({ children: [cell("额定容量 S_{rated}", 3500), cell("400", 2400, { center: true }), cell("kVA", 2406, { center: true })] }),
          new TableRow({ children: [cell("额定电压比", 3500), cell("10/0.4", 2400, { center: true }), cell("kV", 2406, { center: true })] }),
          new TableRow({ children: [cell("阻抗电压 u_k", 3500), cell("4", 2400, { center: true }), cell("%", 2406, { center: true })] }),
          new TableRow({ children: [cell("接线组别", 3500), cell("Dyn11", 2400, { center: true }), cell("—", 2406, { center: true })] }),
        ]
      }),

      heading2("9.3  线路参数"),
      new Table({
        width: { size: 8306, type: WidthType.DXA },
        columnWidths: [3500, 2400, 2406],
        rows: [
          new TableRow({ children: [
            cell("参数", 3500, { bold: true, center: true, shading: "D5E8F0" }),
            cell("数值", 2400, { bold: true, center: true, shading: "D5E8F0" }),
            cell("单位", 2406, { bold: true, center: true, shading: "D5E8F0" }),
          ]}),
          new TableRow({ children: [cell("线路长度 L_{line}", 3500), cell("100", 2400, { center: true }), cell("m", 2406, { center: true })] }),
          new TableRow({ children: [cell("单位电阻 r_0", 3500), cell("0.27", 2400, { center: true }), cell("Ω/km", 2406, { center: true })] }),
          new TableRow({ children: [cell("单位电抗 x_0", 3500), cell("0.35", 2400, { center: true }), cell("Ω/km", 2406, { center: true })] }),
          new TableRow({ children: [cell("等值电阻 R_{line}", 3500), cell("0.027", 2400, { center: true }), cell("Ω", 2406, { center: true })] }),
          new TableRow({ children: [cell("等值电抗 X_{line}", 3500), cell("0.035", 2400, { center: true }), cell("Ω", 2406, { center: true })] }),
          new TableRow({ children: [cell("最大载流量 I_{max}", 3500), cell("380", 2400, { center: true }), cell("A", 2406, { center: true })] }),
        ]
      }),

      heading2("9.4  负荷参数"),
      new Table({
        width: { size: 8306, type: WidthType.DXA },
        columnWidths: [3500, 2400, 2406],
        rows: [
          new TableRow({ children: [
            cell("参数", 3500, { bold: true, center: true, shading: "D5E8F0" }),
            cell("数值", 2400, { bold: true, center: true, shading: "D5E8F0" }),
            cell("单位", 2406, { bold: true, center: true, shading: "D5E8F0" }),
          ]}),
          new TableRow({ children: [cell("额定有功功率 P_{rated}", 3500), cell("150", 2400, { center: true }), cell("kW", 2406, { center: true })] }),
          new TableRow({ children: [cell("额定无功功率 Q_{rated}", 3500), cell("81.82", 2400, { center: true }), cell("kvar", 2406, { center: true })] }),
          new TableRow({ children: [cell("额定视在功率 S_{rated}", 3500), cell("170.45", 2400, { center: true }), cell("kVA", 2406, { center: true })] }),
          new TableRow({ children: [cell("功率因数 cos φ", 3500), cell("0.88", 2400, { center: true }), cell("—", 2406, { center: true })] }),
        ]
      }),

      heading2("9.5  光伏与充电桩参数"),
      new Table({
        width: { size: 8306, type: WidthType.DXA },
        columnWidths: [3500, 2400, 2406],
        rows: [
          new TableRow({ children: [
            cell("参数", 3500, { bold: true, center: true, shading: "D5E8F0" }),
            cell("数值", 2400, { bold: true, center: true, shading: "D5E8F0" }),
            cell("单位", 2406, { bold: true, center: true, shading: "D5E8F0" }),
          ]}),
          new TableRow({ children: [cell("光伏额定功率 P_{pv,rated}", 3500), cell("100", 2400, { center: true }), cell("kW", 2406, { center: true })] }),
          new TableRow({ children: [cell("光伏逆变器效率", 3500), cell("97", 2400, { center: true }), cell("%", 2406, { center: true })] }),
          new TableRow({ children: [cell("光伏谐波含量", 3500), cell("[3.0, 1.5, 0.8, 0.5]", 2400, { center: true }), cell("%", 2406, { center: true })] }),
          new TableRow({ children: [cell("单台充电桩功率", 3500), cell("7", 2400, { center: true }), cell("kW", 2406, { center: true })] }),
          new TableRow({ children: [cell("充电桩充电效率", 3500), cell("95", 2400, { center: true }), cell("%", 2406, { center: true })] }),
          new TableRow({ children: [cell("充电桩谐波含量", 3500), cell("[8.0, 5.0, 3.0, 2.0]", 2400, { center: true }), cell("%", 2406, { center: true })] }),
        ]
      }),

      heading2("9.6  仿真参数"),
      new Table({
        width: { size: 8306, type: WidthType.DXA },
        columnWidths: [3500, 2400, 2406],
        rows: [
          new TableRow({ children: [
            cell("参数", 3500, { bold: true, center: true, shading: "D5E8F0" }),
            cell("数值", 2400, { bold: true, center: true, shading: "D5E8F0" }),
            cell("单位", 2406, { bold: true, center: true, shading: "D5E8F0" }),
          ]}),
          new TableRow({ children: [cell("采样频率 f_s", 3500), cell("12800", 2400, { center: true }), cell("Hz", 2406, { center: true })] }),
          new TableRow({ children: [cell("采样间隔 T_s", 3500), cell("78.125", 2400, { center: true }), cell("μs", 2406, { center: true })] }),
          new TableRow({ children: [cell("仿真时长 T_{stop}", 3500), cell("0.5", 2400, { center: true }), cell("s", 2406, { center: true })] }),
          new TableRow({ children: [cell("总采样点数 N", 3500), cell("6400", 2400, { center: true }), cell("—", 2406, { center: true })] }),
          new TableRow({ children: [cell("每周期采样点数", 3500), cell("256", 2400, { center: true }), cell("—", 2406, { center: true })] }),
          new TableRow({ children: [cell("蒙特卡洛采样次数", 3500), cell("1000", 2400, { center: true }), cell("—", 2406, { center: true })] }),
          new TableRow({ children: [cell("数据集样本量", 3500), cell("10000", 2400, { center: true }), cell("—", 2406, { center: true })] }),
        ]
      }),

      heading2("9.7  国家标准限值"),
      new Table({
        width: { size: 8306, type: WidthType.DXA },
        columnWidths: [3500, 2400, 2406],
        rows: [
          new TableRow({ children: [
            cell("指标", 3500, { bold: true, center: true, shading: "D5E8F0" }),
            cell("限值", 2400, { bold: true, center: true, shading: "D5E8F0" }),
            cell("依据标准", 2406, { bold: true, center: true, shading: "D5E8F0" }),
          ]}),
          new TableRow({ children: [cell("电压偏差", 3500), cell("±7%", 2400, { center: true }), cell("GB/T 12325", 2406, { center: true })] }),
          new TableRow({ children: [cell("电压THD", 3500), cell("5%", 2400, { center: true }), cell("GB/T 14549", 2406, { center: true })] }),
          new TableRow({ children: [cell("电流THD", 3500), cell("8%", 2400, { center: true }), cell("GB/T 14549", 2406, { center: true })] }),
          new TableRow({ children: [cell("三相电压不平衡度", 3500), cell("2%", 2400, { center: true }), cell("GB/T 15543", 2406, { center: true })] }),
          new TableRow({ children: [cell("频率偏差", 3500), cell("±0.5 Hz", 2400, { center: true }), cell("GB/T 15945", 2406, { center: true })] }),
          new TableRow({ children: [cell("变压器负载率", 3500), cell("100%", 2400, { center: true }), cell("DL/T 572", 2406, { center: true })] }),
          new TableRow({ children: [cell("线路载流量", 3500), cell("100%", 2400, { center: true }), cell("GB/T 16895", 2406, { center: true })] }),
        ]
      }),

      new Paragraph({ children: [new PageBreak()] }),

      // ===== 第十章 结论 =====
      heading1("第十章  结论与展望"),

      heading2("10.1  技术方案总结"),
      body("本技术方案构建了一套完整的配电台区电能质量MATLAB仿真体系，其技术路线可概括为“等值建模 → 场景配置 → 潮流计算 → 波形生成 → 指标计算 → 风险评估 → 数据生成”六个递进阶段。各阶段采用的算法原理总结如下：", { indent: true }),
      body("（1）系统建模采用单节点等值策略，将复杂配电网简化为PCC点集中参数模型，兼顾计算效率与物理保真度；"),
      body("（2）潮流计算采用前推回代法的单节点简化形式，直接解析求解电压、电流、功率，无需迭代收敛；"),
      body("（3）时域波形生成采用解析波形叠加法，将基波、谐波、不平衡分量及噪声按物理机制逐层叠加，直接生成高采样率三相波形；"),
      body("（4）电能质量指标计算严格遵循GB/T国家标准，THD采用FFT频谱分析，不平衡度采用对称分量法配合相干DFT基波相量提取；"),
      body("（5）蒙特卡洛方法引入源荷不确定性，通过1000次随机采样统计各指标越限概率；"),
      body("（6）AI数据集通过参数随机化和特征工程，批量生成含20维特征向量和场景标签的标准化训练数据。"),

      heading2("10.2  方案优势"),
      body("与基于Simulink图形建模的传统仿真方法相比，本方案具有以下优势：", { indent: true }),
      body("（1）无需搭建复杂的Simulink电气模型，避免了模块库兼容性问题和连线拓扑调试；"),
      body("（2）纯MATLAB代码实现，便于版本控制、参数批量扫描和自动化流水线集成；"),
      body("（3）解析波形叠加法的计算效率远高于数值积分，单场景仿真耗时小于1秒，适合大规模蒙特卡洛分析和数据集生成；"),
      body("（4）谐波成分和不平衡度可精确控制，便于开展参数敏感性分析和机理研究。"),

      heading2("10.3  局限与改进方向"),
      body("本方案也存在一定的简化与局限：", { indent: true }),
      body("（1）单节点等值模型无法捕捉多支路潮流分布和节点间电压差异，适用于台区总体评估但不适用于馈线级精细化分析；"),
      body("（2）解析波形叠加法基于准稳态假设，无法模拟电磁暂态过程（如短路故障、开关操作冲击）；"),
      body("（3）谐波模型采用简化的固定阶次和含量，未考虑逆变器控制策略动态变化及谐波间相互作用；"),
      body("（4）三相不平衡模型仅考虑幅值不平衡，未涵盖相位不平衡和线路三相阻抗不对称的影响。"),
      body("后续改进方向包括：引入多节点潮流计算以支持馈线级仿真；耦合电磁暂态模型以评估短路和开关冲击；建立逆变器详细控制模型以提升谐波仿真精度；加入线路三相不对称参数以完善不平衡度评估。"),

      new Paragraph({ spacing: { before: 600 } }),
      new Paragraph({
        alignment: AlignmentType.CENTER,
        children: [new TextRun({ text: "— 全文完 —", size: 21, font: "SimSun", color: "888888" })]
      }),
    ]
  }]
});

Packer.toBuffer(doc).then(buffer => {
  fs.writeFileSync("d:\\ai\\prj\\cb\\pq_ai\\matlab_sim\\新能源与充电桩接入影响评估_MATLAB仿真技术方案.docx", buffer);
  console.log("Document created successfully!");
});
