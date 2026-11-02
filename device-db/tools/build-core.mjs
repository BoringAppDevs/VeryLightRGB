// Experimental isolated build of the Qt-free OpenRGB driver core. No hardware is opened by this script.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { execFile } from 'node:child_process';
import { promisify } from 'node:util';
const execute = promisify(execFile);
const repo = path.resolve(import.meta.dirname, '../..');
const root = path.join(repo, 'local/device-db-sources/headless');
const out = path.join(repo, 'local/device-db-sources/core-objects');
const compiler = path.join(repo, 'local/toolchain/llvm-mingw-20261020-ucrt-x86_64/bin/x86_64-w64-mingw32-clang++.exe');
const slash = p => p.replaceAll('\\', '/');
function walk(p) {
  return fs.readdirSync(p, { withFileTypes: true }).flatMap(d => d.isDirectory() ? walk(path.join(p, d.name)) : [path.join(p, d.name)]);
}
fs.mkdirSync(out, { recursive: true });
const dmHeader = path.join(root, 'DetectionManager.h');
const dmText = fs.readFileSync(dmHeader, 'utf8');
if (!dmText.includes('friend class DeviceDbCore;')) {
  const updated = dmText.replace(/class DetectionManager\s*\{/, '$&\n    friend class DeviceDbCore;');
  if (updated === dmText) throw Error('Pinned DetectionManager header shape changed');
  fs.writeFileSync(dmHeader, updated);
}
const rgbHeader=path.join(root,'RGBController/RGBController.h');
const rgbHeaderText=fs.readFileSync(rgbHeader,'utf8');
if(!rgbHeaderText.includes('friend class DeviceDbCore;')) {
  const changed=rgbHeaderText.replace(/class RGBController : public RGBControllerInterface\s*\{/, '$&\n    friend class DeviceDbCore;');
  if(changed===rgbHeaderText) throw Error('Pinned RGBController header shape changed');
  fs.writeFileSync(rgbHeader,changed);
}
const sinowealth = path.join(root, 'Controllers/SinowealthController/SinowealthControllerDetect.cpp');
const sinowealthText = fs.readFileSync(sinowealth, 'utf8');
if (sinowealthText.includes('#include "GenesisXenon200Controller.cpp"')) fs.writeFileSync(sinowealth, sinowealthText.replace('#include "GenesisXenon200Controller.cpp"', '#include "GenesisXenon200Controller.h"'));
const controllerFile = path.join(root, 'RGBController/RGBController.cpp');
const controllerText = fs.readFileSync(controllerFile, 'utf8');
if (!controllerText.includes('// DeviceDb: initialize flags before the thread starts.')) {
  let updated = controllerText.replace(/(SignalShutdown\s*=\s*false;)/, '$1\n    // DeviceDb: initialize flags before the thread starts.\n    CallFlag_UpdateLEDs = false;\n    CallFlag_UpdateMode = false;');
  updated = updated.replace(/(void RGBController::DeviceCallThreadFunction\(\)[\s\S]*?DeviceCallScope scope\(this\);)\s*CallFlag_UpdateLEDs = false;\s*CallFlag_UpdateMode = false;/, '$1');
  if (updated === controllerText) throw Error('Pinned RGBController thread initialization shape changed');
  fs.writeFileSync(controllerFile, updated);
}
const resourceFile=path.join(root,'ResourceManager.cpp');
const resourceText=fs.readFileSync(resourceFile,'utf8');
if(!resourceText.includes('VLR_DEVICE_DB_CORE_DATA')) {
  const before='const wchar_t* appdata = _wgetenv(L"APPDATA");';
  const after=`std::wstring deviceDbFallback;
    const wchar_t* appdata = _wgetenv(L"VLR_DEVICE_DB_CORE_DATA");
    if (!appdata) {
        const wchar_t* temporary = _wgetenv(L"TEMP");
        deviceDbFallback = (temporary ? std::wstring(temporary) : std::wstring(L".")) + L"/VeryLightRGB-DriverCore-" + std::to_wstring(GetCurrentProcessId());
        appdata = deviceDbFallback.c_str();
    }`;
  if(!resourceText.includes(before)) throw Error('Pinned configuration-directory shape changed');
  fs.writeFileSync(resourceFile,resourceText.replace(before,after));
}
const pro = fs.readFileSync(path.join(root, 'OpenRGB.pro'), 'utf8');
const block = pro.slice(pro.indexOf('\nSOURCES +=') + 1).split(/\n\s*\n/)[0];
const common = [...block.matchAll(/\b([\w/.-]+\.(?:cpp|c))\b/g)].map(m => m[1]);
const sources = walk(path.join(root, 'Controllers')).filter(p => p.endsWith('.cpp') && (!/_(?:Linux|FreeBSD|MacOS)/.test(p) || /_Windows/.test(p)));
sources.push(...common.map(p => path.join(root, p)));
sources.push(...[
  'dependencies/libe131/src/e131.c', 'dependencies/hueplusplus-1.2.0/src/WinHttpHandler.cpp', 'dependencies/NVFC/nvapi.cpp',
  'i2c_smbus/Windows/i2c_smbus_amdadl.cpp', 'i2c_smbus/Windows/i2c_smbus_nvapi.cpp', 'i2c_smbus/Windows/i2c_smbus_pawnio.cpp',
  'super_io/super_io_pawnio.cpp', 'scsiapi/scsiapi_windows.c', 'serial_port/find_usb_serial_port_win.cpp', 'wmi/wmi.cpp', 'AutoStart/AutoStart-Windows.cpp'
].map(p => path.join(root, p)));
sources.push(path.join(repo, 'device-db/core/direct_main.cpp'));
sources.push(...walk(path.join(repo, 'local/device-db-sources/mbedtls/library')).filter(p => p.endsWith('.c')));
const includeDirs = new Set(walk(path.join(root, 'Controllers')).filter(p => p.endsWith('.h')).map(p => path.dirname(p)));
for (const p of ['.', 'dependencies/CRCpp', 'dependencies/hueplusplus-1.2.0/include', 'dependencies/hueplusplus-1.2.0/include/hueplusplus',
  'dependencies/httplib', 'dependencies/json', 'dependencies/mdns', 'dmiinfo', 'hidapi_wrapper', 'i2c_smbus', 'i2c_tools', 'interop', 'net_port',
  'pci_ids', 'scsiapi', 'serial_port', 'super_io', 'AutoStart', 'KeyboardLayoutManager', 'RGBController', 'SPDAccessor', 'dependencies/stb',
  'dependencies/display-library/include', 'dependencies/hidapi-hotplug-win/include', 'dependencies/libusb-1.0.27/include', 'dependencies/mbedtls-3.2.1/include',
  'dependencies/NVFC', 'dependencies/PawnIO', 'i2c_smbus/Windows', 'wmi', 'dependencies/libe131/src']) includeDirs.add(path.join(root, p));
const flags = ['-std=c++17', '-O1', '-DWIN32', '-D_MBCS', '-DOPENRGB_HEADLESS', '-DHID_HOTPLUG_ENABLED=1', '-DWIN32_LEAN_AND_MEAN', '-D_CRT_SECURE_NO_WARNINGS',
  '-D_WINSOCK_DEPRECATED_NO_WARNINGS', '-DVERSION_STRING="device-db-core"', '-DBUILDDATE_STRING="2026-11-01"', '-DGIT_COMMIT_ID="cc71485c803842f065353e65426e3a28dca1e6f3"',
  '-DGIT_COMMIT_DATE="pinned"', '-DGIT_BRANCH="headless"', '-fms-extensions', '-Wno-unknown-pragmas', '-Wno-deprecated-declarations'];
// Keep __FILE__, assertions, and debug metadata independent of the builder's profile.
flags.push(...[...new Set([repo, slash(repo)])].map(p => `-ffile-prefix-map=${p}=.`), '-fdebug-compilation-dir=.');
const response = path.join(out, 'flags.rsp');
const quote = a => `"${slash(a).replaceAll('"', '\\"')}"`;
fs.writeFileSync(response, [...flags, ...[...includeDirs].sort().flatMap(p => ['-I', p])].map(quote).join('\n'));
const unique = [...new Set(sources)];
// Header changes must invalidate objects too, including generated compatibility headers.
const headerHash = crypto.createHash('sha256');
for (const file of [...walk(root), ...walk(path.join(repo,'local/device-db-sources/mbedtls')), ...walk(path.join(repo,'device-db/core'))].filter(p=>/\.(h|hpp)$/.test(p)).sort())
  headerHash.update(slash(path.relative(repo,file))).update(fs.readFileSync(file));
const headersDigest = headerHash.digest('hex');
const errors = []; let index = 0, finished = 0;
const objects = unique.map(source => path.join(out, crypto.createHash('sha256').update(source).digest('hex').slice(0, 20) + '.o'));
async function worker() {
  while (index < unique.length) {
    const i = index++, source = unique[i], object = objects[i];
    const args = ['@' + response];
    if (source.endsWith('NanoleafController.cpp')) args.push('-D_WIN32_WINNT=0x0A00');
    if (source.endsWith('TimePattern.cpp')) args.push('-include', path.join(repo, 'device-db/core/mingw_compat.hpp'));
    if (slash(source).includes('/mbedtls/library/')) args.push('-I', path.join(repo, 'local/device-db-sources/mbedtls/library'));
    if (source.endsWith('.c')) args.push('-x', 'c', '-std=c11');
    const digest = crypto.createHash('sha256').update(fs.readFileSync(source)).update(fs.readFileSync(response)).update(headersDigest).update(JSON.stringify(args)).digest('hex');
    if (fs.existsSync(object + '.hash') && fs.readFileSync(object + '.hash', 'utf8') === digest) { finished++; continue; }
    try {
      await execute(compiler, [...args, '-c', source, '-o', object], { maxBuffer: 2 * 1024 * 1024, windowsHide: true });
      fs.writeFileSync(object + '.hash', digest);
    } catch (e) {
      errors.push({ source: slash(path.relative(root, source)), output: e.stderr ?? e.message });
      if (errors.length <= 8) console.error(errors.at(-1));
    }
    finished++;
    if (finished % 100 === 0) console.log(`${finished}/${unique.length} units; ${errors.length} failures`);
  }
}
console.log(`Building ${unique.length} driver/core units with 6 workers`);
await Promise.all(Array.from({ length: 6 }, worker));
fs.writeFileSync(path.join(out, 'errors.json'), JSON.stringify(errors, null, 2));
if (errors.length) { console.error(`${errors.length} units failed; see ${out}/errors.json`); process.exitCode = 1; }
else {
  await execute(process.execPath,[path.join(repo,'device-db/tools/build-transports.mjs')],{maxBuffer:4*1024*1024,windowsHide:true});
  const output = path.join(repo, 'artifacts/device-db-core'); fs.mkdirSync(output, { recursive: true });
  const libs = ['-lshell32', '-ladvapi32', '-luser32', '-lole32', '-loleaut32', '-lws2_32', '-liphlpapi', '-lsetupapi', '-lshlwapi', '-lwbemuuid', '-lwinhttp', '-luuid', '-lhid', '-lbcrypt'];
  const foreign = ['usb.dll.a','hidapi.dll.a','pawnio.dll.a'].map(p => path.join(repo,'local/device-db-sources/transport-objects',p));
  const link = path.join(out, 'link.rsp');
  fs.writeFileSync(link, ['-static', ...objects, ...foreign, ...libs, '-o', path.join(output, 'native-driver-core.exe')].map(quote).join('\n'));
  try { await execute(compiler, ['@' + link], { maxBuffer: 4 * 1024 * 1024, windowsHide: true }); }
  catch (e) { fs.writeFileSync(path.join(out, 'link-errors.txt'), e.stderr ?? e.message); throw e; }
  console.log(`Built ${output}/native-driver-core.exe`);
  fs.mkdirSync(path.join(output, 'modules'), {recursive:true});
  for(const source of walk(path.join(root,'dependencies/PawnIO/modules')).filter(p=>p.endsWith('.bin')))
    fs.copyFileSync(source,path.join(output,'modules',path.basename(source)));
}
