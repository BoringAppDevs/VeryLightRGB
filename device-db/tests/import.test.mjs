import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import test from 'node:test';
import { constant, definitions, resolver, macroCalls, functions, propertyTables } from '../tools/parse.mjs';

const repo = path.resolve(import.meta.dirname, '../..');
const read = name => JSON.parse(fs.readFileSync(path.join(repo, 'device-db', name), 'utf8'));
const { records, sources } = read('catalog.json');
const { families } = read('protocols.json');
const hash = data => crypto.createHash('sha256').update(data).digest('hex');

test('C expressions preserve IDs, signed wildcards, precedence, and aliases without eval', () => {
  const defs = definitions('#define VID 0x1B1C\n#define PID (0xC00 | 11)\nenum { FIRST=7, NEXT, ALIAS=FIRST, LAST };');
  const get = resolver(defs);
  assert.equal(get('VID'), 6940); assert.equal(get('PID'), 3083);
  assert.equal(get('NEXT'), 8); assert.equal(get('LAST'), 8);
  assert.equal(constant('1 + 2 * 3 << 2'), 28);
  assert.equal(constant('-1'), -1);
  assert.equal(constant('"one" " two"'), 'one two');
  assert.equal(constant('process.exit()'), undefined);
  assert.equal(constant('1/0'), undefined);
  assert.equal(constant('UNKNOWN'), undefined);
  assert.equal(constant('1.5'), undefined);
  assert.deepEqual(constant('[[1,2],[3,4]]'), [[1, 2], [3, 4]]);
  assert.equal(resolver(definitions('#define A B\n#define B A'))('A'), undefined);
});

test('balanced registrations ignore comments, macro definitions, and commas inside quoted names', () => {
  const calls = macroCalls('// REGISTER_HID_DETECTOR("fake",f,1,2);\n#define REGISTER_HID_DETECTOR(a,b,c,d) whatever\nREGISTER_HID_DETECTOR_IPU("Device, edition", fn, VID, (PID+1), 2, 0xFF00, 1);');
  assert.equal(calls.length, 1); assert.equal(calls[0].line, 3);
  assert.equal(calls[0].args.length, 7); assert.equal(constant(calls[0].args[0]), 'Device, edition');
  const js = functions('export function VendorId(){ return 0x1234; } export function Validate(endpoint){if(endpoint.interface === 2){return true;}return false;}');
  assert.equal(js.get('Validate').body.at(-2).text, 'false');
});

test('all imported records resolve to pinned sources and never claim hardware validation', () => {
  assert.ok(records.length > 2000); assert.ok(families.length > 150);
  assert.ok(sources.every(s => s.id !== 'signalrgb'), 'Unlicensed research source entered the release catalog');
  assert.equal(new Set(records.map(r => r.id)).size, records.length);
  const familyIds = new Set(families.map(f => f.id));
  const digests = new Map();
  for (const file of read('source-files.json').files) {
    const spec = sources.find(s => s.id === file.source);
    const filename = path.join(repo, spec.cacheDirectory, file.file);
    const data = fs.readFileSync(filename);
    assert.equal(hash(data), file.sha256, `Source digest ${file.file}`);
    digests.set(`${file.source}/${file.file}`, file.sha256);
  }
  for (const record of records) {
    assert.ok(familyIds.has(record.family));
    assert.equal(record.hardwareVerified, false);
    assert.equal(record.requiresDetectorReview, true);
    assert.equal(digests.get(`${record.source.source}/${record.source.file}`), record.source.sha256);
    if (record.signatureComplete && record.transport === 'hid') {
      assert.ok(record.matcher.vid >= 0 && record.matcher.vid <= 65535);
      assert.ok(record.matcher.pid >= 0 && record.matcher.pid <= 65535);
    }
    if (record.source.file.startsWith('Non-Release-Plugins/')) assert.equal(record.enabledByUpstream, false);
  }
});

test('real upstream Corsair identities and protocol constants agree with executable adapter', () => {
  const expected = new Map([
    ['Corsair Lighting Node Pro', 0x0c0b], ['Corsair Lighting Node Core', 0x0c1a],
    ['Corsair Commander Pro', 0x0c10], ['Corsair LS100 Lighting Kit', 0x0c1e],
    ['Corsair 1000D Obsidian', 0x1d00], ['Corsair SPEC OMEGA RGB', 0x1d04], ['Corsair LT100', 0x0c23]
  ]);
  const adapters = records.filter(r => r.nativeAdapter === 'corsair-lighting-node');
  assert.equal(adapters.length, 7);
  for (const r of adapters) { assert.equal(r.matcher.vid, 0x1b1c); assert.equal(r.matcher.pid, expected.get(r.name)); }
  const family = families.find(f => f.id === 'openrgb:CorsairLightingNodeController');
  const values = Object.fromEntries(family.constants.map(c => [c.name, c.value]));
  assert.equal(values.CORSAIR_LIGHTING_NODE_WRITE_PACKET_SIZE, 65);
  assert.equal(values.CORSAIR_LIGHTING_NODE_PACKET_ID_DIRECT, 0x32);
  assert.equal(values.CORSAIR_LIGHTING_NODE_PACKET_ID_COMMIT, 0x33);
  assert.equal(values.CORSAIR_LIGHTING_NODE_PACKET_ID_PORT_STATE, 0x38);
});

test('Wooting macro expands controller-emulation variants to exact IDs', () => {
  const rows = records.filter(r => r.name === 'Wooting 60HE' && r.source.source === 'openrgb');
  assert.deepEqual(rows.map(r => r.matcher.pid).sort((a,b) => a-b), [0x1300, 0x1301, 0x1302]);
  assert.ok(rows.every(r => r.signatureComplete && r.matcher.vid === 0x31e3));
});

test('firmware-gated generic QMK registration never implies support from VID/PID alone', () => {
  const qmk = records.find(r => r.nativeAdapter === 'qmk-openrgb');
  assert.ok(qmk); assert.equal(qmk.signatureComplete, false);
  assert.equal(qmk.matcher.usagePage, 0xff60); assert.equal(qmk.matcher.usage, 0x61);
  assert.equal(qmk.registrationType, 'dynamic-template');
  assert.equal(qmk.hardwareVerified, false);
});

test('constructor tables parse without executing upstream JavaScript',()=>{
  const source='class Library { constructor(){ this.PIDLibrary={0x1CCA:"Bloody A70x",0x11A1:"Bloody A70x"}; } }';
  assert.equal(propertyTables(source,'PIDLibrary')[0].length,2);
  assert.ok(records.every(r => r.source.source !== 'signalrgb'));
});

test('summary counts describe records rather than inflated hardware support', () => {
  const summary = read('summary.json');
  assert.equal(summary.records, records.length);
  assert.equal(summary.distinctNames, new Set(records.map(r => r.name.toLowerCase())).size);
  assert.equal(summary.families, families.length);
  assert.equal(summary.ledLayouts, records.filter(r => r.ledLayout?.positions).length);
  assert.ok(summary.note.includes('not a count of hardware-verified'));
});
