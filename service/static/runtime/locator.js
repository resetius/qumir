// Source line of the latest call to a command marked NeedsLocator: the compiler
// calls __record_locator(line) right before such a call (qumir/modules/module.h).

let currentLine = 0;

export function __record_locator(line) {
  currentLine = Number(line);
}

export function currentLocatorLine() {
  return currentLine;
}
