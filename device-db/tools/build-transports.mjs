// SPDX-License-Identifier: GPL-3.0-or-later
// Build redistributable DLLs from pinned sources, with no MSVC runtime dependency.
import fs from 'node:fs';
import path from 'node:path';
import { execFileSync } from 'node:child_process';
const repo=path.resolve(import.meta.dirname,'../..');
const cache=path.join(repo,'local/device-db-sources');
const output=path.join(repo,'artifacts/device-db-core');
const bin=path.join(repo,'local/toolchain/llvm-mingw-20261020-ucrt-x86_64/bin');
const work=path.join(cache,'transport-objects');
fs.mkdirSync(work,{recursive:true}); fs.mkdirSync(output,{recursive:true});
const privacyFlags=[...new Set([repo,repo.replaceAll('\\','/')])].map(p=>`-ffile-prefix-map=${p}=.`);
privacyFlags.push('-fdebug-compilation-dir=.');
const run=(tool,args)=>execFileSync(path.join(bin,tool+'.exe'),[...privacyFlags,...args],{stdio:'inherit',windowsHide:true});
const hid=path.join(cache,'hidapi-hotplug');
run('x86_64-w64-mingw32-clang',['-shared','-O2','-D_WIN32_WINNT=0x0A00','-I'+path.join(hid,'hidapi-hotplug'),'-I'+path.join(hid,'windows'),path.join(hid,'windows/hid.c'),'-lsetupapi','-lhid','-lcfgmgr32','-Wl,--out-implib,'+path.join(work,'hidapi.dll.a'),'-o',path.join(output,'hidapi-hotplug.dll')]);
fs.writeFileSync(path.join(work,'config.h'),'#define PLATFORM_WINDOWS 1\n#define ENABLE_LOGGING 1\n#define DEFAULT_VISIBILITY __attribute__((visibility("default")))\n#define PRINTF_FORMAT(a,b) __attribute__((format(printf,a,b)))\n');
const usb=path.join(cache,'libusb/libusb');
const usbSources=['core.c','descriptor.c','hotplug.c','io.c','strerror.c','sync.c','os/events_windows.c','os/threads_windows.c','os/windows_common.c','os/windows_usbdk.c','os/windows_winusb.c'];
run('x86_64-w64-mingw32-clang',['-shared','-O2','-std=gnu11','-D_WIN32_WINNT=0x0A00','-I'+work,'-I'+usb,...usbSources.map(p=>path.join(usb,p)),'-lsetupapi','-lcfgmgr32','-Wl,--out-implib,'+path.join(work,'usb.dll.a'),'-o',path.join(output,'libusb-1.0.dll')]);
const pawn=path.join(cache,'pawnio');
run('x86_64-w64-mingw32-clang++',['-shared','-static','-O2','-std=c++20','-DPawnIOLib_EXPORTS','-DPAWNIO_NAME="PawnIO"','-I'+path.join(pawn,'PawnIOLib/include'),'-I'+path.join(pawn,'PawnIO/include'),path.join(pawn,'PawnIOLib/PawnIOLib.cpp'),'-lntdll','-Wl,--out-implib,'+path.join(work,'pawnio.dll.a'),'-o',path.join(output,'PawnIOLib.dll')]);
console.log('Built all three transport DLLs from source.');
