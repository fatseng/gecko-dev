/* -*- Mode: C++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 8 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "PPAPIJSProcessParent.h"
#include "jsfriendapi.h"
#include "mozilla/dom/ContentChild.h"
#include "mozilla/dom/PContentParent.h"
#include "mozilla/ipc/FileDescriptor.h"
#include "mozilla/ipc/ProcessChild.h"
#include "mozilla/Logging.h"
#include "mozilla/Services.h"
#include "nsAnonymousTemporaryFile.h"
#include "nsIObserverService.h"
#include "private/pprio.h"

#ifdef XP_WIN
#include "mozilla/widget/nsDeviceContextSpecWin.h"
#endif

using mozilla::ipc::FileDescriptor;

namespace mozilla {
namespace plugins {

static LazyLogModule gPPAPIJSParentLog("PPAPIJSParent");

class PPAPIJSParent : public PPPAPIJSParent
{
public:
  explicit PPAPIJSParent(nsIPPAPIJSFromPluginCallback* aFromPlugin)
    : PPPAPIJSParent(),
      mFromPlugin(aFromPlugin)
  {}

  virtual ipc::IPCResult AnswerFromPlugin(const nsCString& aAPI,
                                          nsCString* aResult) override;
  virtual ipc::IPCResult RecvAsyncFromPlugin(const nsCString& aAPI) override;
  virtual void ActorDestroy(ActorDestroyReason aWhy) override;

  bool AllocShmem(size_t aSize, Shmem* aMem)
  {
    return AllocUnsafeShmem(aSize, SharedMemory::TYPE_BASIC, aMem);
  }
  void CacheShmem(JSContext* aCx, uintptr_t aAddress, const Shmem& aMem);
  void GetCachedShmem(uintptr_t aAddress,
                      JS::MutableHandle<JS::Value> aArrayBuffer);
  void RemoveCachedShmem(JSContext* aCx, uintptr_t aAddress);

private:
  nsCOMPtr<nsIPPAPIJSFromPluginCallback> mFromPlugin;

  class ShmemAndArrayBuffer : public nsVoidPtrHashKey
  {
  public:
    explicit ShmemAndArrayBuffer(KeyTypePointer aKey)
      : nsVoidPtrHashKey(aKey)
    {}
    ShmemAndArrayBuffer(const ShmemAndArrayBuffer& aToCopy)
      : nsVoidPtrHashKey(aToCopy),
        mShmem(aToCopy.mShmem),
        mArrayBuffer(aToCopy.mArrayBuffer)
    {}
    ~ShmemAndArrayBuffer()
    {}

    enum { ALLOW_MEMMOVE = false };

    Shmem mShmem;
    JS::PersistentRooted<JSObject*> mArrayBuffer;
  };
  nsTHashtable<ShmemAndArrayBuffer> mShmemSet;
};

ipc::IPCResult
PPAPIJSParent::AnswerFromPlugin(const nsCString& aAPI,
                                nsCString* aResult)
{
  MOZ_LOG(gPPAPIJSParentLog, LogLevel::Verbose,
          ("From plugin: %s\n", aAPI.get()));
  nsresult rv = mFromPlugin->ReceiveMessage(aAPI, *aResult);
  if (!aResult->IsEmpty()) {
    MOZ_LOG(gPPAPIJSParentLog, LogLevel::Verbose,
            ("RPC response from plugin: %s\n", aResult->get()));
  }
  return NS_SUCCEEDED(rv) ? IPC_OK() :
         IPC_FAIL(this, "The handler for a PPAPI call failed");
}

ipc::IPCResult
PPAPIJSParent::RecvAsyncFromPlugin(const nsCString& aAPI)
{
  MOZ_LOG(gPPAPIJSParentLog, LogLevel::Verbose,
          ("From plugin: %s\n", aAPI.get()));
  nsCString result;
  nsresult rv = mFromPlugin->ReceiveMessage(aAPI, result);
  MOZ_ASSERT(result.IsEmpty());
  return NS_SUCCEEDED(rv) ? IPC_OK() :
         IPC_FAIL(this, "The handler for a PPAPI call failed");
}

void
PPAPIJSParent::ActorDestroy(ActorDestroyReason aWhy)
{
  if (AbnormalShutdown == aWhy) {
      NS_WARNING("shutting down early because of crash!");
    ipc::ProcessChild::QuickExit();
  }
  mFromPlugin = nullptr;
}

void
PPAPIJSParent::CacheShmem(JSContext* aCx, uintptr_t aAddress, const Shmem& aMem)
{
  ShmemAndArrayBuffer* entry = mShmemSet.PutEntry((void*)aAddress);
  MOZ_ASSERT(!entry->mArrayBuffer);
  entry->mShmem = aMem;
  entry->mArrayBuffer.init(aCx,
                           JS_NewArrayBufferWithExternalContents(aCx,
                                                                 aMem.Size<char>(),
                                                                 aMem.get<char>()));
}

void
PPAPIJSParent::GetCachedShmem(uintptr_t aAddress,
                              JS::MutableHandle<JS::Value> aArrayBuffer)
{
  ShmemAndArrayBuffer* entry = mShmemSet.GetEntry((void*)aAddress);
  aArrayBuffer.setObject(*entry->mArrayBuffer);
}

void
PPAPIJSParent::RemoveCachedShmem(JSContext* aCx, uintptr_t aAddress)
{
  ShmemAndArrayBuffer* entry = mShmemSet.GetEntry((void*)aAddress);
  JS_DetachArrayBuffer(aCx, entry->mArrayBuffer);
  mShmemSet.RemoveEntry(entry);
}

void
PPAPIJSPluginParent::Delete()
{
  RefPtr<DeleteTask<PPAPIJSPluginParent>> task =
    new DeleteTask<PPAPIJSPluginParent>(this);
  XRE_GetIOMessageLoop()->PostTask(task.forget());
}

static std::vector<PPAPIJSPluginParent*> sPPAPIPlugins;

void
PPAPIJSPluginParent::ActorDestroy(ActorDestroyReason aWhy)
{
  sPPAPIPlugins[mJSPluginID] = nullptr;
}

#ifdef XP_WIN
static std::vector<nsDeviceContextSpecWin*> sDeviceContextSpec;
#endif

ipc::IPCResult
PPAPIJSPluginParent::RecvNotifyPageCount(const uint16_t& aID, const int& aPageCount)
{
#ifdef XP_WIN
  printf("---RecvNotifyPageCount---\n");
  sDeviceContextSpec[aID]->SetPDFPageCount(aID, aPageCount);
#endif
  return IPC_OK();
}

ipc::IPCResult
PPAPIJSPluginParent::RecvPrintEMF(const uint16_t& aID, const nsString& aFilePath,
                                  const int& aPageNum, const float& aScaleFactor)
{
#ifdef XP_WIN
  printf("---RecvPrintEMF:  %d---\n", aPageNum);
  sDeviceContextSpec[aID]->PrintEMF(aID, aFilePath, aScaleFactor);
#endif
  return IPC_OK();
}

void
PPAPIJSPluginParent::DeallocPPPAPIJSPluginParent()
{
  Delete();
}

#ifdef XP_WIN
int16_t
PPAPIJSPluginParent::SetDeviceContextSpecWin(
  nsDeviceContextSpecWin* aDeviceContextSpec)
{
  uint16_t i = 0;
  for (; i < sDeviceContextSpec.size(); ++i) {
    if (!sDeviceContextSpec[i]) {
      break;
    }
  }
  if (i == sDeviceContextSpec.size()) {
    sDeviceContextSpec.resize(i + 1);
  }
  sDeviceContextSpec[i] = aDeviceContextSpec;
  return i;
}
#endif

static PPAPIJSProcess* gPPAPIJSProcess = nullptr;

PPAPIJSProcess::PPAPIJSProcess()
{
  MOZ_ASSERT(!gPPAPIJSProcess, "PPAPIJSProcess is a service!");
  gPPAPIJSProcess = this;

  nsCOMPtr<nsIObserverService> obsService =
    mozilla::services::GetObserverService();
  if (obsService) {
    obsService->AddObserver(this, "content-child-shutdown", false);
  }
}

PPAPIJSProcess::~PPAPIJSProcess()
{
  gPPAPIJSProcess = nullptr;
}

NS_IMPL_ISUPPORTS(PPAPIJSProcess, nsIPPAPIJSProcess, nsIObserver)

NS_IMETHODIMP
PPAPIJSProcess::Launch(nsIPPAPIJSFromPluginCallback* aFromPlugin)
{
  MOZ_LOG(gPPAPIJSParentLog, LogLevel::Verbose,
          ("PPAPIJSProcessParent: %d\n", base::GetCurrentProcId()));

  dom::ContentChild* cp = dom::ContentChild::GetSingleton();
  nsresult rv;
  ipc::Endpoint<PPPAPIJSParent> endpoint;
  if (!cp->SendLoadPPAPIPlugin(&endpoint, &rv)) {
    return NS_ERROR_FAILURE;
  }
  NS_ENSURE_SUCCESS(rv, rv);
  mParent = new PPAPIJSParent(aFromPlugin);
  endpoint.Bind(mParent);
  return NS_OK;
}

/* static */
PPAPIJSPluginParent*
PPAPIJSProcess::GetPPAPIJSPluginParent(uint32_t aJSPluginID)
{
  return sPPAPIPlugins[aJSPluginID];
}

/* static */
nsresult
PPAPIJSProcess::SetupBridge(dom::PContentParent* aContentParent,
                            uint32_t aJSPluginID,
                            ipc::Endpoint<PPPAPIJSParent>* aParentEndpoint)
{
  if (aJSPluginID >= sPPAPIPlugins.size()) {
    sPPAPIPlugins.resize(aJSPluginID + 1);
  }

  MOZ_ASSERT(!sPPAPIPlugins[aJSPluginID],
             "Trying to load a plugin that's already loaded.");

  RefPtr<nsPluginHost> pluginHost = nsPluginHost::GetInst();
  nsFakePluginTag* tag = pluginHost->GetPluginTagForID(aJSPluginID);

  ipc::GeckoChildProcessHost* process(
    new ipc::GeckoChildProcessHost(GeckoProcessType_PPAPIJS));
  if (!process->SyncLaunch(tag->PPAPIProcessArgs())) {
    RefPtr<DeleteTask<ipc::GeckoChildProcessHost>> task =
      new DeleteTask<ipc::GeckoChildProcessHost>(process);
    XRE_GetIOMessageLoop()->PostTask(task.forget());
    return NS_ERROR_FAILURE;
  }

  PPAPIJSPluginParent* pluginParent =
    new PPAPIJSPluginParent(aJSPluginID, process);
  if (!pluginParent->Open(process->GetChannel(),
                          base::GetProcId(process->GetChildProcessHandle()))) {
    pluginParent->Delete();
    return NS_ERROR_FAILURE;
  }

  ipc::Endpoint<PPPAPIJSChild> childEndpoint;
  nsresult rv = PPPAPIJS::CreateEndpoints(aContentParent->OtherPid(),
                                          pluginParent->OtherPid(),
                                          aParentEndpoint, &childEndpoint);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    pluginParent->Delete();
    return rv;
  }

  if (!pluginParent->SendBridged(childEndpoint)) {
    pluginParent->Delete();
    return NS_ERROR_FAILURE;
  }

  sPPAPIPlugins[aJSPluginID] = pluginParent;

  return NS_OK;
}

NS_IMETHODIMP
PPAPIJSProcess::Observe(nsISupports* aSubject, const char* aTopic,
                        const char16_t* aData)
{
  if (!strcmp("content-child-shutdown", aTopic)) {
    nsCOMPtr<nsIObserverService> obsService =
      mozilla::services::GetObserverService();
    // Keep this alive while we close the channel.
    RefPtr<PPAPIJSProcess> observer(this);
    if (obsService) {
      obsService->RemoveObserver(observer, "content-child-shutdown");
    }
    if (mParent) {
      mParent->Close();
    }
    return NS_OK;
  }
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
PPAPIJSProcess::SendMessage(const nsACString& aAPI,
                            nsACString& aResult)
{
  MOZ_LOG(gPPAPIJSParentLog, LogLevel::Verbose,
          ("To plugin: %s\n", PromiseFlatCString(aAPI).get()));
  nsCString result;
  if (!mParent->CallToPlugin(nsCString(aAPI), &result)) {
    return NS_ERROR_FAILURE;
  }
  if (!result.IsEmpty()) {
    MOZ_LOG(gPPAPIJSParentLog, LogLevel::Verbose,
            ("RPC response to plugin: %s\n", result.get()));
  }

  aResult = result;

  return NS_OK;
}

NS_IMETHODIMP
PPAPIJSProcess::OpenAndSend(nsIFile* aFile, int32_t aFlags, int32_t aMode,
                            int64_t* aFd)
{
  PRFileDesc* desc;
  nsresult rv;
  if (aFile) {
    rv = aFile->OpenNSPRFileDesc(aFlags, aMode, &desc);
  } else {
    rv = NS_OpenAnonymousTemporaryFile(&desc);
  }
  NS_ENSURE_SUCCESS(rv, rv);

  if (!mParent->CallSendFile(FileDescriptor(FileDescriptor::PlatformHandleType(PR_FileDesc2NativeHandle(desc))), aFd)) {
    // FIXME Close file!
    return NS_ERROR_FAILURE;
  }

  return NS_OK;
}

NS_IMETHODIMP
PPAPIJSProcess::AllocateCachedBuffer(size_t aSize, JSContext* aCx,
                                     uint64_t* aAddress)
{
  ipc::Shmem mem;
  if (!mParent->AllocShmem(aSize, &mem)) {
    return NS_ERROR_FAILURE;
  }

  ipc::Shmem toSend(mem);
  uintptr_t address;
  if (!mParent->CallSendShmem(toSend, 0, 0, &address)) {
    return NS_ERROR_FAILURE;
  }

  mParent->CacheShmem(aCx, address, mem);
  *aAddress = address;

  return NS_OK;
}

NS_IMETHODIMP
PPAPIJSProcess::GetCachedBuffer(uint64_t aAddress, JSContext* aCx,
                                JS::MutableHandle<JS::Value> aArrayBuffer)
{
  mParent->GetCachedShmem((uintptr_t)aAddress, aArrayBuffer);

  return aArrayBuffer.isObject() ? NS_OK : NS_ERROR_FAILURE;
}

NS_IMETHODIMP
PPAPIJSProcess::FreeCachedBuffer(uint64_t aAddress, JSContext* aCx)
{
  mParent->RemoveCachedShmem(aCx, (uintptr_t)aAddress);

  return NS_OK;
}

NS_IMETHODIMP
PPAPIJSProcess::SetBuffer(JS::Handle<JS::Value> aArrayBuffer, size_t aSize,
                          uint64_t aAddress)
{
  ipc::Shmem mem;
  if (!mParent->AllocShmem(aSize, &mem)) {
    return NS_ERROR_FAILURE;
  }

  {
    JSObject* arrayBuffer = &aArrayBuffer.toObject();
    JS::AutoCheckCannotGC nogc;
    void* data;
    bool isSharedMemory;
    if (JS_IsArrayBufferObject(arrayBuffer)) {
      data = JS_GetArrayBufferData(arrayBuffer, &isSharedMemory, nogc);
    } else {
      data = JS_GetArrayBufferViewData(arrayBuffer, &isSharedMemory, nogc);
    }

    memcpy(mem.get<char>(), data, aSize);
  }

  ipc::Shmem toSend(mem);
  uintptr_t unused;
  if (!mParent->CallSendShmem(toSend, (uintptr_t)aAddress, 0, &unused)) {
    return NS_ERROR_FAILURE;
  }

  mParent->DeallocShmem(mem);

  return NS_OK;
}

NS_IMETHODIMP
PPAPIJSProcess::CopyFromBuffer(uint64_t aAddress, size_t aSize,
                               JSContext* aCx,
                               JS::MutableHandle<JS::Value> aArrayBuffer)
{
  ipc::Shmem mem;
  if (!mParent->AllocShmem(aSize, &mem)) {
    return NS_ERROR_FAILURE;
  }

  ipc::Shmem toSend(mem);
  uintptr_t unused;
  if (!mParent->CallSendShmem(toSend, 0, (uintptr_t)aAddress, &unused)) {
    return NS_ERROR_FAILURE;
  }

  aArrayBuffer.setObjectOrNull(JS_NewArrayBuffer(aCx, aSize));

  uint32_t size;
  uint8_t* data;
  bool isSharedMemory;
  js::GetArrayBufferLengthAndData(&aArrayBuffer.toObject(), &size,
                                  &isSharedMemory, &data);
  memcpy(data, mem.get<char>(), size);

  mParent->DeallocShmem(mem);

  return aArrayBuffer.isObject() ? NS_OK : NS_ERROR_FAILURE;
}

} // namespace plugins
} // namespace mozilla
