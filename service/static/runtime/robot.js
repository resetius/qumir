// Robot executor runtime for Qumir
// Implements the Robot field logic with walls, painted cells, radiation/temperature

let __filesAccessor = null;
let __addFileCallback = null;
let __updateFileCallback = null;

// Default field: 7x7, robot at (0,0), no walls, no painted cells
const DEFAULT_FIELD = `; Kumir Robot Field Format
; ========================
;
; Line 1: Field size (width height)
; Line 2: Robot position (x y), 0-indexed from top-left
;
; Remaining lines: Special cell properties
; Format: x y WallN WallE WallS WallW Painted PointMark Radiation Temperature R G B
;
; Walls: 1 = wall present, 0 = no wall
;   WallN - wall to the North (above cell)
;   WallE - wall to the East (right of cell)
;   WallS - wall to the South (below cell)
;   WallW - wall to the West (left of cell)
;
; Painted: 1 = cell is painted, 0 = not painted
; PointMark: 1 = cell has point marker, 0 = no marker (unused)
; Radiation: float value for radiation sensor
; Temperature: float value for temperature sensor
; R G B: color values 0-255 (unused)
;
; Example special cell: 3 2 0 1 0 0 1 0 5.5 20.0 0 0 0
;   Cell at (3,2) with wall to East, painted, radiation=5.5, temp=20.0
;
; Field Size
7 7
; Robot Position
0 0
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

    // Line 2: Robot position (x, y)
    const robotLine = nextDataLine();
    if (robotLine) {
      const parts = robotLine.split(/\s+/);
      this.robotX = parseInt(parts[0], 10) || 0;
      this.robotY = parseInt(parts[1], 10) || 0;
    }

    // Remaining lines: special fields
    // Format: x, y, WallN, WallE, WallS, WallW, Painted, PointMark, Radiation, Temperature, R, G, B
    let dataLine;
    while ((dataLine = nextDataLine()) !== null) {
      const parts = dataLine.split(/\s+/);
      if (parts.length < 2) continue;

      const x = parseInt(parts[0], 10);
      const y = parseInt(parts[1], 10);
      if (isNaN(x) || isNaN(y)) continue;

      // Walls: indices 2,3,4,5 = N,E,S,W (1 = wall, 0 = no wall)
      const wallN = parts[2] === '1';
      const wallE = parts[3] === '1';
      const wallS = parts[4] === '1';
      const wallW = parts[5] === '1';

      // Wall to North = horizontal wall above cell (x,y)
      // In our model: hWalls stores "x,y" meaning wall below (x,y)
      // So wall above (x,y) is wall below (x, y-1) => hWalls.add(`${x},${y-1}`)
      if (wallN && y > 0) {
        this.hWalls.add(`${x},${y - 1}`);
      }
      // Wall to South = wall below (x,y) => hWalls.add(`${x},${y}`)
      if (wallS && y < this.height - 1) {
        this.hWalls.add(`${x},${y}`);
      }
      // Wall to West = wall to the left of (x,y)
      // In our model: vWalls stores "x,y" meaning wall to the right of (x,y)
      // So wall to the left of (x,y) is wall to the right of (x-1, y) => vWalls.add(`${x-1},${y}`)
      if (wallW && x > 0) {
        this.vWalls.add(`${x - 1},${y}`);
      }
      // Wall to East = wall to the right of (x,y) => vWalls.add(`${x},${y}`)
      if (wallE && x < this.width - 1) {
        this.vWalls.add(`${x},${y}`);
      }

      // Painted: index 6 (1 = painted)
      if (parts[6] === '1') {
        this.painted.add(`${x},${y}`);
      }

      // Point mark: index 7 (ignored for now)

      // Radiation: index 8
      if (parts[8] !== undefined) {
        const rad = parseFloat(parts[8]);
        if (!isNaN(rad) && rad !== 0) {
          this.radiation.set(`${x},${y}`, rad);
        }
      }

      // Temperature: index 9
      if (parts[9] !== undefined) {
        const temp = parseFloat(parts[9]);
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
    lines.push(`${this.robotX} ${this.robotY}`);
    lines.push('; A set of special Fields: x, y, Wall N, Wall E, Wall S, Wall W, Painted, PointMark, Radiation, Temperature, R, G, B');

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
      const painted = this.painted.has(key) ? 1 : 0;
      const rad = this.radiation.get(key) || 0;
      const temp = this.temperature.get(key) || 0;
      lines.push(`${x} ${y} ${wallN} ${wallE} ${wallS} ${wallW} ${painted} 0 ${rad} ${temp} 0 0 0`);
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

export function __initRobotField() {
  field.reset();

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

export function robot_left() {
  if (field.hasWallLeft()) {
    robotError('слева стена');
  }
  field.robotX--;
}

export function robot_right() {
  if (field.hasWallRight()) {
    robotError('справа стена');
  }
  field.robotX++;
}

export function robot_up() {
  if (field.hasWallUp()) {
    robotError('сверху стена');
  }
  field.robotY--;
}

export function robot_down() {
  if (field.hasWallDown()) {
    robotError('снизу стена');
  }
  field.robotY++;
}

export function robot_paint() {
  field.paint();
}

export function robot_left_free() {
  return !field.hasWallLeft();
}

export function robot_right_free() {
  return !field.hasWallRight();
}

export function robot_top_free() {
  return !field.hasWallUp();
}

export function robot_bottom_free() {
  return !field.hasWallDown();
}

export function robot_left_wall() {
  return field.hasWallLeft();
}

export function robot_right_wall() {
  return field.hasWallRight();
}

export function robot_top_wall() {
  return field.hasWallUp();
}

export function robot_bottom_wall() {
  return field.hasWallDown();
}

export function robot_cell_painted() {
  return field.isPainted();
}

export function robot_cell_clean() {
  return !field.isPainted();
}

export function robot_radiation() {
  return field.getRadiation();
}

export function robot_temperature() {
  return field.getTemperature();
}
