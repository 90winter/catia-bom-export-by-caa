import fs from 'node:fs/promises';
import path from 'node:path';
import { Workbook, SpreadsheetFile } from '@oai/artifact-tool';

function round4(x) {
  return Number(x.toFixed(4));
}

function parseCsv(text) {
  text = text.replace(/^\uFEFF/, '');
  const rows = [];
  let row = [];
  let cell = '';
  let inQuotes = false;

  for (let i = 0; i < text.length; i++) {
    const c = text[i];
    if (inQuotes) {
      if (c === '"' && text[i + 1] === '"') {
        cell += '"';
        i++;
      } else if (c === '"') {
        inQuotes = false;
      } else {
        cell += c;
      }
    } else if (c === '"') {
      inQuotes = true;
    } else if (c === ',') {
      row.push(cell);
      cell = '';
    } else if (c === '\n') {
      row.push(cell.replace(/\r$/, ''));
      rows.push(row);
      row = [];
      cell = '';
    } else {
      cell += c;
    }
  }

  if (cell || row.length) {
    row.push(cell.replace(/\r$/, ''));
    rows.push(row);
  }
  if (inQuotes) throw new Error('Invalid CSV formatting');
  return rows;
}

// 合法的（非致命）实例 / 分组状态码，需与 CAA worker 输出保持一致
const WARNING_STATUSES = [
  'area_warning',
  'render_warning',
  'inertia_warning',
  'area_render_warning',
  'area_inertia_warning',
  'render_inertia_warning',
  'area_render_inertia_warning'
];
const VALID_GROUP_STATUSES = ['ok', ...WARNING_STATUSES];
const VALID_INSTANCE_STATUSES = ['tiny_volume', 'empty_body', ...VALID_GROUP_STATUSES];

function statusLabelOf(status) {
  if (status === 'empty_body') return '空几何体';
  const parts = [];
  if (status.includes('area')) parts.push('表面积异常');
  if (status.includes('render')) parts.push('预览渲染异常');
  if (status.includes('inertia')) parts.push('惯性矩异常');
  return parts.length ? parts.join(' + ') : '未归类异常';
}

function statusNoteOf(status) {
  if (status === 'empty_body') {
    return '空几何体（无三维实体），未生成体积与质心数据，已跳过';
  }
  const parts = [];
  if (status.includes('area')) parts.push('表面积测量异常（已按保守值处理）');
  if (status.includes('render')) parts.push('预览图渲染失败（已使用空白占位图）');
  if (status.includes('inertia')) parts.push('主惯性矩不可用（已保守单列，未参与归并）');
  return parts.length ? parts.join('；') : '未归类的异常状态';
}

const [csvPath, outputPath, customDensityArg] = process.argv.slice(2);
if (!csvPath || !outputPath) {
  throw new Error('Usage: node build_filtered_bom.mjs groups.csv output.xlsx [customDensity]');
}

const customDensity = Number(customDensityArg);
const hasCustomDensity =
  customDensityArg !== undefined &&
  String(customDensityArg).trim() !== '' &&
  Number.isFinite(customDensity) &&
  customDensity > 0;

const matrix = parseCsv(await fs.readFile(csvPath, 'utf8'));
const head = matrix.shift();
const ix = Object.fromEntries(head.map((x, i) => [x, i]));

for (const k of ['index', 'display_name', 'quantity', 'area_mm2', 'volume_mm3', 'total_volume_mm3', 'cx_mm', 'cy_mm', 'cz_mm', 'image', 'status']) {
  if (!(k in ix)) throw new Error('Missing required field in group CSV: ' + k);
}

const data = matrix.filter(x => x.length > 1);
for (const d of data) {
  if (!VALID_GROUP_STATUSES.includes(d[ix.status])) {
    throw new Error('Incomplete group data: ' + d[ix.index] + ' (status=' + d[ix.status] + ')');
  }
}

const wb = Workbook.create();
const s = wb.worksheets.add('实体BOM');
const m = wb.worksheets.add('材料密度');

s.showGridLines = false;
m.showGridLines = false;

// -------------------------------------------------------------
// Sheet 2: 材料密度 (19 用户指定材料)
// -------------------------------------------------------------
const materials = [
  ['PP-TD20', 1010, '改性聚丙烯 (默认)', '空调壳体(HVAC)、风道、仪表板骨架及支架'],
  ['PP-TD40', 1222, '改性聚丙烯', '高刚性结构件、风扇护风圈、导风罩'],
  ['PP-GF15', 1150, '玻纤增强聚丙烯', '结构支架、风扇叶轮及增强功能件'],
  ['PBT', 1460, '聚对苯二甲酸丁二醇酯', '线束接插件、传感器外壳、电机端盖及电气盒'],
  ['POM', 1410, '聚甲醛', '风门齿轮、连杆机构、执行器摇臂及耐磨卡扣'],
  ['ABS', 1060, '丙烯腈-丁二烯-苯乙烯共聚物', '出风口叶片、操控面板、内外饰外观件'],
  ['PA66', 1530, '增强尼龙 66', '耐高温结构件、水室、歧管及高强紧固卡扣'],
  ['聚丙烯 (PP)', 910, '通用塑料', '储液罐、膨胀水箱、线束护套及通用护板'],
  ['聚乙烯 (PE)', 950, '通用塑料', '吹塑风道、洗涤壶、管路护套及内衬件'],
  ['聚氯乙烯 (PVC)', 1380, '通用塑料', '线束绝缘护套、密封胶条及排水软管'],
  ['聚苯乙烯 (PS)', 1050, '通用塑料', '透明罩壳、仪表装饰件及绝缘外壳'],
  ['尼龙 (PA)', 1140, '工程塑料', '波纹管、线束扎带、油管卡箍及耐磨件'],
  ['聚甲基丙烯酸甲酯 (PMMA)', 1180, '光学工程塑料', '车灯面罩、导光条、透明视窗及指示灯罩'],
  ['聚碳酸酯 (PC)', 1200, '工程塑料', '车灯透镜、高抗冲透明件及仪表罩'],
  ['聚四氟乙烯 (PTFE)', 2200, '氟塑料', '自润滑轴套、耐腐密封圈及耐高温垫片'],
  ['聚对苯二甲酸乙二酯 (PET)', 1380, '工程塑料', '电机绝缘骨架、线圈支架及耐热薄膜件'],
  ['紧固件用钢材', 7850, '金属材料', '自攻螺钉(ST)、螺栓、螺母、垫圈、卡簧及冲压钢支架'],
  ['铝', 2700, '金属材料', '蒸发器、暖风芯体、冷凝器、空调铝管及压板'],
  ['铜', 8900, '金属材料', '膨胀阀配件、螺纹嵌件、导电端子及铜管件']
];

// 命令行 / 外部指定的自定义密度：作为第 21 行材料追加
if (hasCustomDensity) {
  materials.push(['自定义密度', customDensity, '自定义材料', '命令行/外部指定密度']);
}

// 列 F 默认使用的材料牌号
const defaultMaterial = hasCustomDensity ? '自定义密度' : 'PP-TD20';

const matLastRow = materials.length + 1; // 20 / 21

m.getRange(`A1:D1`).values = [['材料牌号 / 名称', '典型密度 (kg/m³)', '材料类别', '汽车工程典型应用场景说明']];
m.getRange(`A1:D1`).format = {
  fill: '#28465B',
  font: { name: 'Arial', bold: true, color: '#FFFFFF', size: 10 },
  wrapText: true,
  verticalAlignment: 'center'
};
m.getRange(`A1:D1`).format.rowHeight = 28;

m.getRange(`A2:D${matLastRow}`).values = materials;
m.getRange(`A2:D${matLastRow}`).format = {
  font: { name: 'Arial', size: 10, color: '#202C35' },
  verticalAlignment: 'center'
};
m.getRange(`A2:B${matLastRow}`).format.fill = '#FFF2D5';
m.getRange(`B2:B${matLastRow}`).setNumberFormat(hasCustomDensity ? '#,##0.###' : '#,##0');
m.getRange(`A1:D${matLastRow}`).format.borders = { preset: 'all', style: 'thin', color: '#C4CDD4' };

const noteRow = matLastRow + 2;
m.getRange(`A${noteRow}:D${noteRow}`).merge();
m.getRange(`A${noteRow}`).values = [['注：以上密度为汽车工程常用牌号典型参考值（单位：kg/m³）。您可直接修改本表中的材料名称或密度数值，「实体BOM」工作表将自动同步更新计算。']];
m.getRange(`A${noteRow}:D${noteRow}`).format.font = { name: 'Arial', size: 10, italic: true, color: '#6A7D8B' };

m.getRange(`A1:A${noteRow}`).format.columnWidth = 28;
m.getRange(`B1:B${noteRow}`).format.columnWidth = 18;
m.getRange(`C1:C${noteRow}`).format.columnWidth = 22;
m.getRange(`D1:D${noteRow}`).format.columnWidth = 55;
m.freezePanes.freezeRows(1);

// -------------------------------------------------------------
// Sheet 1: 实体BOM (15 Columns: A - O)
//   A 序号 | B 零件 / 实体名称 | C 单台用量 | D 单件体积 (mm³)[隐藏]
//   E 单台份体积 (mm³)[隐藏] | F 材料牌号 | G 材料密度 (kg/m³) | H 单台克重
//   I/J/K 组质心 X/Y/Z (mm) | L/M/N 质量矩 X/Y/Z (g·mm)[隐藏] | O 实体预览图
// -------------------------------------------------------------
s.getRange('A2:O2').merge();
s.getRange('A2').values = [['零部件三维实体重量与质心 BOM 明细表']];
s.getRange('A2:O2').format.font = { name: 'Arial', size: 14, bold: true, color: '#1F3448' };

s.getRange('A3:O3').merge();
s.getRange('A3').values = [['说明：1. 相同几何规格（同体积、同表面积）的标准件与对称件已自动归并汇总；2. 质心坐标基于 CATPart 零件设计坐标系（绝对坐标系）；3. 可在「材料牌号」列下拉切换材质，质量与总成质心自动重算。']];
s.getRange('A3:O3').format.font = { name: 'Arial', size: 10, color: '#526778' };

s.getRange('A5:O5').values = [[
  '序号',
  '零件 / 实体名称',
  '单台用量',
  '单件体积 (mm³)',
  '单台份体积 (mm³)',
  '材料牌号',
  '材料密度 (kg/m³)',
  '单台克重',
  '组质心 X (mm)',
  '组质心 Y (mm)',
  '组质心 Z (mm)',
  '质量矩 X (g·mm)',
  '质量矩 Y (g·mm)',
  '质量矩 Z (g·mm)',
  '实体预览图'
]];

s.getRange('A5:O5').format = {
  fill: '#28465B',
  font: { name: 'Arial', bold: true, color: '#FFFFFF', size: 10 },
  wrapText: true,
  verticalAlignment: 'center'
};
s.getRange('A5:O5').format.rowHeight = 35;

const n = data.length;
for (let i = 0; i < n; i++) {
  const d = data[i];
  const r = i + 6;

  const num = k => {
    const v = d[ix[k]];
    if (v === undefined || v.trim() === '') return null;
    const val = Number(v);
    return Number.isFinite(val) ? round4(val) : null;
  };

  const count = num('quantity');
  const v = num('volume_mm3');
  const tv = num('total_volume_mm3');
  const x = num('cx_mm');
  const y = num('cy_mm');
  const z = num('cz_mm');

  if (!Number.isInteger(count) || count < 1 || [v, tv, x, y, z].some(t => t === null)) {
    throw new Error('Invalid numeric parameters in group ' + d[ix.index]);
  }

  // Row values (15 columns: A through O)
  s.getRange(`A${r}:O${r}`).values = [[
    Number(d[ix.index]),
    d[ix.display_name],
    count,
    v,
    tv,
    defaultMaterial,
    null,
    null,
    x,
    y,
    z,
    null,
    null,
    null,
    null
  ]];

  // Clean engineering formulas (no IF/OR/ROUND clutter)
  s.getRange(`G${r}`).formulas = [[`=VLOOKUP(F${r},'材料密度'!$A$2:$B$${matLastRow},2,FALSE)`]];
  s.getRange(`H${r}`).formulas = [[`=E${r}*G${r}/1000000`]];
  s.getRange(`L${r}`).formulas = [[`=H${r}*I${r}`]];
  s.getRange(`M${r}`).formulas = [[`=H${r}*J${r}`]];
  s.getRange(`N${r}`).formulas = [[`=H${r}*K${r}`]];

  s.getRange(`A${r}:O${r}`).format.rowHeightPx = 116;

  // Preview Image in Column O (0-based col index 14)
  const filename = d[ix.image];
  if (!filename) throw new Error('Missing image filename for group ' + d[ix.index]);
  const imageFilePath = path.resolve(path.dirname(csvPath), filename);
  const bytes = await fs.readFile(imageFilePath);

  s.images.add({
    dataUrl: 'data:image/png;base64,' + bytes.toString('base64'),
    anchor: {
      from: { row: r - 1, col: 14, rowOffsetPx: 6, colOffsetPx: 6 },
      extent: { widthPx: 104, heightPx: 104 }
    }
  });
}

const last = n > 0 ? n + 5 : 5;
const total = last + 1;
const center = last + 2;

if (n > 0) {
  // Material validation dropdown
  s.getRange(`F6:F${last}`).dataValidation = {
    rule: {
      type: 'list',
      formula1: `INDIRECT("'材料密度'!$A$2:$A$${matLastRow}")`
    }
  };

  s.getRange(`A6:O${last}`).format.font = { name: 'Arial', size: 10, color: '#202C35' };
  s.getRange(`A6:O${last}`).format.verticalAlignment = 'center';
  s.getRange(`F6:F${last}`).format.fill = '#FFF2D5';

  s.getRange(`C6:C${last}`).setNumberFormat('#,##0');
  s.getRange(`D6:E${last}`).setNumberFormat('#,##0.0');
  s.getRange(`G6:G${last}`).setNumberFormat('#,##0');
  s.getRange(`H6:N${last}`).setNumberFormat('#,##0.00');

  s.freezePanes.freezeRows(5);
}

// Total summary row
s.getRange(`B${total}`).values = [['合计']];
if (n > 0) {
  for (const c of ['C', 'E', 'H', 'L', 'M', 'N']) {
    s.getRange(`${c}${total}`).formulas = [[`=SUM(${c}6:${c}${last})`]];
  }
  s.getRange(`C${total}`).setNumberFormat('#,##0');
} else {
  s.getRange(`C${total}`).values = [[0]];
}

// Center of gravity summary row (prominent, highlighted)
s.getRange(`B${center}`).values = [['核算总成质心坐标 (mm)']];
s.getRange(`H${center}`).values = [['X / Y / Z :']];
if (n > 0) {
  for (const [col, mcol] of [['I', 'L'], ['J', 'M'], ['K', 'N']]) {
    s.getRange(`${col}${center}`).formulas = [[`=${mcol}${total}/H${total}`]];
  }
}

s.getRange(`A${total}:O${center}`).format = {
  font: { name: 'Arial', size: 10, bold: true, color: '#202C35' },
  fill: '#EDF3F6',
  verticalAlignment: 'center'
};
s.getRange(`A${total}:O${center}`).format.rowHeight = 25;
s.getRange(`E${total}:N${center}`).setNumberFormat('#,##0.00');
s.getRange(`A5:O${center}`).format.borders = { preset: 'all', style: 'thin', color: '#C4CDD4' };

// 突出显示「核算总成质心坐标 (mm)」行：独立高亮底色 + 加粗 + 行高 28
s.getRange(`A${center}:O${center}`).format = {
  fill: '#D9E8F5',
  font: { name: 'Arial', size: 11, bold: true, color: '#123A5C' },
  verticalAlignment: 'center',
  horizontalAlignment: 'center'
};
s.getRange(`A${center}:O${center}`).format.rowHeight = 28;
s.getRange(`B${center}`).format.horizontalAlignment = 'left';
s.getRange(`H${center}`).format.horizontalAlignment = 'right';
s.getRange(`I${center}:K${center}`).setNumberFormat('#,##0.00');
s.getRange(`I${center}:K${center}`).format.font = { name: 'Arial', size: 11, bold: true, color: '#0B4A7A' };
s.getRange(`A${center}:O${center}`).format.borders = { preset: 'all', style: 'thin', color: '#8FAFC7' };

// -------------------------------------------------------------
// Sheet 3: 微小几何体筛除记录 (Created only if tiny bodies exist)
// -------------------------------------------------------------
const instancesPath = path.join(path.dirname(csvPath), path.basename(csvPath).replace(/^Entity_Analysis_/, 'Instances_'));
const instanceRows = parseCsv(await fs.readFile(instancesPath, 'utf8'));
const ih = instanceRows.shift();
const ii = Object.fromEntries(ih.map((x, i) => [x, i]));

for (const k of ['index', 'group_index', 'display_name', 'feature_name', 'tag', 'area_mm2', 'volume_mm3', 'cx_mm', 'cy_mm', 'cz_mm', 'status']) {
  if (!(k in ii)) throw new Error('Missing instance field: ' + k);
}

const ar = instanceRows.filter(d => d.length > 1);
const tiny = ar.filter(d => d[ii.status] === 'tiny_volume');
const emptyBodies = ar.filter(d => d[ii.status] === 'empty_body');
const anomalies = ar.filter(d => !['ok', 'tiny_volume'].includes(d[ii.status]));
const invalid = ar.filter(d => !VALID_INSTANCE_STATUSES.includes(d[ii.status]));
if (invalid.length) {
  throw new Error('Invalid instance status detected in instances file: ' + invalid[0][ii.status]);
}

if (ar.length !== tiny.length + emptyBodies.length +
    data.reduce((a, d) => a + Number(d[ix.quantity]), 0)) {
  throw new Error('Instance counts do not match group quantities');
}

const toNum = (d, k) => {
  const v = d[ii[k]];
  if (v === undefined || v.trim() === '') return null;
  const numVal = Number(v);
  return Number.isFinite(numVal) ? round4(numVal) : null;
};

let audit = null;
let av = [];
let warnSheet = null;
let wv = [];
if (tiny.length > 0) {
  audit = wb.worksheets.add('微小几何体筛除记录');
  audit.showGridLines = false;
  audit.getRange('A1:K1').values = [[
    '实例序号',
    '实体名称',
    '特征名称',
    '体标签',
    '体积 (mm³)',
    '质心 X (mm)',
    '质心 Y (mm)',
    '质心 Z (mm)',
    '筛除原因',
    '工程处理说明',
    '精确测量体积 (mm³)'
  ]];
  audit.getRange('A1:K1').format = {
    fill: '#28465B',
    font: { name: 'Arial', bold: true, color: '#FFFFFF', size: 10 },
    wrapText: true,
    verticalAlignment: 'center'
  };
  audit.getRange('A1:K1').format.rowHeight = 32;

  av = tiny.map(d => [
    toNum(d, 'index'),
    d[ii.display_name],
    d[ii.feature_name],
    toNum(d, 'tag'),
    toNum(d, 'volume_mm3'),
    toNum(d, 'cx_mm'),
    toNum(d, 'cy_mm'),
    toNum(d, 'cz_mm'),
    '体积小于 1.0 mm³',
    '微小几何元素（如建模碎屑或参考微元），已自动从正式 BOM 与总质量测算中剔除',
    d[ii.volume_mm3]
  ]);

  audit.getRange(`A2:K${tiny.length + 1}`).values = av;
  audit.getRange(`A2:K${tiny.length + 1}`).format.font = { name: 'Arial', size: 10, color: '#202C35' };
  audit.getRange(`A2:K${tiny.length + 1}`).format.verticalAlignment = 'center';
  audit.getRange(`E2:H${tiny.length + 1}`).setNumberFormat('#,##0.00');
  audit.getRange(`A1:K${tiny.length + 1}`).format.borders = { preset: 'all', style: 'thin', color: '#D9E0E5' };
  audit.freezePanes.freezeRows(1);
}

// -------------------------------------------------------------
// Sheet 4: 异常与警告明细 (Created only if any non-ok instance exists)
//   empty_body / area_warning / render_warning / inertia_warning ...
// -------------------------------------------------------------
if (anomalies.length > 0) {
  warnSheet = wb.worksheets.add('异常与警告明细');
  warnSheet.showGridLines = false;
  warnSheet.getRange('A1:F1').values = [[
    '实例序号',
    '实体名称',
    '特征名称',
    '体标签',
    '异常状态',
    '说明'
  ]];
  warnSheet.getRange('A1:F1').format = {
    fill: '#28465B',
    font: { name: 'Arial', bold: true, color: '#FFFFFF', size: 10 },
    wrapText: true,
    verticalAlignment: 'center'
  };
  warnSheet.getRange('A1:F1').format.rowHeight = 32;

  wv = anomalies.map(d => [
    toNum(d, 'index'),
    d[ii.display_name],
    d[ii.feature_name],
    toNum(d, 'tag'),
    statusLabelOf(d[ii.status]),
    statusNoteOf(d[ii.status])
  ]);

  warnSheet.getRange(`A2:F${anomalies.length + 1}`).values = wv;
  warnSheet.getRange(`A2:F${anomalies.length + 1}`).format.font = { name: 'Arial', size: 10, color: '#202C35' };
  warnSheet.getRange(`A2:F${anomalies.length + 1}`).format.verticalAlignment = 'center';
  warnSheet.getRange(`A2:A${anomalies.length + 1}`).setNumberFormat('#,##0');
  warnSheet.getRange(`D2:D${anomalies.length + 1}`).setNumberFormat('#,##0');
  warnSheet.getRange(`A1:F${anomalies.length + 1}`).format.borders = { preset: 'all', style: 'thin', color: '#D9E0E5' };
  warnSheet.freezePanes.freezeRows(1);
}

// -------------------------------------------------------------
// Recalculate Formulas before calculating auto-column widths
// -------------------------------------------------------------
wb.recalculate();

function letter(i) {
  let t = '';
  for (i++; i > 0; i = Math.floor((i - 1) / 26)) {
    t = String.fromCharCode(65 + ((i - 1) % 26)) + t;
  }
  return t;
}

function widthFor(v) {
  if (v === null || v === undefined) return 0;
  const t = typeof v === 'number'
    ? v.toLocaleString('en-US', { minimumFractionDigits: 1, maximumFractionDigits: 4 })
    : String(v);
  let w = 0;
  for (const c of t) {
    w += c.codePointAt(0) > 255 ? 2.1 : 1.1;
  }
  return w;
}

// Set explicit, well-proportioned column widths for 《实体BOM》
const bomMinColWidths = [8, 30, 10, 18, 20, 22, 18, 18, 16, 16, 16, 20, 20, 20, 18];
const bomMaxColWidths = [12, 55, 14, 25, 25, 30, 22, 22, 20, 20, 20, 25, 25, 25, 18];

const bomValues = s.getRange(`A5:O${center}`).values;
for (let c = 0; c < 15; c++) {
  const colLetter = letter(c);
  if (c === 14) {
    // Fixed width for image preview column
    s.getRange(`${colLetter}1:${colLetter}${center}`).format.columnWidth = 18;
    continue;
  }
  let maxW = 0;
  for (const row of bomValues) {
    if (row && row[c] !== undefined) {
      maxW = Math.max(maxW, widthFor(row[c]));
    }
  }
  const fitted = Math.max(bomMinColWidths[c], Math.min(bomMaxColWidths[c], Math.ceil(maxW + 4)));
  s.getRange(`${colLetter}1:${colLetter}${center}`).format.columnWidth = fitted;
}

// -------------------------------------------------------------
// Hide derived / auxiliary columns on 《实体BOM》
//   D (idx 3)  单件体积
//   E (idx 4)  单台份体积
//   L (idx 11) 质量矩 X
//   M (idx 12) 质量矩 Y
//   N (idx 13) 质量矩 Z
// -------------------------------------------------------------
const HIDDEN_COL_INDEXES = [3, 4, 11, 12, 13];
for (const hiddenColIndex of HIDDEN_COL_INDEXES) {
  const colObj = s.__getOrCreateSingleColumn(hiddenColIndex);
  colObj.hidden = true;
}

// Column widths for 《微小几何体筛除记录》 if created
if (audit) {
  const auditMinWidths = [10, 28, 20, 14, 16, 16, 16, 16, 20, 48, 24];
  const auditRows = [audit.getRange('A1:K1').values[0], ...av];
  for (let c = 0; c < 11; c++) {
    const colLetter = letter(c);
    let maxW = 0;
    for (const row of auditRows) {
      if (row && row[c] !== undefined) {
        maxW = Math.max(maxW, widthFor(row[c]));
      }
    }
    const fitted = Math.max(auditMinWidths[c], Math.min(70, Math.ceil(maxW + 4)));
    audit.getRange(`${colLetter}1:${colLetter}${tiny.length + 1}`).format.columnWidth = fitted;
  }
}

// Column widths for 《异常与警告明细》 if created
if (warnSheet) {
  const warnMinWidths = [10, 28, 20, 14, 24, 70];
  const warnRows = [warnSheet.getRange('A1:F1').values[0], ...wv];
  for (let c = 0; c < 6; c++) {
    const colLetter = letter(c);
    let maxW = 0;
    for (const row of warnRows) {
      if (row && row[c] !== undefined) {
        maxW = Math.max(maxW, widthFor(row[c]));
      }
    }
    const fitted = Math.max(warnMinWidths[c], Math.min(80, Math.ceil(maxW + 4)));
    warnSheet.getRange(`${colLetter}1:${colLetter}${anomalies.length + 1}`).format.columnWidth = fitted;
  }
}

// -------------------------------------------------------------
// Verify workbook and save
// -------------------------------------------------------------
const inspectResult = await wb.inspect({
  kind: 'match',
  searchTerm: '#REF!|#DIV/0!|#VALUE!|#NAME\\?|#N/A|#NUM!',
  options: { useRegex: true, maxResults: 50 }
});
if (inspectResult && inspectResult.ndjson) {
  console.log('Formula Inspection:', inspectResult.ndjson);
}

await fs.mkdir(path.dirname(outputPath), { recursive: true });
await (await SpreadsheetFile.exportXlsx(wb)).save(outputPath);
console.log('SAVED ' + outputPath);