/// <reference path = "elm-core.d.ts" />

/**
 * Functions exported from the Wasm module
 */
interface ElmWasmExports {
  getUnit: () => number;
  getNil: () => number;
  getTrue: () => number;
  getFalse: () => number;
  getNextFieldGroup: () => number;
  getWriteAddr: () => number;
  getMaxWriteAddr: () => number;
  readFloat: (addr: number) => number;
  writeFloat: (addr: number, value: number) => void;
  startWrite: () => number;
  finishedWritingAt: (addr: number) => void;
  callClosure: (addr: number) => number;
  collectGarbage: () => void;
}

/**
 * App-specific info generated by the Elm compiler
 */
interface ElmWasmAppTypes {
  ctors: NameToInt & IntToName; // compile time
  fields: NameToInt & IntToName; // compile time
  fieldGroupNames: string[]; // compile time
  fieldGroups: NameToInt & IntToName; // runtime (app init)
}

interface NameToInt {
  [name: string]: number;
}
interface IntToName {
  [int: number]: string;
}

/*********************************************************************************************
 * `createElmWasmWrapper`
 *
 * Wrap a WebAssembly instance and connect it to Elm's kernel JavaScript.
 * WebAssembly doesn't support Web APIs directly yet, so we need JS to implement most effects.
 *
 * After compiling TS to JS, this function definition must be inserted into Elm's generated JS.
 * It should be the last thing before the `var` statement that assigns to the `main` value.
 * From there, it can call any function in the generated JS and even overwrite some of them!
 * The code insertion requires a build step for now - I'm using Makefiles and Bash scripts -
 * but eventually the Elm compiler will do it.
 *
 * @param instance  A WebAssembly instance containing a compiled Elm app
 * @param memory    The `ArrayBuffer` representing the memory area shared between JS and Wasm
 * @param appTypes  App-specific type info generated by the Elm compiler
 *
 /********************************************************************************************/
function createElmWasmWrapper(
  instance: WebAssembly.Instance,
  memory: ArrayBuffer,
  appTypes: ElmWasmAppTypes
) {
  /* --------------------------------------------------

               INITIALISATION & CONSTANTS

  -------------------------------------------------- */

  const mem32 = new Uint32Array(memory);
  const mem16 = new Uint16Array(memory);
  const wasmExports: ElmWasmExports = instance.exports;

  const wasmConstAddrs = (function() {
    const Unit = wasmExports.getUnit();
    const Nil = wasmExports.getNil();
    const True = wasmExports.getTrue();
    const False = wasmExports.getFalse();
    return {
      Unit,
      Nil,
      True,
      False,
      [Unit]: _Utils_Tuple0,
      [Nil]: _List_Nil,
      [True]: true,
      [False]: false
    };
  })();

  appTypes.fieldGroupNames.forEach(name => {
    const addr = wasmExports.getNextFieldGroup();
    appTypes.fieldGroups[name] = addr;
    appTypes.fieldGroups[addr] = name;
  });

  const CLOSURE_N_MASK = 0xffff0000;
  const CLOSURE_MAX_MASK = 0xffff0000;
  const WORD = 4;
  const TAG_MASK = 0xf;
  const SIZE_MASK = 0xfffffff0;
  const SIZE_SHIFT = 4;

  const textDecoder = new TextDecoder('utf-16le');
  const identity = (f: Function) => f;
  const elmFunctionWrappers = [identity, identity, F2, F3, F4];

  enum Tag {
    Int = 0x0,
    Float = 0x1,
    Char = 0x2,
    String = 0x3,
    List = 0x4,
    Tuple2 = 0x5,
    Tuple3 = 0x6,
    Custom = 0x7,
    Record = 0x8,
    Closure = 0x9,
    GcException = 0xa,
    GcStackEmpty = 0xb,
    GcStackPush = 0xc,
    GcStackPop = 0xd,
    GcStackTailCall = 0xe,
    Unused = 0xf
  }

  interface Header {
    tag: Tag;
    size: number;
  }

  /* --------------------------------------------------

                READ ELM VALUE FROM WASM

  -------------------------------------------------- */

  function readValue(addr: number): any {
    const index = addr / WORD;
    const header = mem32[index];
    const tag: Tag = header & TAG_MASK;
    const size = (header & SIZE_MASK) >> SIZE_SHIFT;

    switch (tag) {
      case Tag.Int: {
        return mem32[index + 1];
      }
      case Tag.Float: {
        return wasmExports.readFloat(addr + WORD);
      }
      case Tag.Char: {
        const idx16 = index << 1;
        const word0 = mem16[idx16 + 1];
        const word1 = mem16[idx16 + 2];
        let s = String.fromCharCode(word0);
        if (word1) s += String.fromCharCode(word1);
        return _Utils_chr(s);
      }
      case Tag.String: {
        const idx16 = index << 1;
        let size16 = size << 1;
        if (mem16[idx16 + size16 - 1] === 0) size16--;
        return textDecoder.decode(mem16.slice(idx16, size16));
      }
      case Tag.List: {
        return addr === wasmConstAddrs.Nil
          ? _List_Nil
          : _List_Cons(
              readValue(mem32[index + 1]),
              readValue(mem32[index + 2])
            );
      }
      case Tag.Tuple2: {
        return _Utils_Tuple2(
          readValue(mem32[index + 1]),
          readValue(mem32[index + 2])
        );
      }
      case Tag.Tuple3: {
        return _Utils_Tuple3(
          readValue(mem32[index + 1]),
          readValue(mem32[index + 2]),
          readValue(mem32[index + 3])
        );
      }
      case Tag.Custom: {
        const elmConst = wasmConstAddrs[addr]; // True/False/Unit
        if (elmConst) return elmConst;
        const wasmCtor = mem32[index + 1];
        const jsCtor = appTypes.ctors[wasmCtor];
        const custom: Record<string, any> = { $: jsCtor };
        let fieldCharCode = 'a'.charCodeAt(0);
        for (let i = index + 2; i < index + size; i++) {
          const field = String.fromCharCode(fieldCharCode++);
          const childAddr = mem32[i];
          custom[field] = readValue(childAddr);
        }
        return custom;
      }
      case Tag.Record: {
        const record: Record<string, any> = {};
        const fgIndex = mem32[index + 1] / WORD;
        const fgSize = mem32[fgIndex];
        for (let i = 1; i <= fgSize; i++) {
          const fieldId = mem32[fgIndex + i];
          const valAddr = mem32[index + 1 + i];
          const fieldName = appTypes.fields[fieldId];
          const value = readValue(valAddr);
          record[fieldName] = value;
        }
        return record;
      }
      case Tag.Closure: {
        const argsInfo = mem32[index + 1];
        const n_values = argsInfo & CLOSURE_N_MASK;
        const max_values = argsInfo & CLOSURE_MAX_MASK;
        const arity = max_values - n_values;
        const evaluator = mem32[index + 2];
        const freeVars: any[] = [];
        for (let i = index + 3; i < index + size; i++) {
          freeVars.push(readValue(mem32[i]));
        }
        const wasmCallback = createWasmCallback(evaluator, freeVars);
        const FN = elmFunctionWrappers[arity];
        return FN(wasmCallback);
      }
      default:
        throw new Error(
          `Tried to decode value with unsupported tag "${Tag[tag]}"`
        );
    }
  }

  function createWasmCallback(evaluator: number, freeVars: any[]) {
    return function wasmCallback() {
      const args = freeVars.slice();
      for (let i = 0; i < arguments.length; i++) {
        args.push(arguments[i]);
      }
      let addr = 0;
      handleWasmWrite((startIndex: number) => {
        const max_values = args.length << 16;
        const n_values = args.length;
        const builder: WasmBuilder = {
          body: [n_values | max_values, evaluator],
          jsChildren: args,
          writer: null
        };
        const result = writeFromBuilder(startIndex, builder, Tag.Closure);
        addr = result.addr;
        return result.nextIndex;
      });
      if (!addr) throw new Error('Failed to write to Wasm');
      wasmExports.callClosure(addr);
    };
  }

  /* --------------------------------------------------

                WRITE ELM VALUES TO WASM

  -------------------------------------------------- */

  let maxWriteIndex32: number;
  let maxWriteIndex16: number;

  function write32(index: number, value: number) {
    if (index > maxWriteIndex32) throw new Error('Wasm heap overflow');
    mem32[index] = value;
  }

  function write16(index: number, value: number) {
    if (index > maxWriteIndex16) throw new Error('Wasm heap overflow');
    mem16[index] = value;
  }

  function handleWasmWrite(writer: (nextIndex: number) => number) {
    for (let attempts = 0; attempts < 2; attempts++) {
      try {
        const maxAddr = wasmExports.getMaxWriteAddr();
        maxWriteIndex16 = maxAddr >> 1;
        maxWriteIndex32 = maxAddr >> 2;
        const addr = wasmExports.getWriteAddr();
        const startIndex = addr >> 2;
        const finishIndex = writer(startIndex);
        wasmExports.finishedWritingAt(finishIndex << 2);
        return;
      } catch (e) {
        wasmExports.collectGarbage();
      }
    }
    throw new Error('Failed to write to Wasm');
  }

  /**
   * Write an Elm value to the Wasm memory
   * Serialises to bytes before writing
   * May throw an error
   */
  function writeValue(value: any) {
    handleWasmWrite(nextIndex => {
      const result = writeValueHelp(value, nextIndex);
      return result.nextIndex;
    });
  }

  type TypeInfo =
    | {
        kind: 'tag';
        value: Tag;
      }
    | {
        kind: 'constAddr';
        value: number;
      };

  type WasmBuilder =
    | {
        body: null;
        jsChildren: null;
        writer: (addr: number) => number;
      }
    | {
        body: number[];
        jsChildren: any[];
        writer: null;
      };

  function writeValueHelp(
    nextIndex: number,
    value: any
  ): { addr: number; nextIndex: number } {
    const typeInfo: TypeInfo = detectElmType(value);
    if (typeInfo.kind === 'constAddr') {
      return { addr: typeInfo.value, nextIndex };
    }
    const tag: Tag = typeInfo.value;
    const builder: WasmBuilder = wasmBuilder(tag, value);

    return writeFromBuilder(nextIndex, builder, tag);
  }

  function detectElmType(elmValue: any): TypeInfo {
    switch (typeof elmValue) {
      case 'number': {
        // There's no way to tell `1 : Int` from `1.0 : Float` at this low level. But `1.2` is definitely a Float.
        // So _for now_ take a _horribly unsafe_ guess, by checking if it's a round number or not.
        // Not cool. This is Elm! Long term, the ambiguity needs to be solved at some higher level.
        // Maybe some lib like `JSON.Encode` so the app dev decides? Pity for it not to be automatic though!
        const isRoundNumberSoGuessInt = elmValue === (elmValue | 0);
        return {
          kind: 'tag',
          value: isRoundNumberSoGuessInt ? Tag.Int : Tag.Float
        };
      }
      case 'string':
        return { kind: 'tag', value: Tag.String };

      case 'boolean':
        return {
          kind: 'constAddr',
          value: elmValue ? wasmConstAddrs.True : wasmConstAddrs.False
        };

      case 'function':
        return { kind: 'tag', value: Tag.Closure };

      case 'object': {
        if (elmValue instanceof String) {
          return { kind: 'tag', value: Tag.Char };
        }
        switch (elmValue.$) {
          case undefined:
            return { kind: 'tag', value: Tag.Record };
          case '[]':
            return { kind: 'constAddr', value: wasmConstAddrs.Nil };
          case '::':
            return { kind: 'tag', value: Tag.List };
          case '#0':
            return { kind: 'constAddr', value: wasmConstAddrs.Unit };
          case '#2':
            return { kind: 'tag', value: Tag.Tuple2 };
          case '#3':
            return { kind: 'tag', value: Tag.Tuple3 };
          default:
            return { kind: 'tag', value: Tag.Custom };
        }
      }
    }
    console.error(elmValue);
    throw new Error('Cannot determine type of Elm value');
  }

  function wasmBuilder(tag: Tag, value: any): WasmBuilder {
    switch (tag) {
      case Tag.Int:
        return {
          body: [value],
          jsChildren: [],
          writer: null
        };
      case Tag.Float:
        return {
          body: null,
          jsChildren: null,
          writer: (addr: number) => {
            wasmExports.writeFloat(addr, value);
            return 2; // words written
          }
        };
      case Tag.Char:
      case Tag.String:
        return {
          body: null,
          jsChildren: null,
          writer: (addr: number) => writeString(value, addr)
        };
      case Tag.Tuple2:
      case Tag.List:
        return {
          body: [],
          jsChildren: [value.a, value.b],
          writer: null
        };
      case Tag.Tuple3:
        return {
          body: [],
          jsChildren: [value.a, value.b, value.c],
          writer: null
        };
      case Tag.Custom: {
        const jsCtor: string = value.$;
        const jsChildren: any[] = [];
        Object.keys(value).forEach(k => {
          if (k !== '$') jsChildren.push(value[k]);
        });
        return {
          body: [appTypes.ctors[jsCtor]],
          jsChildren,
          writer: null
        };
      }
      case Tag.Record: {
        const keys = Object.keys(value);
        keys.sort();
        const fgName = keys.join('$');
        const fgAddr = appTypes.fieldGroups[fgName];
        return {
          body: [fgAddr],
          jsChildren: keys.map(k => value[k]),
          writer: null
        };
      }
      case Tag.Closure: {
        break; // TODO
      }
    }
    console.error(value);
    throw new Error(`Can't write to WebAssembly for tag "${Tag[tag]}"`);
  }

  function writeFromBuilder(
    nextIndex: number,
    builder: WasmBuilder,
    tag: Tag
  ): { addr: number; nextIndex: number } {
    if (builder.writer) {
      const headerIndex = nextIndex;
      const wordsWritten = builder.writer((headerIndex + 1) * WORD);
      const size = 1 + wordsWritten;
      write32(headerIndex, encodeHeader({ tag, size }));
      return {
        addr: headerIndex,
        nextIndex: headerIndex + size
      };
    } else {
      const { body, jsChildren } = builder;
      const childAddrs: number[] = [];
      jsChildren.forEach(child => {
        const update = writeValueHelp(nextIndex, child);
        childAddrs.push(update.addr);
        nextIndex = update.nextIndex;
      });

      const addr = nextIndex >> 2;
      const size = 1 + body.length + childAddrs.length;
      write32(nextIndex++, encodeHeader({ tag, size }));
      body.forEach(word => {
        write32(nextIndex++, word);
      });
      childAddrs.forEach(pointer => {
        write32(nextIndex++, pointer);
      });
      return { addr, nextIndex };
    }
  }

  function encodeHeader({ tag, size }: Header): number {
    return tag | (size << SIZE_SHIFT);
  }

  function writeString(s: string, idx32: number): number {
    const offset = idx32 << 1;
    const lenAligned = s.length + (s.length % 2); // for odd length, write an extra word (gets coerced to 0)
    for (let i = 0; i < lenAligned; i++) {
      write16(offset + i, s.charCodeAt(i));
    }
    const wordsWritten = lenAligned >> 1;
    return wordsWritten;
  }

  /* --------------------------------------------------

                    EXPORTS

  -------------------------------------------------- */

  return {
    readValue,
    writeValue
  };
}
