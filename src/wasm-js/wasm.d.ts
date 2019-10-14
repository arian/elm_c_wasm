declare const mem32: Uint32Array;
declare const mem16: Uint16Array;
declare const wasmExports: {
  getUnit: () => number;
  getNil: () => number;
  getTrue: () => number;
  getFalse: () => number;
  getNextFieldGroup: () => number;
  writeFloat: (addr: number, value: number) => void;
  readFloat: (addr: number) => number;
  getWriteAddr: () => number;
  getMaxWriteAddr: () => number;
  startWrite: () => number;
  finishWrite: (end: number) => void;
  executeThunk: (addr: number) => number;
  collectGarbage: () => void;
};
