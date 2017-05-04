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
  static PPAPIJSPluginChild* sInstance;
};

PPAPIJSPluginChild* PPAPIJSPluginChild::sInstance;

ipc::IPCResult
PPAPIJSPluginChild::RecvBridged(Endpoint<PPPAPIJSChild>&& endpoint)
{
  PPAPIJSChild::sInstance = new PPAPIJSChild();
  DebugOnly<bool> ok = endpoint.Bind(PPAPIJSChild::sInstance);
  return IPC_OK();
}

bool
PPAPIJSProcessChild::Init(int aArgc, char* aArgv[])
{
  MOZ_LOG(gPPAPIJSChildLog, LogLevel::Verbose,
          ("PPAPIJSProcessChild::Init: %d\n", base::GetCurrentProcId()));

  nsresult rv = nsThreadManager::get().Init();
  if (NS_WARN_IF(NS_FAILED(rv))) {
    printf_stderr("Farmer PPAPIJSProcessChild::Init -1\n");
    return false;
  }

  BackgroundHangMonitor::Startup();

  MOZ_ASSERT(!PPAPIJSChild::sInstance);
  PPAPIJSPluginChild::sInstance = new PPAPIJSPluginChild();
  if (!PPAPIJSPluginChild::sInstance->Open(ipc::IOThreadChild::channel(), ParentPid(),
                                           ipc::IOThreadChild::message_loop())) {
    printf_stderr("Farmer PPAPIJSProcessChild::Init -2\n");
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
    printf_stderr("Farmer PPAPIJSProcessChild::Init -3\n");
    return false;
  }

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
