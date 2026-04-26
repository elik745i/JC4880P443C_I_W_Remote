const SVG_NS = "http://www.w3.org/2000/svg";

const TARGETS = {
  bleController: { label: "BLE Controller", symbol: "kBleCalibrationLayout" },
  localController: { label: "Local Controller", symbol: "kLocalControllerLayout" },
};

const state = {
  layoutDocument: null,
  selectedTarget: "bleController",
  selectedKey: null,
  pointerDrag: null,
  previewPressKey: null,
  shapeEditMode: false,
  shapeHandleDrag: null,
  shapeSelectedPointIndex: null,
  zoomPercent: 100,
  dimensionsLinked: true,
  copiedShape: null,
};

const overlayLayer = document.getElementById("overlayLayer");
const previewFrame = document.getElementById("previewFrame");
const deviceCanvas = document.getElementById("deviceCanvas");
const canvasViewport = document.getElementById("canvasViewport");
const canvasZoomLayer = document.getElementById("canvasZoomLayer");
const controllerImage = document.getElementById("controllerImage");
const statusNode = document.getElementById("status");
const emptySelection = document.getElementById("emptySelection");
const inspectorForm = document.getElementById("inspectorForm");
const shapeEditor = document.getElementById("shapeEditor");
const shapeEditorGroup = document.querySelector(".shape-editor-group");
const layoutTargetSelect = document.getElementById("layoutTargetSelect");
const togglePointEditButton = document.getElementById("togglePointEditButton");
const deletePointButton = document.getElementById("deletePointButton");
const resetShapeButton = document.getElementById("resetShapeButton");
const refreshCodeButton = document.getElementById("refreshCodeButton");
const backgroundFile = document.getElementById("backgroundFile");
const backgroundUploadButton = document.getElementById("backgroundUploadButton");
const zoomRange = document.getElementById("zoomRange");
const zoomValue = document.getElementById("zoomValue");
const addButtonSelect = document.getElementById("addButtonSelect");
const addButtonButton = document.getElementById("addButtonButton");
const deleteButtonButton = document.getElementById("deleteButtonButton");
const dimensionLinkButton = document.getElementById("dimensionLinkButton");
const copyShapeButton = document.getElementById("copyShapeButton");
const pasteShapeButton = document.getElementById("pasteShapeButton");

const fields = {
  key: document.getElementById("fieldKey"),
  x: document.getElementById("fieldX"),
  xLabel: document.getElementById("fieldXLabelText"),
  y: document.getElementById("fieldY"),
  yLabel: document.getElementById("fieldYLabelText"),
  w: document.getElementById("fieldW"),
  wLabel: document.getElementById("fieldWLabelText"),
  h: document.getElementById("fieldH"),
  hLabel: document.getElementById("fieldHLabelText"),
  hWrapper: document.getElementById("fieldHWrapper"),
  rotation: document.getElementById("fieldRotation"),
  label: document.getElementById("fieldLabel"),
  textSize: document.getElementById("fieldTextSize"),
  textStyle: document.getElementById("fieldTextStyle"),
  textColor: document.getElementById("fieldTextColor"),
  fillColor: document.getElementById("fieldFillColor"),
  borderColor: document.getElementById("fieldBorderColor"),
  borderWidth: document.getElementById("fieldBorderWidth"),
  shapeType: document.getElementById("fieldShapeType"),
  cornerRadius: document.getElementById("fieldCornerRadius"),
  cornerRadiusWrapper: document.getElementById("fieldCornerRadiusWrapper"),
  cornerDiameter: document.getElementById("fieldCornerDiameter"),
  cornerDiameterWrapper: document.getElementById("fieldCornerDiameterWrapper"),
  functionType: document.getElementById("fieldFunctionType"),
  analogLevel: document.getElementById("fieldAnalogLevel"),
  analogLevelWrapper: document.getElementById("fieldAnalogLevelWrapper"),
  dpadX: document.getElementById("fieldDpadX"),
  dpadXWrapper: document.getElementById("fieldDpadXWrapper"),
  dpadY: document.getElementById("fieldDpadY"),
  dpadYWrapper: document.getElementById("fieldDpadYWrapper"),
};

const collectionSpecs = [
  { name: "triggerBars", mode: "rect", defaultShape: "roundedRect" },
  { name: "shoulders", mode: "rect", defaultShape: "roundedRect" },
  { name: "sticks", mode: "square", defaultShape: "circle" },
  { name: "dpadButtons", mode: "circle", defaultShape: "circle" },
  { name: "faceButtons", mode: "circle", defaultShape: "circle" },
];

function cloneJson(value) {
  return JSON.parse(JSON.stringify(value));
}

function clamp(value, min, max) {
  return Math.min(max, Math.max(min, value));
}

function currentZoomScale() {
  return state.zoomPercent / 100;
}

function isItemEnabled(item) {
  return item?.visual?.enabled !== false;
}

function setStatus(message) {
  statusNode.textContent = message;
}

function numericFieldValue(field, fallback = 0) {
  const value = Number(field?.value);
  return Number.isFinite(value) ? value : fallback;
}

function refreshBackgroundPreview() {
  controllerImage.src = `/controller.png?ts=${Date.now()}`;
}

function activeLayout() {
  return state.layoutDocument.layouts[state.selectedTarget];
}

function itemUsesIndependentHeight(item, spec) {
  return spec.mode !== "rect" ? true : true;
}

function getItemRotation(item) {
  return Number(item.visual?.shape?.rotationDegrees ?? 0);
}

function setItemRotation(item, rotationDegrees) {
  item.visual.shape.rotationDegrees = Number(rotationDegrees) || 0;
}

function updateDimensionLinkButton() {
  dimensionLinkButton.dataset.linked = String(state.dimensionsLinked);
  dimensionLinkButton.setAttribute("aria-pressed", state.dimensionsLinked ? "true" : "false");
  const label = state.dimensionsLinked
    ? "Keep width and height linked"
    : "Allow width and height to change independently";
  dimensionLinkButton.setAttribute("aria-label", label);
  dimensionLinkButton.setAttribute("title", label);
}

function updateShapeClipboardButtons() {
  const hasSelection = Boolean(state.selectedKey);
  copyShapeButton.disabled = !hasSelection;
  pasteShapeButton.disabled = !hasSelection || !state.copiedShape;
}

function cloneShapeClipboard(item) {
  return {
    fillColor: item.visual.fillColor,
    borderColor: item.visual.borderColor,
    borderWidth: item.visual.borderWidth,
    rotationDegrees: getItemRotation(item),
    shape: cloneJson(item.visual.shape),
  };
}

function copySelectedShape() {
  if (!state.selectedKey) {
    return;
  }
  const { item } = getItemMeta(state.selectedKey);
  state.copiedShape = cloneShapeClipboard(item);
  updateShapeClipboardButtons();
  setStatus(`Copied shape from ${itemDisplayName(item)}.`);
}

function pasteCopiedShape() {
  if (!state.selectedKey || !state.copiedShape) {
    return;
  }
  const { item, spec } = getItemMeta(state.selectedKey);
  item.visual.fillColor = state.copiedShape.fillColor;
  item.visual.borderColor = state.copiedShape.borderColor;
  item.visual.borderWidth = state.copiedShape.borderWidth;
  item.visual.shape = cloneJson(state.copiedShape.shape);
  item.visual.shape.type = normalizeShapeType(item.visual.shape.type, spec);
  setItemRotation(item, state.copiedShape.rotationDegrees);
  if (rawShapeType(item, spec) === "custom") {
    ensureCustomPoints(item, spec);
  }
  render();
  setStatus(`Pasted shape onto ${itemDisplayName(item)}.`);
}

function updateDeletePointButton(item = null) {
  const pointCount = item?.visual?.shape?.points?.length ?? 0;
  deletePointButton.disabled = !state.shapeEditMode || state.shapeSelectedPointIndex == null || pointCount <= 3;
}

function deleteSelectedPoint() {
  if (!state.selectedKey || state.shapeSelectedPointIndex == null) {
    return;
  }
  const { item, spec } = getItemMeta(state.selectedKey);
  if (rawShapeType(item, spec) !== "custom") {
    return;
  }
  if (item.visual.shape.points.length <= 3) {
    setStatus("Custom shapes need at least three points.");
    updateDeletePointButton(item);
    return;
  }
  item.visual.shape.points.splice(state.shapeSelectedPointIndex, 1);
  state.shapeSelectedPointIndex = null;
  render();
  setStatus(`Deleted point from ${itemDisplayName(item)}.`);
}

function itemDisplayName(item) {
  return item.visual?.label || item.id;
}

function disabledItemsForActiveLayout() {
  const layout = activeLayout();
  const items = [];
  for (const spec of collectionSpecs) {
    layout[spec.name].forEach((item, index) => {
      if (!isItemEnabled(item)) {
        items.push({ key: itemKey(spec.name, index), item, spec });
      }
    });
  }
  return items;
}

function rawShapeType(item, spec) {
  return normalizeShapeType(item.visual.shape.type, spec);
}

function shapeRadiusThreshold(item, spec) {
  const width = getItemWidth(item, spec);
  const height = getItemHeight(item, spec);
  return Math.max(0, Math.min(width, height) / 2);
}

function effectiveShapeType(item, spec) {
  const shapeType = rawShapeType(item, spec);
  if (shapeType !== "roundedRect") {
    return shapeType;
  }

  const threshold = shapeRadiusThreshold(item, spec);
  if (threshold > 0 && item.visual.shape.cornerRadius >= threshold) {
    const width = getItemWidth(item, spec);
    const height = getItemHeight(item, spec);
    return Math.abs(width - height) <= 1 ? "circle" : "capsule";
  }

  return shapeType;
}

function syncShapeToSelection(item, spec, selectedShapeType) {
  const nextShapeType = normalizeShapeType(selectedShapeType, spec);
  const previousEffectiveShape = effectiveShapeType(item, spec);
  const threshold = shapeRadiusThreshold(item, spec);

  item.visual.shape.type = nextShapeType;

  if (nextShapeType === "roundedRect") {
    if (previousEffectiveShape === "capsule" || previousEffectiveShape === "circle" || item.visual.shape.cornerRadius >= threshold) {
      item.visual.shape.cornerRadius = clamp(Math.round(threshold * 0.4) || 18, 0, Math.max(18, Math.floor(threshold)));
    }
    return;
  }

  if (nextShapeType === "capsule" || nextShapeType === "circle") {
    item.visual.shape.cornerRadius = Math.max(item.visual.shape.cornerRadius, threshold || 999);
    return;
  }

  if (nextShapeType === "custom") {
    ensureCustomPoints(item, spec);
    return;
  }

  if (item.visual.shape.cornerRadius > 48) {
    item.visual.shape.cornerRadius = 18;
  }
}

function getItemWidth(item, spec) {
  if (spec.mode === "circle") {
    return Number(item.width ?? item.size ?? 0);
  }
  if (spec.mode === "square") {
    return Number(item.width ?? item.size ?? 0);
  }
  return Number(item.width ?? 0);
}

function getItemHeight(item, spec) {
  if (spec.mode === "circle") {
    return Number(item.height ?? item.size ?? 0);
  }
  if (spec.mode === "square") {
    return Number(item.height ?? item.size ?? 0);
  }
  return Number(item.height ?? 0);
}

function setItemDimensions(item, spec, width, height) {
  const nextWidth = Number(width);
  const nextHeight = Number(height);
  item.width = nextWidth;
  item.height = nextHeight;

  if (spec.mode === "circle" || spec.mode === "square") {
    item.size = Math.round((nextWidth + nextHeight) / 2);
  }
}

function shapeUsesCornerRadius(shapeType) {
  return ["roundedRect", "triangle", "star", "pentagon", "custom"].includes(shapeType);
}

function isCircleFamily(shapeType) {
  return shapeType === "circle";
}

function scaleX(value) {
  return (value * activeLayout().previewFrame.width) / activeLayout().controllerSource.width;
}

function scaleY(value) {
  return (value * activeLayout().previewFrame.height) / activeLayout().controllerSource.height;
}

function unscaleX(value) {
  return Math.round(((value / currentZoomScale()) * activeLayout().controllerSource.width) / activeLayout().previewFrame.width);
}

function unscaleY(value) {
  return Math.round(((value / currentZoomScale()) * activeLayout().controllerSource.height) / activeLayout().previewFrame.height);
}

function scaleText(value) {
  return Math.max(10, Math.round((value * activeLayout().previewFrame.width) / activeLayout().controllerSource.width));
}

function svg(tagName, attrs = {}) {
  const node = document.createElementNS(SVG_NS, tagName);
  Object.entries(attrs).forEach(([key, value]) => node.setAttribute(key, value));
  return node;
}

function itemKey(collection, index) {
  return `${collection}:${index}`;
}

function fontStyleCss(textStyle) {
  return {
    fontWeight: textStyle.includes("bold") ? "700" : "400",
    fontStyle: textStyle.includes("italic") ? "italic" : "normal",
  };
}

function getItemMeta(key) {
  const [collectionName, rawIndex] = key.split(":");
  const index = Number(rawIndex);
  const spec = collectionSpecs.find((entry) => entry.name === collectionName);
  const item = activeLayout()[collectionName][index];
  return { collectionName, index, spec, item };
}

function normalizeShapeType(shapeType, spec) {
  if (shapeType === "ellipse") {
    return "circle";
  }
  if (shapeType === "polygon") {
    return "custom";
  }
  if (shapeType === "roundedTriangle") {
    return "triangle";
  }
  if (shapeType === "pentagonalBase") {
    return "pentagon";
  }
  return shapeType || spec.defaultShape;
}

function defaultFunctionType(spec) {
  if (spec.name === "triggerBars") {
    return "analog";
  }
  if (spec.name === "dpadButtons") {
    return "dpad";
  }
  return "tactile";
}

function createDefaultVisual(spec, item) {
  const fillColor = item.color || "#E2E8F0";
  const borderColor = item.color || "#94A3B8";
  return {
    enabled: true,
    label: item.id,
    fillColor,
    borderColor,
    borderWidth: 2,
    textColor: fillColor === "#E2E8F0" ? "#0F172A" : "#FFFFFF",
    textSize: spec.mode === "rect" ? 14 : 16,
    textStyle: "bold",
    functionType: defaultFunctionType(spec),
    preview: {
      analogLevel: spec.name === "triggerBars" ? 72 : 50,
      dpadX: 0,
      dpadY: 0,
    },
    shape: {
      type: spec.defaultShape,
      cornerRadius: spec.defaultShape === "roundedRect" ? 18 : 999,
      points: [],
    },
  };
}

function defaultPolygonPoints(spec) {
  if (spec.mode === "circle") {
    return [
      { x: 50, y: 2 },
      { x: 78, y: 10 },
      { x: 96, y: 50 },
      { x: 78, y: 90 },
      { x: 50, y: 98 },
      { x: 22, y: 90 },
      { x: 4, y: 50 },
      { x: 22, y: 10 },
    ];
  }
  return [
    { x: 8, y: 8 },
    { x: 92, y: 8 },
    { x: 92, y: 92 },
    { x: 8, y: 92 },
  ];
}

function presetPoints(shapeType) {
  if (shapeType === "triangle") {
    return [
      { x: 50, y: 6 },
      { x: 92, y: 84 },
      { x: 8, y: 84 },
    ];
  }
  if (shapeType === "star") {
    return [
      { x: 50, y: 3 },
      { x: 61, y: 34 },
      { x: 95, y: 35 },
      { x: 68, y: 55 },
      { x: 79, y: 89 },
      { x: 50, y: 69 },
      { x: 21, y: 89 },
      { x: 32, y: 55 },
      { x: 5, y: 35 },
      { x: 39, y: 34 },
    ];
  }
  if (shapeType === "pentagon") {
    return [
      { x: 50, y: 4 },
      { x: 92, y: 35 },
      { x: 76, y: 88 },
      { x: 24, y: 88 },
      { x: 8, y: 35 },
    ];
  }
  return [];
}

function ensureCustomPoints(item, spec) {
  if (item.visual.shape.points.length === 0) {
    item.visual.shape.points = defaultPolygonPoints(spec);
  }
}

function normalizeSingleLayout(layout) {
  for (const spec of collectionSpecs) {
    layout[spec.name].forEach((item) => {
      item.visual = item.visual || createDefaultVisual(spec, item);
      item.visual.enabled = item.visual.enabled !== false;
      item.visual.label = item.visual.label || item.id;
      item.visual.fillColor = item.visual.fillColor || item.color || "#E2E8F0";
      item.visual.borderColor = item.visual.borderColor || item.color || "#94A3B8";
      item.visual.borderWidth = Number(item.visual.borderWidth ?? 2);
      item.visual.textColor = item.visual.textColor || "#0F172A";
      item.visual.textSize = Number(item.visual.textSize ?? 16);
      item.visual.textStyle = item.visual.textStyle || "bold";
      item.visual.functionType = item.visual.functionType || defaultFunctionType(spec);
      item.visual.preview = item.visual.preview || {};
      item.visual.preview.analogLevel = Number(item.visual.preview.analogLevel ?? (spec.name === "triggerBars" ? 72 : 50));
      item.visual.preview.dpadX = Number(item.visual.preview.dpadX ?? 0);
      item.visual.preview.dpadY = Number(item.visual.preview.dpadY ?? 0);
      item.visual.shape = item.visual.shape || {
        type: spec.defaultShape,
        cornerRadius: spec.defaultShape === "roundedRect" ? 18 : 999,
        rotationDegrees: 0,
        points: [],
      };
      item.visual.shape.type = normalizeShapeType(item.visual.shape.type, spec);
      item.visual.shape.cornerRadius = Number(item.visual.shape.cornerRadius ?? (item.visual.shape.type === "roundedRect" ? 18 : 999));
      item.visual.shape.rotationDegrees = Number(item.visual.shape.rotationDegrees ?? 0);
      item.visual.shape.points = Array.isArray(item.visual.shape.points) ? item.visual.shape.points : [];
      if (spec.mode === "circle") {
        item.width = Number(item.width ?? item.size ?? 0);
        item.height = Number(item.height ?? item.size ?? item.width ?? 0);
      }
      if (spec.mode === "square") {
        item.width = Number(item.width ?? item.size ?? 0);
        item.height = Number(item.height ?? item.size ?? item.width ?? 0);
      }
      if (item.visual.shape.type === "custom") {
        ensureCustomPoints(item, spec);
      }
    });
  }
  return layout;
}

function normalizeLayoutDocument(layoutDocument) {
  if (!layoutDocument.layouts) {
    const baseLayout = normalizeSingleLayout(cloneJson(layoutDocument));
    return {
      selectedTarget: "bleController",
      layouts: {
        bleController: baseLayout,
        localController: cloneJson(baseLayout),
      },
    };
  }

  const documentCopy = cloneJson(layoutDocument);
  if (!documentCopy.layouts.bleController) {
    documentCopy.layouts.bleController = cloneJson(Object.values(documentCopy.layouts)[0]);
  }
  if (!documentCopy.layouts.localController) {
    documentCopy.layouts.localController = cloneJson(documentCopy.layouts.bleController);
  }
  documentCopy.layouts.bleController = normalizeSingleLayout(documentCopy.layouts.bleController);
  documentCopy.layouts.localController = normalizeSingleLayout(documentCopy.layouts.localController);
  documentCopy.selectedTarget = documentCopy.selectedTarget || "bleController";
  return documentCopy;
}

function geometryForItem(item, spec) {
  if (spec.mode === "circle") {
    const width = scaleX(getItemWidth(item, spec));
    const height = scaleY(getItemHeight(item, spec));
    return {
      left: scaleX(item.centerX) - width / 2,
      top: scaleY(item.centerY) - height / 2,
      width,
      height,
    };
  }
  if (spec.mode === "square") {
    const width = scaleX(getItemWidth(item, spec));
    const height = scaleY(getItemHeight(item, spec));
    return {
      left: scaleX(item.x),
      top: scaleY(item.y),
      width,
      height,
    };
  }
  return {
    left: scaleX(item.x),
    top: scaleY(item.y),
    width: scaleX(item.width),
    height: scaleY(item.height),
  };
}

function smoothClosedPath(points) {
  if (points.length === 0) {
    return "";
  }
  if (points.length < 3) {
    return `M ${points.map((point) => `${point.x} ${point.y}`).join(" L ")} Z`;
  }
  let path = `M ${(points[0].x + points[1].x) / 2} ${(points[0].y + points[1].y) / 2}`;
  for (let index = 0; index < points.length; index += 1) {
    const current = points[index];
    const next = points[(index + 1) % points.length];
    const midX = (current.x + next.x) / 2;
    const midY = (current.y + next.y) / 2;
    path += ` Q ${current.x} ${current.y} ${midX} ${midY}`;
  }
  return `${path} Z`;
}

function straightClosedPath(points) {
  if (points.length === 0) {
    return "";
  }
  return `M ${points[0].x} ${points[0].y} ${points.slice(1).map((point) => `L ${point.x} ${point.y}`).join(" ")} Z`;
}

function roundedClosedPath(points, radius) {
  if (points.length === 0) {
    return "";
  }
  if (points.length < 3 || radius <= 0) {
    return straightClosedPath(points);
  }

  const segments = points.map((point, index) => {
    const previous = points[(index - 1 + points.length) % points.length];
    const next = points[(index + 1) % points.length];
    const inDx = previous.x - point.x;
    const inDy = previous.y - point.y;
    const outDx = next.x - point.x;
    const outDy = next.y - point.y;
    const inLength = Math.hypot(inDx, inDy) || 1;
    const outLength = Math.hypot(outDx, outDy) || 1;
    const cut = Math.min(radius, inLength / 2, outLength / 2);
    return {
      start: {
        x: point.x + (inDx / inLength) * cut,
        y: point.y + (inDy / inLength) * cut,
      },
      corner: point,
      end: {
        x: point.x + (outDx / outLength) * cut,
        y: point.y + (outDy / outLength) * cut,
      },
    };
  });

  let path = `M ${segments[0].start.x} ${segments[0].start.y}`;
  segments.forEach((segment, index) => {
    const nextSegment = segments[(index + 1) % segments.length];
    path += ` Q ${segment.corner.x} ${segment.corner.y} ${segment.end.x} ${segment.end.y}`;
    path += ` L ${nextSegment.start.x} ${nextSegment.start.y}`;
  });
  return `${path} Z`;
}

function colorToRgb(color) {
  const normalized = color.replace("#", "");
  const hex = normalized.length === 3
    ? normalized.split("").map((value) => value + value).join("")
    : normalized;
  return {
    r: Number.parseInt(hex.slice(0, 2), 16),
    g: Number.parseInt(hex.slice(2, 4), 16),
    b: Number.parseInt(hex.slice(4, 6), 16),
  };
}

function rgbToColor({ r, g, b }) {
  return `#${[r, g, b].map((value) => clamp(Math.round(value), 0, 255).toString(16).padStart(2, "0")).join("")}`;
}

function mixColor(color, target, amount) {
  const fromRgb = colorToRgb(color);
  const toRgb = colorToRgb(target);
  return rgbToColor({
    r: fromRgb.r + ((toRgb.r - fromRgb.r) * amount),
    g: fromRgb.g + ((toRgb.g - fromRgb.g) * amount),
    b: fromRgb.b + ((toRgb.b - fromRgb.b) * amount),
  });
}

function pressedFillColor(color) {
  return mixColor(color, "#0f172a", 0.28);
}

function createGeometryNode(item, spec, options = {}) {
  const shapeType = effectiveShapeType(item, spec);
  const fill = options.fill ?? item.visual.fillColor;
  const stroke = options.stroke ?? item.visual.borderColor;
  const borderWidth = String(options.borderWidth ?? item.visual.borderWidth);
  const common = {
    fill,
    stroke,
    "stroke-width": borderWidth,
    "vector-effect": "non-scaling-stroke",
    class: options.className ?? "shape-outline",
  };

  if (shapeType === "circle") {
    return svg("ellipse", {
      cx: "50",
      cy: "50",
      rx: "49",
      ry: "49",
      ...common,
    });
  }

  if (shapeType === "roundedRect") {
    const cornerRadius = clamp(item.visual.shape.cornerRadius, 0, 49);
    return svg("rect", {
      x: "1",
      y: "1",
      width: "98",
      height: "98",
      rx: String(cornerRadius),
      ry: String(cornerRadius),
      ...common,
    });
  }

  if (shapeType === "capsule") {
    return svg("rect", {
      x: "1",
      y: "1",
      width: "98",
      height: "98",
      rx: "49",
      ry: "49",
      ...common,
    });
  }

  const points = shapeType === "custom"
    ? (item.visual.shape.points.length ? item.visual.shape.points : defaultPolygonPoints(spec))
    : presetPoints(shapeType);
  const pathBuilder = shapeUsesCornerRadius(shapeType)
    ? (shapePoints) => roundedClosedPath(shapePoints, clamp(item.visual.shape.cornerRadius, 0, 48))
    : straightClosedPath;
  return svg("path", {
    d: pathBuilder(points),
    ...common,
  });
}

function appendAnalogPreview(overlaySvg, item, spec, key) {
  const fillId = `analog-${state.selectedTarget}-${key.replace(/[^a-z0-9_-]/gi, "-")}`;
  const analogLevel = clamp(Number(item.visual.preview.analogLevel ?? 50), 0, 100);
  const defs = svg("defs");
  const clipPath = svg("clipPath", { id: fillId });
  clipPath.appendChild(createGeometryNode(item, spec, {
    fill: "#ffffff",
    stroke: "none",
    borderWidth: 0,
  }));
  defs.appendChild(clipPath);
  overlaySvg.appendChild(defs);

  overlaySvg.appendChild(createGeometryNode(item, spec, {
    fill: mixColor(item.visual.fillColor, "#ffffff", 0.55),
    stroke: item.visual.borderColor,
  }));

  const fillRect = svg("rect", {
    x: "0",
    y: String(100 - analogLevel),
    width: "100",
    height: String(analogLevel),
    fill: item.visual.fillColor,
    opacity: "0.95",
    "clip-path": `url(#${fillId})`,
  });

  if (spec.mode === "rect") {
    fillRect.setAttribute("x", "0");
    fillRect.setAttribute("y", "0");
    fillRect.setAttribute("width", String(analogLevel));
    fillRect.setAttribute("height", "100");
  }

  overlaySvg.appendChild(fillRect);
  overlaySvg.appendChild(createGeometryNode(item, spec, {
    fill: "none",
    stroke: item.visual.borderColor,
  }));
}

function appendDpadPreview(overlaySvg, item, spec) {
  overlaySvg.appendChild(createGeometryNode(item, spec, {
    fill: item.visual.fillColor,
    stroke: item.visual.borderColor,
  }));

  overlaySvg.appendChild(svg("line", {
    x1: "26",
    y1: "50",
    x2: "74",
    y2: "50",
    class: "dpad-axis-line",
  }));
  overlaySvg.appendChild(svg("line", {
    x1: "50",
    y1: "26",
    x2: "50",
    y2: "74",
    class: "dpad-axis-line",
  }));

  const indicatorX = 50 + (clamp(Number(item.visual.preview.dpadX ?? 0), -100, 100) * 0.18);
  const indicatorY = 50 - (clamp(Number(item.visual.preview.dpadY ?? 0), -100, 100) * 0.18);
  overlaySvg.appendChild(svg("circle", {
    cx: String(indicatorX),
    cy: String(indicatorY),
    r: "10",
    class: "dpad-indicator-dot",
  }));
}

function renderOverlayItem(collectionName, item, index, spec) {
  const element = document.createElement("button");
  element.type = "button";
  element.className = "overlay-item";
  const key = itemKey(collectionName, index);
  const isPressedPreview = state.previewPressKey === key;
  if (state.selectedKey === key) {
    element.classList.add("selected");
  }

  const geometry = geometryForItem(item, spec);
  element.style.left = `${geometry.left}px`;
  element.style.top = `${geometry.top}px`;
  element.style.width = `${geometry.width}px`;
  element.style.height = `${geometry.height}px`;

  const overlaySvg = svg("svg", {
    viewBox: "0 0 100 100",
    preserveAspectRatio: "none",
    class: "overlay-svg",
  });
  overlaySvg.style.transformOrigin = "50% 50%";
  overlaySvg.style.transform = `rotate(${getItemRotation(item)}deg)`;

  if (item.visual.functionType === "analog") {
    appendAnalogPreview(overlaySvg, item, spec, key);
  } else if (item.visual.functionType === "dpad") {
    appendDpadPreview(overlaySvg, item, spec);
  } else {
    overlaySvg.appendChild(createGeometryNode(item, spec, {
      fill: isPressedPreview ? pressedFillColor(item.visual.fillColor) : item.visual.fillColor,
      stroke: item.visual.borderColor,
    }));
  }

  element.appendChild(overlaySvg);

  const content = document.createElement("span");
  content.className = "overlay-content";
  const label = document.createElement("span");
  label.className = "overlay-label";
  label.textContent = item.visual.label;
  label.style.color = item.visual.textColor;
  const metaText = item.visual.functionType === "analog"
    ? `${clamp(Number(item.visual.preview.analogLevel ?? 50), 0, 100)}%`
    : item.visual.functionType === "dpad"
      ? `X ${clamp(Number(item.visual.preview.dpadX ?? 0), -100, 100)}  Y ${clamp(Number(item.visual.preview.dpadY ?? 0), -100, 100)}`
      : "";
  const hasMeta = metaText.length > 0;
  const fittedLabelSize = Math.max(
    10,
    Math.floor(Math.min(geometry.width, geometry.height) * (hasMeta ? 0.22 : 0.32)),
  );
  label.style.fontSize = `${Math.min(scaleText(item.visual.textSize), fittedLabelSize)}px`;
  Object.assign(label.style, fontStyleCss(item.visual.textStyle));
  content.appendChild(label);

  const meta = document.createElement("span");
  meta.className = "overlay-meta";
  meta.textContent = metaText;
  meta.style.fontSize = `${Math.max(9, Math.floor(Math.min(geometry.width, geometry.height) * 0.16))}px`;
  meta.classList.toggle("hidden", !meta.textContent);
  if (meta.textContent) {
    content.appendChild(meta);
  }

  element.appendChild(content);

  element.addEventListener("pointerdown", (event) => {
    event.preventDefault();
    state.previewPressKey = key;
    selectItem(key);
    try {
      element.setPointerCapture(event.pointerId);
    } catch {
      // The integrated browser can dispatch synthetic pointer sequences for clicks.
    }
    state.pointerDrag = {
      key,
      pointerId: event.pointerId,
      startX: event.clientX,
      startY: event.clientY,
      itemSnapshot: cloneJson(item),
    };
  });

  element.addEventListener("pointermove", (event) => {
    if (!state.pointerDrag || state.pointerDrag.key !== key || state.pointerDrag.pointerId !== event.pointerId) {
      return;
    }

    const deltaX = event.clientX - state.pointerDrag.startX;
    const deltaY = event.clientY - state.pointerDrag.startY;
    const sourceDeltaX = unscaleX(deltaX);
    const sourceDeltaY = unscaleY(deltaY);
    const currentItem = activeLayout()[collectionName][index];
    const snapshot = state.pointerDrag.itemSnapshot;

    if (spec.mode === "circle") {
      currentItem.centerX = snapshot.centerX + sourceDeltaX;
      currentItem.centerY = snapshot.centerY + sourceDeltaY;
    } else {
      currentItem.x = snapshot.x + sourceDeltaX;
      currentItem.y = snapshot.y + sourceDeltaY;
    }

    render();
  });

  const clearPreviewPress = (event) => {
    if (state.pointerDrag && state.pointerDrag.pointerId === event.pointerId) {
      state.pointerDrag = null;
    }
    if (state.previewPressKey === key) {
      state.previewPressKey = null;
      render();
    }
  };

  element.addEventListener("pointerup", clearPreviewPress);
  element.addEventListener("pointercancel", clearPreviewPress);

  return element;
}

function renderAddButtonToolbar() {
  const disabledItems = state.layoutDocument ? disabledItemsForActiveLayout() : [];
  addButtonSelect.replaceChildren();

  if (disabledItems.length === 0) {
    const option = document.createElement("option");
    option.value = "";
    option.textContent = "No hidden buttons";
    addButtonSelect.appendChild(option);
    addButtonSelect.disabled = true;
    addButtonButton.disabled = true;
    return;
  }

  disabledItems.forEach(({ key, item, spec }) => {
    const option = document.createElement("option");
    option.value = key;
    option.textContent = `${itemDisplayName(item)} (${spec.name})`;
    addButtonSelect.appendChild(option);
  });
  addButtonSelect.disabled = false;
  addButtonButton.disabled = false;
}

function restoreSelectedButton() {
  if (!state.layoutDocument || !addButtonSelect.value) {
    return;
  }
  const { item } = getItemMeta(addButtonSelect.value);
  item.visual.enabled = true;
  state.selectedKey = addButtonSelect.value;
  render();
  setStatus(`Restored ${itemDisplayName(item)} to the canvas.`);
}

function deleteSelectedButton() {
  if (!state.selectedKey) {
    return;
  }
  const { item } = getItemMeta(state.selectedKey);
  const removedLabel = itemDisplayName(item);
  item.visual.enabled = false;
  state.selectedKey = null;
  state.pointerDrag = null;
  state.previewPressKey = null;
  state.shapeEditMode = false;
  state.shapeSelectedPointIndex = null;
  render();
  setStatus(`Removed ${removedLabel} from the canvas. Apply will persist it as disabled in code.`);
}

function selectItem(key) {
  const { item } = getItemMeta(key);
  if (!isItemEnabled(item)) {
    return;
  }
  state.selectedKey = key;
  render();
}

function setCornerFields(radius) {
  fields.cornerRadius.value = radius;
  fields.cornerDiameter.value = radius * 2;
}

function updateInspector() {
  if (!state.selectedKey) {
    fields.key.value = "";
    emptySelection.classList.remove("hidden");
    inspectorForm.classList.add("hidden");
    shapeEditorGroup.classList.add("hidden");
    deleteButtonButton.disabled = true;
    state.shapeSelectedPointIndex = null;
    updateDeletePointButton();
    updateDimensionLinkButton();
    updateShapeClipboardButtons();
    return;
  }

  const { collectionName, item, spec } = getItemMeta(state.selectedKey);
  emptySelection.classList.add("hidden");
  inspectorForm.classList.remove("hidden");
  deleteButtonButton.disabled = false;
  fields.key.value = `${state.selectedTarget}.${collectionName}.${item.id}`;
  const shapeType = effectiveShapeType(item, spec);
  const width = getItemWidth(item, spec);
  const height = getItemHeight(item, spec);

  fields.xLabel.textContent = spec.mode === "circle" ? "Center X" : "X";
  fields.yLabel.textContent = spec.mode === "circle" ? "Center Y" : "Y";
  fields.wLabel.textContent = isCircleFamily(shapeType) && width === height ? "Size / Width" : "Width";
  fields.hLabel.textContent = "Height";

  if (spec.mode === "circle") {
    fields.x.value = item.centerX;
    fields.y.value = item.centerY;
    fields.w.value = width;
    fields.h.value = height;
    fields.hWrapper.classList.remove("hidden");
  } else if (spec.mode === "square") {
    fields.x.value = item.x;
    fields.y.value = item.y;
    fields.w.value = width;
    fields.h.value = height;
    fields.hWrapper.classList.remove("hidden");
  } else {
    fields.x.value = item.x;
    fields.y.value = item.y;
    fields.w.value = item.width;
    fields.h.value = item.height;
    fields.hWrapper.classList.remove("hidden");
  }

  fields.label.value = item.visual.label;
  fields.textSize.value = item.visual.textSize;
  fields.textStyle.value = item.visual.textStyle;
  fields.textColor.value = item.visual.textColor;
  fields.fillColor.value = item.visual.fillColor;
  fields.borderColor.value = item.visual.borderColor;
  fields.borderWidth.value = item.visual.borderWidth;
  fields.rotation.value = getItemRotation(item);
  fields.shapeType.value = shapeType;
  setCornerFields(item.visual.shape.cornerRadius);
  fields.functionType.value = item.visual.functionType;
  fields.analogLevel.value = item.visual.preview.analogLevel;
  fields.dpadX.value = item.visual.preview.dpadX;
  fields.dpadY.value = item.visual.preview.dpadY;

  const usesCornerRadius = shapeUsesCornerRadius(shapeType);
  fields.cornerRadiusWrapper.classList.toggle("hidden", !usesCornerRadius);
  fields.cornerDiameterWrapper.classList.toggle("hidden", !usesCornerRadius);

  const usesAnalogPreview = item.visual.functionType === "analog";
  fields.analogLevelWrapper.classList.toggle("hidden", !usesAnalogPreview);
  const usesDpadPreview = item.visual.functionType === "dpad";
  fields.dpadXWrapper.classList.toggle("hidden", !usesDpadPreview);
  fields.dpadYWrapper.classList.toggle("hidden", !usesDpadPreview);

  const usesCustomShape = rawShapeType(item, spec) === "custom";
  shapeEditorGroup.classList.toggle("hidden", !usesCustomShape);
  if (!usesCustomShape) {
    state.shapeEditMode = false;
    state.shapeSelectedPointIndex = null;
  }

  togglePointEditButton.textContent = state.shapeEditMode ? "Finish Points" : "Edit Points";
  togglePointEditButton.classList.toggle("secondary", state.shapeEditMode);
  updateDeletePointButton(item);
  updateDimensionLinkButton();
  updateShapeClipboardButtons();
}

function projectPointToSegment(point, start, end) {
  const dx = end.x - start.x;
  const dy = end.y - start.y;
  if (dx === 0 && dy === 0) {
    return start;
  }
  const t = Math.max(0, Math.min(1, ((point.x - start.x) * dx + (point.y - start.y) * dy) / (dx * dx + dy * dy)));
  return { x: start.x + dx * t, y: start.y + dy * t };
}

function insertPointAtNearestSegment(points, newPoint) {
  if (points.length < 2) {
    points.push({ x: Math.round(newPoint.x), y: Math.round(newPoint.y) });
    return;
  }
  let bestIndex = 1;
  let bestDistance = Number.POSITIVE_INFINITY;
  for (let index = 0; index < points.length; index += 1) {
    const start = points[index];
    const end = points[(index + 1) % points.length];
    const projection = projectPointToSegment(newPoint, start, end);
    const dx = projection.x - newPoint.x;
    const dy = projection.y - newPoint.y;
    const distance = dx * dx + dy * dy;
    if (distance < bestDistance) {
      bestDistance = distance;
      bestIndex = index + 1;
    }
  }
  points.splice(bestIndex, 0, { x: Math.round(newPoint.x), y: Math.round(newPoint.y) });
}

function renderShapeEditor() {
  shapeEditor.replaceChildren();
  if (!state.selectedKey) {
    return;
  }

  const { item, spec } = getItemMeta(state.selectedKey);
  if (!isItemEnabled(item)) {
    return;
  }
  if (item.visual.shape.type !== "custom") {
    return;
  }

  const previewShape = createGeometryNode(item, spec, {
    fill: item.visual.fillColor,
    stroke: item.visual.borderColor,
  });
  previewShape.setAttribute("transform", "translate(20 20) scale(2)");
  shapeEditor.appendChild(previewShape);

  ensureCustomPoints(item, spec);
  const scaledPoints = item.visual.shape.points.map((point) => ({
    x: 20 + point.x * 2,
    y: 20 + point.y * 2,
  }));

  shapeEditor.appendChild(svg("polyline", {
    points: scaledPoints.map((point) => `${point.x},${point.y}`).join(" "),
    class: "shape-control-line",
  }));

  if (!state.shapeEditMode) {
    return;
  }

  const hitPath = svg("path", {
    d: smoothClosedPath(scaledPoints),
    class: "shape-hit",
  });
  hitPath.addEventListener("pointerdown", (event) => {
    event.preventDefault();
    state.shapeSelectedPointIndex = null;
    const rect = shapeEditor.getBoundingClientRect();
    const canvasX = ((event.clientX - rect.left) / rect.width) * 240;
    const canvasY = ((event.clientY - rect.top) / rect.height) * 240;
    insertPointAtNearestSegment(item.visual.shape.points, {
      x: clamp((canvasX - 20) / 2, 0, 100),
      y: clamp((canvasY - 20) / 2, 0, 100),
    });
    render();
  });
  shapeEditor.appendChild(hitPath);

  scaledPoints.forEach((point, index) => {
    const handle = svg("circle", {
      cx: String(point.x),
      cy: String(point.y),
      r: "6",
      class: "shape-handle",
    });
    if (state.shapeSelectedPointIndex === index) {
      handle.classList.add("selected");
    }
    handle.addEventListener("pointerdown", (event) => {
      event.preventDefault();
      state.shapeSelectedPointIndex = index;
      state.shapeHandleDrag = { index, pointerId: event.pointerId };
      try {
        handle.setPointerCapture(event.pointerId);
      } catch {
        // The integrated browser can dispatch synthetic pointer sequences for clicks.
      }
    });
    handle.addEventListener("pointermove", (event) => {
      if (!state.shapeHandleDrag || state.shapeHandleDrag.index !== index || state.shapeHandleDrag.pointerId !== event.pointerId) {
        return;
      }
      state.shapeSelectedPointIndex = index;
      const rect = shapeEditor.getBoundingClientRect();
      const canvasX = ((event.clientX - rect.left) / rect.width) * 240;
      const canvasY = ((event.clientY - rect.top) / rect.height) * 240;
      item.visual.shape.points[index] = {
        x: clamp(Math.round((canvasX - 20) / 2), 0, 100),
        y: clamp(Math.round((canvasY - 20) / 2), 0, 100),
      };
      render();
    });
    handle.addEventListener("pointerup", (event) => {
      if (state.shapeHandleDrag && state.shapeHandleDrag.pointerId === event.pointerId) {
        state.shapeHandleDrag = null;
      }
      state.shapeSelectedPointIndex = index;
      render();
    });
    shapeEditor.appendChild(handle);
  });
}

function render() {
  if (!state.layoutDocument) {
    return;
  }

  layoutTargetSelect.value = state.selectedTarget;
  const layout = activeLayout();
  const zoomScale = currentZoomScale();
  deviceCanvas.style.width = `${layout.deviceCanvas.width}px`;
  deviceCanvas.style.height = `${layout.deviceCanvas.height}px`;
  deviceCanvas.style.transform = `scale(${zoomScale})`;
  deviceCanvas.dataset.canvasLabel = `${TARGETS[state.selectedTarget].label} · ${layout.deviceCanvas.width} x ${layout.deviceCanvas.height}`;
  canvasZoomLayer.style.width = `${layout.deviceCanvas.width * zoomScale}px`;
  canvasZoomLayer.style.height = `${layout.deviceCanvas.height * zoomScale}px`;
  previewFrame.style.left = `${layout.previewFrame.x}px`;
  previewFrame.style.top = `${layout.previewFrame.y}px`;
  previewFrame.style.width = `${layout.previewFrame.width}px`;
  previewFrame.style.height = `${layout.previewFrame.height}px`;
  zoomRange.value = String(state.zoomPercent);
  zoomValue.textContent = `${state.zoomPercent}%`;

  overlayLayer.replaceChildren();
  for (const spec of collectionSpecs) {
    layout[spec.name].forEach((item, index) => {
      if (isItemEnabled(item)) {
        overlayLayer.appendChild(renderOverlayItem(spec.name, item, index, spec));
      }
    });
  }

  if (state.selectedKey) {
    const { item } = getItemMeta(state.selectedKey);
    if (!isItemEnabled(item)) {
      state.selectedKey = null;
    }
  }

  renderAddButtonToolbar();
  updateInspector();
  renderShapeEditor();
}

async function loadLayout() {
  setStatus("Loading the generated controller layout from the code that the P4 build uses...");
  const response = await fetch("/api/layout");
  const result = await response.json();
  if (!response.ok) {
    setStatus(result.error || "Load failed.");
    return;
  }
  state.layoutDocument = normalizeLayoutDocument(result);
  state.selectedTarget = state.layoutDocument.selectedTarget in TARGETS ? state.layoutDocument.selectedTarget : "bleController";
  state.selectedKey = null;
  state.pointerDrag = null;
  state.previewPressKey = null;
  state.shapeEditMode = false;
  state.shapeSelectedPointIndex = null;
  state.shapeHandleDrag = null;
  render();
  setStatus(`Loaded ${TARGETS[state.selectedTarget].label} from the generated header currently used by the codebase.`);
}

async function refreshLayoutFromCode() {
  setStatus("Refreshing the controller layout from the generated C++ header in the codebase...");
  const response = await fetch("/api/layout/from-header", { method: "POST" });
  const result = await response.json();
  if (!response.ok) {
    setStatus(result.error || "Refresh failed.");
    return;
  }
  state.layoutDocument = normalizeLayoutDocument(result.layout);
  state.selectedTarget = state.selectedTarget in TARGETS ? state.selectedTarget : "bleController";
  state.selectedKey = null;
  state.pointerDrag = null;
  state.previewPressKey = null;
  state.shapeEditMode = false;
  state.shapeSelectedPointIndex = null;
  state.shapeHandleDrag = null;
  render();
  setStatus(`Refreshed BLE and Local controller layouts from ${result.headerPath}`);
}

function readFileAsDataUrl(file) {
  return new Promise((resolve, reject) => {
    const reader = new FileReader();
    reader.onerror = () => reject(new Error("Could not read PNG file"));
    reader.onload = () => resolve(reader.result);
    reader.readAsDataURL(file);
  });
}

async function uploadBackgroundImage() {
  const file = backgroundFile.files[0];
  if (!file) {
    setStatus("Choose a PNG file before replacing the background.");
    return;
  }
  if (file.type && file.type !== "image/png") {
    setStatus("Only PNG files are supported for the controller background.");
    return;
  }

  setStatus("Replacing the background PNG and regenerating the firmware asset...");
  const response = await fetch("/api/background", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({
      name: file.name,
      data: await readFileAsDataUrl(file),
    }),
  });
  const result = await response.json();
  if (!response.ok) {
    setStatus(result.error || "Background replacement failed.");
    return;
  }

  refreshBackgroundPreview();
  backgroundFile.value = "";
  setStatus(`Background updated at ${result.imagePath} and regenerated at ${result.assetPath}`);
}

async function applyLayout() {
  const currentTarget = layoutTargetSelect.value in TARGETS ? layoutTargetSelect.value : state.selectedTarget;
  state.selectedTarget = currentTarget;
  state.layoutDocument.selectedTarget = currentTarget;
  setStatus(`Applying ${TARGETS[currentTarget].label} to ${TARGETS[currentTarget].symbol} in code...`);
  const response = await fetch("/api/layout", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(state.layoutDocument),
  });
  const result = await response.json();
  if (!response.ok) {
    setStatus(result.error || "Apply failed.");
    return;
  }
  state.layoutDocument = normalizeLayoutDocument(result.layout);
  state.selectedTarget = currentTarget;
  state.layoutDocument.selectedTarget = currentTarget;
  render();
  setStatus(`Applied ${TARGETS[currentTarget].label} to ${TARGETS[currentTarget].symbol} and regenerated ${result.headerPath}. Build the firmware manually when you are ready.`);
}

function onInspectorChange(event) {
  if (!state.selectedKey) {
    return;
  }

  const { collectionName, index, spec } = getItemMeta(state.selectedKey);
  const item = activeLayout()[collectionName][index];
  const changedField = event?.target ?? null;
  const previousShapeType = rawShapeType(item, spec);
  const previousEffectiveShape = effectiveShapeType(item, spec);
  const previousWidth = getItemWidth(item, spec);
  const previousHeight = getItemHeight(item, spec);
  const requestedShapeType = fields.shapeType.value;
  syncShapeToSelection(item, spec, fields.shapeType.value);
  const shapeSelectionChanged = requestedShapeType !== previousEffectiveShape;
  item.visual.shape.cornerRadius = shapeSelectionChanged
    ? item.visual.shape.cornerRadius
    : numericFieldValue(fields.cornerRadius, item.visual.shape.cornerRadius);
  const usesIndependentHeight = itemUsesIndependentHeight(item, spec);
  let requestedWidth = numericFieldValue(fields.w, previousWidth);
  let requestedHeight = numericFieldValue(fields.h, previousHeight);

  if (state.dimensionsLinked && usesIndependentHeight && (changedField === fields.w || changedField === fields.h)) {
    if (changedField === fields.w) {
      requestedHeight = previousWidth > 0 && previousHeight > 0
        ? Math.max(1, Math.round((requestedWidth / previousWidth) * previousHeight))
        : requestedWidth;
      fields.h.value = requestedHeight;
    } else if (changedField === fields.h) {
      requestedWidth = previousHeight > 0 && previousWidth > 0
        ? Math.max(1, Math.round((requestedHeight / previousHeight) * previousWidth))
        : requestedHeight;
      fields.w.value = requestedWidth;
    }
  }

  if (spec.mode === "circle") {
    item.centerX = numericFieldValue(fields.x, item.centerX);
    item.centerY = numericFieldValue(fields.y, item.centerY);
    setItemDimensions(
      item,
      spec,
      requestedWidth,
      requestedHeight,
    );
  } else if (spec.mode === "square") {
    item.x = numericFieldValue(fields.x, item.x);
    item.y = numericFieldValue(fields.y, item.y);
    setItemDimensions(item, spec, requestedWidth, requestedHeight);
  } else {
    item.x = numericFieldValue(fields.x, item.x);
    item.y = numericFieldValue(fields.y, item.y);
    item.width = requestedWidth;
    item.height = requestedHeight;
  }

  item.visual.label = fields.label.value;
  item.visual.textSize = numericFieldValue(fields.textSize, item.visual.textSize);
  item.visual.textStyle = fields.textStyle.value;
  item.visual.textColor = fields.textColor.value;
  item.visual.fillColor = fields.fillColor.value;
  item.visual.borderColor = fields.borderColor.value;
  item.visual.borderWidth = numericFieldValue(fields.borderWidth, item.visual.borderWidth);
  setItemRotation(item, numericFieldValue(fields.rotation, getItemRotation(item)));
  item.visual.functionType = fields.functionType.value;
  item.visual.preview.analogLevel = clamp(numericFieldValue(fields.analogLevel, item.visual.preview.analogLevel), 0, 100);
  item.visual.preview.dpadX = clamp(numericFieldValue(fields.dpadX, item.visual.preview.dpadX), -100, 100);
  item.visual.preview.dpadY = clamp(numericFieldValue(fields.dpadY, item.visual.preview.dpadY), -100, 100);

  if (rawShapeType(item, spec) === "custom") {
    ensureCustomPoints(item, spec);
  } else if (previousShapeType === "custom") {
    state.shapeEditMode = false;
    state.shapeSelectedPointIndex = null;
  }

  render();
}

function onCornerRadiusChange() {
  fields.cornerDiameter.value = Number(fields.cornerRadius.value) * 2;
  onInspectorChange();
}

function onCornerDiameterChange() {
  fields.cornerRadius.value = Math.round(Number(fields.cornerDiameter.value) / 2);
  onInspectorChange();
}

layoutTargetSelect.addEventListener("change", () => {
  state.selectedTarget = layoutTargetSelect.value;
  state.layoutDocument.selectedTarget = state.selectedTarget;
  state.selectedKey = null;
  state.pointerDrag = null;
  state.previewPressKey = null;
  state.shapeEditMode = false;
  state.shapeSelectedPointIndex = null;
  render();
  setStatus(`Now editing ${TARGETS[state.selectedTarget].label}. Apply will update ${TARGETS[state.selectedTarget].symbol} in code.`);
});

refreshCodeButton.addEventListener("click", () => {
  refreshLayoutFromCode().catch((error) => setStatus(`Refresh failed: ${error}`));
});

document.getElementById("applyButton").addEventListener("click", () => {
  applyLayout().catch((error) => setStatus(`Apply failed: ${error}`));
});

backgroundUploadButton.addEventListener("click", () => {
  uploadBackgroundImage().catch((error) => setStatus(`Background update failed: ${error}`));
});

addButtonButton.addEventListener("click", () => {
  restoreSelectedButton();
});

addButtonSelect.addEventListener("change", () => {
  addButtonButton.disabled = !addButtonSelect.value;
});

deleteButtonButton.addEventListener("click", () => {
  deleteSelectedButton();
});

dimensionLinkButton.addEventListener("click", () => {
  state.dimensionsLinked = !state.dimensionsLinked;
  updateDimensionLinkButton();
});

copyShapeButton.addEventListener("click", () => {
  copySelectedShape();
});

pasteShapeButton.addEventListener("click", () => {
  pasteCopiedShape();
});

togglePointEditButton.addEventListener("click", () => {
  if (!state.selectedKey) {
    return;
  }
  const { item, spec } = getItemMeta(state.selectedKey);
  if (rawShapeType(item, spec) !== "custom") {
    item.visual.shape.type = "custom";
    fields.shapeType.value = "custom";
    ensureCustomPoints(item, spec);
  }
  state.shapeEditMode = !state.shapeEditMode;
  state.shapeSelectedPointIndex = null;
  render();
});

deletePointButton.addEventListener("click", () => {
  deleteSelectedPoint();
});

resetShapeButton.addEventListener("click", () => {
  if (!state.selectedKey) {
    return;
  }
  const { item, spec } = getItemMeta(state.selectedKey);
  item.visual.shape.points = [];
  state.shapeSelectedPointIndex = null;
  if (rawShapeType(item, spec) === "custom") {
    ensureCustomPoints(item, spec);
  }
  render();
});

zoomRange.addEventListener("input", () => {
  state.zoomPercent = clamp(Number(zoomRange.value) || 100, 50, 400);
  render();
});

[
  fields.x,
  fields.y,
  fields.w,
  fields.h,
  fields.label,
  fields.textSize,
  fields.textStyle,
  fields.textColor,
  fields.fillColor,
  fields.borderColor,
  fields.borderWidth,
  fields.rotation,
  fields.shapeType,
  fields.functionType,
  fields.analogLevel,
  fields.dpadX,
  fields.dpadY,
].forEach((field) => {
  field.addEventListener("input", onInspectorChange);
  field.addEventListener("change", onInspectorChange);
});

fields.cornerRadius.addEventListener("input", onCornerRadiusChange);
fields.cornerRadius.addEventListener("change", onCornerRadiusChange);
fields.cornerDiameter.addEventListener("input", onCornerDiameterChange);
fields.cornerDiameter.addEventListener("change", onCornerDiameterChange);

refreshBackgroundPreview();
loadLayout().catch((error) => setStatus(`Load failed: ${error}`));