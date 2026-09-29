//------------------------------------------------------------------------
// Controller tests: skin selection state (incl. migration from older
// versions) and the parameter text round trip the readouts rely on.
//------------------------------------------------------------------------

#include "../source/JSIF_controller.h"
#include "../source/JSIF_cids.h"
#include "../source/JSIF_ui.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ustring.h"
#include "public.sdk/source/common/memorystream.h"

#include <cmath>
#include <cstdio>
#include <string>

// normally defined by the SDK's bundle entry (macmain.cpp), which a test executable doesn't use
void* moduleHandle = nullptr;

using namespace Steinberg;
using namespace yg331;

static int failures = 0;
static int checks = 0;

#define CHECK(cond)                                                              \
	do                                                                           \
	{                                                                            \
		++checks;                                                                \
		if (!(cond))                                                             \
		{                                                                        \
			++failures;                                                          \
			fprintf (stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);     \
		}                                                                        \
	} while (0)

static bool near (double a, double b) { return std::fabs (a - b) < 1e-6; }

struct Controller
{
	JSIF_Controller* c = new JSIF_Controller;
	Controller () { c->initialize (nullptr); }
	~Controller ()
	{
		c->terminate ();
		c->release ();
	}
	JSIF_Controller* operator-> () { return c; }
};

// controller state as written by v2.0.x: 11 fields, GUI as 2-entry normalized value
static IPtr<MemoryStream> legacyState (double gui)
{
	auto stream = owned (new MemoryStream);
	IBStreamer s (stream, kLittleEndian);
	s.writeDouble (0.5);  // input
	s.writeDouble (0.25); // effect
	s.writeDouble (0.5);  // curve
	s.writeDouble (1.0);  // output
	s.writeDouble (0.0);  // OS
	s.writeInt32 (0);     // clip
	s.writeInt32 (1);     // in
	s.writeInt32 (0);     // split
	s.writeDouble (0.0);  // zoom
	s.writeDouble (0.0);  // phase
	s.writeDouble (gui);
	stream->seek (0, IBStream::kIBSeekSet, nullptr);
	return stream;
}

static void testSkinState ()
{
	{
		Controller fresh;
		CHECK (near (fresh->getParamNormalized (kGuiSwitch), 1.0)); // Modern by default
	}
	{
		Controller c;
		CHECK (c->setState (legacyState (1.0)) == kResultTrue);
		CHECK (near (c->getParamNormalized (kGuiSwitch), 0.5)); // old Twarch stays Twarch
		CHECK (near (c->getParamNormalized (kParamEffect), 0.25));
	}
	{
		Controller c;
		c->setState (legacyState (0.0));
		CHECK (near (c->getParamNormalized (kGuiSwitch), 0.0)); // old Original stays Original
	}

	for (int skin = 0; skin < kNumGuiSkins; ++skin)
	{
		auto stream = owned (new MemoryStream);
		{
			Controller a;
			a->setParamNormalized (kGuiSwitch, skin / double (kNumGuiSkins - 1));
			CHECK (a->getState (stream) == kResultTrue);
		}
		// older versions read the 11th field as Original (0) / Twarch (1)
		stream->seek (8 * 5 + 4 * 3 + 8 * 2, IBStream::kIBSeekSet, nullptr);
		double legacy = -1.;
		IBStreamer (stream, kLittleEndian).readDouble (legacy);
		CHECK (near (legacy, skin == kGuiTwarch ? 1.0 : 0.0));

		stream->seek (0, IBStream::kIBSeekSet, nullptr);
		Controller b;
		CHECK (b->setState (stream) == kResultTrue);
		CHECK (near (b->getParamNormalized (kGuiSwitch), skin / double (kNumGuiSkins - 1)));
	}

	CHECK (std::string (guiTemplateName (kGuiOriginal)) == "Original");
	CHECK (std::string (guiTemplateName (kGuiTwarch)) == "Twarch");
	CHECK (std::string (guiTemplateName (kGuiModern)) == "Modern");
}

static std::string paramString (JSIF_Controller* c, Vst::ParamID id, double norm)
{
	Vst::String128 str {};
	c->getParamStringByValue (id, norm, str);
	char utf8[128] {};
	UString (str, 128).toAscii (utf8, 128);
	return utf8;
}

static double parseParam (JSIF_Controller* c, Vst::ParamID id, const char* text)
{
	Vst::String128 str {};
	UString (str, 128).fromAscii (text);
	Vst::ParamValue norm = -1.;
	c->getParamValueByString (id, str, norm);
	return norm;
}

static void testReadouts ()
{
	Controller c;
	using VSTGUI::ModernValueEdit;
	// what the Modern readouts show for the host's parameter strings
	CHECK (ModernValueEdit::formatValue (paramString (c.c, kParamInput, 14. / 24.).c_str (), 1, "dB") == "2.0 dB");
	CHECK (ModernValueEdit::formatValue (paramString (c.c, kParamInput, 0.5).c_str (), 1, "dB") == "0.0 dB");
	CHECK (ModernValueEdit::formatValue (paramString (c.c, kParamOutput, 11. / 12.).c_str (), 1, "dB") == "-1.0 dB");
	CHECK (ModernValueEdit::formatValue (paramString (c.c, kParamEffect, 0.5).c_str (), 1, "%") == "50.0 %");
	CHECK (ModernValueEdit::formatValue (paramString (c.c, kParamCurve, 0.75).c_str (), 1, "") == "25.0");

	// typing into the Effect and Curve readouts
	CHECK (near (parseParam (c.c, kParamEffect, "50"), 0.5));
	CHECK (near (parseParam (c.c, kParamEffect, "37.5 %"), 0.375));
	CHECK (near (parseParam (c.c, kParamCurve, "25"), 0.75));
	CHECK (near (parseParam (c.c, kParamCurve, "-50"), 0.0));
	CHECK (near (parseParam (c.c, kParamInput, "0"), 0.5));
	CHECK (near (parseParam (c.c, kParamOutput, "-6 dB"), 0.5));
}

int main ()
{
	testSkinState ();
	testReadouts ();
	printf ("%d checks, %d failures\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
