/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/ModuleUtils.h"
#include "PPAPIJSProcessParent.h"
#include "nsPluginHost.h"
#include "nsPluginsCID.h"

using mozilla::plugins::PPAPIJSProcess;

NS_GENERIC_FACTORY_SINGLETON_CONSTRUCTOR(nsPluginHost, nsPluginHost::GetInst)
NS_GENERIC_FACTORY_CONSTRUCTOR(PPAPIJSProcess)
NS_DEFINE_NAMED_CID(NS_PLUGIN_HOST_CID);
#define PPAPIJS_PROCESS_CID \
  { 0x38c0af25, 0xa109, 0x4a4a, \
    { 0xa8, 0x7b, 0x66, 0x7a, 0x8c, 0x5a, 0xc8, 0x7a } }
NS_DEFINE_NAMED_CID(PPAPIJS_PROCESS_CID);

static const mozilla::Module::CIDEntry kPluginCIDs[] = {
  { &kNS_PLUGIN_HOST_CID, false, nullptr, nsPluginHostConstructor },
  { &kPPAPIJS_PROCESS_CID, true, nullptr, PPAPIJSProcessConstructor },
  { nullptr }
};

static const mozilla::Module::ContractIDEntry kPluginContracts[] = {
  { MOZ_PLUGIN_HOST_CONTRACTID, &kNS_PLUGIN_HOST_CID },
  { MOZ_PPAPIJS_PROCESS_CONTRACTID, &kPPAPIJS_PROCESS_CID },
  { nullptr }
};

static const mozilla::Module kPluginModule = {
  mozilla::Module::kVersion,
  kPluginCIDs,
  kPluginContracts
};

NSMODULE_DEFN(nsPluginModule) = &kPluginModule;
