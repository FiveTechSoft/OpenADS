/* OpenADS transport driver beneath Harbour's unmodified hb_vf front end.
 * Plain paths are not accepted by this driver. Remote names are explicitly
 * oads<connection-handle>:<jail-relative-name>.
 */
#define _HB_FILE_IMPLEMENTATION_
#include "hbapi.h"
#include "hbapifs.h"
#include "hbapiitm.h"
#include "hbapierr.h"
#include "hbthread.h"
#include "hbvm.h"
#include "hbset.h"
#include "ace.h"
#include "vf_wire.h"
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

typedef struct _HB_FILE { const HB_FILE_FUNCS* pFuncs; ADSHANDLE conn; HB_U32 id; } HB_FILE;
typedef struct { void* allocation; const HB_BYTE* data; HB_SIZE size; HB_MAXINT a,b; HB_ERRCODE error; } VF_REPLY;
static HB_FILE_FUNCS s_vf;
static HB_CRITICAL_NEW(s_register_lock);
static HB_BOOL s_registered=HB_FALSE;
static HB_U64 vf_get(const HB_BYTE* p,unsigned n) { HB_U64 v=0;unsigned i;for(i=0;i<n;++i)v|=((HB_U64)p[i])<<(8*i);return v; }
static void vf_put(HB_BYTE* p,HB_U64 v,unsigned n) { unsigned i;for(i=0;i<n;++i)p[i]=(HB_BYTE)(v>>(8*i)); }
static HB_MAXINT vf_signed(HB_U64 u) { HB_I64 s;memcpy(&s,&u,8);return (HB_MAXINT)s; }
static HB_BOOL vf_path(const char* name,ADSHANDLE* conn,const char** path) {
    const char* p;HB_U64 n=0;
    if(!name || hb_strnicmp(name,"oads",4)!=0)return HB_FALSE;
    p=name+4;if(*p<'0'||*p>'9')return HB_FALSE;
    while(*p>='0'&&*p<='9') {
        const unsigned digit=(unsigned)(*p++-'0');
        if(n>(0xffffffffUL-digit)/10)return HB_FALSE;
        n=n*10+digit;
    }
    if(*p!=':' || n==0)return HB_FALSE;
    *conn=(ADSHANDLE)n;++p;while(*p=='/'||*p=='\\')++p;*path=p;return HB_TRUE;
}
static void vf_release(VF_REPLY* r) { if(r->allocation)AdsVfFree(r->allocation);memset(r,0,sizeof(*r)); }
static HB_BOOL vf_call(ADSHANDLE conn,unsigned op,HB_U32 id,HB_MAXINT a,HB_MAXINT b,HB_U32 flags,
                      const char* p1,const char* p2,const void* data,HB_SIZE size,VF_REPLY* r) {
    HB_SIZE n1=p1?strlen(p1):0,n2=p2?strlen(p2):0,n=OADS_VF_REQUEST_SIZE+n1+n2+size;
    HB_BYTE* request;UNSIGNED32 rc,outsize=0;void* out=NULL;
    memset(r,0,sizeof(*r));
    if(n1>OADS_VF_PATH_MAX||n2>OADS_VF_PATH_MAX||size>OADS_VF_CHUNK_MAX){hb_fsSetError(87);return HB_FALSE;}
    request=(HB_BYTE*)hb_xgrab(n);memset(request,0,OADS_VF_REQUEST_SIZE);
    memcpy(request,"OVF1",4);request[4]=OADS_VF_VERSION;request[5]=(HB_BYTE)op;
    vf_put(request+8,id,4);vf_put(request+12,(HB_U64)a,8);vf_put(request+20,(HB_U64)b,8);vf_put(request+28,flags,4);
    vf_put(request+32,n1,4);vf_put(request+36,n2,4);vf_put(request+40,size,4);
    if(n1)memcpy(request+44,p1,n1);
    if(n2)memcpy(request+44+n1,p2,n2);
    if(size)memcpy(request+44+n1+n2,data,size);
    hb_vmUnlock();rc=AdsVfRequest(conn,request,(UNSIGNED32)n,&out,&outsize);hb_vmLock();hb_xfree(request);
    if(rc!=0){if(out)AdsVfFree(out);hb_fsSetError((HB_ERRCODE)rc);return HB_FALSE;}
    if(outsize<OADS_VF_REPLY_SIZE||vf_get((HB_BYTE*)out+20,4)!=outsize-OADS_VF_REPLY_SIZE){
        if(out)AdsVfFree(out);
        hb_fsSetError(13);return HB_FALSE;
    }
    r->allocation=out;r->data=(const HB_BYTE*)out+24;r->size=outsize-24;
    r->error=(HB_ERRCODE)vf_get((HB_BYTE*)out,4);r->a=vf_signed(vf_get((HB_BYTE*)out+4,8));r->b=vf_signed(vf_get((HB_BYTE*)out+12,8));
    hb_fsSetError(r->error);return HB_TRUE;
}
static HB_BOOL vf_named(unsigned op,const char* name,const char* second,HB_MAXINT a,HB_MAXINT b,HB_U32 flags,VF_REPLY* r) {
    ADSHANDLE conn,other;const char* p;const char* p2="";
    if(!vf_path(name,&conn,&p)){hb_fsSetError(3);return HB_FALSE;}
    if(second && (!vf_path(second,&other,&p2)||other!=conn)){hb_fsSetError(17);return HB_FALSE;}
    return vf_call(conn,op,0,a,b,flags,p,p2,NULL,0,r);
}
static HB_BOOL vf_bool(unsigned op,const char* name,const char* second,HB_MAXINT a,HB_MAXINT b,HB_U32 flags) {
    VF_REPLY r;HB_BOOL ok=HB_FALSE;if(vf_named(op,name,second,a,b,flags,&r)){ok=r.a!=0;vf_release(&r);}return ok;
}
static HB_BOOL vf_accept(PHB_FILE_FUNCS f,const char* name){HB_SYMBOL_UNUSED(f);return name&&hb_strnicmp(name,"oads",4)==0&&name[4]>='0'&&name[4]<='9';}
static HB_BOOL vf_exists(PHB_FILE_FUNCS f,const char* name,char* ret){HB_BOOL ok;HB_SYMBOL_UNUSED(f);ok=vf_bool(OADS_VF_EXISTS,name,NULL,0,0,0);if(ok&&ret)hb_strncpy(ret,name,HB_PATH_MAX-1);return ok;}
static HB_BOOL vf_delete(PHB_FILE_FUNCS f,const char* n){HB_SYMBOL_UNUSED(f);return vf_bool(OADS_VF_ERASE,n,NULL,0,0,0);}
static HB_BOOL vf_rename(PHB_FILE_FUNCS f,const char* n,const char* to){HB_SYMBOL_UNUSED(f);return vf_bool(OADS_VF_RENAME,n,to,0,0,0);}
static HB_BOOL vf_copy(PHB_FILE_FUNCS f,const char* n,const char* to){HB_SYMBOL_UNUSED(f);return hb_fsCopy(n,to);}
static HB_BOOL vf_direxists(PHB_FILE_FUNCS f,const char* n){HB_SYMBOL_UNUSED(f);return vf_bool(OADS_VF_DIREXISTS,n,NULL,0,0,0);}
static HB_BOOL vf_dirmake(PHB_FILE_FUNCS f,const char* n){HB_SYMBOL_UNUSED(f);return vf_bool(OADS_VF_DIRMAKE,n,NULL,0,0,0);}
static HB_BOOL vf_dirremove(PHB_FILE_FUNCS f,const char* n){HB_SYMBOL_UNUSED(f);return vf_bool(OADS_VF_DIRREMOVE,n,NULL,0,0,0);}
static double vf_dirspace(PHB_FILE_FUNCS f,const char* n,HB_USHORT type){VF_REPLY r;double value=0;HB_SYMBOL_UNUSED(f);if(vf_named(OADS_VF_DIRSPACE,n,NULL,0,0,type,&r)){value=(double)r.a;vf_release(&r);}return value;}
static PHB_ITEM vf_directory(PHB_FILE_FUNCS f,const char* n,const char* attr){
    VF_REPLY r;ADSHANDLE conn;const char* p;PHB_ITEM array=NULL;HB_SYMBOL_UNUSED(f);
    if(vf_path(n,&conn,&p)&&vf_call(conn,OADS_VF_DIRECTORY,0,0,0,0,*p?p:"*",attr,NULL,0,&r)){
        HB_U32 blob=(HB_U32)r.b;HB_SIZE total=(HB_SIZE)r.a,done=0;char* all=NULL;
        if(r.a>=0 && (HB_U64)r.a<=(HB_U64)((HB_SIZE)-1)-1 && total>=r.size){
            all=(char*)hb_xgrab(total+1);if(r.size)memcpy(all,r.data,r.size);done=r.size;
        }
        vf_release(&r);
        while(all && done<total){
            if(!blob || !vf_call(conn,OADS_VF_BLOBREAD,blob,(HB_MAXINT)done,0,0,NULL,NULL,NULL,0,&r)){hb_xfree(all);all=NULL;break;}
            if(r.error || !r.size || r.size>total-done){vf_release(&r);hb_xfree(all);all=NULL;hb_fsSetError(13);break;}
            memcpy(all+done,r.data,r.size);done+=r.size;vf_release(&r);
        }
        if(all){const char* bytes=all;HB_SIZE size=total;array=hb_itemDeserialize(&bytes,&size);hb_xfree(all);}
    }
    return array?array:hb_itemArrayNew(0);
}
static HB_BOOL vf_timeget(PHB_FILE_FUNCS f,const char* n,long* day,long* ms){VF_REPLY r;HB_BOOL ok=HB_FALSE;HB_SYMBOL_UNUSED(f);if(vf_named(OADS_VF_TIMEGET,n,NULL,0,0,0,&r)){ok=r.a!=0&&r.size==16;if(ok){*day=(long)vf_signed(vf_get(r.data,8));*ms=(long)vf_signed(vf_get(r.data+8,8));}vf_release(&r);}return ok;}
static HB_BOOL vf_timeset(PHB_FILE_FUNCS f,const char* n,long day,long ms){HB_SYMBOL_UNUSED(f);return vf_bool(OADS_VF_TIMESET,n,NULL,day,ms,0);}
static HB_BOOL vf_attrget(PHB_FILE_FUNCS f,const char* n,HB_FATTR* attr){VF_REPLY r;HB_BOOL ok=HB_FALSE;HB_SYMBOL_UNUSED(f);if(vf_named(OADS_VF_ATTRGET,n,NULL,0,0,0,&r)){ok=r.a!=0;*attr=(HB_FATTR)r.b;vf_release(&r);}return ok;}
static HB_BOOL vf_attrset(PHB_FILE_FUNCS f,const char* n,HB_FATTR attr){HB_SYMBOL_UNUSED(f);return vf_bool(OADS_VF_ATTRSET,n,NULL,0,0,(HB_U32)attr);}
static HB_BOOL vf_link(PHB_FILE_FUNCS f,const char* n,const char* to){HB_SYMBOL_UNUSED(f);return vf_bool(OADS_VF_LINK,n,to,0,0,0);}
static HB_BOOL vf_linksym(PHB_FILE_FUNCS f,const char* n,const char* to){HB_SYMBOL_UNUSED(f);return vf_bool(OADS_VF_LINKSYM,n,to,0,0,0);}
static char* vf_linkread(PHB_FILE_FUNCS f,const char* n){VF_REPLY r;char* value=NULL;HB_SYMBOL_UNUSED(f);if(vf_named(OADS_VF_LINKREAD,n,NULL,0,0,0,&r)){if(!r.error)value=hb_strndup((const char*)r.data,r.size);vf_release(&r);}return value;}
/* Match hb_fsExtName's client SET DEFAULT/PATH selection, but ask the
 * selected remote namespace, never the client disk, whether a candidate exists. */
static HB_BOOL vf_remote_exists(ADSHANDLE conn,const char* path) {
    VF_REPLY r;HB_BOOL exists=HB_FALSE;
    if(vf_call(conn,OADS_VF_EXISTS,0,0,0,0,path,NULL,NULL,0,&r)) {exists=r.a!=0;vf_release(&r);}
    return exists;
}
static char* vf_extname(ADSHANDLE conn,const char* name,const char* ext,HB_FATTR flags,const char* paths) {
    PHB_FNAME parts=hb_fsFNameSplit(name);char* result=(char*)hb_xgrab(HB_PATH_MAX);
    HB_BOOL found=HB_FALSE;HB_PATHNAMES *next,*search=NULL;
    if(ext&&((flags&FXO_FORCEEXT)||!parts->szExtension))parts->szExtension=ext;
    if(parts->szPath)hb_fsFNameMerge(result,parts);
    else if(flags&FXO_DEFAULTS) {
        const char* def=hb_setGetDefault();
        if(def){parts->szPath=def;hb_fsFNameMerge(result,parts);found=vf_remote_exists(conn,result);}
        if(!found && !(flags&(FXO_TRUNCATE|FXO_APPEND|FXO_UNIQUE)) && hb_setGetPath()) {
            for(next=hb_setGetFirstSetPath();!found&&next;next=next->pNext) {
                parts->szPath=next->szPath;hb_fsFNameMerge(result,parts);found=vf_remote_exists(conn,result);
            }
        }
        if(!found){parts->szPath=def;hb_fsFNameMerge(result,parts);}
    } else if(paths&&*paths) {
        hb_fsAddSearchPath(paths,&search);
        for(next=search;!found&&next;next=next->pNext) {
            parts->szPath=next->szPath;hb_fsFNameMerge(result,parts);found=vf_remote_exists(conn,result);
        }
        hb_fsFreeSearchPath(search);
        if(!found){parts->szPath=NULL;hb_fsFNameMerge(result,parts);}
    } else hb_fsFNameMerge(result,parts);
    hb_xfree(parts);return result;
}
static PHB_FILE vf_open(PHB_FILE_FUNCS f,const char* n,const char* ext,HB_FATTR flags,const char* paths,PHB_ITEM error){
    VF_REPLY r;ADSHANDLE conn;const char* p;PHB_FILE file=NULL;char* resolved;
    if(!vf_path(n,&conn,&p)){hb_fsSetError(3);return NULL;}
    resolved=vf_extname(conn,p,ext,flags,paths);
    if(vf_call(conn,OADS_VF_OPEN,0,0,0,(HB_U32)flags,resolved,NULL,NULL,0,&r)){
        if(r.a>0&&!r.error){file=(PHB_FILE)hb_xgrab(sizeof(HB_FILE));file->pFuncs=f;file->conn=conn;file->id=(HB_U32)r.a;}
        vf_release(&r);
    }
    if(file && (flags&FXO_COPYNAME)) {
        char final[HB_PATH_MAX];int prefix=snprintf(final,sizeof(final),"oads%lu:",(unsigned long)conn);
        if(prefix>0 && (HB_SIZE)prefix<sizeof(final))hb_strncpy(final+prefix,resolved,sizeof(final)-(HB_SIZE)prefix-1);
        hb_strncpy((char*)HB_UNCONST(n),final,HB_PATH_MAX-1);
    }
    if(error){hb_errPutFileName(error,n);if(!file){hb_errPutOsCode(error,hb_fsError());hb_errPutGenCode(error,(flags&FXO_TRUNCATE)?EG_CREATE:EG_OPEN);}}
    hb_xfree(resolved);return file;
}
static void vf_close(PHB_FILE f){VF_REPLY r;if(vf_call(f->conn,OADS_VF_CLOSE,f->id,0,0,0,NULL,NULL,NULL,0,&r))vf_release(&r);hb_xfree(f);}
static HB_BOOL vf_unlock(PHB_FILE f,HB_FOFFSET start,HB_FOFFSET len);
static HB_BOOL vf_lock(PHB_FILE f,HB_FOFFSET start,HB_FOFFSET len,int type){
    HB_BOOL ok=HB_FALSE;VF_REPLY r;
    if(type&1)return vf_unlock(f,start,len);
    for(;;){if(!vf_call(f->conn,OADS_VF_LOCK,f->id,start,len,(HB_U32)(type&~0x0200),NULL,NULL,NULL,0,&r))break;
        ok=r.a!=0;{HB_BOOL again=!ok&&(type&0x0200)&&r.error==33&&r.b==0;vf_release(&r);if(!again)break;}hb_idleSleep(0.01);
    }return ok;
}
static HB_BOOL vf_unlock(PHB_FILE f,HB_FOFFSET start,HB_FOFFSET len){VF_REPLY r;HB_BOOL ok=HB_FALSE;if(vf_call(f->conn,OADS_VF_UNLOCK,f->id,start,len,0,NULL,NULL,NULL,0,&r)){ok=r.a!=0;vf_release(&r);}return ok;}
static int vf_locktest(PHB_FILE f,HB_FOFFSET start,HB_FOFFSET len,int type){VF_REPLY r;int value=-1;if(vf_call(f->conn,OADS_VF_LOCKTEST,f->id,start,len,(HB_U32)type,NULL,NULL,NULL,0,&r)){value=(int)r.a;vf_release(&r);}return value;}
static HB_SIZE vf_read_common(PHB_FILE f,void* data,HB_SIZE size,HB_FOFFSET offset,HB_BOOL at){
    HB_SIZE done=0;VF_REPLY r;
    do {HB_SIZE n=HB_MIN(size-done,OADS_VF_CHUNK_MAX);HB_SIZE got;
        if(!vf_call(f->conn,at?OADS_VF_READAT:OADS_VF_READ,f->id,(HB_MAXINT)n,(HB_MAXINT)offset+(HB_MAXINT)done,0,NULL,NULL,NULL,0,&r))return done?done:(HB_SIZE)FS_ERROR;
        if(r.a<0){vf_release(&r);return done?done:(HB_SIZE)FS_ERROR;}
        got=r.size;if(got>n){vf_release(&r);hb_fsSetError(13);return done?done:(HB_SIZE)FS_ERROR;}
        if(got)memcpy((char*)data+done,r.data,got);
        done+=got;vf_release(&r);if(got<n)break;
    }while(done<size);return done;
}
static HB_SIZE vf_write_common(PHB_FILE f,const void* data,HB_SIZE size,HB_FOFFSET offset,HB_BOOL at){
    HB_SIZE done=0;VF_REPLY r;
    do {HB_SIZE n=HB_MIN(size-done,OADS_VF_CHUNK_MAX);HB_SIZE wrote;
        if(!vf_call(f->conn,at?OADS_VF_WRITEAT:OADS_VF_WRITE,f->id,0,(HB_MAXINT)offset+(HB_MAXINT)done,0,NULL,NULL,data?(const char*)data+done:NULL,n,&r))return done?done:(HB_SIZE)FS_ERROR;
        if(r.a<0){vf_release(&r);return done?done:(HB_SIZE)FS_ERROR;}
        wrote=(HB_SIZE)r.a;if(wrote>n){vf_release(&r);hb_fsSetError(13);return done?done:(HB_SIZE)FS_ERROR;}
        done+=wrote;vf_release(&r);if(wrote<n)break;
    }while(done<size);return done;
}
static HB_SIZE vf_read(PHB_FILE f,void* b,HB_SIZE n,HB_MAXINT timeout){HB_SYMBOL_UNUSED(timeout);return vf_read_common(f,b,n,0,HB_FALSE);}
static HB_SIZE vf_write(PHB_FILE f,const void* b,HB_SIZE n,HB_MAXINT timeout){HB_SYMBOL_UNUSED(timeout);return vf_write_common(f,b,n,0,HB_FALSE);}
static HB_SIZE vf_readat(PHB_FILE f,void* b,HB_SIZE n,HB_FOFFSET at){return vf_read_common(f,b,n,at,HB_TRUE);}
static HB_SIZE vf_writeat(PHB_FILE f,const void* b,HB_SIZE n,HB_FOFFSET at){return vf_write_common(f,b,n,at,HB_TRUE);}
static HB_BOOL vf_trunc(PHB_FILE f,HB_FOFFSET at){VF_REPLY r;HB_BOOL ok=HB_FALSE;if(vf_call(f->conn,OADS_VF_TRUNC,f->id,at,0,0,NULL,NULL,NULL,0,&r)){ok=r.a!=0;vf_release(&r);}return ok;}
static HB_FOFFSET vf_seek(PHB_FILE f,HB_FOFFSET at,HB_USHORT flags){VF_REPLY r;HB_FOFFSET pos=0;if(vf_call(f->conn,OADS_VF_SEEK,f->id,at,0,flags,NULL,NULL,NULL,0,&r)){pos=(HB_FOFFSET)r.a;vf_release(&r);}return pos;}
static HB_FOFFSET vf_size(PHB_FILE f){VF_REPLY r;HB_FOFFSET n=0;if(vf_call(f->conn,OADS_VF_SIZE,f->id,0,0,0,NULL,NULL,NULL,0,&r)){n=(HB_FOFFSET)r.a;vf_release(&r);}return n;}
static HB_BOOL vf_eof(PHB_FILE f){VF_REPLY r;HB_BOOL eof=HB_TRUE;if(vf_call(f->conn,OADS_VF_EOF,f->id,0,0,0,NULL,NULL,NULL,0,&r)){eof=r.a!=0;vf_release(&r);}return eof;}
static void vf_flush(PHB_FILE f,HB_BOOL dirty){HB_SYMBOL_UNUSED(f);HB_SYMBOL_UNUSED(dirty);}
static void vf_commit(PHB_FILE f){VF_REPLY r;if(vf_call(f->conn,OADS_VF_COMMIT,f->id,0,0,0,NULL,NULL,NULL,0,&r))vf_release(&r);}
static HB_BOOL vf_config(PHB_FILE f,int index,PHB_ITEM value){HB_SYMBOL_UNUSED(f);if(index==HB_VF_IONAME){hb_itemPutC(value,"OADS:");return HB_TRUE;}return HB_FALSE;}
static HB_FHANDLE vf_handle(PHB_FILE f){HB_SYMBOL_UNUSED(f);return FS_ERROR;}
void oads_vf_register(void){
    hb_threadEnterCriticalSection(&s_register_lock);
    if(!s_registered){
        s_vf.Accept=vf_accept;s_vf.Exists=vf_exists;s_vf.Delete=vf_delete;s_vf.Rename=vf_rename;s_vf.Copy=vf_copy;
        s_vf.DirExists=vf_direxists;s_vf.DirMake=vf_dirmake;s_vf.DirRemove=vf_dirremove;s_vf.DirSpace=vf_dirspace;s_vf.Directory=vf_directory;
        s_vf.TimeGet=vf_timeget;s_vf.TimeSet=vf_timeset;s_vf.AttrGet=vf_attrget;s_vf.AttrSet=vf_attrset;s_vf.Link=vf_link;s_vf.LinkSym=vf_linksym;s_vf.LinkRead=vf_linkread;
        s_vf.Open=vf_open;s_vf.Close=vf_close;s_vf.Lock=vf_lock;s_vf.LockTest=vf_locktest;s_vf.Read=vf_read;s_vf.Write=vf_write;
        s_vf.ReadAt=vf_readat;s_vf.WriteAt=vf_writeat;s_vf.TruncAt=vf_trunc;s_vf.Seek=vf_seek;s_vf.Size=vf_size;s_vf.Eof=vf_eof;
        s_vf.Flush=vf_flush;s_vf.Commit=vf_commit;s_vf.Configure=vf_config;s_vf.Handle=vf_handle;
        s_registered=hb_fileRegisterFull(&s_vf);
    }hb_threadLeaveCriticalSection(&s_register_lock);
}
HB_FUNC(OADS_VFPATH){
    ADSHANDLE conn=(ADSHANDLE)hb_parnint(1);const char* path=hb_parc(2);char prefix[40];int len;
    oads_vf_register();
    if(!HB_ISNUM(1)||!conn||(HB_MAXINT)conn!=hb_parnint(1)||!path){hb_errRT_BASE_SubstR(EG_ARG,2021,NULL,HB_ERR_FUNCNAME,HB_ERR_ARGS_BASEPARAMS);return;}
    len=snprintf(prefix,sizeof(prefix),"oads%lu:",(unsigned long)conn);
    {HB_SIZE n=strlen(path);char* name=(char*)hb_xgrab((HB_SIZE)len+n+1);memcpy(name,prefix,(HB_SIZE)len);memcpy(name+len,path,n+1);hb_retclen_buffer(name,(HB_SIZE)len+n);}
}

HB_FUNC(OADS_VFTEMPFILESERVER){
    ADSHANDLE conn;const char* dir;const char* remote=hb_parc(2);const char* prefix=hb_parc(3);const char* ext=hb_parc(4);VF_REPLY r;
    oads_vf_register();
    if(!vf_path(remote,&conn,&dir)){hb_fsSetFError(3);hb_errRT_BASE_SubstR(EG_ARG,2021,NULL,HB_ERR_FUNCNAME,HB_ERR_ARGS_BASEPARAMS);return;}
    if(vf_call(conn,OADS_VF_TEMPSERVER,0,0,0,(HB_U32)hb_parnldef(5,FC_NORMAL),dir,prefix?prefix:"",ext,ext?strlen(ext):0,&r)){
        if(!r.error && r.a>0){
            PHB_FILE file=(PHB_FILE)hb_xgrab(sizeof(HB_FILE));HB_SIZE n=strlen(remote);HB_SIZE sep=n&&remote[n-1]!='/'&&remote[n-1]!='\\';
            char* name=(char*)hb_xgrab(n+sep+r.size+1);memcpy(name,remote,n);if(sep)name[n++]='/';memcpy(name+n,r.data,r.size);name[n+r.size]=0;
            file->pFuncs=&s_vf;file->conn=conn;file->id=(HB_U32)r.a;hb_fileItemPut(hb_param(-1,HB_IT_ANY),file);hb_storc(name,1);hb_xfree(name);
        }
        vf_release(&r);
    }
    hb_fsSetFError(hb_fsError());
}
