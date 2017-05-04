/* -*- Mode: C++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 8 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef dom_plugins_PPAPIJSProcessChild_h
#define dom_plugins_PPAPIJSProcessChild_h 1

#include "mozilla/ipc/ProcessChild.h"
#include "mozilla/plugins/PPPAPIJSChild.h"

namespace mozilla {
namespace plugins {

class PPAPIJSProcessChild : public mozilla::ipc::ProcessChild
{
public:
  explicit PPAPIJSProcessChild(ProcessId aParentPid)
    : ProcessChild(aParentPid)
    , mRPCLibrary(nullptr)
    , mPluginLibrary(nullptr)
  { }

  virtual bool Init(int aArgc, char* aArgv[]) override;
  virtual void CleanUp() override;

private:
  PRLibrary* mRPCLibrary;
  PRLibrary* mPluginLibrary;
};

} // namespace plugins
} // namespace mozilla

#endif // ifndef dom_plugins_PPAPIJSProcessChild_h
