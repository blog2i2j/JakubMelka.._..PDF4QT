//    Copyright (C) 2020 Jakub Melka
//
//    This file is part of PdfForQt.
//
//    PdfForQt is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    PdfForQt is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with PDFForQt.  If not, see <https://www.gnu.org/licenses/>.

#include "pdftoolfetchimages.h"
#include "pdfpagecontentprocessor.h"
#include "pdfconstants.h"
#include "pdfexecutionpolicy.h"

namespace pdftool
{

static PDFToolFetchImages s_fetchImagesApplication;

class PDFImageContentExtractorProcessor : public pdf::PDFPageContentProcessor
{
    using BaseClass = PDFPageContentProcessor;

public:
    explicit PDFImageContentExtractorProcessor(const pdf::PDFPage* page,
                                               const pdf::PDFDocument* document,
                                               const pdf::PDFFontCache* fontCache,
                                               const pdf::PDFCMS* cms,
                                               const pdf::PDFOptionalContentActivity* optionalContentActivity,
                                               QMatrix pagePointToDevicePointMatrix,
                                               const pdf::PDFMeshQualitySettings& meshQualitySettings,
                                               pdf::PDFInteger pageIndex,
                                               PDFToolFetchImages* tool) :
        BaseClass(page, document, fontCache, cms, optionalContentActivity, pagePointToDevicePointMatrix, meshQualitySettings),
        m_pageIndex(pageIndex),
        m_tool(tool)
    {

    }

protected:
    virtual bool isContentSuppressedByOC(pdf::PDFObjectReference ocgOrOcmd) override;
    virtual bool isContentKindSuppressed(ContentKind kind) const override;
    virtual void performImagePainting(const QImage& image) override;

private:
    pdf::PDFInteger m_pageIndex;
    PDFToolFetchImages* m_tool;
};

bool PDFImageContentExtractorProcessor::isContentSuppressedByOC(pdf::PDFObjectReference ocgOrOcmd)
{
    Q_UNUSED(ocgOrOcmd);
    return false;
}

bool PDFImageContentExtractorProcessor::isContentKindSuppressed(ContentKind kind) const
{
    switch (kind)
    {
        case ContentKind::Shapes:
        case ContentKind::Text:
        case ContentKind::Shading:
            return true;

        case ContentKind::Tiling:
        case ContentKind::Images:
            return false; // Tiling can have images

        default:
        {
            Q_ASSERT(false);
            break;
        }
    }

    return false;
}


void PDFImageContentExtractorProcessor::performImagePainting(const QImage& image)
{
    m_tool->onImageExtracted(m_pageIndex, image);
}

QString PDFToolFetchImages::getStandardString(PDFToolAbstractApplication::StandardString standardString) const
{
    switch (standardString)
    {
        case Command:
            return "fetch-images";

        case Name:
            return PDFToolTranslationContext::tr("Fetch images");

        case Description:
            return PDFToolTranslationContext::tr("Fetch image content from document.");

        default:
            Q_ASSERT(false);
            break;
    }

    return QString();
}

int PDFToolFetchImages::execute(const PDFToolOptions& options)
{
    pdf::PDFDocument document;
    QByteArray sourceData;
    if (!readDocument(options, document, &sourceData))
    {
        return ErrorDocumentReading;
    }

    if (!document.getStorage().getSecurityHandler()->isAllowed(pdf::PDFSecurityHandler::Permission::CopyContent))
    {
        PDFConsole::writeError(PDFToolTranslationContext::tr("Document doesn't allow to copy content."), options.outputCodec);
        return ErrorPermissions;
    }

    QString parseError;
    std::vector<pdf::PDFInteger> pageIndices = options.getPageRange(document.getCatalog()->getPageCount(), parseError, true);

    if (!parseError.isEmpty())
    {
        PDFConsole::writeError(parseError, options.outputCodec);
        return ErrorInvalidArguments;
    }

    QString errorMessage;
    Options optionFlags = getOptionsFlags();
    if (!options.imageExportSettings.validate(&errorMessage, false, optionFlags.testFlag(ImageExportSettingsFiles), optionFlags.testFlag(ImageExportSettingsResolution)))
    {
        PDFConsole::writeError(errorMessage, options.outputCodec);
        return ErrorInvalidArguments;
    }

    // We are ready to render the document
    pdf::PDFOptionalContentActivity optionalContentActivity(&document, pdf::OCUsage::Export, nullptr);
    pdf::PDFCMSManager cmsManager(nullptr);
    cmsManager.setSettings(options.cmsSettings);
    pdf::PDFCMSPointer cms = cmsManager.getCurrentCMS();
    pdf::PDFMeshQualitySettings meshQualitySettings;
    pdf::PDFFontCache fontCache(pdf::DEFAULT_FONT_CACHE_LIMIT, pdf::DEFAULT_REALIZED_FONT_CACHE_LIMIT);
    pdf::PDFModifiedDocument md(&document, &optionalContentActivity);
    fontCache.setDocument(md);
    fontCache.setCacheShrinkEnabled(nullptr, false);

    auto processPageContents = [&, this](pdf::PDFInteger pageIndex)
    {
        const pdf::PDFCatalog* catalog = document.getCatalog();
        if (!catalog->getPage(pageIndex))
        {
            // Invalid page index
            return;
        }

        const pdf::PDFPage* page = catalog->getPage(pageIndex);
        Q_ASSERT(page);

        PDFImageContentExtractorProcessor processor(page, &document, &fontCache, cms.data(), &optionalContentActivity,
                                                    QMatrix(), meshQualitySettings, pageIndex, this);
        processor.processContents();
    };

    pdf::PDFExecutionPolicy::execute(pdf::PDFExecutionPolicy::Scope::Page, pageIndices.begin(), pageIndices.end(), processPageContents);
    fontCache.setCacheShrinkEnabled(nullptr, true);

    return ExitSuccess;
}

PDFToolAbstractApplication::Options PDFToolFetchImages::getOptionsFlags() const
{
    return ConsoleFormat | OpenDocument | PageSelector | ImageWriterSettings | ImageExportSettingsFiles | ColorManagementSystem;
}

void PDFToolFetchImages::onImageExtracted(pdf::PDFInteger pageIndex, const QImage& image)
{

}

}   // namespace pdftool
