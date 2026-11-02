import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { execFileSync } from 'node:child_process';
const repo = path.resolve(import.meta.dirname, '../..');
const binary = path.join(repo, 'artifacts/device-db-core/native-driver-core.exe');
const manifest = JSON.parse(execFileSync(binary, ['detectors'], {encoding:'utf8', maxBuffer:8*1024*1024, windowsHide:true}));
if (manifest.serverRequired !== false || manifest.hardwareDetectionStarted !== false) throw Error('Unsafe detector manifest');
const canonical = (name, transport, matcher) => {
  const kind = transport === 'hid-wrapped' ? 'hid' : transport;
  const keys = kind === 'hid' ? ['vid','pid','interface','usagePage','usage','bus'] : kind === 'i2c-pci' ? ['vendor','device','subvendor','subdevice','address'] : [];
  return JSON.stringify([name,kind,...keys.map(k=>matcher[k]??-1)]);
};
const compiled = new Map();
for(const record of manifest.detectors) {
  const key = canonical(record.name,record.transport,record);
  compiled.set(key,[...(compiled.get(key)??[]),record]);
}
const catalog = JSON.parse(fs.readFileSync(path.join(repo,'device-db/catalog.json')));
const mappings = [];
for(const r of catalog.records) {
  if(r.source.source!=='openrgb') continue;
  const matches = compiled.get(canonical(r.name,r.transport,r.matcher));
  if(matches) mappings.push({recordId:r.id,detectorName:r.name,transport:r.transport,provider:'openrgb-driver-core',status:'native-experimental',hardwareVerified:false});
}
const countBy = key => Object.fromEntries([...new Set(manifest.detectors.map(d=>d[key]))].sort().map(k=>[k,manifest.detectors.filter(d=>d[key]===k).length]));
const summary = {schemaVersion:1,compiledRegistrations:manifest.detectors.length,distinctDetectorNames:new Set(manifest.detectors.map(d=>d.name)).size,
  mappedDatabaseRecords:mappings.length,transports:countBy('transport'),serverRequired:false,qtRequired:false,hardwareVerified:false,
  sourceRevision:'cc71485c803842f065353e65426e3a28dca1e6f3',compiledTranslationUnits:1083};
fs.writeFileSync(path.join(repo,'device-db/core/compiled-detectors.json'),JSON.stringify({...summary,detectors:manifest.detectors},null,2)+'\n');
fs.writeFileSync(path.join(repo,'device-db/providers.json'),JSON.stringify({schemaVersion:1,summary,mappings},null,2)+'\n');
const files = ['native-driver-core.exe','hidapi-hotplug.dll','libusb-1.0.dll','PawnIOLib.dll'].map(name=>({file:name,sha256:crypto.createHash('sha256').update(fs.readFileSync(path.join(path.dirname(binary),name))).digest('hex')}));
fs.writeFileSync(path.join(repo,'artifacts/device-db-core/build-manifest.json'),JSON.stringify({...summary,files},null,2)+'\n');
console.log(JSON.stringify(summary,null,2));
