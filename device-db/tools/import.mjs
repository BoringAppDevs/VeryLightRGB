import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { fileURLToPath } from 'node:url';
import { tokens, constant, definitions, resolver, macroCalls, functions, splitArguments, propertyTables } from './parse.mjs';

const repo = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const research = process.argv.includes('--research');
const destination = path.join(repo, research ? 'local/device-db-research' : 'device-db');
fs.mkdirSync(destination, { recursive: true });
const lock = JSON.parse(fs.readFileSync(path.join(repo, 'device-db/sources.lock.json')));
// The public SignalRGB snapshot has no repository-wide license grant. Keep its
// source-derived predicates/layouts in the ignored research cache, never releases.
if (!research) lock.sources = lock.sources.filter(s => s.id !== 'signalrgb');
const sha = value => crypto.createHash('sha256').update(value).digest('hex');
const slash = value => value.split(path.sep).join('/');
const text = ts => ts.map(t => t.text).join(' ');
const write = (name, value) => fs.writeFileSync(path.join(destination, name), JSON.stringify(value, null, 2) + '\n');
function walk(directory) {
  if (!fs.existsSync(directory)) return [];
  return fs.readdirSync(directory, { withFileTypes: true }).sort((a, b) => a.name.localeCompare(b.name, 'en')).flatMap(e =>
    e.isDirectory() ? walk(path.join(directory, e.name)) : e.isFile() ? [path.join(directory, e.name)] : []);
}
const records = [], families = [], issues = [], fileManifest = [];
function sourceRef(spec, relative, line, digest) {
  const hostPath = spec.id === 'openrgb' ? `/src/commit/${spec.revision}/` : `/-/blob/${spec.revision}/`;
  return { source: spec.id, revision: spec.revision, file: relative, line, sha256: digest,
    url: spec.repository + hostPath + relative.split('/').map(encodeURIComponent).join('/') + (line ? `#L${line}` : '') };
}
function ioSites(source, relative) {
  const ts = tokens(source), out = [];
  const known = /^(?:hid_(?:write|read|read_timeout|send_feature_report|get_feature_report)|i2c_smbus_\w+|libusb_\w+|NvAPI_\w+|send_report|get_report|write|read|control_transfer|bulk_transfer|send|recv)$/;
  for (let i = 0; i < ts.length - 1; i++) {
    if (!known.test(ts[i].text) || ts[i + 1].text !== '(') continue;
    if (['write', 'read', 'send', 'recv'].includes(ts[i].text) && ts[i - 1]?.text !== '.') continue;
    try {
      const { args } = splitArguments(ts, i + 2);
      out.push({ api: ts[i].text, file: relative, line: ts[i].line, arguments: args.map(text) });
    } catch { /* malformed upstream source is recorded by unresolved metadata */ }
  }
  return out;
}

function cpp(spec) {
  const root = path.join(repo, spec.cacheDirectory);
  const paths = walk(path.join(root, 'Controllers')).filter(p => /\.(?:cpp|h|hpp)$/.test(p));
  paths.push(...walk(path.join(root, 'pci_ids')).filter(p => /\.h$/.test(p)));
  for (const dir of ['SPDAccessor', 'hidapi_wrapper', 'i2c_smbus']) paths.push(...walk(path.join(root, dir)).filter(p => /\.h$/.test(p)));
  paths.push(...fs.readdirSync(root).filter(p => /\.h$/.test(p)).map(p => path.join(root, p)));
  const hidHeader = path.join(root, 'dependencies/hidapi-win/include/hidapi.h');
  if (fs.existsSync(hidHeader)) paths.push(hidHeader);
  const files = new Map(paths.map(file => {
    const source = fs.readFileSync(file, 'utf8');
    return [file, { source, defs: definitions(source), relative: slash(path.relative(root, file)), digest: sha(source) }];
  }));
  // Unique global definitions are useful for IDs in shared headers; ambiguity is never guessed.
  const global = new Map(), ambiguous = new Set(), byBasename = new Map();
  for (const [file, data] of files) {
    const name = path.basename(file); byBasename.set(name, [...(byBasename.get(name) ?? []), file]);
    for (const [key, value] of data.defs) {
      if (global.has(key) && text(global.get(key)) !== text(value)) ambiguous.add(key);
      else global.set(key, value);
    }
    fileManifest.push({ source: spec.id, file: data.relative, sha256: data.digest });
  }
  for (const key of ambiguous) global.delete(key);
  const environments = new Map();
  function environment(file, seen = new Set()) {
    if (environments.has(file)) return environments.get(file);
    if (seen.has(file) || !files.has(file)) return new Map();
    seen = new Set([...seen, file]);
    const data = files.get(file), local = new Map();
    for (const m of data.source.matchAll(/^\s*#\s*include\s*["<]([^">]+)[">]/gm)) {
      const candidates = [path.resolve(path.dirname(file), m[1]), path.resolve(root, m[1])];
      const indexed = byBasename.get(path.basename(m[1]));
      if (indexed?.length === 1) candidates.push(indexed[0]);
      const included = candidates.find(p => files.has(p));
      if (included) for (const pair of environment(included, seen)) local.set(...pair);
    }
    for (const pair of data.defs) local.set(...pair);
    environments.set(file, local); return local;
  }
  const familyMap = new Map();
  for (const [file, data] of files) {
    if (!data.relative.startsWith('Controllers/')) continue;
    const family = data.relative.split('/')[1], id = `openrgb:${family}`;
    const item = familyMap.get(id) ?? { id, source: spec.id, name: family, files: [], constants: [], io: [], recordCount: 0 };
    item.files.push(sourceRef(spec, data.relative, 1, data.digest));
    const get = resolver(environment(file), global);
    for (const [name, expr] of data.defs) {
      const value = constant(expr, get);
      if (typeof value === 'number' || typeof value === 'string') item.constants.push({ name, value, file: data.relative });
    }
    item.io.push(...ioSites(data.source, data.relative));
    familyMap.set(id, item);
    for (const call of macroCalls(data.source)) {
      if (!/^REGISTER_(?:(?:DYNAMIC_)?(?:HID|I2C|DETECTOR)|USB_SERIAL|WOOTING)/.test(call.macro)) continue;
      const raw = call.args.map(text), values = call.args.map(a => constant(a, get));
      if (call.macro === 'REGISTER_I2C_BUS_DETECTOR') continue;
      const name = typeof values[0] === 'string' ? values[0] : raw[0];
      const matcher = {}, unresolved = [];
      const assign = (key, index) => {
        if (typeof values[index] === 'number') matcher[key] = values[index];
        else { unresolved.push({ field: key, expression: raw[index] ?? '' }); }
      };
      let transport = 'custom', signatureComplete = false;
      const macro = call.macro.replace('REGISTER_DYNAMIC_', 'REGISTER_').replace('HID_WRAPPED_', 'HID_').replace(/_DISABLED$/, '');
      if (macro.startsWith('REGISTER_HID_DETECTOR')) {
        transport = 'hid';
        const suffix = macro.slice('REGISTER_HID_DETECTOR'.length);
        const letters = suffix.replace(/_ONLY$/, '').replace(/^_/, '');
        let n = 2;
        if (!suffix.endsWith('_ONLY')) { assign('vid', n++); assign('pid', n++); }
        for (const letter of letters) {
          const key = { I: 'interface', P: 'usagePage', U: 'usage', B: 'bus' }[letter];
          if (key) assign(key, n++); else unresolved.push({ field: 'macro', expression: call.macro });
        }
        signatureComplete = Number.isInteger(matcher.vid) && matcher.vid >= 0 && Number.isInteger(matcher.pid) && matcher.pid >= 0 && !unresolved.length;
      } else if (macro.startsWith('REGISTER_I2C_PCI_DETECTOR')) {
        transport = 'i2c-pci';
        ['vendor', 'device', 'subvendor', 'subdevice', 'address'].forEach((key, i) => assign(key, i + 2));
        signatureComplete = !unresolved.length;
      } else if (macro.startsWith('REGISTER_I2C_DRAM_DETECTOR')) {
        transport = 'smbus-dram'; assign('jedec', 2); assign('dramType', 3);
      } else if (macro.startsWith('REGISTER_I2C')) transport = 'smbus';
      else if (macro.startsWith('REGISTER_USB_SERIAL')) { transport = 'serial'; assign('vid', 2); assign('pid', 3); signatureComplete = !unresolved.length; }
      else if (macro.startsWith('REGISTER_WOOTING')) { transport = 'hid'; unresolved.push({ field: 'macro', expression: 'Custom Wooting registration: inspect function and firmware variants' }); }
      const record = {
        id: `openrgb:${sha([data.relative, call.macro, ...raw].join('|')).slice(0, 24)}`,
        name, family: id, transport, matcher, signatureComplete,
        enabledByUpstream: !call.macro.endsWith('_DISABLED'),
        status: 'protocol-source', nativeAdapter: family === 'CorsairLightingNodeController' ? 'corsair-lighting-node' : family === 'QMKController' && raw[1]?.includes('OpenRGB') ? 'qmk-openrgb' : null, hardwareVerified: false,
        detector: { macro: call.macro, function: raw[1], arguments: raw },
        source: sourceRef(spec, data.relative, call.line, data.digest), unresolved,
        // A registration can hide additional checks and firmware probes in the detector.
        requiresDetectorReview: true
      };
      if (macro === 'REGISTER_WOOTING_DETECTOR') {
        record.detector.function = 'DetectWootingControllers';
        assign('vid', 1); assign('basePid', 2);
        // Exact expansion of NONE_PID / XINP_PID / DINP_PID in this pinned source.
        if (Number.isInteger(matcher.vid) && Number.isInteger(matcher.basePid)) {
          for (const [variant, bits] of [['none', 2], ['xinput', 0], ['directinput', 1]]) {
            const expanded = structuredClone(record);
            expanded.id += `:${variant}`;
            expanded.matcher = { vid: matcher.vid, pid: matcher.basePid | bits };
            expanded.variant = variant; expanded.unresolved = []; expanded.signatureComplete = true;
            records.push(expanded); item.recordCount++;
          }
          continue;
        }
      }
      if (call.macro.startsWith('REGISTER_DYNAMIC_HID') && typeof values[0] !== 'string') {
        record.name = raw[1]?.includes('Vial') ? 'QMK VialRGB (user-configured IDs)' : 'QMK OpenRGB (user-configured IDs)';
        record.registrationType = 'dynamic-template';
      }
      if (record.nativeAdapter) record.status = 'native-experimental';
      records.push(record); item.recordCount++;
    }
  }
  families.push(...familyMap.values());
}

function js(spec) {
  const root = path.join(repo, spec.cacheDirectory);
  const paths = ['Plugins', 'Non-Release-Plugins'].flatMap(dir => walk(path.join(root, dir))).filter(p => p.endsWith('.js') && !p.endsWith('.test.js'));
  for (const file of paths) {
    const source = fs.readFileSync(file, 'utf8'), relative = slash(path.relative(root, file)), digest = sha(source);
    const funcs = functions(source), defs = definitions(source), get = resolver(defs);
    fileManifest.push({ source: spec.id, file: relative, sha256: digest });
    if (!funcs.has('Name') && !funcs.has('VendorId')) continue;
    const unresolved = [];
    function value(name, fallback = undefined) {
      const fn = funcs.get(name); if (!fn) return fallback;
      const at = fn.body.findIndex(t => t.text === 'return');
      const end = fn.body.findIndex((t, i) => i > at && t.text === ';');
      const result = at < 0 ? undefined : constant(fn.body.slice(at + 1, end < 0 ? fn.body.length : end), get);
      if (result === undefined) unresolved.push({ field: name, expression: text(fn.body) });
      return result ?? fallback;
    }
    const name = value('Name', path.basename(file, '.js')), matcher = {};
    const vid = value('VendorId'), pid = value('ProductId');
    if (Number.isInteger(vid)) matcher.vid = vid;
    if (Number.isInteger(pid)) matcher.pid = pid;
    let transport = String(value('Type', 'hid')).toLowerCase();
    const validate = funcs.get('Validate');
    if (validate) {
      // Preserve the full expression; extracted comparisons do NOT constitute a valid matcher.
      matcher.validateSource = text(validate.body);
    }
    const family = `signalrgb:${relative.replace(/\.js$/, '')}`;
    const record = {
      id: `signalrgb:${sha(relative).slice(0, 24)}`, name, family, transport, matcher,
      signatureComplete: Number.isInteger(vid) && Number.isInteger(pid) && !validate && !unresolved.length,
      enabledByUpstream: !relative.startsWith('Non-Release-Plugins/'),
      status: 'protocol-source', nativeAdapter: null, hardwareVerified: false,
      metadata: { publisher: value('Publisher', ''), deviceType: value('DeviceType', ''),
        validationRequiresReview: Boolean(validate), lifecycle: ['Initialize', 'Render', 'Shutdown'].filter(n => funcs.has(n)),
        imports: [...source.matchAll(/\bimport\s+[\s\S]*?\s+from\s+["']([^"']+)["']/g)].map(m => m[1]) },
      ledLayout: { canvasSize: value('Size', null), names: value('LedNames', null), positions: value('LedPositions', null) },
      source: sourceRef(spec, relative, funcs.get('Name')?.line ?? 1, digest), unresolved, requiresDetectorReview: true
    };
    // SignalRGB accepts PID arrays and table-driven plugins. Expand them into concrete identities.
    const pidFunction = funcs.get('ProductId');
    let variants = [];
    if (Array.isArray(pid) && pid.every(n => Number.isInteger(n) && n >= 0 && n <= 65535)) variants = pid.map(pid => ({ pid }));
    if (pidFunction && /^return Object \. keys \( \w+ \. PIDLibrary \) ;?$/.test(text(pidFunction.body))) {
      const tables = propertyTables(source, 'PIDLibrary');
      if (tables.length === 1) variants = tables[0].filter(e => Number.isInteger(Number(e.key)) && Number(e.key) >= 0 && Number(e.key) <= 65535)
        .map(e => ({ pid: Number(e.key), properties: e.value, propertySource: e.expression }));
    }
    let inserted = 0;
    if (variants.length && Number.isInteger(vid)) {
      const layouts = propertyTables(source, 'LEDLibrary');
      for (const variant of variants) {
        const expanded = structuredClone(record);
        expanded.id += `:${variant.pid.toString(16).padStart(4, '0')}`;
        expanded.matcher.pid = variant.pid;
        const modelName = typeof variant.properties === 'string' ? variant.properties : variant.properties?.name ?? variant.properties?.Name;
        if (modelName) expanded.name += ` · ${modelName}`;
        let properties = variant.properties;
        if (typeof properties === 'string' && layouts.length === 1) properties = layouts[0].find(e => e.key === properties)?.value;
        if (properties && typeof properties === 'object') {
          expanded.modelProperties = properties;
          const endpoint = properties.endpoint ?? properties.Endpoint;
          if (endpoint) {
            for (const [field, input] of [['interface', 'interface'], ['usage', 'usage'], ['usagePage', 'usage_page']])
              if (Number.isInteger(endpoint[input])) expanded.matcher[field] = endpoint[input];
          }
          expanded.ledLayout = { canvasSize: properties.size ?? null, names: properties.LedNames ?? null, positions: properties.LedPositions ?? null };
        }
        expanded.unresolved = expanded.unresolved.filter(e => e.field !== 'ProductId');
        expanded.signatureComplete = !validate && !expanded.unresolved.length;
        records.push(expanded); inserted++;
      }
    } else { records.push(record); inserted = 1; }
    const constants = [...defs].map(([name, ts]) => ({ name, value: constant(ts, get), file: relative })).filter(x => typeof x.value === 'number' || typeof x.value === 'string');
    families.push({ id: family, source: spec.id, name, files: [sourceRef(spec, relative, 1, digest)], constants, io: ioSites(source, relative), recordCount: inserted });
  }
  // Shared protocol implementations are indexed even when they are not a device plugin.
  for (const file of walk(path.join(root, 'Protocols')).filter(p => p.endsWith('.js') && !p.endsWith('.test.js'))) {
    const source = fs.readFileSync(file, 'utf8'), relative = slash(path.relative(root, file));
    const digest = sha(source); fileManifest.push({ source: spec.id, file: relative, sha256: digest });
    families.push({ id: `signalrgb:${relative}`, source: spec.id, name: path.basename(file), files: [sourceRef(spec, relative, 1, digest)], constants: [], io: ioSites(source, relative), recordCount: 0 });
  }
}

for (const spec of lock.sources) {
  const root = path.join(repo, spec.cacheDirectory);
  if (!fs.existsSync(root)) throw Error(`Missing source cache: run device-db/tools/fetch.ps1 (${spec.id})`);
  if (spec.id === 'openrgb') cpp(spec); else if (spec.id === 'signalrgb') js(spec);
}
records.sort((a, b) => a.id.localeCompare(b.id)); families.sort((a, b) => a.id.localeCompare(b.id));
if (new Set(records.map(r => r.id)).size !== records.length) throw Error('Duplicate record IDs');
const counts = key => Object.fromEntries([...new Set(records.map(r => r[key]))].sort().map(k => [k, records.filter(r => r[key] === k).length]));
const summary = {
  schemaVersion: 1, records: records.length, distinctNames: new Set(records.map(r => r.name.toLowerCase())).size,
  families: families.length, sourceFiles: fileManifest.length,
  exactUpstreamSignatures: records.filter(r => r.signatureComplete).length,
  entriesWithUnresolvedExpressions: records.filter(r => r.unresolved.length).length,
  transport: counts('transport'), sources: Object.fromEntries(lock.sources.map(s => [s.id, records.filter(r => r.source.source === s.id).length])),
  statuses: counts('status'),
  constants: families.reduce((n, f) => n + f.constants.length, 0), ioSites: families.reduce((n, f) => n + f.io.length, 0),
  ledLayouts: records.filter(r => r.ledLayout?.positions).length,
  executableStandaloneAdapters: ['corsair-lighting-node (7 USB IDs)', 'qmk-openrgb (firmware revisions 9/B/C/D; handshake required)'],
  nativeAdaptersInMainApplication: ['MSI MAG B850M MORTAR WIFI (MS-7E61)', 'Gainward RTX 5080 Phoenix'],
  note: 'Records are upstream detector registrations or plugins, not a count of hardware-verified native devices. No upstream code was executed.'
};
write('catalog.json', { schemaVersion: 1, sources: lock.sources, records });
write('protocols.json', { schemaVersion: 1, families });
write('source-files.json', { schemaVersion: 1, files: fileManifest.sort((a, b) => `${a.source}/${a.file}`.localeCompare(`${b.source}/${b.file}`)) });
write('summary.json', summary);
write('import-issues.json', { schemaVersion: 1, issues, unresolved: records.filter(r => r.unresolved.length).map(r => ({ id: r.id, name: r.name, source: r.source, expressions: r.unresolved })) });
console.log(JSON.stringify(summary, null, 2));
