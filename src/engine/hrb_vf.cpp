// Exact-vf disk metadata is delegated to the same Harbour runtime as the
// client front end. Data handles use the existing no-follow jail opener.
#include "engine/hrb_harbour.h"
#include "engine/vf_service.h"
#include "engine/vf_guard.h"
#include "engine/server_fs.h"
#include "platform/file.h"
#include <algorithm>
#include <cerrno>
#include <filesystem>
#include <limits>
#include <map>
#include <mutex>
#include <random>
#include <string>
#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#ifdef __APPLE__
#include <dirent.h>
#include <fcntl.h>
#include <sys/mount.h>
#include <ctime>
#endif
#endif

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wconversion"
#pragma clang diagnostic ignored "-Wsign-conversion"
#pragma clang diagnostic ignored "-Wpedantic"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wpedantic"
#endif
#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#include "hbapi.h"
#include "hbapifs.h"
#include "hbapiitm.h"
#include "hbdate.h"
#include "hbvm.h"
#include "fileio.ch"
#include "directry.ch"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace openads::engine::vf {
namespace {
using Request=network::vf::Request;
using Reply=network::vf::Reply;
std::mutex provider_mu;
struct VmScope {
    bool entered=hrb_udf::vf_vm_enter();
    ~VmScope() { if(entered) hrb_udf::vf_vm_leave(); }
};
void os_error(const util::Error& e) {
#ifdef _WIN32
    ::SetLastError(static_cast<DWORD>(e.sub_code));
#else
    errno=e.sub_code ? e.sub_code : EACCES;
#endif
    hb_fsSetIOError(HB_FALSE,0);
}
// Enumerate links without following them. Only a separately pinned in-jail
// target may supply followed-link metadata. This avoids a directory listing
// becoming an indirect read of metadata outside the selected data root.
#ifndef __APPLE__
PHB_ITEM jailed_directory(const Request& q,const Request& original) {
    const std::string enumerate_attributes=q.path2+"DHSL";
    PHB_ITEM names=hb_fsDirectory(q.path1.c_str(),enumerate_attributes.c_str(),HB_TRUE);
    PHB_ITEM result=hb_itemArrayNew(0);
    const HB_FATTR wanted=HB_FA_ARCHIVE|HB_FA_READONLY|hb_fsAttrEncode(q.path2.c_str());
    const auto directory=std::filesystem::path(original.path1).parent_path();
    const bool root_spec=std::filesystem::path(original.path1).lexically_normal()==std::filesystem::path(original.root).lexically_normal();
    for(HB_SIZE i=1;i<=hb_arrayLen(names);++i) {
        PHB_ITEM row=hb_arrayGetItemPtr(names,i);
        if(root_spec) {
            auto spelling=std::filesystem::path(original.client_path1).filename().string();
            if(spelling.empty())spelling=".";
            hb_arraySetC(row,F_NAME,spelling.c_str());
        }
        const char* name=hb_arrayGetCPtr(row,F_NAME);
        const std::string original_attributes=hb_arrayGetCPtr(row,F_ATTR);
        const auto entry=root_spec?original.root:(directory/name).string();
        PathGuard guard;
        const auto pinned=guard.pin(original.root,entry,true);
        const bool link=(hb_fsAttrEncode(original_attributes.c_str())&HB_FA_LINK)!=0;
        if(!pinned && !(link && pinned.error().sub_code==ENOENT))continue;
#ifndef _WIN32
        if(link && pinned && (wanted&HB_FA_LINK)==0) {
            PHB_ITEM target=hb_fsDirectory(pinned.value().c_str(),"DHS",HB_TRUE);
            if(hb_arrayLen(target)==1) {
                PHB_ITEM followed=hb_arrayGetItemPtr(target,1);
                hb_arraySetC(followed,F_NAME,name);
                std::string attributes=hb_arrayGetCPtr(followed,F_ATTR);
                if(original_attributes.find('H')!=std::string::npos && attributes.find('H')==std::string::npos)attributes+='H';
                if(attributes.find('L')==std::string::npos)attributes+='L';
                hb_arraySetC(followed,F_ATTR,attributes.c_str());
                hb_itemCopy(row,followed);
            }
            hb_itemRelease(target);
        }
#endif
        const HB_FATTR actual=hb_fsAttrEncode(hb_arrayGetCPtr(row,F_ATTR));
        const HB_FATTR restricted=HB_FA_HIDDEN|HB_FA_SYSTEM|HB_FA_LABEL|HB_FA_DIRECTORY;
        if((actual&restricted&~wanted)==0)hb_arrayAdd(result,row);
    }
    hb_itemRelease(names);
    return result;
}
#endif
#ifdef __APPLE__
// Darwin's /dev/fd entries support final-descriptor metadata, but not child
// traversal. Use *at operations and fdopendir for pinned parent directories.
PHB_ITEM mac_directory(const Request& original,const PathGuard& parent) {
    PHB_ITEM result=hb_itemArrayNew(0);
    HB_FATTR wanted=HB_FA_ARCHIVE|HB_FA_READONLY|hb_fsAttrEncode(original.path2.c_str());
    const auto pattern=parent.leaf_name();
    if(!pattern.empty() && pattern[0]=='.')wanted|=HB_FA_HIDDEN;
    DIR* dir=::fdopendir(::dup(parent.descriptor()));
    if(!dir){hb_fsSetIOError(HB_FALSE,0);return result;}
    const auto directory=std::filesystem::path(original.path1).parent_path();
    const bool root_spec=std::filesystem::path(original.path1).lexically_normal()==std::filesystem::path(original.root).lexically_normal();
    while(auto* entry=::readdir(dir)) {
        const std::string name=entry->d_name;
        if(!hb_strMatchFile(name.c_str(),pattern.c_str()))continue;
        struct stat st{};
        if(::fstatat(parent.descriptor(),name.c_str(),&st,AT_SYMLINK_NOFOLLOW)!=0)continue;
        const bool link=S_ISLNK(st.st_mode);
        const auto spelling=root_spec?original.root:(directory/name).string();
        PathGuard target;
        const auto pin=target.pin(original.root,spelling,true);
        if(!pin && !(link && pin.error().sub_code==ENOENT))continue;
        if(link && pin && !(wanted&HB_FA_LINK) && ::fstat(target.descriptor(),&st)!=0)continue;
        HB_FATTR attr=hb_fsAttrFromRaw(st.st_mode);
        if(link)attr|=HB_FA_LINK;
        if(name.size()>1 && name[0]=='.' && name!="..")attr|=HB_FA_HIDDEN;
        const HB_FATTR restricted=HB_FA_HIDDEN|HB_FA_SYSTEM|HB_FA_LABEL|HB_FA_DIRECTORY;
        if(attr&restricted&~wanted)continue;
        const time_t seconds=st.st_mtime;struct tm local{};
        if(!::localtime_r(&seconds,&local))continue;
        PHB_ITEM row=hb_itemArrayNew(F_LEN);char attributes[32],clock[16];
        hb_snprintf(clock,sizeof(clock),"%02d:%02d:%02d",local.tm_hour,local.tm_min,local.tm_sec);
        std::string output_name=name;
        if(root_spec){output_name=std::filesystem::path(original.client_path1).filename().string();if(output_name.empty())output_name=".";}
        char* converted=nullptr;const char* decoded=hb_osDecodeCP(output_name.c_str(),&converted,nullptr);
        hb_arraySetC(row,F_NAME,decoded);if(converted)hb_xfree(converted);
        hb_arraySetNInt(row,F_SIZE,st.st_size);
        hb_arraySetC(row,F_TIME,clock);hb_arraySetC(row,F_ATTR,hb_fsAttrDecode(attr,attributes));
        // Harbour's Darwin finder has second, not nanosecond, precision.
        hb_arraySetTDT(row,F_DATE,hb_dateEncode(local.tm_year+1900,local.tm_mon+1,local.tm_mday),hb_timeEncode(local.tm_hour,local.tm_min,local.tm_sec,0));
        hb_arrayAddForward(result,row);hb_itemRelease(row);
    }
    ::closedir(dir);hb_fsSetError(0);return result;
}
#endif

struct Entry {
    std::shared_ptr<FsNative> native;
    std::string path;
    std::int64_t pos=0;
    bool readable=true,writable=false,shared=true;
};
class HarbourVfService final:public Service {
    std::map<std::uint32_t,Entry> files_;
    std::uint32_t next_=1,blob_next_=1;
    std::map<std::uint32_t,std::vector<std::uint8_t>> blobs_;
    std::uint32_t allocate() {
        while(next_==0 || files_.count(next_)) ++next_;
        return next_++;
    }
    Reply open(const Request& q) {
        Reply r;
        if(files_.size()>=kMaxSessionFiles) { r.error=4; return r; }
        unsigned flags=q.flags;
        std::shared_ptr<FsNative> reuse;
        unsigned logical_mode=flags&3u;
        platform::VfOpenOptions o;
        o.access=flags&3u;
        const unsigned deny=flags&0x70u;
        o.share=deny==FO_EXCLUSIVE ? 0u : deny==FO_DENYWRITE ? 1u : deny==FO_DENYREAD ? 2u : 3u;
        o.create=(flags&(FXO_TRUNCATE|FXO_APPEND|FXO_UNIQUE))!=0;
        o.truncate=(flags&FXO_TRUNCATE)!=0;
        o.exclusive=(flags&FXO_UNIQUE)!=0;
        o.private_handle=(flags&0x80u)!=0;
        o.share_lock=(flags&FXO_SHARELOCK)!=0;
        bool shared=(flags&(FO_DENYREAD|FO_DENYWRITE|FO_EXCLUSIVE))==0;
#ifndef _WIN32
        // filebuf.c upgrades shared write-only opens when read access exists,
        // and readonly opens cannot carry a POSIX exclusive share lock.
#if defined(HB_USE_SHARELOCKS) && !defined(HB_USE_BSDLOCKS)
        if(o.share_lock && o.access==0 && !shared) { o.share=3;shared=true; }
        if(o.share_lock && o.access==1 && shared) {
            if(::access(q.path1.c_str(),R_OK)==0 || ::access(q.path1.c_str(),F_OK)!=0){o.access=2;logical_mode=2;}
            else o.share_lock=false;
        }
#endif
#if defined(HB_USE_BSDLOCKS)
        o.bsd_share_lock=true;o.shared_lock=shared;
#else
        o.shared_lock=o.access==0 || shared;
#endif
        o.native_attributes=0666;
        if(auto old=fs_find_native(q.path1)) {
            if(!shared || o.truncate) { r.error=o.truncate?5:32;return r; }
            for(const auto& pair:files_) {
                if(pair.second.native==old && !pair.second.shared) { r.error=32;return r; }
            }
            const auto mode=old->vf_mode<0?(old->writable?2:0):old->vf_mode;
            if(mode==2 || static_cast<unsigned>(mode)==o.access)reuse=old;
            else logical_mode=2;
        }
#else
        o.native_attributes=0x80;
#endif
        auto native=reuse;
        if(!native) {
        auto opened=platform::File::open_vf(q.path1,o);
        if(!opened) { os_error(opened.error());r.error=hb_fsError();
            if(opened.error().message.find("share lock")!=std::string::npos)r.error=o.truncate?5:32;
            else if(opened.error().message.find("vf truncate")!=std::string::npos)r.error=5;
            return r; }
        auto candidate=std::make_shared<FsNative>();
        candidate->file=std::move(opened.value());candidate->writable=o.access!=0;candidate->readable=o.access!=1;
        candidate->vf_mode=static_cast<int>(logical_mode);
#ifdef _WIN32
        // Harbour does not merge Windows file opens (device/inode are zero).
        // Keep that platform behavior in the new family only.
        native=std::move(candidate);
#else
        native=fs_attach_native(q.path1,candidate);
#endif
        }
        Entry e; e.native=std::move(native);e.path=q.path1;
        e.readable=(flags&3u)!=FO_WRITE;e.writable=(flags&3u)!=FO_READ;e.shared=shared;
        const auto id=allocate();files_.emplace(id,std::move(e));r.value1=id;
        return r;
    }
public:
    ~HarbourVfService() override { std::lock_guard<std::mutex> lock(provider_mu); files_.clear(); }
    Reply execute(const Request& input) override {
        Request q=input;
        std::lock_guard<std::mutex> lock(provider_mu);
        VmScope vm;
        Reply r;
        if(!vm.entered) { r.error=50;return r; }
        hb_fsSetError(0);
        if(q.op==OADS_VF_BLOBREAD) {
            auto it=blobs_.find(q.handle);
            if(it==blobs_.end() || q.value1<0 || static_cast<std::uint64_t>(q.value1)>it->second.size()) { r.error=6;return r; }
            const auto start=static_cast<std::size_t>(q.value1);
            const auto count=std::min<std::size_t>(OADS_VF_CHUNK_MAX,it->second.size()-start);
            r.data.assign(it->second.begin()+static_cast<std::ptrdiff_t>(start),it->second.begin()+static_cast<std::ptrdiff_t>(start+count));
            r.value1=static_cast<std::int64_t>(r.data.size());
            if(start+count==it->second.size())blobs_.erase(it);
            return r;
        }
        if(q.op==OADS_VF_TEMPSERVER) {
            if(files_.size()>=kMaxSessionFiles){r.error=4;return r;}
            const std::string ext(q.data.begin(),q.data.end());
            if(q.path2.find_first_of("/\\:")!=std::string::npos || ext.find_first_of("/\\:")!=std::string::npos || ext.find('\0')!=std::string::npos){r.error=87;return r;}
            static std::mt19937_64 random(std::random_device{}());
            for(unsigned attempt=0;attempt<128;++attempt) {
                const auto name=q.path2+std::to_string(random())+ext;
                const auto path=(std::filesystem::path(q.path1)/name).string();
                platform::VfOpenOptions o;o.create=true;o.exclusive=true;o.share=0;
#ifdef _WIN32
                o.native_attributes=q.flags?0x20u|(q.flags&7u):0x80u;
#else
                o.native_attributes=static_cast<std::uint32_t>(HB_FA_POSIX_ATTR(q.flags));
                if(!o.native_attributes){o.native_attributes=(q.flags&1u)?0444u:0666u;if(q.flags&4u)o.native_attributes|=0111u;if(q.flags&2u)o.native_attributes&=0700u;}
#endif
                auto opened=platform::File::open_vf(path,o);
                if(!opened){os_error(opened.error());r.error=hb_fsError();if(r.error==80)continue;return r;}
                auto native=std::make_shared<FsNative>();native->file=std::move(opened.value());native->writable=true;
#ifndef _WIN32
                native=fs_attach_native(path,native);
#endif
                Entry e;e.native=native;e.path=path;e.readable=e.writable=true;e.shared=false;
                const auto id=allocate();files_.emplace(id,std::move(e));r.value1=id;
                r.data.assign(name.begin(),name.end());r.error=0;return r;
            }
            r.error=80;return r;
        }
        PathGuard first_guard,second_guard;
        if(q.op>=OADS_VF_EXISTS && q.op<=OADS_VF_LINKREAD) {
            bool follows=q.op==OADS_VF_EXISTS || q.op==OADS_VF_DIREXISTS || q.op==OADS_VF_DIRSPACE ||
                q.op==OADS_VF_TIMEGET || q.op==OADS_VF_TIMESET || q.op==OADS_VF_ATTRGET || q.op==OADS_VF_ATTRSET;
#ifdef _WIN32
            // Windows hard-link creation follows the source pathname. Keep
            // the validated leaf pinned too so it cannot become a reparse
            // point between the boundary check and CreateHardLink.
            if(q.op==OADS_VF_LINK || q.op==OADS_VF_LINKREAD)follows=true;
#endif
            if(q.op!=OADS_VF_LINKSYM) {
                auto pin=first_guard.pin(q.root,q.path1,follows);
                if(!pin){os_error(pin.error());r.error=hb_fsError();return r;}q.path1=pin.value();
            }
            if(q.op==OADS_VF_RENAME || q.op==OADS_VF_LINK || q.op==OADS_VF_LINKSYM) {
                auto pin=second_guard.pin(q.root,q.path2,false);
                if(!pin){os_error(pin.error());r.error=hb_fsError();return r;}q.path2=pin.value();
            }
        }
        const auto* p=q.path1.c_str();const auto* p2=q.path2.c_str();
        if(q.op==OADS_VF_OPEN) return open(q);
        if(q.op==OADS_VF_CLOSE) {
            auto it=files_.find(q.handle);
            if(it==files_.end()) r.error=6;else { files_.erase(it);r.value1=1; }
            return r;
        }
        if(q.op>=OADS_VF_READ && q.op<=OADS_VF_LOCKTEST) {
            auto it=files_.find(q.handle);
            if(it==files_.end()) { r.error=6;return r; }
            auto& e=it->second;
            auto& file=e.native->file;
            if(q.op==OADS_VF_LOCK || q.op==OADS_VF_UNLOCK || q.op==OADS_VF_LOCKTEST) {
                FsFile proxy;proxy.vf_semantics=true;proxy.path=e.path;proxy.writable=e.writable;proxy.stream.native=e.native;
                const std::uint8_t op=q.op==OADS_VF_LOCK?0:q.op==OADS_VF_UNLOCK?1:2;
                auto v=fs_lock_step(proxy,static_cast<std::uint64_t>(q.value1),static_cast<std::uint64_t>(q.value2),static_cast<std::uint16_t>(q.flags),op);
                if(v) { r.value1=v.value();if(q.op==OADS_VF_LOCK && v.value()==0) r.error=33; }
                else { r.error=v.error().code==5012?33:5;r.value1=q.op==OADS_VF_LOCKTEST?-1:0;r.value2=1; }
                return r;
            }
            std::lock_guard<std::mutex> guard(e.native->mu);
#ifdef _WIN32
            const auto h=static_cast<HB_FHANDLE>(reinterpret_cast<std::intptr_t>(file.native_handle()));
#else
            const auto h=static_cast<HB_FHANDLE>(reinterpret_cast<std::intptr_t>(file.native_handle())-1);
#endif
            if(q.op==OADS_VF_READ || q.op==OADS_VF_READAT) {
                if(q.value1<0 || q.value1>OADS_VF_CHUNK_MAX) { r.error=87;return r; }
                r.data.resize(static_cast<std::size_t>(q.value1));
#ifdef _WIN32
                // Native Windows filebuf uses the OS file position. ReadAt on
                // its synchronous handle can move that position too.
                const auto n=q.op==OADS_VF_READAT?hb_fsReadAt(h,r.data.data(),r.data.size(),q.value2):hb_fsReadLarge(h,r.data.data(),r.data.size());
#else
                const auto pos=q.op==OADS_VF_READAT?q.value2:e.pos;
                const auto n=hb_fsReadAt(h,r.data.data(),r.data.size(),pos);
#endif
                r.error=hb_fsError();r.data.resize(n);r.value1=static_cast<std::int64_t>(n);
#ifndef _WIN32
                if(q.op==OADS_VF_READ)e.pos+=r.value1;
#endif
            } else if(q.op==OADS_VF_WRITE || q.op==OADS_VF_WRITEAT) {
#ifdef _WIN32
                const auto n=q.op==OADS_VF_WRITEAT?hb_fsWriteAt(h,q.data.data(),q.data.size(),q.value2):hb_fsWriteLarge(h,q.data.data(),q.data.size());
#else
                const auto pos=q.op==OADS_VF_WRITEAT?q.value2:e.pos;
                const auto n=hb_fsWriteAt(h,q.data.data(),q.data.size(),pos);
#endif
                r.error=hb_fsError();r.value1=static_cast<std::int64_t>(n);
#ifndef _WIN32
                if(q.op==OADS_VF_WRITE)e.pos+=r.value1;
#endif
            } else if(q.op==OADS_VF_COMMIT) {
                hb_fsCommit(h);r.error=hb_fsError();
            } else if(q.op==OADS_VF_FLUSH) {
                // Native filebuf.c has no additional buffered bytes.
            } else if(q.op==OADS_VF_TRUNC) {
                r.value1=hb_fsTruncAt(h,q.value1);r.error=hb_fsError();
#ifndef _WIN32
                if(r.value1)e.pos=q.value1;
#endif
            } else {
#ifdef _WIN32
                if(q.op==OADS_VF_SEEK)r.value1=hb_fsSeekLarge(h,q.value1,static_cast<HB_USHORT>(q.flags));
                else if(q.op==OADS_VF_EOF)r.value1=hb_fsEof(h);
                else r.value1=hb_fsGetSize(h);
                r.error=hb_fsError();
#else
                auto size=file.size();
                if(!size){os_error(size.error());r.error=hb_fsError();return r;}
                if(size.value()>static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())){r.error=25;return r;}
                const auto n=static_cast<std::int64_t>(size.value());
                if(q.op==OADS_VF_SIZE)r.value1=n;
                else if(q.op==OADS_VF_EOF)r.value1=e.pos>=n;
                else if(q.op==OADS_VF_SEEK) {
                    const auto base=(q.flags&FS_END)?n:(q.flags&FS_RELATIVE)?e.pos:0;
                    if((q.value1>0 && base>std::numeric_limits<std::int64_t>::max()-q.value1) ||
                       (q.value1<0 && q.value1 < -base))r.error=25;
                    else e.pos=base+q.value1;
                    r.value1=e.pos;
                }
#endif
            }
            return r;
        }
#ifdef __APPLE__
        // These operations name a leaf, not its target. Descriptor-relative
        // syscalls preserve the pinned parent even if its pathname is moved.
        int mac_status=0;bool mac_done=true;
        switch(q.op) {
            case OADS_VF_ERASE:mac_status=::unlinkat(first_guard.descriptor(),first_guard.leaf_name().c_str(),0);break;
            case OADS_VF_RENAME:mac_status=::renameat(first_guard.descriptor(),first_guard.leaf_name().c_str(),second_guard.descriptor(),second_guard.leaf_name().c_str());break;
            case OADS_VF_DIRMAKE:mac_status=::mkdirat(first_guard.descriptor(),first_guard.leaf_name().c_str(),0777);break;
            case OADS_VF_DIRREMOVE:mac_status=::unlinkat(first_guard.descriptor(),first_guard.leaf_name().c_str(),AT_REMOVEDIR);break;
            case OADS_VF_LINK:mac_status=::linkat(first_guard.descriptor(),first_guard.leaf_name().c_str(),second_guard.descriptor(),second_guard.leaf_name().c_str(),0);break;
            case OADS_VF_LINKSYM:mac_status=::symlinkat(p,second_guard.descriptor(),second_guard.leaf_name().c_str());break;
            case OADS_VF_LINKREAD: {
                std::vector<char> target(65536);
                const auto n=::readlinkat(first_guard.descriptor(),first_guard.leaf_name().c_str(),target.data(),target.size());
                if(n<0)mac_status=-1;else if(static_cast<std::size_t>(n)==target.size()){errno=ENAMETOOLONG;mac_status=-1;}
                else r.data.assign(target.begin(),target.begin()+n);
                break;
            }
            case OADS_VF_ATTRGET: {
                struct stat metadata{};mac_status=::fstat(first_guard.descriptor(),&metadata);
                if(!mac_status){r.value1=1;r.value2=hb_fsAttrFromRaw(metadata.st_mode);}break;
            }
            case OADS_VF_DIRSPACE: {
                struct statfs disk{};mac_status=::fstatfs(first_guard.descriptor(),&disk);
                if(!mac_status){const auto count=q.flags==1?disk.f_bfree:q.flags==2?disk.f_blocks-disk.f_bfree:q.flags==3?disk.f_blocks:disk.f_bavail;r.value1=static_cast<std::int64_t>(static_cast<double>(count)*static_cast<double>(disk.f_bsize));}
                break;
            }
            default:mac_done=false;break;
        }
        if(mac_done){hb_fsSetIOError(mac_status==0,0);r.error=hb_fsError();if(q.op!=OADS_VF_LINKREAD && q.op!=OADS_VF_DIRSPACE && q.op!=OADS_VF_ATTRGET)r.value1=mac_status==0;return r;}
#endif
        switch(q.op) {
            case OADS_VF_EXISTS:r.value1=hb_fsFileExists(p);break;
            case OADS_VF_ERASE:r.value1=hb_fsDelete(p);break;
            case OADS_VF_RENAME:r.value1=hb_fsRename(p,p2);break;
            case OADS_VF_DIREXISTS:r.value1=hb_fsDirExists(p);break;
            case OADS_VF_DIRMAKE:r.value1=hb_fsMkDir(p);break;
            case OADS_VF_DIRREMOVE:r.value1=hb_fsRmDir(p);break;
            case OADS_VF_DIRSPACE: {
                const double n=hb_fsDiskSpace(p,static_cast<HB_USHORT>(q.flags));
                r.value1=static_cast<std::int64_t>(n);break;
            }
            case OADS_VF_DIRECTORY: {
#ifdef __APPLE__
                PHB_ITEM array=mac_directory(input,first_guard);
#else
                PHB_ITEM array=jailed_directory(q,input);
#endif
                HB_SIZE size=0;char* bytes=hb_itemSerialize(array,0,&size);
                hb_itemRelease(array);
                r.value1=static_cast<std::int64_t>(size);
                if(bytes && size>OADS_VF_CHUNK_MAX) {
                    if(blobs_.size()>=kMaxSessionFiles)r.error=4;
                    else {
                        while(blob_next_==0 || blobs_.count(blob_next_))++blob_next_;
                        const auto id=blob_next_++;
                        blobs_[id]=std::vector<std::uint8_t>(bytes,bytes+size);
                        r.value2=id;r.data.assign(bytes,bytes+OADS_VF_CHUNK_MAX);
                    }
                } else if(bytes)r.data.assign(bytes,bytes+size);
                if(bytes)hb_xfree(bytes);
                break;
            }
            case OADS_VF_TIMEGET: {
                long day=0,ms=0;r.value1=hb_fsGetFileTime(p,&day,&ms);
                // Two output values are encoded as fixed 64-bit fields in data.
                network::vf::put(r.data,static_cast<std::uint64_t>(day),8);
                network::vf::put(r.data,static_cast<std::uint64_t>(ms),8);break;
            }
            case OADS_VF_TIMESET:r.value1=hb_fsSetFileTime(p,static_cast<long>(q.value1),static_cast<long>(q.value2));break;
            case OADS_VF_ATTRGET: {
                HB_FATTR attr=0;r.value1=hb_fsGetAttr(p,&attr);r.value2=attr;break;
            }
            case OADS_VF_ATTRSET:r.value1=hb_fsSetAttr(p,q.flags);break;
            case OADS_VF_LINK:r.value1=hb_fsLink(p,p2);break;
            case OADS_VF_LINKSYM:r.value1=hb_fsLinkSym(p,p2);break;
            case OADS_VF_LINKREAD: {
#ifdef _WIN32
                // Harbour's Windows helper omits CloseHandle. A server must
                // not retain a private lookup handle or prevent later erase.
                const auto attributes=::GetFileAttributesA(p);
                const DWORD flags=attributes!=INVALID_FILE_ATTRIBUTES && (attributes&FILE_ATTRIBUTE_DIRECTORY)?FILE_FLAG_BACKUP_SEMANTICS:FILE_ATTRIBUTE_NORMAL;
                HANDLE lookup=::CreateFileA(p,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,flags,nullptr);
                if(lookup==INVALID_HANDLE_VALUE)hb_fsSetIOError(HB_FALSE,0);
                else {
                    char target[HB_PATH_MAX];
                    const auto size=::GetFinalPathNameByHandleA(lookup,target,sizeof(target),0);
                    const auto error=::GetLastError();::CloseHandle(lookup);::SetLastError(error);
                    if(size<sizeof(target)){if(size)r.data.assign(target,target+size);hb_fsSetIOError(HB_TRUE,0);}
                    else hb_fsSetError(9);
                }
#else
                char* target=hb_fsLinkRead(p);
                if(target){r.data.assign(target,target+std::char_traits<char>::length(target));hb_xfree(target);}
#endif
                break;
            }
            default:r.error=50;return r;
        }
        if(!r.error)r.error=hb_fsError();
        return r;
    }
};
} // namespace
void install_harbour_provider() { set_factory([] { return std::make_shared<HarbourVfService>(); }); }
} // namespace openads::engine::vf
namespace openads::engine::hrb_udf {
void register_vf_provider() { vf::install_harbour_provider(); }
}
