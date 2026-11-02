#include "driver.hpp"
#include <sddl.h>

namespace vlr {
namespace {
struct PipePair { Handle host, child; };
PipePair pipe(bool input) {
    static std::atomic<unsigned long> sequence{0};
    const auto name=L"\\\\.\\pipe\\VeryLightRGB.Driver."+std::to_wstring(GetCurrentProcessId())+L"."+std::to_wstring(++sequence);
    PipePair result;
    PSECURITY_DESCRIPTOR descriptor=nullptr;
    const auto acl=L"D:P(A;;GA;;;"+userSid()+L")(A;;GA;;;SY)";
    if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(acl.c_str(),SDDL_REVISION_1,&descriptor,nullptr))
        throw std::runtime_error(winError("Driver pipe ACL"));
    SECURITY_ATTRIBUTES hostSecurity{sizeof(hostSecurity),descriptor,FALSE};
    result.host.reset(CreateNamedPipeW(name.c_str(), (input?PIPE_ACCESS_OUTBOUND:PIPE_ACCESS_INBOUND)|FILE_FLAG_OVERLAPPED|FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_BYTE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,1,65536,65536,0,&hostSecurity));
    LocalFree(descriptor);
    if(!result.host) throw std::runtime_error(winError("Create driver pipe"));
    SECURITY_ATTRIBUTES security{sizeof(security),nullptr,TRUE};
    result.child.reset(CreateFileW(name.c_str(),input?GENERIC_READ:GENERIC_WRITE,0,&security,OPEN_EXISTING,0,nullptr));
    if(!result.child) throw std::runtime_error(winError("Connect driver pipe"));
    if(!ConnectNamedPipe(result.host.h,nullptr) && GetLastError()!=ERROR_PIPE_CONNECTED)
        throw std::runtime_error(winError("Driver pipe connection"));
    return result;
}
DWORD transfer(HANDLE pipeHandle, HANDLE child, bool writing, void *data, DWORD size, DWORD timeout) {
    Handle event(CreateEventW(nullptr,TRUE,FALSE,nullptr));
    if(!event) throw std::runtime_error(winError("Driver I/O event"));
    OVERLAPPED ov{}; ov.hEvent=event.h;
    DWORD done=0;
    const BOOL ok=writing?WriteFile(pipeHandle,data,size,&done,&ov):ReadFile(pipeHandle,data,size,&done,&ov);
    if(!ok) {
        if(GetLastError()!=ERROR_IO_PENDING) throw std::runtime_error(winError("Native driver disconnected"));
        HANDLE waits[]={event.h,child};
        const auto wait=WaitForMultipleObjects(2,waits,FALSE,timeout);
        if(wait!=WAIT_OBJECT_0) {
            CancelIoEx(pipeHandle,&ov);
            TerminateProcess(child,1);
            GetOverlappedResult(pipeHandle,&ov,&done,TRUE);
            throw std::runtime_error(wait==WAIT_TIMEOUT?"Native driver response timed out":"Native driver worker exited");
        }
        if(!GetOverlappedResult(pipeHandle,&ov,&done,FALSE)) throw std::runtime_error(winError("Native driver pipe"));
    }
    if(!done) throw std::runtime_error("Native driver disconnected");
    return done;
}
}
Driver::Driver(bool memoryTest) {
    const auto binary=std::filesystem::path(executablePath()).parent_path()/L"native-drivers"/L"native-driver-core.exe";
    if(!std::filesystem::exists(binary)) throw std::runtime_error("Native driver bundle is missing; reinstall Very Light RGB");
    auto in=pipe(true),out=pipe(false);
    SECURITY_ATTRIBUTES security{sizeof(security),nullptr,TRUE};
    Handle errors(CreateFileW(L"NUL",GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,&security,OPEN_EXISTING,0,nullptr));
    if(!errors) throw std::runtime_error(winError("Driver diagnostic output"));
    const auto directory=dataDirectory()/L"NativeDrivers";
    std::filesystem::create_directories(directory);
    auto command=L"\""+binary.wstring()+L"\" "+(memoryTest?L"memory-stream":L"stream")+L" \""+directory.wstring()+L"\" --host-pid "+std::to_wstring(GetCurrentProcessId());
    SIZE_T bytes=0;
    InitializeProcThreadAttributeList(nullptr,1,0,&bytes);
    Bytes attributes(bytes);
    auto list=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
    if(!InitializeProcThreadAttributeList(list,1,0,&bytes)) throw std::runtime_error(winError("Driver process attributes"));
    HANDLE handles[]={in.child.h,out.child.h,errors.h};
    STARTUPINFOEXW startup{}; startup.StartupInfo.cb=sizeof(startup); startup.lpAttributeList=list;
    startup.StartupInfo.dwFlags=STARTF_USESTDHANDLES|STARTF_USESHOWWINDOW;
    startup.StartupInfo.wShowWindow=SW_HIDE;
    startup.StartupInfo.hStdInput=in.child.h; startup.StartupInfo.hStdOutput=out.child.h; startup.StartupInfo.hStdError=errors.h;
    const bool updated=UpdateProcThreadAttribute(list,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,handles,sizeof(handles),nullptr,nullptr)!=0;
    PROCESS_INFORMATION child{};
    const bool created=updated && CreateProcessW(binary.c_str(),command.data(),nullptr,nullptr,TRUE,
        CREATE_SUSPENDED|CREATE_NO_WINDOW|EXTENDED_STARTUPINFO_PRESENT,nullptr,binary.parent_path().c_str(),&startup.StartupInfo,&child);
    const auto error=GetLastError(); DeleteProcThreadAttributeList(list);
    if(!created) throw std::runtime_error(winError("Start native driver worker",error));
    process.reset(child.hProcess); Handle thread(child.hThread);
    job.reset(CreateJobObjectW(nullptr,nullptr));
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{}; limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if(!job || !SetInformationJobObject(job.h,JobObjectExtendedLimitInformation,&limits,sizeof(limits)) || !AssignProcessToJobObject(job.h,process.h)) {
        TerminateProcess(process.h,1); throw std::runtime_error(winError("Native driver job"));
    }
    input=std::move(in.host); output=std::move(out.host);
    if(ResumeThread(thread.h)==DWORD(-1)) throw std::runtime_error(winError("Resume native worker"));
}
Driver::~Driver() {
    input.reset();
    if(process && WaitForSingleObject(process.h,1500)!=WAIT_OBJECT_0) TerminateProcess(process.h,1);
}
Json Driver::request(const Json &value,DWORD timeout) {
    auto data=value.dump()+"\n";
    if(data.size()>MaxJson) throw std::runtime_error("Native driver request too large");
    size_t sent=0;
    while(sent<data.size()) sent+=transfer(input.h,process.h,true,data.data()+sent,DWORD(data.size()-sent),timeout);
    const auto deadline=GetTickCount64()+timeout;
    while(buffered.find('\n')==std::string::npos) {
        if(buffered.size()>MaxJson) throw std::runtime_error("Native driver response too large");
        char chunk[16384];
        const auto now=GetTickCount64();
        if(now>=deadline) {TerminateProcess(process.h,1);throw std::runtime_error("Native driver response timed out");}
        const auto count=transfer(output.h,process.h,false,chunk,sizeof(chunk),DWORD(deadline-now));
        buffered.append(chunk,count);
    }
    const auto end=buffered.find('\n');
    auto result=parseJson(buffered.substr(0,end)); buffered.erase(0,end+1);
    if(!result.value("ok",false)) throw std::runtime_error(result.value("error",std::string("Native driver error")));
    return result;
}
std::vector<Descriptor> Driver::descriptors(const Json &values) {
    std::vector<Descriptor> out;
    for(const auto &controller:values) {
        if(!controller.value("directCapable",false)) continue;
        for(const auto &zone:controller.at("zones")) {
            const int actual=zone.at("ledCount"),maximum=zone.at("maxLeds");
            if(actual<=0 && maximum<=0) continue;
            Descriptor d; d.zone=zone.at("index");
            d.controllerKey=vlr::hash(controller.at("id").get<std::string>());
            d.id="driver:"+hash(controller.at("id").get<std::string>()+"|"+std::to_string(d.zone)+"|"+zone.at("name").get<std::string>());
            d.name=controller.at("name").get<std::string>()+" · "+zone.at("name").get<std::string>();
            if(d.name.size()>500) d.name=controller.at("name");
            d.maxLeds=std::clamp(std::max(actual,maximum),1,4096);
            d.count=actual>0?actual:std::min(12,d.maxLeds);
            d.configurable=zone.value("minLeds",actual)!=maximum && maximum>0;
            d.single=d.count==1&&!d.configurable;
            d.driver={{"Id",controller.at("id")},{"Detector",controller.at("detector")},{"Path",controller.value("path",std::string())},{"PhysicalLeds",actual}};
            out.push_back(std::move(d));
            if(out.size()>256) throw std::runtime_error("Too many native driver outputs");
        }
    }
    return out;
}
std::vector<Descriptor> Driver::discover(bool board,bool gpu,std::vector<std::string> &notes) {
    auto result=request({{"op","open-auto"},{"excludeHid",board?Json::array({Json::array({0x0db0,0x0076})}):Json::array()},{"excludeGpu",gpu}},45000);
    devices=result.at("devices");
    for(const auto &note:result.value("warnings",Json::array())) notes.push_back(note.get<std::string>());
    return descriptors(devices);
}
std::vector<Descriptor> Driver::open(const std::vector<Plan> &targets) {
    std::set<std::string> detectors,paths; bool bus=false;
    for(const auto &p:targets) if(p.device.core()) {
        detectors.insert(p.device.driver.at("Detector"));
        const auto path=p.device.driver.value("Path",std::string()); if(!path.empty()) paths.insert(path); else bus=true;
    }
    auto result=request({{"op","open"},{"detectors",detectors},{"paths",paths},{"allowBusAccess",bus}},45000);
    devices=result.at("devices");
    return descriptors(devices);
}
void Driver::prepare(const Plan &target) {
    auto available=descriptors(devices);
    auto found=std::find_if(available.begin(),available.end(),[&](const auto &d){return d.id==target.device.id;});
    if(found==available.end()) throw std::runtime_error("Native output unavailable; Reload devices");
    if(found->driver.value("PhysicalLeds",0)!=target.count) {
        if(!found->configurable) throw std::runtime_error("Native output length changed; Reload devices");
        devices=request({{"op","resize"},{"id",found->driver.at("Id")},{"zone",found->zone},{"ledCount",target.count}},15000).at("devices");
    }
}
void Driver::frame(const Descriptor &d,const Bytes &rgb) {
    if(rgb.empty()||rgb.size()%3) throw std::runtime_error("Invalid native driver frame");
    Json colors=Json::array();
    for(size_t i=0;i<rgb.size();i+=3) colors.push_back({rgb[i],rgb[i+1],rgb[i+2]});
    request({{"op","zone-frame"},{"id",d.driver.at("Id")},{"zone",d.zone},{"colors",colors}},5000);
}
void Driver::check() {
    if(WaitForSingleObject(process.h,0)==WAIT_OBJECT_0) throw std::runtime_error("Native driver worker exited; reconnecting");
    request({{"op","check"}},5000);
}
std::vector<Descriptor> discoverDrivers(const std::vector<Descriptor> &native,std::vector<std::string> &notes) {
    try {
        const bool board=std::any_of(native.begin(),native.end(),[](const auto &d){return d.id.rfind("native:msi:",0)==0;});
        const bool gpu=std::any_of(native.begin(),native.end(),[](const auto &d){return d.id==gpuDescriptor().id;});
        Driver worker; return worker.discover(board,gpu,notes);
    } catch(const std::exception &e) {notes.push_back(e.what());return {};}
}
}
