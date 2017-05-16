/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef PDFVIAEMFPRINTHELPER_H_
#define PDFVIAEMFPRINTHELPER_H_

#include "nsCOMPtr.h"
#include "PDFiumEngineShim.h"
#include "mozilla/Vector.h"

/* include windows.h for the HDC definitions that we need. */
#include <windows.h>

class nsIFile;
class nsFileInputStream;

namespace mozilla {
namespace widget {

/**
 * This class helps draw a PDF file to a given Windows DC.
 * To do that it first converts the PDF file to EMF.
 * Windows EMF:
 * https://msdn.microsoft.com/en-us/windows/hardware/drivers/print/emf-data-type
 */
class PDFViaEMFPrintHelper
{
public:
  PDFViaEMFPrintHelper(PRLibrary* aPDFiumLibrary);
  ~PDFViaEMFPrintHelper();

  /** Loads the specified PDF file. */
  NS_IMETHOD OpenDocument(nsIFile *aFile, uint16_t aID);

  /** Releases document buffer. */
  void CloseDocument(uint16_t aID);

  int GetPageCount(uint16_t aID);

  /** Convert specified PDF page to EMF and draw the EMF onto the given DC. */
  bool DrawPage(uint16_t aID, HDC aPrinterDC, unsigned int aPageIndex,
                int aPageWidth, int aPageHeight, float& aScaleFactor);

  /** Convert specified PDF page to EMF and save it to file. */
  bool DrawPageToFile(uint16_t aID, const wchar_t* aFilePath,
                      unsigned int aPageIndex, int aPageWidth, int aPageHeight,
                      float& aScaleFactor);

private:

  bool RenderPageToDC(uint16_t aID, HDC aDC, unsigned int aPageIndex,
                      int aPageWidth, int aPageHeight, float& aScaleFactor);

  UniquePtr<PDFiumEngineShim> mPDFiumEngine;
  PRLibrary*                  mPDFiumLibrary;
  std::map<uint16_t, FPDF_DOCUMENT> mIDHandlerMap;
};

} // namespace widget
} // namespace mozilla

#endif /* PDFVIAEMFPRINTHELPER_H_ */