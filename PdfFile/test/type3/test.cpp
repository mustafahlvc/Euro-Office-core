/*
 * SPDX-FileCopyrightText: 2026 Euro-Office contributors
 * SPDX-License-Identifier: AGPL-3.0-only
 */

// The PDF viewer selects and searches the text layer that CDrawingFile::GetGlyphs
// builds by drawing a page on CHTMLRendererText. Text in a Type 3 font used to be
// missing from it. Each case builds a page in memory with "HELLO" in a Type 3 font
// (filled rectangle glyphs, Unicode only in /ToUnicode, as Chrome writes them),
// draws it the way GetGlyphs does and decodes the resulting text layer.

#include "gtest/gtest.h"

#include <cstring>
#include <string>
#include <vector>

#include "../../PdfFile.h"
#include "../../../DesktopEditor/graphics/pro/Fonts.h"
#include "../../../DesktopEditor/graphics/pro/js/wasm/src/HTMLRendererText.h"

namespace
{
	const double c_dFontSize = 18;
	const wchar_t* c_wsText = L"HELLO";
	const double c_arrAdvances[] = {650, 650, 600, 600, 700}; // glyph space, 1000 per em

	std::string Stream(const std::string& sBody)
	{
		return "<< /Length " + std::to_string(sBody.size()) + " >>\nstream\n" + sBody + "\nendstream";
	}

	// One page, 400 x 200 pt; dScale wraps the content in "dScale 0 0 dScale 0 0 cm"
	// (Chrome maps CSS px to pt this way) while "HELLO" stays at (40, 80) pt on the page
	std::string MakeType3Pdf(double dScale)
	{
		char sContent[256];
		snprintf(sContent, sizeof(sContent), "%g 0 0 %g 0 0 cm\nBT /T3 %g Tf %g %g Td (HELLO) Tj ET",
				 dScale, dScale, c_dFontSize, 40 / dScale, 80 / dScale);

		std::vector<std::string> arrObjects = {
			"<< /Type /Catalog /Pages 2 0 R >>",
			"<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
			"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 400 200] /Contents 4 0 R"
			" /Resources << /Font << /T3 5 0 R >> >> >>",
			Stream(sContent),
			"<< /Type /Font /Subtype /Type3 /FontBBox [0 0 700 700] /FontMatrix [0.001 0 0 0.001 0 0]"
			" /CharProcs << /gE 6 0 R /gH 7 0 R /gL 8 0 R /gO 9 0 R >>"
			" /Encoding << /Type /Encoding /Differences [69 /gE 72 /gH 76 /gL 79 /gO] >>"
			" /FirstChar 69 /LastChar 79 /Widths [650 0 0 650 0 0 0 600 0 0 700]"
			" /ToUnicode 10 0 R /Resources << >> >>",
			Stream("650 0 50 0 600 700 d1\n50 0 100 700 re 150 600 450 100 re 150 300 350 100 re 150 0 450 100 re f"),
			Stream("650 0 50 0 600 700 d1\n50 0 100 700 re 500 0 100 700 re 150 300 350 100 re f"),
			Stream("600 0 50 0 550 700 d1\n50 0 100 700 re 150 0 400 100 re f"),
			Stream("700 0 50 0 650 700 d1\n50 0 100 700 re 550 0 100 700 re 150 600 400 100 re 150 0 400 100 re f"),
			Stream("/CIDInit /ProcSet findresource begin 12 dict begin begincmap\n"
				   "/CIDSystemInfo << /Registry (Adobe) /Ordering (UCS) /Supplement 0 >> def\n"
				   "/CMapName /Adobe-Identity-UCS def /CMapType 2 def\n"
				   "1 begincodespacerange <00> <FF> endcodespacerange\n"
				   "4 beginbfchar <45> <0045> <48> <0048> <4C> <004C> <4F> <004F> endbfchar\n"
				   "endcmap CMapName currentdict /CMap defineresource pop end end")
		};

		std::string sPdf = "%PDF-1.4\n";
		std::vector<size_t> arrOffsets;
		for (size_t i = 0; i < arrObjects.size(); ++i)
		{
			arrOffsets.push_back(sPdf.size());
			sPdf += std::to_string(i + 1) + " 0 obj\n" + arrObjects[i] + "\nendobj\n";
		}
		size_t nXref = sPdf.size();
		sPdf += "xref\n0 " + std::to_string(arrObjects.size() + 1) + "\n0000000000 65535 f \n";
		for (size_t nOffset : arrOffsets)
		{
			char sEntry[32];
			snprintf(sEntry, sizeof(sEntry), "%010zu 00000 n \n", nOffset);
			sPdf += sEntry;
		}
		sPdf += "trailer\n<< /Size " + std::to_string(arrObjects.size() + 1) + " /Root 1 0 R >>\nstartxref\n" +
				std::to_string(nXref) + "\n%%EOF\n";
		return sPdf;
	}

	struct TTextChar
	{
		int nUnicode;
		double dOffset; // from the previous char, mm
		double dWidth;  // mm
	};
	struct TTextLayer
	{
		int nSymbols = 0;
		std::vector<std::vector<TTextChar>> arrLines;
	};

	// Decodes the buffer of CHTMLRendererText::GetBuffer (see CHText::DumpLine)
	TTextLayer Decode(const BYTE* pBuffer)
	{
		TTextLayer oLayer;
		if (!pBuffer)
			return oLayer;

		size_t nPos = 0;
		auto Int = [&]() { int n; memcpy(&n, pBuffer + nPos, 4); nPos += 4; return n; };
		auto Double = [&]() { return Int() / 10000.0; };

		int nLen = Int();
		Int(); Int(); // paragraphs, words
		oLayer.nSymbols = Int();
		Int(); // spaces
		while ((int)nPos < nLen)
		{
			Double(); Double();            // x, y
			if (pBuffer[nPos++])           // not horizontal
				{ Double(); Double(); }    // direction
			Double(); Double(); Int();     // ascent, descent, line width
			int nCount = Int();
			std::vector<TTextChar> arrChars;
			for (int i = 0; i < nCount; ++i)
			{
				TTextChar oChar;
				oChar.dOffset  = i ? Double() : 0;
				oChar.nUnicode = Int();
				oChar.dWidth   = Double();
				arrChars.push_back(oChar);
			}
			oLayer.arrLines.push_back(arrChars);
		}
		return oLayer;
	}

	double PtToMM(double dPt) { return dPt * 25.4 / 72.0; }
}

class CPdfType3TextTest : public testing::Test
{
protected:
	static NSFonts::IApplicationFonts* m_pFonts;

	static void SetUpTestSuite()
	{
		// Type 3 glyphs live in the PDF, no font files are needed
		m_pFonts = NSFonts::NSApplication::Create();
		std::vector<std::wstring> arFiles;
		m_pFonts->InitializeFromArrayFiles(arFiles);
	}
	static void TearDownTestSuite()
	{
		RELEASEINTERFACE(m_pFonts);
	}

	// Draws page 0 the way CDrawingFile::GetGlyphs does
	TTextLayer GetTextLayer(const std::string& sPdf)
	{
		CPdfFile oPdf(m_pFonts);
		EXPECT_TRUE(oPdf.LoadFromMemory((BYTE*)sPdf.data(), (DWORD)sPdf.size()));

		NSHtmlRenderer::CHTMLRendererText oText;
		oText.Init(&oPdf, 8);
		oPdf.DrawPageOnRenderer(&oText, 0, NULL);
		return Decode(oText.GetBuffer());
	}
};
NSFonts::IApplicationFonts* CPdfType3TextTest::m_pFonts = NULL;

TEST_F(CPdfType3TextTest, TextLayerHasType3Text)
{
	TTextLayer oLayer = GetTextLayer(MakeType3Pdf(1));

	std::wstring wsText;
	for (const auto& arrLine : oLayer.arrLines)
		for (const TTextChar& oChar : arrLine)
			wsText += (wchar_t)oChar.nUnicode;

	EXPECT_EQ(std::wstring(c_wsText), wsText);
	EXPECT_EQ(5, oLayer.nSymbols);
	EXPECT_EQ(1u, oLayer.arrLines.size());
}

// Selection boxes come from these widths: each must span the glyph's advance on the
// page, also when the content stream scales its coordinates
TEST_F(CPdfType3TextTest, WidthsAreAdvancesOnThePage)
{
	for (double dScale : {1.0, 0.75})
	{
		SCOPED_TRACE("cm scale " + std::to_string(dScale));
		TTextLayer oLayer = GetTextLayer(MakeType3Pdf(dScale));
		ASSERT_EQ(1u, oLayer.arrLines.size());
		const std::vector<TTextChar>& arrChars = oLayer.arrLines[0];
		ASSERT_EQ(5u, arrChars.size());

		for (size_t i = 0; i < arrChars.size(); ++i)
		{
			SCOPED_TRACE("char " + std::to_string(i));
			// the font size is in the scaled units, the advance on the page is not
			double dAdvance = PtToMM(c_arrAdvances[i] / 1000 * c_dFontSize * dScale);
			EXPECT_NEAR(dAdvance, arrChars[i].dWidth, 1e-3);
			if (i)
				EXPECT_NEAR(arrChars[i - 1].dWidth, arrChars[i].dOffset, 1e-3);
		}
	}
}

// Ordinary rendering still runs the glyph procedures
TEST_F(CPdfType3TextTest, GlyphsStillRender)
{
	std::string sPdf = MakeType3Pdf(0.75);
	CPdfFile oPdf(m_pFonts);
	ASSERT_TRUE(oPdf.LoadFromMemory((BYTE*)sPdf.data(), (DWORD)sPdf.size()));

	// 2 px per pt; "HELLO" is 43.2 x 9.45 pt at (40, 80) pt from the bottom left, so
	// columns 80..166 and top-down rows 221..240, with about 590 px of glyph rectangles
	const int nW = 800, nH = 400;
	BYTE* pPixels = oPdf.ConvertToPixels(0, nW, nH, true);
	ASSERT_NE(nullptr, pPixels);

	int nDark = 0;
	for (int y = 210; y < 250; ++y)
		for (int x = 70; x < 180; ++x)
		{
			const BYTE* p = pPixels + (y * nW + x) * 4;
			nDark += (p[0] + p[1] + p[2]) < 3 * 128 ? 1 : 0;
		}
	delete[] pPixels;
	EXPECT_GT(nDark, 300);
}
