// Robot executor runtime for Qumir
// Implements the Robot field logic with walls, painted cells, radiation/temperature

let __filesAccessor = null;
let __addFileCallback = null;
let __updateFileCallback = null;

let __animationDelay = 150; // ms between steps

// JS future runtime (set by __bindFutureRuntime from app.js).
let __futureRuntime = null;

export function __bindFutureRuntime(fr) {
  __futureRuntime = fr;
}

// Default field: 7x7, robot at (0,0), no walls, no painted cells
const DEFAULT_FIELD = `; Kumir Robot Field Format
; ========================
;
; Line 1: Field size (width height)
; Line 2: Robot position (x y), 1-indexed from top-left
;
; Remaining lines: Special cell properties
; Format: x y Wall Color Radiation Temperature Symbol Symbol1 Point
;
; Wall is a bit mask: 1 = North, 2 = South, 4 = West, 8 = East
;
; Color: 0 = not painted, non-zero = painted
; Radiation: float value for radiation sensor
; Temperature: float value for temperature sensor
;
; Example special cell: 3 2 8 1 5.5 20.0 $ $ 0
;   Cell at (3,2) with wall to East, painted, radiation=5.5, temp=20.0
;
; Field Size
7 7
; Robot Position
1 1
`.trim();

export class RobotField {
  constructor() {
    this.width = 7;
    this.height = 7;
    this.robotX = 0;
    this.robotY = 0;
    this.painted = new Set();
    this.hWalls = new Set(); // horizontal walls: "x,y" means wall below cell (x,y)
    this.vWalls = new Set(); // vertical walls: "x,y" means wall to the right of cell (x,y)
    this.radiation = new Map(); // "x,y" -> value
    this.temperature = new Map(); // "x,y" -> value
  }

  reset() {
    this.width = 10;
    this.height = 8;
    this.robotX = 0;
    this.robotY = 0;
    this.painted.clear();
    this.hWalls.clear();
    this.vWalls.clear();
    this.radiation.clear();
    this.temperature.clear();
  }

  addWalls(x, y, wallN, wallE, wallS, wallW) {
    // Wall to North = horizontal wall above cell (x,y).
    // In our model: hWalls stores "x,y" meaning wall below (x,y).
    if (wallN && y > 0) {
      this.hWalls.add(`${x},${y - 1}`);
    }
    // Wall to South = wall below (x,y).
    if (wallS && y < this.height - 1) {
      this.hWalls.add(`${x},${y}`);
    }
    // Wall to West = wall to the left of (x,y).
    // In our model: vWalls stores "x,y" meaning wall to the right of (x,y).
    if (wallW && x > 0) {
      this.vWalls.add(`${x - 1},${y}`);
    }
    // Wall to East = wall to the right of (x,y).
    if (wallE && x < this.width - 1) {
      this.vWalls.add(`${x},${y}`);
    }
  }

  parseField(text) {
    this.reset();
    const lines = text.split(/\r?\n/);
    let lineIndex = 0;

    // Helper to get next non-comment, non-empty line
    const nextDataLine = () => {
      while (lineIndex < lines.length) {
        const line = lines[lineIndex++].trim();
        if (line && !line.startsWith(';')) {
          return line;
        }
      }
      return null;
    };

    // Line 1: Field size (x, y)
    const sizeLine = nextDataLine();
    if (sizeLine) {
      const parts = sizeLine.split(/\s+/);
      this.width = parseInt(parts[0], 10) || 7;
      this.height = parseInt(parts[1], 10) || 7;
    }

    // Line 2: Robot position (x, y), 1-indexed in .fil.
    const robotLine = nextDataLine();
    if (robotLine) {
      const parts = robotLine.split(/\s+/);
      this.robotX = Math.max(0, (parseInt(parts[0], 10) || 1) - 1);
      this.robotY = Math.max(0, (parseInt(parts[1], 10) || 1) - 1);
    }

    // Remaining lines: x y Wall Color Radiation Temperature Symbol Symbol1 Point.
    // Coordinates are 1-indexed in .fil.
    // Wall is a bit mask: 1 = North, 2 = South, 4 = West, 8 = East.
    let dataLine;
    while ((dataLine = nextDataLine()) !== null) {
      const parts = dataLine.split(/\s+/);
      if (parts.length < 2) continue;

      const x = parseInt(parts[0], 10) - 1;
      const y = parseInt(parts[1], 10) - 1;
      if (isNaN(x) || isNaN(y)) continue;

      const wall = parseInt(parts[2] || '0', 10) || 0;
      this.addWalls(x, y, (wall & 1) !== 0, (wall & 8) !== 0, (wall & 2) !== 0, (wall & 4) !== 0);

      const color = parseInt(parts[3] || '0', 10) || 0;
      if (color !== 0) {
        this.painted.add(`${x},${y}`);
      }

      if (parts[4] !== undefined) {
        const rad = parseFloat(parts[4]);
        if (!isNaN(rad) && rad !== 0) {
          this.radiation.set(`${x},${y}`, rad);
        }
      }

      if (parts[5] !== undefined) {
        const temp = parseFloat(parts[5]);
        if (!isNaN(temp) && temp !== 0) {
          this.temperature.set(`${x},${y}`, temp);
        }
      }
    }
  }

  // Check if there's a wall preventing movement
  hasWallLeft() {
    if (this.robotX <= 0) return true;
    return this.vWalls.has(`${this.robotX - 1},${this.robotY}`);
  }

  hasWallRight() {
    if (this.robotX >= this.width - 1) return true;
    return this.vWalls.has(`${this.robotX},${this.robotY}`);
  }

  hasWallUp() {
    if (this.robotY <= 0) return true;
    return this.hWalls.has(`${this.robotX},${this.robotY - 1}`);
  }

  hasWallDown() {
    if (this.robotY >= this.height - 1) return true;
    return this.hWalls.has(`${this.robotX},${this.robotY}`);
  }

  isPainted() {
    return this.painted.has(`${this.robotX},${this.robotY}`);
  }

  paint() {
    this.painted.add(`${this.robotX},${this.robotY}`);
  }

  getRadiation() {
    return this.radiation.get(`${this.robotX},${this.robotY}`) || 0;
  }

  getTemperature() {
    return this.temperature.get(`${this.robotX},${this.robotY}`) || 0;
  }

  toText() {
    let lines = [];
    lines.push('; Field Size: x, y');
    lines.push(`${this.width} ${this.height}`);
    lines.push('; Robot position: x, y');
    lines.push(`${this.robotX + 1} ${this.robotY + 1}`);
    lines.push('; A set of special Fields: x, y, Wall, Color, Radiation, Temperature, Symbol, Symbol1, Point');

    // Collect all cells with special properties
    const specialCells = new Set();
    for (const key of this.painted) specialCells.add(key);
    for (const key of this.hWalls) {
      // hWalls "x,y" = wall below (x,y), which is South wall for (x,y) and North wall for (x,y+1)
      const [x, y] = key.split(',').map(Number);
      specialCells.add(`${x},${y}`);
      if (y + 1 < this.height) specialCells.add(`${x},${y + 1}`);
    }
    for (const key of this.vWalls) {
      // vWalls "x,y" = wall right of (x,y), which is East wall for (x,y) and West wall for (x+1,y)
      const [x, y] = key.split(',').map(Number);
      specialCells.add(`${x},${y}`);
      if (x + 1 < this.width) specialCells.add(`${x + 1},${y}`);
    }
    for (const key of this.radiation.keys()) specialCells.add(key);
    for (const key of this.temperature.keys()) specialCells.add(key);

    for (const key of specialCells) {
      const [x, y] = key.split(',').map(Number);
      const wallN = (y > 0 && this.hWalls.has(`${x},${y - 1}`)) ? 1 : 0;
      const wallS = (y < this.height - 1 && this.hWalls.has(`${x},${y}`)) ? 1 : 0;
      const wallW = (x > 0 && this.vWalls.has(`${x - 1},${y}`)) ? 1 : 0;
      const wallE = (x < this.width - 1 && this.vWalls.has(`${x},${y}`)) ? 1 : 0;
      const wall = wallN + wallS * 2 + wallW * 4 + wallE * 8;
      const color = this.painted.has(key) ? 1 : 0;
      const rad = this.radiation.get(key) || 0;
      const temp = this.temperature.get(key) || 0;
      lines.push(`${x + 1} ${y + 1} ${wall} ${color} ${rad.toFixed(6)} ${temp.toFixed(6)} $ $ 0`);
    }

    return lines.join('\n');
  }
}

export const field = new RobotField();

// Error helper - throws with message
function robotError(msg) {
  const line = `Робот: ${msg}`;
  throw new Error(line);
}

// File manager integration - same pattern as io.js
export function __setRobotFilesAccessor(accessor, addFile, updateFile) {
  __filesAccessor = accessor;
  __addFileCallback = addFile;
  __updateFileCallback = updateFile;
}

function getFiles() {
  return typeof __filesAccessor === 'function' ? __filesAccessor() : [];
}

// Lazy loading flag - field is loaded on first robot command
let __fieldLoaded = false;

// Load field from .fil file (called lazily on first robot command)
function ensureFieldLoaded() {
  if (__fieldLoaded) return;
  __fieldLoaded = true;

  const files = getFiles();

  // Look for first .fil file
  let filFile = null;
  for (const f of files) {
    const name = (f.name || '').toLowerCase();
    if (name.endsWith('.fil')) {
      filFile = f;
      break;
    }
  }

  if (filFile && filFile.content) {
    field.parseField(filFile.content);
  } else {
    field.parseField(DEFAULT_FIELD);
    // Create default .fil file if callback available
    if (typeof __addFileCallback === 'function') {
      __addFileCallback({ name: 'robot.fil', content: DEFAULT_FIELD });
    }
  }

}

export function __initRobotField() {
  field.reset();
  __fieldLoaded = false;
}

// Preview field from .fil file without running program
// Returns true if field was loaded, false otherwise
export function __previewField() {
  field.reset();
  __fieldLoaded = true;

  const files = getFiles();

  // Look for first .fil file
  let filFile = null;
  for (const f of files) {
    const name = (f.name || '').toLowerCase();
    if (name.endsWith('.fil')) {
      filFile = f;
      break;
    }
  }

  if (filFile && filFile.content) {
    field.parseField(filFile.content);
    return true;
  } else {
    field.parseField(DEFAULT_FIELD);
    return false;
  }
}

export function __resetRobot() {
  __initRobotField();
}

export function __getRobotState() {
  return {
    x: field.robotX,
    y: field.robotY,
    width: field.width,
    height: field.height,
    painted: Array.from(field.painted),
    hWalls: Array.from(field.hWalls),
    vWalls: Array.from(field.vWalls)
  };
}

// Runtime API functions (exported for WASM)

function _robotOp(execute) {
  if (!__futureRuntime) { execute(); return 0; }
  const h = __futureRuntime.allocFuture();
  __futureRuntime.enqueuePendingOp({ h, execute });
  return h;
}

export function robot_left() {
  return _robotOp(() => {
    ensureFieldLoaded();
    if (field.hasWallLeft()) robotError('слева стена');
    field.robotX--;
  });
}

export function robot_right() {
  return _robotOp(() => {
    ensureFieldLoaded();
    if (field.hasWallRight()) robotError('справа стена');
    field.robotX++;
  });
}

export function robot_up() {
  return _robotOp(() => {
    ensureFieldLoaded();
    if (field.hasWallUp()) robotError('сверху стена');
    field.robotY--;
  });
}

export function robot_down() {
  return _robotOp(() => {
    ensureFieldLoaded();
    if (field.hasWallDown()) robotError('снизу стена');
    field.robotY++;
  });
}

export function robot_paint() {
  return _robotOp(() => {
    ensureFieldLoaded();
    field.paint();
  });
}

export function robot_left_free() {
  ensureFieldLoaded();
  return !field.hasWallLeft();
}

export function robot_right_free() {
  ensureFieldLoaded();
  return !field.hasWallRight();
}

export function robot_top_free() {
  ensureFieldLoaded();
  return !field.hasWallUp();
}

export function robot_bottom_free() {
  ensureFieldLoaded();
  return !field.hasWallDown();
}

export function robot_left_wall() {
  ensureFieldLoaded();
  return field.hasWallLeft();
}

export function robot_right_wall() {
  ensureFieldLoaded();
  return field.hasWallRight();
}

export function robot_top_wall() {
  ensureFieldLoaded();
  return field.hasWallUp();
}

export function robot_bottom_wall() {
  ensureFieldLoaded();
  return field.hasWallDown();
}

export function robot_cell_painted() {
  ensureFieldLoaded();
  return field.isPainted();
}

export function robot_cell_clean() {
  ensureFieldLoaded();
  return !field.isPainted();
}

export function robot_radiation() {
  ensureFieldLoaded();
  return field.getRadiation();
}

export function robot_temperature() {
  ensureFieldLoaded();
  return field.getTemperature();
}

export function __setAnimationDelay(delay) {
  __animationDelay = delay;
}

export function __getAnimationDelay() {
  return __animationDelay;
}
