#define COBJMACROS
#include <windows.h>
#include <objbase.h>
#include <stdio.h>
#include <string.h>
typedef HRESULT (WINAPI *PFN)(IUnknown*, DWORD, IUnknown**);
static PFN pGet;
static const IID IID_IMarshal2_ = {0x000001cf,0,0,{0xc0,0,0,0,0,0,0,0x46}};
typedef struct { IUnknown IUnknown_iface; LONG ref; IUnknown *inner; } Outer;
static HRESULT WINAPI O_QI(IUnknown *i, REFIID r, void **pv){ Outer *o=(Outer*)i; *pv=NULL;
  if (IsEqualIID(r,&IID_IUnknown)) { *pv=i; IUnknown_AddRef(i); return S_OK; }
  if (IsEqualIID(r,&IID_IMarshal) && o->inner) return IUnknown_QueryInterface(o->inner,r,pv);
  return E_NOINTERFACE; }
static ULONG WINAPI O_AR(IUnknown *i){ return InterlockedIncrement(&((Outer*)i)->ref); }
static ULONG WINAPI O_RL(IUnknown *i){ return InterlockedDecrement(&((Outer*)i)->ref); }
static IUnknownVtbl OV={O_QI,O_AR,O_RL};
static void oinit(Outer *o){ o->IUnknown_iface.lpVtbl=&OV; o->ref=1; o->inner=NULL; }
static const char *st(IUnknown *in){ return in==NULL?"NULL":(in==(void*)1?"untouched":"set"); }
int main(int argc,char**argv){
  setvbuf(stdout,NULL,_IONBF,0);
  pGet=(PFN)GetProcAddress(LoadLibraryA("combase.dll"),"CoGetStdMarshalEx");
  CoInitializeEx(NULL,COINIT_APARTMENTTHREADED);
  if(argc>1&&!strcmp(argv[1],"nullout")){ Outer o; oinit(&o); printf("T7 NULL ppInner hr=%08lx\n",pGet(&o.IUnknown_iface,1,NULL)); return 0; }
  { Outer o; oinit(&o); IStream *s; CreateStreamOnHGlobal(NULL,TRUE,&s);
    HRESULT m=CoMarshalInterface(s,&IID_IUnknown,&o.IUnknown_iface,MSHCTX_INPROC,NULL,MSHLFLAGS_NORMAL);
    IUnknown *in=(void*)1; HRESULT h=pGet(&o.IUnknown_iface,1,&in); printf("T6 after plain marshal(hr=%08lx): hr=%08lx inner=%s ref=%ld\n",m,h,st(in),o.ref);
    if(SUCCEEDED(h)&&in&&in!=(void*)1) IUnknown_Release(in);
    LARGE_INTEGER z={0}; IStream_Seek(s,z,STREAM_SEEK_SET,NULL); printf("T6 ReleaseMarshalData hr=%08lx\n",CoReleaseMarshalData(s));
    printf("T6 Disconnect hr=%08lx ref=%ld\n",CoDisconnectObject(&o.IUnknown_iface,0),o.ref); IStream_Release(s);
    in=(void*)1; h=pGet(&o.IUnknown_iface,1,&in); printf("T6b after disconnect: hr=%08lx inner=%s\n",h,st(in)); if(SUCCEEDED(h)&&in&&in!=(void*)1) IUnknown_Release(in); }
  { Outer o; oinit(&o); IUnknown *in=NULL; HRESULT h=pGet(&o.IUnknown_iface,1,&in); printf("T8 create hr=%08lx\n",h);
    IUnknown_Release(in); IUnknown *in2=(void*)1; h=pGet(&o.IUnknown_iface,1,&in2); printf("T8 after inner released (never marshaled): hr=%08lx inner=%s\n",h,st(in2)); if(SUCCEEDED(h)&&in2&&in2!=(void*)1) IUnknown_Release(in2); }
  { Outer o; oinit(&o); IUnknown *in=NULL; pGet(&o.IUnknown_iface,1,&in); o.inner=in;
    IMarshal *m1=NULL,*m2=NULL; IUnknown_QueryInterface(in,&IID_IMarshal,(void**)&m1); IUnknown_QueryInterface(in,&IID_IMarshal2_,(void**)&m2);
    printf("T9 IMarshal2==IMarshal %d\n",m1==m2); if(m2) IMarshal_Release(m2);
    printf("T9 DisconnectObject unmarshaled hr=%08lx\n",IMarshal_DisconnectObject(m1,0));
    IStream *s; CreateStreamOnHGlobal(NULL,TRUE,&s);
    HRESULT mm=IMarshal_MarshalInterface(m1,s,&IID_IUnknown,&o.IUnknown_iface,MSHCTX_INPROC,NULL,MSHLFLAGS_NORMAL); ULARGE_INTEGER pos; LARGE_INTEGER z={0}; IStream_Seek(s,z,STREAM_SEEK_CUR,&pos);
    printf("T9 direct MarshalInterface hr=%08lx bytes=%lu ref=%ld\n",mm,(ULONG)pos.QuadPart,o.ref);
    IStream_Seek(s,z,STREAM_SEEK_SET,NULL); DWORD hdr[2]={0}; ULONG rd; IStream_Read(s,hdr,8,&rd); printf("T9 objref sig=%08lx flags=%lx\n",hdr[0],hdr[1]);
    IStream_Seek(s,z,STREAM_SEEK_SET,NULL); printf("T9 ReleaseMarshalData via IMarshal hr=%08lx ref=%ld\n",IMarshal_ReleaseMarshalData(m1,s),o.ref);
    printf("T9 DisconnectObject hr=%08lx ref=%ld\n",IMarshal_DisconnectObject(m1,0),o.ref);
    IMarshal_Release(m1); IStream_Release(s); o.inner=NULL; printf("T9 inner release->%lu ref=%ld\n",IUnknown_Release(in),o.ref); }
  CoUninitialize(); printf("done\n"); return 0; }
