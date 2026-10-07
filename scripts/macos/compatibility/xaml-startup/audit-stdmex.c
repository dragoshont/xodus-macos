#define COBJMACROS
#include <windows.h>
#include <objbase.h>
#include <stdio.h>

typedef HRESULT (WINAPI *PFN)(IUnknown*, DWORD, IUnknown**);
static PFN pGet;
static const IID IID_IMarshal2_ = {0x000001cf,0,0,{0xc0,0,0,0,0,0,0,0x46}};
static const IID IID_IStdMarshalInfo_ = {0x00000018,0,0,{0xc0,0,0,0,0,0,0,0x46}};
static const IID IID_IExternalConnection_ = {0x00000019,0,0,{0xc0,0,0,0,0,0,0,0x46}};
static const IID IID_IStdIdentity_ = {0x0000001b,0,0,{0xc0,0,0,0,0,0,0,0x46}};
static const IID IID_IProxyManager_ = {0x00000008,0,0,{0xc0,0,0,0,0,0,0,0x46}};
static const IID IID_IClientSecurity_ = {0x0000013d,0,0,{0xc0,0,0,0,0,0,0,0x46}};
static const IID IID_IMultiQI_ = {0x00000020,0,0,{0xc0,0,0,0,0,0,0,0x46}};
static const IID IID_IRpcOptions_ = {0x00000144,0,0,{0xc0,0,0,0,0,0,0,0x46}};
static const IID IID_INoMarshal_ = {0xecc8691b,0xc1db,0x4dc0,{0x85,0x5e,0x65,0xf6,0xc5,0x51,0xaf,0x49}};
static const IID IID_IAgileObject_ = {0x94ea2b94,0xe9cc,0x49e0,{0xc0,0xff,0xee,0x64,0xca,0x8f,0x5b,0x90}};
static const IID IID_IMarshalOptions_ = {0x4c1e39e1,0xe3e3,0x4296,{0xaa,0x86,0xec,0x93,0x8d,0x89,0x6e,0x92}};

typedef struct { IUnknown IUnknown_iface; LONG ref; IUnknown *inner; int log; } Outer;
static void pg(const GUID *g){ printf("{%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}",g->Data1,g->Data2,g->Data3,g->Data4[0],g->Data4[1],g->Data4[2],g->Data4[3],g->Data4[4],g->Data4[5],g->Data4[6],g->Data4[7]); }
static HRESULT WINAPI O_QI(IUnknown *i, REFIID r, void **pv){
  Outer *o=(Outer*)i; *pv=NULL;
  if (o->log && !IsEqualIID(r,&IID_IUnknown)) { printf("    [outer QI "); pg(r); printf("]\n"); }
  if (IsEqualIID(r,&IID_IUnknown)) { *pv=i; IUnknown_AddRef(i); return S_OK; }
  if (IsEqualIID(r,&IID_IMarshal) && o->inner) return IUnknown_QueryInterface(o->inner,r,pv);
  return E_NOINTERFACE; }
static ULONG WINAPI O_AR(IUnknown *i){ return InterlockedIncrement(&((Outer*)i)->ref); }
static ULONG WINAPI O_RL(IUnknown *i){ return InterlockedDecrement(&((Outer*)i)->ref); }
static IUnknownVtbl OV={O_QI,O_AR,O_RL};
static void oinit(Outer *o){ o->IUnknown_iface.lpVtbl=&OV; o->ref=1; o->inner=NULL; o->log=1; }

static const IID *qis[]={&IID_IUnknown,&IID_IMarshal,&IID_IMarshal2_,&IID_IStdMarshalInfo_,&IID_IExternalConnection_,&IID_IStdIdentity_,&IID_IProxyManager_,&IID_IClientSecurity_,&IID_IMultiQI_,&IID_IRpcOptions_,&IID_INoMarshal_,&IID_IAgileObject_,&IID_IMarshalOptions_};
static const char *qin[]={"IUnknown","IMarshal","IMarshal2","IStdMarshalInfo","IExternalConnection","IStdIdentity","IProxyManager","IClientSecurity","IMultiQI","IRpcOptions","INoMarshal","IAgileObject","IMarshalOptions"};

static DWORD WINAPI noinit(void *p){ Outer o; oinit(&o); IUnknown *in=(void*)1; HRESULT hr=pGet(&o.IUnknown_iface,1,&in); printf("T0 no-COM flags=1 hr=%08lx inner=%s ref=%ld\n",hr,in==NULL?"NULL":(in==(void*)1?"untouched":"set"),o.ref); if(SUCCEEDED(hr)&&in&&in!=(void*)1) IUnknown_Release(in); return 0; }

static IStream *gstm;
static DWORD WINAPI mta(void *p){ CoInitializeEx(NULL,COINIT_MULTITHREADED); IUnknown *x=NULL; LARGE_INTEGER z={0}; IStream_Seek(gstm,z,STREAM_SEEK_SET,NULL); HRESULT hr=CoUnmarshalInterface(gstm,&IID_IUnknown,(void**)&x); printf("T4 MTA unmarshal hr=%08lx proxy=%s\n",hr,x?(x==p?"SAME-AS-OUTER":"distinct"):"NULL"); if(x){ IUnknown *m=NULL; HRESULT h2=IUnknown_QueryInterface(x,&IID_IMarshal,(void**)&m); printf("T4 proxy QI IMarshal hr=%08lx\n",h2); if(m) IUnknown_Release(m); IUnknown_Release(x);} CoUninitialize(); return 0; }
static void pump(HANDLE h){ for(;;){ DWORD r=MsgWaitForMultipleObjects(1,&h,FALSE,5000,QS_ALLINPUT); if(r==WAIT_OBJECT_0) break; if(r==WAIT_TIMEOUT){ printf("  pump TIMEOUT\n"); break;} MSG m; while(PeekMessageW(&m,0,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);} } }

int main(void){
  setvbuf(stdout,NULL,_IONBF,0);
  HMODULE cb=LoadLibraryA("combase.dll"); pGet=(PFN)GetProcAddress(cb,"CoGetStdMarshalEx");
  printf("combase=%p CoGetStdMarshalEx=%p ole32=%p\n",cb,pGet,GetProcAddress(LoadLibraryA("ole32.dll"),"CoGetStdMarshalEx"));
  if(!pGet) return 1;
  HANDLE t=CreateThread(NULL,0,noinit,NULL,0,NULL); WaitForSingleObject(t,INFINITE); CloseHandle(t);
  printf("CoInit STA hr=%08lx\n",CoInitializeEx(NULL,COINIT_APARTMENTTHREADED));
  DWORD fl[]={0,1,2,3,4,8,0x80000000};
  for(int k=0;k<7;k++){ Outer o; oinit(&o); IUnknown *in=(void*)1; HRESULT hr=pGet(&o.IUnknown_iface,fl[k],&in);
    printf("T1 flags=%lx hr=%08lx inner=%s outerref=%ld\n",fl[k],hr,in==NULL?"NULL":(in==(void*)1?"untouched":"set"),o.ref);
    if(SUCCEEDED(hr)&&in&&in!=(void*)1){ ULONG r=IUnknown_Release(in); printf("   inner Release->%lu outerref=%ld\n",r,o.ref);} }
  { IUnknown *in=(void*)1; HRESULT hr=pGet(NULL,1,&in); printf("T2 NULL outer f=1 hr=%08lx inner=%s\n",hr,in==NULL?"NULL":(in==(void*)1?"untouched":"set")); if(SUCCEEDED(hr)&&in&&in!=(void*)1) IUnknown_Release(in);
    in=(void*)1; hr=pGet(NULL,2,&in); printf("T2 NULL outer f=2 hr=%08lx inner=%s\n",hr,in==NULL?"NULL":(in==(void*)1?"untouched":"set")); if(SUCCEEDED(hr)&&in&&in!=(void*)1) IUnknown_Release(in); }
  for(DWORD f=1;f<=2;f++){
    Outer o; oinit(&o); IUnknown *in=NULL; HRESULT hr=pGet(&o.IUnknown_iface,f,&in);
    printf("T3 flags=%lu hr=%08lx outerref=%ld\n",f,hr,o.ref); if(FAILED(hr)||!in) continue;
    o.inner=in; o.log=1;
    for(int q=0;q<13;q++){ void *pv=NULL; LONG b=o.ref; HRESULT h=IUnknown_QueryInterface(in,qis[q],&pv);
      printf("  inner QI %-20s hr=%08lx %s outer %ld->%ld\n",qin[q],h,pv?(pv==in?"==inner":(pv==&o.IUnknown_iface?"==outer":"other")):"NULL",b,o.ref);
      if(pv){ if(q==1){ IMarshal *m=pv; void *u=NULL; IMarshal_QueryInterface(m,&IID_IUnknown,&u); printf("    IMarshal->QI(IUnknown)=%s outerref=%ld\n",u==&o.IUnknown_iface?"outer":(u==in?"inner":"other"),o.ref); if(u) IUnknown_Release((IUnknown*)u);
          void *u2=NULL; HRESULT h3=IMarshal_QueryInterface(m,&IID_IStdIdentity_,&u2); printf("    IMarshal->QI(IStdIdentity) hr=%08lx\n",h3); if(u2) IUnknown_Release((IUnknown*)u2);
          CLSID c={0}; DWORD sz=0; HRESULT a=IMarshal_GetUnmarshalClass(m,&IID_IUnknown,&o.IUnknown_iface,MSHCTX_INPROC,NULL,MSHLFLAGS_NORMAL,&c); printf("    GetUnmarshalClass inproc hr=%08lx ",a); pg(&c); printf("\n");
          a=IMarshal_GetUnmarshalClass(m,&IID_IUnknown,&o.IUnknown_iface,MSHCTX_LOCAL,NULL,MSHLFLAGS_NORMAL,&c); printf("    GetUnmarshalClass local hr=%08lx ",a); pg(&c); printf("\n");
          a=IMarshal_GetMarshalSizeMax(m,&IID_IUnknown,&o.IUnknown_iface,MSHCTX_INPROC,NULL,MSHLFLAGS_NORMAL,&sz); printf("    GetMarshalSizeMax hr=%08lx size=%lu\n",a,sz); }
        IUnknown_Release((IUnknown*)pv); } }
    if(f==1){ CreateStreamOnHGlobal(NULL,TRUE,&gstm); printf("T4 refs before marshal outer=%ld\n",o.ref);
      HRESULT m=CoMarshalInterface(gstm,&IID_IUnknown,&o.IUnknown_iface,MSHCTX_INPROC,NULL,MSHLFLAGS_NORMAL); printf("T4 CoMarshalInterface hr=%08lx outerref=%ld\n",m,o.ref);
      if(SUCCEEDED(m)){ HANDLE th=CreateThread(NULL,0,mta,&o.IUnknown_iface,0,NULL); pump(th); CloseHandle(th); printf("T4 after MTA outerref=%ld\n",o.ref); }
      IUnknown *in2=(void*)1; HRESULT h2=pGet(&o.IUnknown_iface,1,&in2); printf("T5 second call same outer hr=%08lx inner=%s same=%d\n",h2,in2==NULL?"NULL":(in2==(void*)1?"untouched":"set"),in2==in); if(SUCCEEDED(h2)&&in2&&in2!=(void*)1) IUnknown_Release(in2);
      printf("T4 CoDisconnectObject hr=%08lx outerref=%ld\n",CoDisconnectObject(&o.IUnknown_iface,0),o.ref);
      IStream_Release(gstm); }
    o.inner=NULL; ULONG r=IUnknown_Release(in); printf("T3 final inner Release->%lu outerref=%ld\n",r,o.ref);
  }
  CoUninitialize(); printf("done\n"); return 0; }
