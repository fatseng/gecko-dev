/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Global PPP functions --------------------------------------------------------

#include "ppapi/c/ppp.h"
#include "pdf/pdf.h"

PP_EXPORT int32_t PPP_InitializeModule(PP_Module module_id,
                                       PPB_GetInterface get_browser_interface) {
    return chrome_pdf::PPP_InitializeModule(module_id, get_browser_interface);
}

PP_EXPORT void PPP_ShutdownModule() {
    chrome_pdf::PPP_ShutdownModule();
}

PP_EXPORT const void* PPP_GetInterface(const char* interface_name) {
    return chrome_pdf::PPP_GetInterface(interface_name);
}
