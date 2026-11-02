// Static source inspection only: never evaluate or import upstream JavaScript/C++.
export function tokens(source) {
  const result = [];
  const pattern = /\s+|\/\/[^\n]*|\/\*[\s\S]*?\*\/|(?:u8|[LuU])?"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'|0[xX][\da-fA-F]+[uUlL]*|\d+(?:\.\d+)?[uUlLfF]*|[A-Za-z_$][\w$]*|===|!==|>>>|<<|>>|==|!=|<=|>=|&&|\|\||=>|[^\s]/gy;
  let match, line = 1;
  while ((match = pattern.exec(source))) {
    const text = match[0];
    if (!/^\s|^\/\//.test(text) && !text.startsWith('/*')) result.push({ text, offset: match.index, line });
    line += (text.match(/\n/g) || []).length;
  }
  return result;
}

export function splitArguments(ts, start) {
  const args = []; let current = [], depth = 0;
  for (let i = start; i < ts.length; i++) {
    const t = ts[i].text;
    if (t === ')' && depth === 0) { if (current.length) args.push(current); return { args, end: i }; }
    if (t === ',' && depth === 0) { args.push(current); current = []; continue; }
    if ('([{'.includes(t)) depth++;
    if (')]}'.includes(t)) depth--;
    current.push(ts[i]);
  }
  throw new Error('Unterminated argument list');
}

function stringValue(text) {
  if (!/^(?:u8|[LuU])?["']/.test(text)) return undefined;
  const quoted = text.replace(/^(?:u8|[LuU])(?=")/, '');
  return quoted.slice(1, -1).replace(/\\(?:x([\da-fA-F]+)|u([\da-fA-F]{4})|([0-7]{1,3})|(.))/g,
    (_, hex, unicode, octal, ch) => hex ? String.fromCharCode(parseInt(hex, 16)) : unicode ? String.fromCharCode(parseInt(unicode, 16)) : octal ? String.fromCharCode(parseInt(octal, 8)) : ({ n: '\n', r: '\r', t: '\t', '0': '\0' }[ch] ?? ch));
}

const precedence = { '|': 1, '^': 2, '&': 3, '<<': 4, '>>': 4, '+': 5, '-': 5, '*': 6, '/': 6, '%': 6 };
export function constant(input, resolve = () => undefined) {
  const ts = typeof input === 'string' ? tokens(input) : input;
  let index = 0;
  function primary() {
    const t = ts[index++]?.text;
    if (t === undefined) throw Error('Missing value');
    if (t === '(') { const value = expression(0); if (ts[index++]?.text !== ')') throw Error('Unclosed expression'); return value; }
    if (t === '[') {
      const value = [];
      while (ts[index]?.text !== ']') {
        if (index >= ts.length) throw Error('Unclosed array');
        value.push(expression(0));
        if (ts[index]?.text === ',') index++; else if (ts[index]?.text !== ']') throw Error('Invalid array');
      }
      index++; return value;
    }
    if (t === '{') {
      const value = Object.create(null);
      while (ts[index]?.text !== '}') {
        if (index >= ts.length) throw Error('Unclosed object');
        const keyToken = ts[index++]?.text;
        const key = stringValue(keyToken ?? '') ?? (/^0x/i.test(keyToken ?? '') ? String(parseInt(keyToken, 16)) : keyToken);
        if (!key || ts[index++]?.text !== ':') throw Error('Unsupported object property');
        value[key] = expression(0);
        if (ts[index]?.text === ',') index++; else if (ts[index]?.text !== '}') throw Error('Invalid object');
      }
      index++; return value;
    }
    if (['+', '-', '~'].includes(t)) { const v = primary(); if (typeof v !== 'number') throw Error('Non-numeric unary'); return t === '-' ? -v : t === '~' ? ~v : v; }
    const str = stringValue(t);
    if (str !== undefined) {
      let value = str;
      while (stringValue(ts[index]?.text ?? '') !== undefined) value += stringValue(ts[index++].text);
      return value;
    }
    if (/^0[xX]/.test(t)) return parseInt(t.replace(/[uUlL]+$/, ''), 16);
    if (/^\d/.test(t)) { const v = Number(t.replace(/[uUlLfF]+$/, '')); if (!Number.isFinite(v)) throw Error('Invalid numeric literal'); return v; }
    if (t === 'true') return true;
    if (t === 'false') return false;
    const resolved = resolve(t);
    if (resolved === undefined) throw Error(`Unknown symbol ${t}`);
    return resolved;
  }
  function expression(min) {
    let left = primary();
    while ((precedence[ts[index]?.text] ?? -1) >= min) {
      const op = ts[index++].text, right = expression(precedence[op] + 1);
      if (op === '+' && typeof left === 'string' && typeof right === 'string') { left += right; continue; }
      if (typeof left !== 'number' || typeof right !== 'number') throw Error('Non-numeric operands');
      switch (op) {
        case '+': left += right; break; case '-': left -= right; break; case '*': left *= right; break;
        case '/': if (!right) throw Error('Divide by zero'); left = Math.trunc(left / right); break;
        case '%': if (!right) throw Error('Divide by zero'); left %= right; break;
        case '<<': left <<= right; break; case '>>': left >>= right; break;
        case '&': left &= right; break; case '|': left |= right; break; case '^': left ^= right; break;
      }
    }
    return left;
  }
  try { const value = expression(0); return index === ts.length && (typeof value !== 'number' || Number.isSafeInteger(value)) ? value : undefined; }
  catch { return undefined; }
}

export function definitions(source) {
  const map = new Map();
  const joined = source.replace(/\\\r?\n/g, '');
  // Do not mistake function-like macros for constants.
  for (const m of joined.matchAll(/^\s*#\s*define\s+(\w+)([^\r\n]*)/gm)) {
    if (m[2].startsWith('(')) continue;
    const expression = tokens(m[2]);
    if (expression.length) map.set(m[1], expression);
  }
  const ts = tokens(source);
  for (let i = 0; i < ts.length; i++) {
    if (ts[i].text !== 'enum') continue;
    let start = i + 1;
    while (start < ts.length && !['{', ';'].includes(ts[start].text)) start++;
    if (ts[start]?.text !== '{') continue;
    let previous;
    for (let j = start + 1; j < ts.length && ts[j].text !== '}';) {
      const name = ts[j++].text;
      if (!/^[A-Za-z_]\w*$/.test(name)) break;
      let expression;
      if (ts[j]?.text === '=') {
        j++; const exprStart = j; let depth = 0;
        while (j < ts.length) { const t = ts[j].text; if (!depth && [',', '}'].includes(t)) break; if (t === '(') depth++; if (t === ')') depth--; j++; }
        expression = ts.slice(exprStart, j);
      } else expression = tokens(previous ? `${previous}+1` : '0');
      map.set(name, expression); previous = name;
      if (ts[j]?.text === ',') j++; else break;
    }
  }
  // Common static/constexpr integral and string declarations; arrays remain unresolved.
  for (let i = 1; i < ts.length - 2; i++) {
    if (ts[i].text !== '=' || !/^[A-Za-z_]\w*$/.test(ts[i - 1].text)) continue;
    let end = i + 1;
    while (end < ts.length && ![';', '{', '}'].includes(ts[end].text)) end++;
    if (ts[end]?.text === ';' && end - i < 40 && !map.has(ts[i - 1].text)) map.set(ts[i - 1].text, ts.slice(i + 1, end));
  }
  return map;
}

export function resolver(local, global = new Map()) {
  const cache = new Map(), pending = new Set();
  const get = name => {
    if (cache.has(name)) return cache.get(name);
    if (pending.has(name)) return undefined;
    const ts = local.get(name) ?? global.get(name);
    if (!ts) return undefined;
    pending.add(name); const value = constant(ts, get); pending.delete(name);
    cache.set(name, value); return value;
  };
  return get;
}

export function macroCalls(source, accept = name => name.startsWith('REGISTER_')) {
  const ts = tokens(source), calls = [];
  for (let i = 0; i < ts.length - 1; i++) {
    if (!accept(ts[i].text) || ts[i + 1].text !== '(') continue;
    const prefix = source.slice(source.lastIndexOf('\n', ts[i].offset) + 1, ts[i].offset);
    if (/^\s*#/.test(prefix)) continue;
    const { args, end } = splitArguments(ts, i + 2);
    calls.push({ macro: ts[i].text, args, line: ts[i].line }); i = end;
  }
  return calls;
}

export function functions(source) {
  const ts = tokens(source), result = new Map();
  for (let i = 0; i < ts.length - 4; i++) {
    if (ts[i].text !== 'function' || !/^[\w$]+$/.test(ts[i + 1].text) || ts[i + 2].text !== '(') continue;
    const { args, end } = splitArguments(ts, i + 3);
    if (ts[end + 1]?.text !== '{') continue;
    let depth = 1, stop = end + 2;
    while (stop < ts.length && depth) { if (ts[stop].text === '{') depth++; if (ts[stop].text === '}') depth--; stop++; }
    result.set(ts[i + 1].text, { args: args.map(a => a.map(t => t.text).join('')), body: ts.slice(end + 2, stop - 1), line: ts[i].line });
  }
  return result;
}

// Read one literal property table without running its surrounding constructor or class.
export function propertyTables(source, property) {
  const ts = tokens(source), tables = [];
  for (let i = 0; i < ts.length - 3; i++) {
    if (ts[i].text !== property || ts[i + 1].text !== '=' || ts[i + 2].text !== '{') continue;
    let j = i + 3; const entries = [];
    while (j < ts.length && ts[j].text !== '}') {
      const key = constant([ts[j]]) ?? ts[j].text;
      if (ts[j + 1]?.text !== ':') break;
      j += 2; const start = j; let depth = 0;
      while (j < ts.length) {
        const t = ts[j].text;
        if (!depth && [',', '}'].includes(t)) break;
        if ('([{'.includes(t)) depth++;
        if (')]}'.includes(t)) depth--;
        j++;
      }
      const expression = ts.slice(start, j);
      entries.push({ key, value: constant(expression), expression: expression.map(t => t.text).join(' ') });
      if (ts[j]?.text === ',') j++; else break;
    }
    tables.push(entries);
  }
  return tables;
}
