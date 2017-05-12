/* -*- Mode: C++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 8 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "PPAPIJSProcessChild.h"
#include "nsThreadManager.h"
#include "nsThreadUtils.h"
#include "prlink.h"
#include "mozilla/BackgroundHangMonitor.h"
#include "mozilla/ipc/IOThreadChild.h"
#include "mozilla/plugins/PPPAPIJSPluginChild.h"

#ifdef XP_WIN
#include "mozilla/widget/PDFViaEMFPrintHelper.h"
#include "nsPrintfCString.h"
#include "nsIFile.h"
#include "nsLocalFile.h"

using namespace mozilla::widget;
#endif

namespace mozilla {
namespace plugins {

static LazyLogModule gPPAPIJSChildLog("PPAPIJSChild");

typedef void (*FromPlugin_t)(const char*, bool, char**);
typedef char* (*ToPlugin_t)(const char*);

typedef int32_t PP_Module;
typedef const void* (*PPB_GetInterface)(const char* interface_name);
typedef const void* (*PP_GetInterface_Func)(const char* interface_name);
typedef int32_t (*PP_InitializeModule_Func)(PP_Module module,
                                            PPB_GetInterface get_browser_interface);

typedef void (*Initialize_t)(FromPlugin_t aRpcFromPlugin,
                             PP_GetInterface_Func aGetInterface,
                             PP_InitializeModule_Func aInitializeModule);

ToPlugin_t sToPlugin;

class PPAPIJSChild : public PPPAPIJSChild
{
public:
  virtual ipc::IPCResult AnswerToPlugin(const nsCString& aAPI,
                                        nsCString* aResult) override;
  virtual ipc::IPCResult AnswerSendFile(const FileDescriptor& aFile,
                                        int64_t* aFd) override;
  virtual ipc::IPCResult AnswerSendShmem(Shmem&& aMem,
                                         const uintptr_t& aCopyTo,
                                         const uintptr_t& aCopyFrom,
                                         uintptr_t* aAddress) override;
  virtual void ActorDestroy(ActorDestroyReason why) override;
  static PPAPIJSChild* sInstance;
};

PPAPIJSChild* PPAPIJSChild::sInstance;

ipc::IPCResult
PPAPIJSChild::AnswerToPlugin(const nsCString& aAPI, nsCString* aResult)
{
  MOZ_LOG(gPPAPIJSChildLog, LogLevel::Verbose, ("To plugin: %s\n", aAPI.get()));
  aResult->Assign(sToPlugin(aAPI.get()));
  if (!aResult->IsEmpty()) {
    MOZ_LOG(gPPAPIJSChildLog, LogLevel::Verbose,
            ("RPC response to plugin: %s\n", aResult->get()));
  }
  return IPC_OK();
}

ipc::IPCResult
PPAPIJSChild::AnswerSendFile(const FileDescriptor& aFile, int64_t* aFd)
{
  *aFd = aFile.ClonePlatformHandle().release();
  return IPC_OK();
}

void
PPAPIJSChild::ActorDestroy(ActorDestroyReason why)
{
  if (AbnormalShutdown == why) {
    NS_WARNING("shutting down early because of crash!");
    ipc::ProcessChild::QuickExit();
  }

  // doesn't matter why we're being destroyed; it's up to us to
  // initiate (clean) shutdown
  XRE_ShutdownChildProcess();
}

ipc::IPCResult
PPAPIJSChild::AnswerSendShmem(Shmem&& aMem,
                              const uintptr_t& aCopyTo,
                              const uintptr_t& aCopyFrom,
                              uintptr_t* aAddress)
{
  MOZ_ASSERT(aCopyTo == 0 || aCopyFrom == 0);
  void* address = aMem.get<char>();
  if (aCopyTo > 0) {
    memcpy((void*)aCopyTo, address, aMem.Size<char>());
  } else if (aCopyFrom > 0) {
    memcpy(address, (void*)aCopyFrom, aMem.Size<char>());
  }
  *aAddress = (uintptr_t)address;
  return IPC_OK();
}

class NonMainThreadFromPluginTask : public Runnable
{
public:
  explicit NonMainThreadFromPluginTask(nsDependentCString& aJSON)
    : mJSON(aJSON)
  {
  }

  NS_IMETHOD Run() override
  {
    MOZ_LOG(gPPAPIJSChildLog, LogLevel::Verbose,
            ("From plugin (task): %s\n", mJSON.get()));
    PPAPIJSChild::sInstance->SendAsyncFromPlugin(mJSON);
    return NS_OK;
  }

private:
  nsCString mJSON;
};

static void
FromPlugin(const char* aJSON, bool aAbortIfNonMainThread, char** aResult)
{
  MOZ_LOG(gPPAPIJSChildLog, LogLevel::Verbose, ("From plugin: %s\n", aJSON));

  MessageLoop* mainLoop = ipc::ProcessChild::message_loop();
  bool isMainThread = MessageLoop::current() == mainLoop;

  nsDependentCString json(aJSON);
  if (aAbortIfNonMainThread) {
    if (!isMainThread) {
      MOZ_CRASH("We didn't expect this API to be called from a different "
                "thread than the main thread!");
      return;
    }
  } else {
    if (json.Length() == 70 && Substring(json, 56, 12).EqualsLiteral("IsMainThread")) {
      MOZ_ASSERT(json.EqualsLiteral("{\"__interface\":\"PPB_Core\",\"__version\":\"1.0\",\"__method\":\"IsMainThread\"}"),
                 "Unexpected IsMainThread string in api!");
      *aResult = strdup(isMainThread ? "[1]" : "[0]");
      return;
    }

    RefPtr<Runnable> task = new NonMainThreadFromPluginTask(json);
    mainLoop->PostTask(task.forget());
    return;
  }

  MOZ_ASSERT(isMainThread);

  nsCString result;
  if (!PPAPIJSChild::sInstance->CallFromPlugin(json, &result)) {
    return;
  }
  MOZ_ASSERT(result.IsEmpty() == !aResult,
             "Expecting a result and didn't get one or vice versa.");
  if (!result.IsEmpty()) {
    *aResult = ToNewCString(result);
  }
}

class PPAPIJSPluginChild : public PPPAPIJSPluginChild
{
public:
  virtual ipc::IPCResult RecvBridged(Endpoint<PPPAPIJSChild>&& endpoint) override;

  virtual ipc::IPCResult RecvStartPrint(const uint16_t& aID,
                                        const nsString& aFilePath) override;

  virtual ipc::IPCResult RecvConvertPDFToEMF(const uint16_t& aID,
                                             const int& aPageNum,
                                             const int& aPageWidth,
                                             const int& aPageHeight) override;
  virtual ipc::IPCResult RecvFinishPrint(const uint16_t& aID) override;

  static PPAPIJSPluginChild* sInstance;
#ifdef XP_WIN
  void InitPrintHelper(PRLibrary* aPluginLibrary);
#endif

protected:
#ifdef XP_WIN
  UniquePtr<PDFViaEMFPrintHelper> mPDFPrintHelper;
  nsString mFilePath;
#endif
};

PPAPIJSPluginChild* PPAPIJSPluginChild::sInstance;

ipc::IPCResult
PPAPIJSPluginChild::RecvBridged(Endpoint<PPPAPIJSChild>&& endpoint)
{
  PPAPIJSChild::sInstance = new PPAPIJSChild();
  DebugOnly<bool> ok = endpoint.Bind(PPAPIJSChild::sInstance);
  return IPC_OK();
}

ipc::IPCResult
PPAPIJSPluginChild::RecvStartPrint(const uint16_t& aID, const nsString& aFilePath)
{
#ifdef XP_WIN
  printf_stderr("\nPPAPIJSPluginChild::RecvStartPrint\n\n");

  RefPtr<nsLocalFile> localFile = new nsLocalFile;
  localFile->InitWithPath(aFilePath);

  nsresult rv = mPDFPrintHelper->OpenDocument(localFile);
  printf_stderr("\n\n\nFarmer RecvNotifyPrint =============== 3\n\n\n");
   if (NS_FAILED(rv)) {
     return IPC_OK();
   }

  int mTotal_page_num = mPDFPrintHelper->GetPageCount();
  printf_stderr("\n\n\nFarmer RecvNotifyPrint =============== 4\n\n\n");
  if (mTotal_page_num <= 0) {
    mPDFPrintHelper = nullptr;
    return IPC_OK();
  }

  int32_t end = aFilePath.RFind("\\");
  nsString temp;
  aFilePath.Left(temp, end);
  temp.Append(NS_ConvertUTF8toUTF16("\\test.emf").get());

  int page_width = 4961, page_height = 7016;
  mPDFPrintHelper->DrawPageToFile(temp.get(), 2, page_width, page_height);
  //mPDFPrintHelper->DrawPageToFile(L"C:\\Users\\user\\AppData\\LocalLow\\Mozilla\\ChromiumPrinting2.emf", 2, page_width, page_height);
  mPDFPrintHelper->CloseDocument();
  mPDFPrintHelper = nullptr;

  Unused << SendNotifyPageCount(0, mTotal_page_num);
#endif
  return IPC_OK();
}

ipc::IPCResult
PPAPIJSPluginChild::RecvConvertPDFToEMF(const uint16_t& aID,
                                        const int& aPageNum,
                                        const int& aPageWidth,
                                        const int& aPageHeight)
{
#ifdef XP_WIN

#endif
  return IPC_OK();
}

ipc::IPCResult
PPAPIJSPluginChild::RecvFinishPrint(const uint16_t& aID)
{
#ifdef XP_WIN

#endif
  return IPC_OK();
}

#ifdef XP_WIN
void
PPAPIJSPluginChild::InitPrintHelper(PRLibrary* aPluginLibrary)
{
  mPDFPrintHelper = MakeUnique<PDFViaEMFPrintHelper>(aPluginLibrary);
}
#endif

bool
PPAPIJSProcessChild::Init(int aArgc, char* aArgv[])
{
  MOZ_LOG(gPPAPIJSChildLog, LogLevel::Verbose,
          ("PPAPIJSProcessChild::Init: %d\n", base::GetCurrentProcId()));

  nsresult rv = nsThreadManager::get().Init();
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return false;
  }

  BackgroundHangMonitor::Startup();

  MOZ_ASSERT(!PPAPIJSChild::sInstance);
  PPAPIJSPluginChild::sInstance = new PPAPIJSPluginChild();
  if (!PPAPIJSPluginChild::sInstance->Open(ipc::IOThreadChild::channel(), ParentPid(),
                                           ipc::IOThreadChild::message_loop())) {
    return false;
  }

  for (int idx = aArgc; idx > 0; --idx) {
    if (!aArgv[idx]) {
      continue;
    }

    if (!strcmp(aArgv[idx], "-rpclib")) {
      MOZ_ASSERT(!mRPCLibrary);
      if (!mRPCLibrary) {
        mRPCLibrary = PR_LoadLibrary(aArgv[idx + 1]);
      }
    } else if (!strcmp(aArgv[idx], "-pluginlib")) {
      MOZ_ASSERT(!mPluginLibrary);
      if (!mPluginLibrary) {
        mPluginLibrary = PR_LoadLibrary(aArgv[idx + 1]);
      }
    }
  }

  if (!mRPCLibrary || !mPluginLibrary) {
    return false;
  }
#ifdef XP_WIN
  PPAPIJSPluginChild::sInstance->InitPrintHelper(mPluginLibrary);
#endif
  Initialize_t Initialize = (Initialize_t)PR_FindFunctionSymbol(mRPCLibrary, "Initialize");
  sToPlugin = (ToPlugin_t)PR_FindFunctionSymbol(mRPCLibrary, "CallFromJSON");

  PP_GetInterface_Func PPP_GetInterface = (PP_GetInterface_Func)PR_FindFunctionSymbol(mPluginLibrary, "PPP_GetInterface");
  PP_InitializeModule_Func PPP_InitializeModule = (PP_InitializeModule_Func)PR_FindFunctionSymbol(mPluginLibrary, "PPP_InitializeModule");

  Initialize(FromPlugin, PPP_GetInterface, PPP_InitializeModule);
printf_stderr("Farmer PPAPIJSProcessChild::Init -4\n");
  return true;
}

void
PPAPIJSProcessChild::CleanUp()
{
  MOZ_LOG(gPPAPIJSChildLog, LogLevel::Verbose,
          ("PPAPIJSProcessChild::CleanUp: %d\n", base::GetCurrentProcId()));

  BackgroundHangMonitor::Shutdown();

  delete PPAPIJSPluginChild::sInstance;
  PPAPIJSPluginChild::sInstance = nullptr;
}

} // namespace plugins
} // namespace mozilla
