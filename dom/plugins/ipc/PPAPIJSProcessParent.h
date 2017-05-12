/* -*- Mode: C++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 8 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef dom_plugins_PPAPIJSProcessParent_h
#define dom_plugins_PPAPIJSProcessParent_h 1

#include "mozilla/ipc/GeckoChildProcessHost.h"
#include "mozilla/plugins/PPPAPIJSParent.h"
#include "nsIObserver.h"
#include "nsIPPAPIJSProcess.h"
#include "mozilla/plugins/PPPAPIJSPluginParent.h"

#ifdef XP_WIN
class nsDeviceContextSpecWin;
#endif

namespace mozilla {
namespace plugins {

class PPAPIJSParent;

class PPAPIJSPluginParent : public PPPAPIJSPluginParent
{
public:
  explicit PPAPIJSPluginParent(uint32_t aJSPluginID,
                               ipc::GeckoChildProcessHost* aProcess)
    : mJSPluginID(aJSPluginID),
      mProcess(aProcess)
  { }
  void Delete();
  virtual void ActorDestroy(ActorDestroyReason aWhy) override;
  virtual void DeallocPPPAPIJSPluginParent() override;

  virtual ipc::IPCResult RecvNotifyPageCount(const uint16_t& aID,
                                             const int& aPageCount) override;
  virtual ipc::IPCResult RecvPrintEMF(const uint16_t& aID,
                                      const nsString& aFilePath,
                                      const int& aPageNum) override;
#ifdef XP_WIN
  int16_t SetDeviceContextSpecWin(nsDeviceContextSpecWin* aDeviceContextSpec);
#endif

private:
  uint32_t mJSPluginID;
  nsAutoPtr<ipc::GeckoChildProcessHost> mProcess;
  nsDeviceContextSpecWin* mPDFDeviceContextSpec;
};

class PPAPIJSProcess : public nsIPPAPIJSProcess,
                       public nsIObserver
{
public:
  PPAPIJSProcess();

  NS_DECL_ISUPPORTS
  NS_DECL_NSIPPAPIJSPROCESS
  NS_DECL_NSIOBSERVER

  static nsresult SetupBridge(dom::PContentParent* aContentParent,
                              uint32_t aJSPluginID,
                              ipc::Endpoint<PPPAPIJSParent>* aParentEndpoint);
  static PPAPIJSPluginParent* GetPPAPIJSPluginParent(uint32_t aJSPluginID);

private:
  virtual ~PPAPIJSProcess();
  PPAPIJSParent* mParent;
};

} // namespace plugins
} // namespace mozilla

#endif // ifndef dom_plugins_PPAPIJSProcessParent_h
