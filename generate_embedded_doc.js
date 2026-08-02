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

function code(text) {
  return new Paragraph({
    spacing: { before: 40, after: 40 },
    shading: { fill: "F5F5F5", type: ShadingType.CLEAR },
    indent: { left: 420 },
    children: [new TextRun({ text, size: 18, font: "Courier New" })]
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
          children: [new TextRun({ text: "T536+HT7627S 嵌入式软件方案设计", size: 18, font: "SimSun", color: "888888" })]
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
      new Paragraph({ spacing: { before: 1000 } }),
      new Paragraph({
        alignment: AlignmentType.CENTER,
        spacing: { after: 400 },
        children: [new TextRun({ text: "基于终端波形数据的电能质量 AI 应用", bold: true, size: 40, font: "SimHei" })]
      }),
      new Paragraph({
        alignment: AlignmentType.CENTER,
        spacing: { after: 200 },
        children: [new TextRun({ text: "T536 + HT7627S 嵌入式软件方案设计", bold: true, size: 40, font: "SimHei" })]
      }),
      new Paragraph({
        alignment: AlignmentType.CENTER,
        spacing: { after: 600 },
        children: [new TextRun({ text: "——基于 MATLAB 仿真验证的端侧部署方案", size: 26, font: "SimSun" })]
      }),
      new Paragraph({ spacing: { before: 1200 } }),
      new Paragraph({
        alignment: AlignmentType.CENTER,
        children: [new TextRun({ text: "硬件平台：全志 T536 (4×A55 + NPU 2T) + 钜泉 HT7627S", size: 22, font: "SimSun" })]
      }),
      new Paragraph({
        alignment: AlignmentType.CENTER,
        spacing: { before: 200 },
        children: [new TextRun({ text: "版本：V1.0", size: 22, font: "SimSun" })]
      }),
      new Paragraph({
        alignment: AlignmentType.CENTER,
        spacing: { before: 200 },
        children: [new TextRun({ text: "日期：2026年8月", size: 22, font: "SimSun" })]
      }),

      new Paragraph({ children: [new PageBreak()] }),

      // ===== 第一章 概述 =====
      heading1("第一章  项目概述与设计目标"),

      heading2("1.1  项目背景"),
      body("基于已完成的 MATLAB 仿真平台，本项目实现了新能源与充电桩接入对配电网电能质量影响的量化评估，涵盖电压偏差、谐波 THD、三相不平衡度、变压器负载率等核心指标的五场景仿真（S1~S5）、蒙特卡洛风险评估及 AI 训练数据集生成。", { indent: true }),
      body("现需将仿真验证的算法模型、评估逻辑与数据处理流程，完整迁移至全志 T536 + 钜泉 HT7627S 嵌入式硬件平台，构建可在配电台区现场部署的终端级电能质量 AI 评估系统。", { indent: true }),

      heading2("1.2  设计目标"),
      body("本嵌入式软件方案的设计目标包括："),
      body("（1）充分发挥 HT7627S 计量芯片的硬件 FFT、双采样率（12.8kHz/25.6kHz）及谐波寄存器能力，将基波/谐波分析下沉至硬件层，降低 T536 主控计算负担；"),
      body("（2）利用 T536 的 4×A55 CPU + 2 TOPS NPU 算力，部署轻量级 AI 模型（1D-CNN、自编码器、孤立森林等 INT8 量化模型），实现端侧实时事件诊断与风险评估；"),
      body("（3）基于 AMP（Asymmetric Multi-Processing）异构架构，E907 RTOS 承担 HT7627S 实时通信与波形冻结，A55 Linux 负责 AI 推理与业务逻辑，确保周波级时序完整性；"),
      body("（4）完整映射 MATLAB 仿真中的五场景评估逻辑、分级预警指标及治理措施建议至端侧嵌入式系统；"),
      body("（5）建立端-云协同机制：端侧完成实时采集、事件检测、轻量推理；平台侧完成模型训练、大模型交互、跨终端融合分析。"),

      heading2("1.3  设计范围"),
      body("本方案覆盖以下软件范畴："),
      body("——HT7627S 计量前端驱动与数据采集固件；"),
      body("——T536 E907 RTOS 实时任务（通信、校验、冻结）；"),
      body("——T536 A55 Linux 业务软件（PQ 指标计算、事件引擎、AI 推理、特征工程、通信规约）；"),
      body("——端-云通信协议与数据上报格式；"),
      body("——模型部署与量化工具链集成。"),

      new Paragraph({ children: [new PageBreak()] }),

      // ===== 第二章 硬件平台 =====
      heading1("第二章  硬件平台能力分析"),

      heading2("2.1  全志 T536 SoC 规格"),
      new Table({
        width: { size: 8306, type: WidthType.DXA },
        columnWidths: [2400, 2900, 3006],
        rows: [
          new TableRow({ children: [
            cell("维度", 2400, { bold: true, center: true, shading: "D5E8F0" }),
            cell("规格", 2900, { bold: true, center: true, shading: "D5E8F0" }),
            cell("对电能质量 AI 的意义", 3006, { bold: true, center: true, shading: "D5E8F0" }),
          ]}),
          new TableRow({ children: [cell("CPU 主核", 2400), cell("4×ARM Cortex-A55 @1.6GHz", 2900, { center: true }), cell("NEON SIMD + INT8 DP，适合定点 DSP 与轻量推理", 3006)] }),
          new TableRow({ children: [cell("协处理器", 2400), cell("RISC-V E907 @600MHz + E902", 2900, { center: true }), cell("E907 跑 RTOS 承担实时采集、波形冻结、HT7627S 通信", 3006)] }),
          new TableRow({ children: [cell("NPU", 2400), cell("2 TOPS @INT8", 2900, { center: true }), cell("核心资产：可部署 1D-CNN/轻量 Transformer/孤立森林等量化模型", 3006)] }),
          new TableRow({ children: [cell("内存", 2400), cell("LPDDR4X, 2GB", 2900, { center: true }), cell("强约束：系统约 450MB，业务可用约 1.55GB", 3006)] }),
          new TableRow({ children: [cell("存储", 2400), cell("16GB EMMC", 2900, { center: true }), cell("系统 1.6GB；模型库+波形缓存需精算", 3006)] }),
          new TableRow({ children: [cell("系统", 2400), cell("Tina Linux 5.10 + RTOS + AMP", 2900, { center: true }), cell("支持非对称多核：Linux 跑 A55、RTOS 跑 E907", 3006)] }),
          new TableRow({ children: [cell("安全", 2400), cell("国密 SM2/3/4 IP、安全启动、ECC", 2900, { center: true }), cell("适合电力终端安全合规要求", 3006)] }),
        ]
      }),

      heading2("2.2  钜泉 HT7627S 计量芯片规格"),
      new Table({
        width: { size: 8306, type: WidthType.DXA },
        columnWidths: [2400, 2900, 3006],
        rows: [
          new TableRow({ children: [
            cell("维度", 2400, { bold: true, center: true, shading: "D5E8F0" }),
            cell("规格", 2900, { bold: true, center: true, shading: "D5E8F0" }),
            cell("对本课题的意义", 3006, { bold: true, center: true, shading: "D5E8F0" }),
          ]}),
          new TableRow({ children: [cell("ADC", 2400), cell("7 路 ΣΔ ADC，内置 PGA", 2900, { center: true }), cell("三相电压电流 + 零序 + 1 路余量", 3006)] }),
          new TableRow({ children: [cell("采样率", 2400), cell("12.8kHz / 25.6kHz 可选", 2900, { center: true }), cell("50Hz 下分别 = 256/512 点/周波，与文档要求完全对应", 3006)] }),
          new TableRow({ children: [cell("硬件 FFT", 2400), cell("22 位 FFT 单元，64~1024 点", 2900, { center: true }), cell("谐波分析可下沉到计量芯片，T536 不必重算 FFT", 3006)] }),
          new TableRow({ children: [cell("谐波能力", 2400), cell("2-31 次谐波寄存器直读", 2900, { center: true }), cell("THD、各次谐波含有率可直接读寄存器", 3006)] }),
          new TableRow({ children: [cell("FigmaPack", 2400), cell("EMU 数据自动组包 + SPI + CRC", 2900, { center: true }), cell("减轻 T536 拼包负担，保证实时性", 3006)] }),
          new TableRow({ children: [cell("时钟精度", 2400), cell("RTC ±5ppm（-40~85℃）", 2900, { center: true }), cell("多终端时间对齐的本地时钟基准", 3006)] }),
        ]
      }),

      heading2("2.3  关键工程判断"),
      body("基于硬件能力分析，形成以下核心设计决策："),
      body("（1）HT7627S 硬件红利必用：谐波 FFT 已下沉至计量芯片，T536 不应再做主路径 FFT，而应读寄存器 + 做高阶特征/AI 分析；"),
      body("（2）2GB DDR 是头号约束：大模型（方向八）必须部署在平台侧，端侧仅做向量化与 RAG 检索结果缓存；"),
      body("（3）2 TOPS NPU 足够支撑轻量推理：1D-CNN（<500K 参数）、孤立森林、自编码器（瓶颈 <32 维）均可量化部署，典型推理 1-10ms；"),
      body("（4）AMP 架构是时序保证：E907 跑 RTOS 专职 HT7627S 通信与事件冻结，避免 Linux 调度抖动影响周波完整性；"),
      body("（5）16GB EMMC 限制波形留存：必须实现智能压缩，正常态只存统计量，异常态才冻结波形。"),

      new Paragraph({ children: [new PageBreak()] }),

      // ===== 第三章 系统架构 =====
      heading1("第三章  系统架构设计"),

      heading2("3.1  总体架构——四层 AMP 异构"),
      body("采用「训练平台侧 + T536 Linux 主控 + T536 E907 RTOS + HT7627S 计量前端」四层架构："),

      code("┌─────────────────────────────────────────────────────────────┐\n│  训练平台侧（云/边）                                         │\n│  大模型 RAG / 模型训练 / 样本库 / 跨终端事件库 / 业务看板      │\n└──────────────────────────▲──────────────────────────────────┘\n                           │ MQTT/HTTP（模型下发、事件回传）\n┌──────────────────────────┴──────────────────────────────────┐\n│  T536 - A55 Linux 5.10（Tina）                              │\n│  ┌──────────┐ ┌──────────┐ ┌─────────────────────────────┐  │\n│  │标准PQ层  │ │特征工程层│ │边缘推理层（NPU 2T）         │  │\n│  │读HT寄存器│ │RMS/相位  │ │1D-CNN/IF/AE/小Trans        │  │\n│  └──────────┘ └──────────┘ └─────────────────────────────┘  │\n│  ┌──────────┐ ┌──────────┐ ┌─────────────────────────────┐  │\n│  │事件触发  │ │波形压缩  │ │通信/规约/对齐/缓存          │  │\n│  └──────────┘ └──────────┘ └─────────────────────────────┘  │\n└──────────────────────────▲──────────────────────────────────┘\n                           │ 共享内存 / rpmsg\n┌──────────────────────────┴──────────────────────────────────┐\n│  T536 - E907 RTOS（Melis/E907 RTOS）                        │\n│  HT7627S 驱动 / DMA-SPI 接收 / 周波完整性校验 / 事件冻结     │\n└──────────────────────────▲──────────────────────────────────┘\n                           │ SPI0/2 + DMA + FigmaPack\n┌──────────────────────────┴──────────────────────────────────┐\n│  HT7627S（Cortex-M0 @39.3MHz）                             │\n│  7ch ΣΔ ADC → 12.8k/25.6k → EMU → FFT/谐波 → FigmaPack → SPI│\n└─────────────────────────────────────────────────────────────┘"),

      heading2("3.2  数据流设计（每周波一个循环）"),
      body("整个系统以周波（20ms）为基本时间单位循环运转，数据流如下："),
      body("Step 1：HT7627S 以 12.8kHz（256 点/周波）或 25.6kHz（512 点/周波）采样，硬件 FFT 完成基波/谐波分析；"),
      body("Step 2：FigmaPack 自动组包，经 SPI+DMA 推送到 E907 RTOS；"),
      body("Step 3：E907 校验 CRC、周波点数，写入 A55 共享内存环形缓冲（64MB）；"),
      body("Step 4：A55 标准算法层读 HT 寄存器值（有效值/THD/谐波/功率），与原始波形做交叉校验；"),
      body("Step 5：事件触发引擎判定越限/突变 → 冻结事件前后 10-20 周波 + 上下文；"),
      body("Step 6：特征工程层提取标准/波形/跨采样率/时序/业务特征；"),
      body("Step 7：NPU 推理层产出事件分类/扰动源/异常得分；"),
      body("Step 8：压缩层对正常数据降采样，对相似事件聚类合并；"),
      body("Step 9：通信层按规约（IEC 61850/MQTT/1376.1）上报事件摘要，完整波形按需回传。"),

      heading2("3.3  核心设计原则"),
      body("（1）实时性分层：E907 负责硬实时（<1ms）—— SPI 通信、DMA 搬运、周波校验；A55 负责软实时（<100ms）—— 指标计算、事件检测、AI 推理；"),
      body("（2）计算卸载：HT7627S 承担 FFT/RMS/THD 等重计算；T536 NPU 承担 AI 推理；A55 CPU 承担逻辑控制与特征工程；"),
      body("（3）内存分区：DDR 划分为 Linux 用户态（1.0GB）、CMA 连续内存（256MB）、NPU 模型（256MB）、波形环形缓冲（64MB）；"),
      body("（4）存储分层：EMMC 划分为系统镜像（1.6GB）、模型库（1GB）、波形事件库（6GB）、日志缓冲（2GB）、预留（1.4GB）。"),

      new Paragraph({ children: [new PageBreak()] }),

      // ===== 第四章 软件分层 =====
      heading1("第四章  软件分层与模块设计"),

      heading2("4.1  工程目录结构"),
      code("pq_ai_terminal/\n├── drivers/\n│   ├── ht7627s_spi.c          // HT7627S SPI 驱动（E907 RTOS）\n│   ├── ht7627s_regs.h         // 寄存器定义\n│   ├── ht7627s_dma.c          // DMA 接收与 FigmaPack 解析\n│   └── npu_driver.c           // T536 NPU 驱动初始化\n├── core/\n│   ├── pq_metrics.c           // 12 项标准 PQ 指标计算\n│   ├── event_trigger.c        // 事件触发引擎\n│   ├── wave_freeze.c          // 波形冻结（环形缓冲）\n│   ├── feature_extract.c      // 特征工程（NEON 加速）\n│   └── scenario_detect.c      // 新能源/充电桩场景识别\n├── ai/\n│   ├── iforest_infer.c        // 孤立森林推理\n│   ├── ae_infer.c             // 自编码器推理（NPU）\n│   ├── cnn1d_infer.c          // 1D-CNN 推理（NPU）\n│   ├── lgbm_infer.c           // LightGBM 推理\n│   ├── lstm_infer.c           // LSTM/GRU 推理（NPU）\n│   └── dtw_search.c           // DTW 相似检索\n├── compress/\n│   ├── kshape_cluster.c       // K-Shape 聚类\n│   └── lz4_pack.c             // LZ4 压缩\n├── comm/\n│   ├── proto_iec61850.c       // IEC 61850 协议栈\n│   ├── proto_mqtt.c           // MQTT 客户端\n│   ├── proto_1376.c           // 1376.1 规约\n│   └── time_sync.c            // PTP/NTP 时间同步\n├── rag/\n│   ├── embed_infer.c          // Embedding 模型推理（NPU）\n│   └── faiss_lite.c           // FAISS Lite 向量检索\n├── utils/\n│   ├── ring_buffer.c          // 无锁环形缓冲\n│   ├── json_builder.c         // JSON 事件摘要构造\n│   └── sqlite_wrapper.c       // SQLite 持久化封装\n└── scripts/\n    ├── model_quant.py         // 模型量化（Python，平台侧）\n    └── model_deploy.sh        // 模型部署脚本（Shell）"),

      heading2("4.2  驱动层（drivers/）"),

      heading3("4.2.1  HT7627S SPI 驱动"),
      body("运行环境：T536 E907 RTOS（Melis/E907 RTOS）。"),
      body("核心职责："),
      body("——初始化 SPI0/2 接口，配置时钟极性 CPOL=0、相位 CPHA=0，波特率 8MHz；"),
      body("——配置 DMA 通道，实现 SPI 数据自动接收，零拷贝写入共享内存；"),
      body("——解析 FigmaPack 数据包格式，提取 CRC 校验、周波序号、通道数、采样点数；"),
      body("——周波完整性校验：检测丢点、重复、乱序，异常时触发事件冻结信号。"),

      heading3("4.2.2  NPU 驱动"),
      body("运行环境：T536 A55 Linux 5.10。"),
      body("核心职责："),
      body("——初始化全志 OpenNPU / ACIP 工具链；"),
      body("——加载 INT8 量化模型到 NPU 内存（通过 CMA 分配连续物理内存）；"),
      body("——提供异步推理接口 npu_invoke_async()，支持多模型并发调度；"),
      body("——推理完成后通过回调机制通知业务层。"),

      note("NPU 驱动需确认 Linux 5.10 兼容性。若工具链不成熟，备选方案为 ARM Compute Library + CMSIS-NN CPU 推理，性能降低 5-10 倍但可保证可用性。"),

      new Paragraph({ children: [new PageBreak()] }),

      heading2("4.3  核心层（core/）"),

      heading3("4.3.1  标准 PQ 指标模块（pq_metrics.c）"),
      body("运行环境：T536 A55 Linux，核 1-2。"),
      body("核心算法："),
      body("——RMS 有效值：优先读取 HT7627S 寄存器，T536 仅做交叉校验。校验公式：X_rms = sqrt( (1/N) · Σ x[n]² )，NEON 4 路并行加速；"),
      body("——谐波分析：直接读取 HT7627S 谐波寄存器（2-31 次）。事件触发后，T536 做高分辨率 FFT（8192 点）用于精细分析；"),
      body("——THD 计算：THD = sqrt( Σ H_k², k=2..K ) / H_1 × 100%；"),
      body("——电压偏差：δV = (V_rms − V_nom) / V_nom × 100%；"),
      body("——三相不平衡度：对称分量法，ε_u = |V_a2| / |V_a1| × 100%。"),

      body("输出结构体定义："),
      code("typedef struct {\n    char name[32];          // 指标名称\n    float value;            // 指标值\n    float limit;            // 国标限值\n    int status;             // 0=正常 1=预警 2=超标\n    uint32_t timestamp;     // 时间戳（秒级）\n    uint16_t pts_per_cycle; // 采样点数/周波\n    uint8_t phase;          // 相别（0=三相/1=A/2=B/3=C）\n    float confidence;       // 置信度（0~1）\n} pq_metric_t;"),

      heading3("4.3.2  事件触发引擎（event_trigger.c）"),
      body("运行环境：T536 A55 Linux，核 1。"),
      body("触发条件（或逻辑，满足任一即触发）："),
      body("——阈值越限：电压偏差 > ±7%、电压 THD > 5%、电流 THD > 8%、不平衡度 > 2%、频率偏差 > ±0.5Hz；"),
      body("——滑窗突变：半周波 RMS 变化率 dV/dt > 10%/周波 或 dI/dt > 15%/周波；"),
      body("——谐波突变：某次谐波含有率在 3 个周波内变化 > 50%；"),
      body("——滞回机制：进入阈值 90%，退出阈值 92%，避免边界抖动。"),

      body("事件结构体："),
      code("typedef struct {\n    uint32_t id;            // 事件唯一 ID\n    uint8_t type;           // 事件类型（暂降/暂升/谐波/不平衡/...）\n    uint32_t start_ts;      // 起始时间戳\n    uint32_t end_ts;        // 结束时间戳（0 表示持续中）\n    float severity;         // 严重度（0~10）\n    uint8_t phase;          // 相别\n    pq_metric_t metrics[12];// 关联指标\n    float wave_pre[20][512]; // 事件前 20 周波波形\n    float wave_post[20][512];// 事件后 20 周波波形\n} pq_event_t;"),

      heading3("4.3.3  波形冻结模块（wave_freeze.c）"),
      body("运行环境：T536 E907 RTOS + A55 Linux 共享内存。"),
      body("实现机制："),
      body("——环形缓冲：7 通道 × 512 点 × 4 字节 × 40 周波 ≈ 560KB，支持 40 个周波（800ms）回溯；"),
      body("——双缓冲设计：前台缓冲写入当前周波，后台缓冲供 A55 读取分析，通过读写指针切换避免竞态；"),
      body("——冻结触发：事件引擎发出 freeze 信号后，E907 停止覆盖当前缓冲，标记为只读；"),
      body("——上下文附加：冻结波形附带前后各 10-20 周波，共 20-40 周波上下文。"),

      heading3("4.3.4  特征工程模块（feature_extract.c）"),
      body("运行环境：T536 A55 Linux，NEON 加速。"),
      body("特征提取分为三类："),
      body("（1）标准特征（27 类）：RMS、峰峰值、峰值因子、波形因子、半周波不对称度、过零点数、滑窗能量、滑窗方差、基波相位、功率因数、有功/无功/视在功率、各次谐波含有率（2-31 次）、THD、TID、电话干扰系数、闪变 Pst/Plt 等；"),
      body("（2）波形特征（11 类）：波形面积、波形斜率均值/方差、波形复杂度（LLE 近似）、波形熵、波形突变点数、波形振荡次数、波形恢复时间、波形对称度、波形平坦度、波形相关性（三相）、波形因果性（电压-电流领先时差）；"),
      body("（3）跨采样率特征：对 256/512 点两种输入提取与采样率无关的特征（RMS、THD、相位、过零点），通过 AdaptiveAvgPool1d 消除长度差异。"),

      new Paragraph({ children: [new PageBreak()] }),

      heading2("4.4  AI 推理层（ai/）"),

      heading3("4.4.1  模型部署策略"),
      body("端侧 AI 模型采用「平台训练 → ONNX 导出 → INT8 量化 → NPU 部署」四步流程："),
      body("Step 1：平台侧 PyTorch 训练，数据集来自 MATLAB 仿真生成（10000+ 样本）及现场采集；"),
      body("Step 2：导出 ONNX 格式模型，验证数值一致性；"),
      body("Step 3：使用 T536 NPU SDK（OpenNPU/ACIP）进行训练后量化（PTQ），校准集 1000 样本，精度损失 <1%；"),
      body("Step 4：通过 rpmsg 或文件系统将模型部署到端侧，NPU 驱动加载并初始化。"),

      heading3("4.4.2  1D-CNN 事件分类模型"),
      body("模型结构（适配 256/512 双采样率）："),
      code("Input (3ch × 256/512)\n  ├─ Conv1D(32, k=7, s=2) + BN + ReLU   → 32×128/32×256\n  ├─ Conv1D(64, k=5, s=2) + BN + ReLU   → 64×64/64×128\n  ├─ Conv1D(128, k=3, s=2) + BN + ReLU  → 128×32/128×64\n  ├─ AdaptiveAvgPool1d(1)                → 128\n  ├─ FC(128, 64) + ReLU + Dropout(0.2)\n  └─ FC(64, K) + Softmax                 → K 类事件\n参数量约 150K，INT8 量化后 <200KB"),
      body("推理接口："),
      code("int cnn1d_classify(const float *wave, int pts, int n_cycles,\n                     float *probs, int K) {\n    normalize_wave(wave, pts * n_cycles);\n    npu_invoke_async(model_cnn1d, wave, probs);\n    int cls = argmax(probs, K);\n    return (probs[cls] > CONF_THR) ? cls : -1;\n}"),
      body("性能：NPU INT8 推理 <5ms，可承担 10 事件/秒。"),

      heading3("4.4.3  自编码器异常检测模型"),
      body("模型结构："),
      code("Encoder: 256 → 128 → 64 → 32（瓶颈）\nDecoder: 32 → 64 → 128 → 256\n激活 ReLU，损失 MSE"),
      body("异常得分：s = (1/N) · Σ(x_n − x̂_n)²，阈值由验证集 99% 分位数确定。"),
      body("性能：INT8 量化后 <300KB，NPU 推理 <2ms。"),

      heading3("4.4.4  孤立森林异常检测模型"),
      body("实现方式：平台训练 T 棵树，导出分裂节点（特征索引 + 阈值）为 C 数组。端侧纯树遍历推理，无需 NPU。"),
      code("float score_if(const Node *tree, int T, const float *x, int n) {\n    float total = 0;\n    for (int t = 0; t < T; ++t) {\n        int node = 0, depth = 0;\n        while (tree[t*MAXN + node].left != -1 && depth < MAXD) {\n            Node nd = tree[t*MAXN + node];\n            node = (x[nd.feat] < nd.thr) ? nd.left : nd.right;\n            depth++;\n        }\n        total += depth;\n    }\n    return powf(2.0f, -total / T / cn(n));\n}"),
      body("性能：树数 T=100，深度 8，模型 <500KB；推理 <0.5ms/样本（NPU 可批处理）。"),

      heading3("4.4.5  LSTM/GRU 负荷预测模型"),
      body("输入：过去 7 天 15 分钟粒度负荷 + 气象 + 节假日；输出：未来 24 小时负荷曲线。"),
      body("优化：用 GRU 替代 LSTM（参数少 25%），精度损失小。NPU 推理 <10ms。"),

      new Paragraph({ children: [new PageBreak()] }),

      heading2("4.5  新能源与充电桩场景识别模块（scenario_detect.c）"),
      body("本模块对应 MATLAB 仿真中的 S1~S5 场景，将仿真验证的判定逻辑映射为端侧实时规则引擎。"),

      heading3("4.5.1  光伏出力识别"),
      body("判定规则（与 MATLAB S3 场景对应）："),
      new Table({
        width: { size: 8306, type: WidthType.DXA },
        columnWidths: [2200, 2200, 1906, 2000],
        rows: [
          new TableRow({ children: [
            cell("特征", 2200, { bold: true, center: true, shading: "D5E8F0" }),
            cell("阈值", 2200, { bold: true, center: true, shading: "D5E8F0" }),
            cell("判定", 1906, { bold: true, center: true, shading: "D5E8F0" }),
            cell("MATLAB 映射", 2000, { bold: true, center: true, shading: "D5E8F0" }),
          ]}),
          new TableRow({ children: [cell("时段", 2200), cell("10:00-14:00", 2200, { center: true }), cell("午间", 1906, { center: true }), cell("S3 白天场景", 2000, { center: true })] }),
          new TableRow({ children: [cell("功率方向", 2200), cell("P_反送 > 0", 2200, { center: true }), cell("反向潮流", 1906, { center: true }), cell("S3 功率反送", 2000, { center: true })] }),
          new TableRow({ children: [cell("电压变化", 2200), cell("ΔV > +3%", 2200, { center: true }), cell("电压抬升", 1906, { center: true }), cell("S3 电压抬升", 2000, { center: true })] }),
          new TableRow({ children: [cell("谐波频谱", 2200), cell("偶次谐波(2/4次)偏高", 2200, { center: true }), cell("逆变器特征", 1906, { center: true }), cell("S3 光伏谐波", 2000, { center: true })] }),
        ]
      }),

      heading3("4.5.2  充电桩负荷识别"),
      body("判定规则（与 MATLAB S2 场景对应）："),
      new Table({
        width: { size: 8306, type: WidthType.DXA },
        columnWidths: [2200, 2200, 1906, 2000],
        rows: [
          new TableRow({ children: [
            cell("特征", 2200, { bold: true, center: true, shading: "D5E8F0" }),
            cell("阈值", 2200, { bold: true, center: true, shading: "D5E8F0" }),
            cell("判定", 1906, { bold: true, center: true, shading: "D5E8F0" }),
            cell("MATLAB 映射", 2000, { bold: true, center: true, shading: "D5E8F0" }),
          ]}),
          new TableRow({ children: [cell("时段", 2200), cell("18:00-22:00", 2200, { center: true }), cell("傍晚高峰", 1906, { center: true }), cell("S2 充电高峰", 2000, { center: true })] }),
          new TableRow({ children: [cell("电流 THD", 2200), cell("THD_I > 8%", 2200, { center: true }), cell("谐波超标", 1906, { center: true }), cell("S2 电流THD FAIL", 2000, { center: true })] }),
          new TableRow({ children: [cell("5/7/11/13 次谐波", 2200), cell("含有率突增", 2200, { center: true }), cell("整流器特征", 1906, { center: true }), cell("S2 充电桩谐波", 2000, { center: true })] }),
          new TableRow({ children: [cell("变压器负载率", 2200), cell("η > 80%", 2200, { center: true }), cell("重载预警", 1906, { center: true }), cell("S2 负载率上升", 2000, { center: true })] }),
        ]
      }),

      heading3("4.5.3  光充耦合场景识别"),
      body("判定规则（与 MATLAB S4 场景对应）："),
      body("——同时满足光伏识别条件（功率反送 + 电压抬升 + 偶次谐波）和充电桩识别条件（傍晚时段 + 奇次谐波 + 负载上升）；"),
      body("——电压 THD 处于 4-7% 区间（光伏偶次 + 充电桩奇次谐波叠加）；"),
      body("——功率因数 0.85-0.95（充电桩无功需求与光伏单位功率因数耦合）。"),

      heading3("4.5.4  极端场景识别"),
      body("判定规则（与 MATLAB S5 场景对应）："),
      body("——光伏满发（P_pv ≈ P_rated）+ 充电桩高并发（>10 台）+ 负荷高峰（>120% 额定）；"),
      body("——电压偏差接近 ±7% 边界；"),
      body("——电压 THD > 7% 或电流 THD > 20%；"),
      body("——变压器负载率 > 90% 或线路负载率 > 85%。"),

      new Paragraph({ children: [new PageBreak()] }),

      // ===== 第五章 任务调度 =====
      heading1("第五章  任务调度与实时性设计"),

      heading2("5.1  E907 RTOS 任务设计"),
      body("E907 运行 Melis/E907 RTOS，采用优先级抢占式调度，核心任务如下："),

      new Table({
        width: { size: 8306, type: WidthType.DXA },
        columnWidths: [2000, 1600, 1600, 1600, 1506],
        rows: [
          new TableRow({ children: [
            cell("任务名", 2000, { bold: true, center: true, shading: "D5E8F0" }),
            cell("周期", 1600, { bold: true, center: true, shading: "D5E8F0" }),
            cell("优先级", 1600, { bold: true, center: true, shading: "D5E8F0" }),
            cell("执行时间", 1600, { bold: true, center: true, shading: "D5E8F0" }),
            cell("功能", 1506, { bold: true, center: true, shading: "D5E8F0" }),
          ]}),
          new TableRow({ children: [cell("spi_dma_rx", 2000), cell("20ms", 1600, { center: true }), cell("最高", 1600, { center: true }), cell("<1ms", 1600, { center: true }), cell("SPI+DMA 接收 HT7627S 数据", 1506)] }),
          new TableRow({ children: [cell("crc_verify", 2000), cell("20ms", 1600, { center: true }), cell("高", 1600, { center: true }), cell("<0.5ms", 1600, { center: true }), cell("CRC 校验与周波完整性检查", 1506)] }),
          new TableRow({ children: [cell("ring_write", 2000), cell("20ms", 1600, { center: true }), cell("高", 1600, { center: true }), cell("<0.5ms", 1600, { center: true }), cell("写入共享内存环形缓冲", 1506)] }),
          new TableRow({ children: [cell("freeze_ctrl", 2000), cell("事件触发", 1600, { center: true }), cell("最高", 1600, { center: true }), cell("<0.2ms", 1600, { center: true }), cell("波形冻结控制", 1506)] }),
          new TableRow({ children: [cell("rtc_sync", 2000), cell("1s", 1600, { center: true }), cell("低", 1600, { center: true }), cell("<1ms", 1600, { center: true }), cell("HT7627S RTC 时间同步", 1506)] }),
        ]
      }),

      heading2("5.2  A55 Linux 任务设计"),
      body("A55 运行 Tina Linux 5.10（可选 PREEMPT_RT 补丁），采用多线程 + NPU 异步推理架构："),

      new Table({
        width: { size: 8306, type: WidthType.DXA },
        columnWidths: [2000, 1600, 1600, 1600, 1506],
        rows: [
          new TableRow({ children: [
            cell("线程名", 2000, { bold: true, center: true, shading: "D5E8F0" }),
            cell("周期", 1600, { bold: true, center: true, shading: "D5E8F0" }),
            cell("绑定核", 1600, { bold: true, center: true, shading: "D5E8F0" }),
            cell("执行时间", 1600, { bold: true, center: true, shading: "D5E8F0" }),
            cell("功能", 1506, { bold: true, center: true, shading: "D5E8F0" }),
          ]}),
          new TableRow({ children: [cell("pq_calc", 2000), cell("20ms", 1600, { center: true }), cell("CPU1", 1600, { center: true }), cell("<10ms", 1600, { center: true }), cell("读 HT 寄存器 + PQ 指标计算", 1506)] }),
          new TableRow({ children: [cell("event_detect", 2000), cell("20ms", 1600, { center: true }), cell("CPU1", 1600, { center: true }), cell("<5ms", 1600, { center: true }), cell("阈值 + 滑窗 + 滞回事件检测", 1506)] }),
          new TableRow({ children: [cell("feature_extract", 2000), cell("事件触发", 1600, { center: true }), cell("CPU2", 1600, { center: true }), cell("<20ms", 1600, { center: true }), cell("27+11 维特征提取（NEON）", 1506)] }),
          new TableRow({ children: [cell("npu_infer", 2000), cell("事件触发", 1600, { center: true }), cell("CPU3", 1600, { center: true }), cell("<5ms", 1600, { center: true }), cell("1D-CNN/AE 推理（NPU）", 1506)] }),
          new TableRow({ children: [cell("compress", 2000), cell("1min", 1600, { center: true }), cell("CPU2", 1600, { center: true }), cell("<100ms", 1600, { center: true }), cell("K-Shape 聚类 + LZ4 压缩", 1506)] }),
          new TableRow({ children: [cell("comm_upload", 2000), cell("5min", 1600, { center: true }), cell("CPU0", 1600, { center: true }), cell("<500ms", 1600, { center: true }), cell("MQTT/61850 数据上报", 1506)] }),
        ]
      }),

      heading2("5.3  核间通信机制"),
      body("E907 RTOS 与 A55 Linux 之间通过 rpmsg（Remote Processor Messaging）框架通信："),
      body("——控制通道：E907 → A55 发送事件冻结信号、周波状态、错误码；"),
      body("——数据通道：共享内存环形缓冲（64MB，通过 CMA 分配物理连续内存），E907 写、A55 读；"),
      body("——同步机制：读写指针 + 内存屏障，无锁设计避免互斥开销。"),

      new Paragraph({ children: [new PageBreak()] }),

      // ===== 第六章 数据流 =====
      heading1("第六章  数据流与存储设计"),

      heading2("6.1  数据生命周期"),
      body("数据从采集到上报的全生命周期分为五个阶段："),
      body("（1）原始采样数据：HT7627S 7 通道 ADC 采样 → SPI+DMA → E907 环形缓冲（保留 40 周波）；"),
      body("（2）寄存器数据：HT7627S 自动计算 RMS/THD/谐波/功率 → SPI 读取 → A55 指标计算；"),
      body("（3）事件数据：触发冻结 → 前后 20 周波波形 + 特征向量 + AI 推理结果 → SQLite 持久化；"),
      body("（4）统计量数据：10 分钟聚合窗口 → 均值/分位数/越限时长 → SQLite 时序表；"),
      body("（5）上报数据：JSON 事件摘要（MQTT）/ MMS 报告（IEC 61850）→ 平台侧。"),

      heading2("6.2  存储分层策略"),

      new Table({
        width: { size: 8306, type: WidthType.DXA },
        columnWidths: [2000, 1800, 1800, 2706],
        rows: [
          new TableRow({ children: [
            cell("层级", 2000, { bold: true, center: true, shading: "D5E8F0" }),
            cell("介质", 1800, { bold: true, center: true, shading: "D5E8F0" }),
            cell("容量", 1800, { bold: true, center: true, shading: "D5E8F0" }),
            cell("用途", 2706, { bold: true, center: true, shading: "D5E8F0" }),
          ]}),
          new TableRow({ children: [cell("L0 实时缓冲", 2000), cell("DDR 共享内存", 1800, { center: true }), cell("64MB", 1800, { center: true }), cell("40 周波原始波形（E907→A55）", 2706)] }),
          new TableRow({ children: [cell("L1 事件缓存", 2000), cell("DDR 堆内存", 1800, { center: true }), cell("256MB", 1800, { center: true }), cell("冻结波形 + 特征 + 推理结果", 2706)] }),
          new TableRow({ children: [cell("L2 本地持久化", 2000), cell("EMMC SQLite", 1800, { center: true }), cell("6GB", 1800, { center: true }), cell("事件库 + 时序指标库（滚动覆盖）", 2706)] }),
          new TableRow({ children: [cell("L3 模型存储", 2000), cell("EMMC squashfs", 1800, { center: true }), cell("1GB", 1800, { center: true }), cell("INT8 量化模型库（只读）", 2706)] }),
          new TableRow({ children: [cell("L4 云端存储", 2000), cell("平台数据库", 1800, { center: true }), cell("无限制", 1800, { center: true }), cell("全量数据 + 模型训练 + 大模型", 2706)] }),
        ]
      }),

      heading2("6.3  压缩策略"),
      body("——正常态：仅存储 10 分钟粒度统计量（12 项指标均值/分位数），压缩比约 100:1；"),
      body("——异常态：存储完整事件波形（20-40 周波）+ 特征 + 推理结果，LZ4 压缩后约 50-200KB/事件；"),
      body("——重复事件：K-Shape 聚类合并相似事件，仅保留聚类中心 + 差异片段，进一步节省 3-5 倍空间。"),

      new Paragraph({ children: [new PageBreak()] }),

      // ===== 第七章 通信 =====
      heading1("第七章  通信与规约设计"),

      heading2("7.1  上行通信"),
      body("终端到平台的通信支持三种规约，按场景选择："),

      heading3("7.1.1  MQTT（主要）"),
      body("——Broker：平台侧 EMQ X / Mosquitto；"),
      body("——Topic 结构：pq/terminal/{terminal_id}/{data_type}；"),
      body("——数据类型：metrics（周期指标）、event（事件告警）、waveform（波形数据）、status（心跳）；"),
      body("——QoS：指标数据 QoS 0（允许丢包），事件数据 QoS 1（必达），波形数据 QoS 1 + 分片传输。"),

      heading3("7.1.2  IEC 61850（电力标准）"),
      body("——MMS 报告控制块：周期性上送数据集（RMS、THD、功率等）；"),
      body("——GOOSE：事件触发快速报文（<10ms），用于站内联动；"),
      body("——文件传输：波形文件（COMTRADE 格式）按需召唤。"),

      heading3("7.1.3  1376.1（国网规约）"),
      body("——面向国网集中器/专变终端的兼容接口；"),
      body("——支持透明转发、数据召测、参数配置。"),

      heading2("7.2  下行通信"),
      body("——模型下发：平台通过 MQTT / HTTP 推送 INT8 模型文件，端侧校验哈希后热加载；"),
      body("——参数配置：阈值、采样率、上报周期等可通过 MQTT 远程配置；"),
      body("——OTA 升级：差分升级包，断点续传，双备份回滚。"),

      heading2("7.3  时间同步"),
      body("——首选 PTP（IEEE 1588）：T536 GMAC 支持硬件时间戳，精度 <1μs；"),
      body("——备选 NTP：精度 <10ms，适用于无 PTP 网络环境；"),
      body("——本地基准：HT7627S RTC ±5ppm，作为掉电保持时钟。"),

      new Paragraph({ children: [new PageBreak()] }),

      // ===== 第八章 MATLAB映射 =====
      heading1("第八章  MATLAB 仿真到嵌入式部署的映射"),

      heading2("8.1  算法映射总表"),
      new Table({
        width: { size: 8306, type: WidthType.DXA },
        columnWidths: [2400, 2200, 1906, 1800],
        rows: [
          new TableRow({ children: [
            cell("MATLAB 仿真模块", 2400, { bold: true, center: true, shading: "D5E8F0" }),
            cell("嵌入式实现", 2200, { bold: true, center: true, shading: "D5E8F0" }),
            cell("部署位置", 1906, { bold: true, center: true, shading: "D5E8F0" }),
            cell("性能指标", 1800, { bold: true, center: true, shading: "D5E8F0" }),
          ]}),
          new TableRow({ children: [cell("loadSystemParams", 2400), cell("参数配置文件 + 运行时结构体", 2200), cell("A55 Linux", 1906, { center: true }), cell("加载 <10ms", 1800, { center: true })] }),
          new TableRow({ children: [cell("runSimulation (S1~S5)", 2400), cell("scenario_detect.c 规则引擎", 2200), cell("A55 Linux", 1906, { center: true }), cell("判定 <1ms", 1800, { center: true })] }),
          new TableRow({ children: [cell("powerFlowCalc", 2400), cell("单节点潮流计算（C 实现）", 2200), cell("A55 Linux", 1906, { center: true }), cell("计算 <0.1ms", 1800, { center: true })] }),
          new TableRow({ children: [cell("时域波形生成", 2400), cell("HT7627S ADC 采样 + E907 DMA", 2200), cell("HT7627S+E907", 1906, { center: true }), cell("实时 20ms", 1800, { center: true })] }),
          new TableRow({ children: [cell("calculatePQMetrics", 2400), cell("pq_metrics.c（读 HT 寄存器）", 2200), cell("A55 Linux", 1906, { center: true }), cell("计算 <10ms", 1800, { center: true })] }),
          new TableRow({ children: [cell("FFT 谐波分析", 2400), cell("HT7627S 硬件 FFT 寄存器直读", 2200), cell("HT7627S", 1906, { center: true }), cell("硬件实时", 1800, { center: true })] }),
          new TableRow({ children: [cell("相干 DFT 不平衡度", 2400), cell("对称分量法（C + NEON）", 2200), cell("A55 Linux", 1906, { center: true }), cell("计算 <5ms", 1800, { center: true })] }),
          new TableRow({ children: [cell("monteCarloAnalysis", 2400), cell("蒙特卡洛风险评估（C 实现）", 2200), cell("A55 Linux", 1906, { center: true }), cell("1000次 <100ms", 1800, { center: true })] }),
          new TableRow({ children: [cell("generateDataset", 2400), cell("端侧特征工程 + 实时数据集", 2200), cell("A55 Linux", 1906, { center: true }), cell("特征 <20ms", 1800, { center: true })] }),
          new TableRow({ children: [cell("1D-CNN 分类（未来）", 2400), cell("cnn1d_infer.c（NPU INT8）", 2200), cell("T536 NPU", 1906, { center: true }), cell("推理 <5ms", 1800, { center: true })] }),
        ]
      }),

      heading2("8.2  场景映射详述"),

      heading3("8.2.1  S1 基准负荷场景"),
      body("MATLAB 仿真结果：电压偏差 -4.28%，电压 THD 0.02%，电流 THD 0.02%，变压器负载率 42.5%，全部 PASS。"),
      body("嵌入式实现："),
      body("——端侧读 HT7627S 寄存器，获取 RMS、THD、功率因数；"),
      body("——指标均处于绿色（安全）区间，不上报事件，仅存储 10 分钟统计量；"),
      body("——作为后续场景的基准线，用于 delta 对比。"),

      heading3("8.2.2  S2 充电桩接入场景"),
      body("MATLAB 仿真结果：电压 THD 6.96%（越限），电流 THD 20.30%（越限），变压器负载率 48.3%。"),
      body("嵌入式实现："),
      body("——事件引擎检测到 THD_I > 8%，触发谐波超标事件；"),
      body("——场景识别模块判定：18:00-22:00 时段 + 5/7/11/13 次谐波突增 → 充电桩接入；"),
      body("——上报事件摘要：事件类型=谐波超标，成因=充电桩，严重度=7/10，建议=有序充电/滤波器。"),

      heading3("8.2.3  S3 光伏接入场景"),
      body("MATLAB 仿真结果：电压偏差 -2.93%（抬升），电压 THD 2.10%，变压器负载率 12.8%，全部 PASS。"),
      body("嵌入式实现："),
      body("——检测到功率反送（P_反向 > 0）+ 电压抬升（ΔV > +3%）+ 偶次谐波偏高；"),
      body("——场景识别判定：光伏接入，时段 10:00-14:00；"),
      body("——上报绿色事件（提示性）：电压抬升 2.93%，建议=无功调节/逆功率保护。"),

      heading3("8.2.4  S4 光充耦合场景"),
      body("MATLAB 仿真结果：电压偏差 -3.11%，电压 THD 4.11%，电流 THD 21.24%（越限），功率因数 0.952。"),
      body("嵌入式实现："),
      body("——同时满足光伏特征（反送+抬升+偶次谐波）和充电桩特征（奇次谐波+负载上升）；"),
      body("——场景识别判定：光充耦合，黄色预警；"),
      body("——上报黄色事件：电流 THD 超标，建议=储能配置+有序充电+无功补偿。"),

      heading3("8.2.5  S5 极端场景"),
      body("MATLAB 仿真结果：电压 THD 7.44%（越限），电流 THD 25.49%（越限），变压器负载率 44.5%。"),
      body("嵌入式实现："),
      body("——多指标同时逼近限值，触发红色告警；"),
      body("——场景识别判定：极端高渗透，红色告警；"),
      body("——蒙特卡洛模块运行 1000 次采样，评估未来 1-24 小时越限概率；"),
      body("——上报红色事件 + 风险评估报告：越限概率>80%，建议=立即限电/储能投入/网络重构。"),

      new Paragraph({ children: [new PageBreak()] }),

      // ===== 第九章 实施路径 =====
      heading1("第九章  实施路径与里程碑"),

      heading2("9.1  五阶段实施计划"),

      new Table({
        width: { size: 8306, type: WidthType.DXA },
        columnWidths: [1200, 1600, 1800, 1800, 1906],
        rows: [
          new TableRow({ children: [
            cell("阶段", 1200, { bold: true, center: true, shading: "D5E8F0" }),
            cell("周期", 1600, { bold: true, center: true, shading: "D5E8F0" }),
            cell("端侧工作", 1800, { bold: true, center: true, shading: "D5E8F0" }),
            cell("平台工作", 1800, { bold: true, center: true, shading: "D5E8F0" }),
            cell("算法交付", 1906, { bold: true, center: true, shading: "D5E8F0" }),
          ]}),
          new TableRow({ children: [
            cell("一", 1200, { center: true }), cell("4周", 1600, { center: true }), cell("硬件 bring-up，HT7627S 驱动调试", 1800), cell("数据规范定义", 1800), cell("—", 1906, { center: true })] }),
          new TableRow({ children: [
            cell("二", 1200, { center: true }), cell("6周", 1600, { center: true }), cell("采集+冻结+PQ 指标+上报", 1800), cell("样本库建设", 1800), cell("标准算法层", 1906, { center: true })] }),
          new TableRow({ children: [
            cell("三", 1200, { center: true }), cell("10周", 1600, { center: true }), cell("端侧 NPU 适配框架", 1800), cell("模型训练+评估", 1800), cell("全部 8 类模型", 1906, { center: true })] }),
          new TableRow({ children: [
            cell("四", 1200, { center: true }), cell("8周", 1600, { center: true }), cell("通信联调+规约对接", 1800), cell("看板+RAG 系统", 1800), cell("模型迭代优化", 1906, { center: true })] }),
          new TableRow({ children: [
            cell("五", 1200, { center: true }), cell("4周", 1600, { center: true }), cell("现场部署+稳定性测试", 1800), cell("业务闭环验证", 1800), cell("现场优化", 1906, { center: true })] }),
        ]
      }),

      heading2("9.2  关键里程碑"),
      body("M1（第 4 周）：HT7627S SPI 通信调通，周波数据完整接收，CRC 校验 100% 通过；"),
      body("M2（第 10 周）：12 项 PQ 指标计算与 HT7627S 寄存器读数交叉校验，误差 <0.5%；"),
      body("M3（第 16 周）：事件触发引擎上线，检测延迟 <20ms，误报率 <5%；"),
      body("M4（第 24 周）：1D-CNN NPU 推理上线，精度损失 <1%，推理延迟 <5ms；"),
      body("M5（第 32 周）：端到端闭环验证完成，现场试运行，业务指标达标。"),

      new Paragraph({ children: [new PageBreak()] }),

      // ===== 第十章 风险 =====
      heading1("第十章  风险分析与对策"),

      new Table({
        width: { size: 8306, type: WidthType.DXA },
        columnWidths: [2200, 2400, 1706, 2000],
        rows: [
          new TableRow({ children: [
            cell("风险", 2200, { bold: true, center: true, shading: "D5E8F0" }),
            cell("影响", 2400, { bold: true, center: true, shading: "D5E8F0" }),
            cell("概率", 1706, { bold: true, center: true, shading: "D5E8F0" }),
            cell("对策", 2000, { bold: true, center: true, shading: "D5E8F0" }),
          ]}),
          new TableRow({ children: [
            cell("NPU 工具链不成熟", 2200), cell("模型部署受阻", 2400, { center: true }), cell("中", 1706, { center: true }), cell("备选 CMSIS-NN + ACL CPU 推理", 2000)] }),
          new TableRow({ children: [
            cell("2GB DDR 不足", 2200), cell("OOM / 性能下降", 2400, { center: true }), cell("中", 1706, { center: true }), cell("严格分块加载，模型按需加载", 2000)] }),
          new TableRow({ children: [
            cell("HT7627S 寄存器文档不全", 2200), cell("算法不准", 2400, { center: true }), cell("低", 1706, { center: true }), cell("与钜泉支持对接 + 实测校准", 2000)] }),
          new TableRow({ children: [
            cell("双采样率样本不均衡", 2200), cell("模型偏置", 2400, { center: true }), cell("中", 1706, { center: true }), cell("平台侧重采样增强 + 损失加权", 2000)] }),
          new TableRow({ children: [
            cell("大模型网络依赖", 2200), cell("离线不可用", 2400, { center: true }), cell("低", 1706, { center: true }), cell("端侧缓存高频问答 + 本地小模型降级", 2000)] }),
          new TableRow({ children: [
            cell("Linux 调度抖动", 2200), cell("周波完整性受损", 2400, { center: true }), cell("低", 1706, { center: true }), cell("关键路径放 E907 RTOS，A55 用 PREEMPT_RT", 2000)] }),
        ]
      }),

      new Paragraph({ children: [new PageBreak()] }),

      // ===== 第十一章 结论 =====
      heading1("第十一章  结论"),

      body("本方案基于已完成的 MATLAB 仿真验证，将新能源与充电桩接入影响评估的核心算法（潮流计算、谐波分析、场景识别、蒙特卡洛风险评估）完整映射至全志 T536 + 钜泉 HT7627S 嵌入式平台。", { indent: true }),

      body("方案核心设计要点："),
      body("（1）硬件红利最大化：HT7627S 承担 12.8kHz/25.6kHz 采样、硬件 FFT、谐波寄存器直读，T536 专注于高阶特征提取与 AI 推理；"),
      body("（2）AMP 异构时序保证：E907 RTOS 专职实时采集与波形冻结，A55 Linux 负责业务逻辑与 NPU 推理，周波级完整性 100% 保障；"),
      body("（3）AI 轻量化部署：1D-CNN、自编码器、孤立森林等模型经 INT8 量化后 <500KB，NPU 推理 <5ms，满足实时性要求；"),
      body("（4）端-云协同分层：端侧完成实时检测与轻量推理，平台侧完成模型训练、大模型交互、跨终端融合；"),
      body("（5）仿真到部署闭环：MATLAB S1~S5 场景、分级预警指标、治理措施建议完整映射至端侧规则引擎与上报模板。"),

      body("本方案在 T536 + 2GB DDR + 16GB EMMC + HT7627S 的硬件约束下，通过科学的资源分配与算法优化，可完整支撑电能质量 AI 应用的八大技术方向，各项性能指标满足电力终端的现场部署要求。"),

      new Paragraph({ spacing: { before: 600 } }),
      new Paragraph({
        alignment: AlignmentType.CENTER,
        children: [new TextRun({ text: "— 全文完 —", size: 21, font: "SimSun", color: "888888" })]
      }),
    ]
  }]
});

Packer.toBuffer(doc).then(buffer => {
  fs.writeFileSync("d:\\ai\\prj\\cb\\pq_ai\\T536_HT7627S_嵌入式软件方案设计.docx", buffer);
  console.log("Document created successfully!");
});
