//------------------------------------------------------------------------
// Unit tests for the Modern editor views (fader detent, meter, readouts).
//------------------------------------------------------------------------

#include "../source/JSIF_ui.h"

#include <cmath>
#include <cstdio>

using namespace VSTGUI;

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

static bool near (double a, double b, double eps = 1e-5) { return std::fabs (a - b) < eps; }

static void testFaderDetent ()
{
	// 120 x 792 view, cap 116 high -> travel 676
	auto fader = makeOwned<ModernFader> (CRect (0, 0, 120, 792));
	CHECK (near (fader->getTravel (), 676.));

	// Input fader: -12..+12 dB, 0 dB = 0.5
	fader->setDetent (0.5f);
	const double d = 0.5 * 676.;
	const double zone = fader->detentZone;

	// the whole dead zone maps to the detent value
	CHECK (near (fader->dragToValue (d), 0.5));
	CHECK (near (fader->dragToValue (d + zone), 0.5));
	CHECK (near (fader->dragToValue (d + 2. * zone), 0.5));

	// just outside the dead zone the value continues without a jump
	CHECK (near (fader->dragToValue (d - 1.), 0.5 - 1. / 676.));
	CHECK (near (fader->dragToValue (d + 2. * zone + 1.), 0.5 + 1. / 676.));

	// ends of travel
	CHECK (near (fader->dragToValue (0.), 0.));
	CHECK (near (fader->dragToValue (676. + 2. * zone), 1.));
	CHECK (near (fader->dragToValue (-50.), 0.));
	CHECK (near (fader->dragToValue (5000.), 1.));

	// valueToDrag and dragToValue round-trip
	for (float v : {0.f, 0.1f, 0.25f, 0.49f, 0.5f, 0.51f, 0.75f, 1.f})
		CHECK (near (fader->dragToValue (fader->valueToDrag (v)), v, 1e-4));

	// the detent sits in the middle of its dead zone
	CHECK (near (fader->valueToDrag (0.5f), d + zone));

	// wheel / arrow steps stop on the detent when crossing it
	CHECK (near (fader->stepValue (0.495f, 0.01f), 0.5));
	CHECK (near (fader->stepValue (0.505f, -0.01f), 0.5));
	CHECK (near (fader->stepValue (0.5f, 0.01f), 0.51));
	CHECK (near (fader->stepValue (0.5f, -0.01f), 0.49));
	CHECK (near (fader->stepValue (0.2f, 0.01f), 0.21));
	CHECK (near (fader->stepValue (0.995f, 0.01f), 1.));

	fader->setValueNormalized (0.5f);
	CHECK (fader->isAtDetent ());
	fader->setValueNormalized (0.5001f);
	CHECK (!fader->isAtDetent ());

	// Output fader: -12..0 dB, 0 dB = 1.0 (detent at the top end)
	fader->setDetent (1.f);
	CHECK (near (fader->dragToValue (676.), 1.));
	CHECK (near (fader->dragToValue (676. + zone), 1.));
	CHECK (near (fader->dragToValue (675.), 675. / 676.));
	CHECK (near (fader->stepValue (0.995f, 0.01f), 1.));

	// no detent: plain linear mapping
	fader->setDetent (-1.f);
	CHECK (!fader->hasDetent ());
	CHECK (near (fader->dragToValue (338.), 0.5));
	CHECK (near (fader->stepValue (0.495f, 0.01f), 0.505));
}

static void testFaderMagnet ()
{
	// Effect: 0..100 %, magnetic whole percents, small detent at 50 %
	auto fader = makeOwned<ModernFader> (CRect (0, 0, 120, 792));
	fader->snapStep = 0.01f;
	fader->snapStrength = 0.2f;

	// within 0.2 % of a whole number snaps onto it
	CHECK (near (fader->applyMagnet (0.3712f), 0.37));
	CHECK (near (fader->applyMagnet (0.3688f), 0.37));
	CHECK (near (fader->applyMagnet (0.9990f), 1.0));
	CHECK (near (fader->applyMagnet (0.0015f), 0.0));
	// decimals away from whole numbers stay reachable
	CHECK (near (fader->applyMagnet (0.3750f), 0.375));
	CHECK (near (fader->applyMagnet (0.3725f), 0.3725));
	CHECK (near (fader->applyMagnet (0.3675f), 0.3675));

	// the 50 % detent still wins inside its dead zone
	fader->setDetent (0.5f);
	fader->detentZone = 10.;
	const double d = 0.5 * fader->getTravel ();
	CHECK (near (fader->dragToValue (d + 10.), 0.5));
	CHECK (near (fader->applyMagnet (0.5f), 0.5));
	CHECK (near (fader->applyMagnet (0.5012f), 0.5));

	// no snap step: values pass through unchanged
	fader->snapStep = 0.f;
	CHECK (near (fader->applyMagnet (0.3712f), 0.3712));
}

static void testMeter ()
{
	auto meter = makeOwned<ModernMeter> (CRect (0, 0, 26, 744), 25);
	// the processor quantizes to multiples of 0.04 (VuPPMconvert)
	const float steps[] = {0.f, 0.04f, 0.40f, 0.72f, 0.76f, 0.96f, 1.f};
	const int expected[] = {0, 1, 10, 18, 19, 24, 25};
	for (int i = 0; i < 7; ++i)
	{
		meter->setValueNormalized (steps[i]);
		CHECK (meter->getLitSegments () == expected[i]);
	}
	// zone colours: < -12 dB purple, -12..0 blue, >= 0 dB red
	CHECK (meter->segmentColor (9) == meter->lowColor);
	CHECK (meter->segmentColor (10) == meter->midColor);
	CHECK (meter->segmentColor (18) == meter->midColor);
	CHECK (meter->segmentColor (19) == meter->highColor);
	CHECK (meter->segmentColor (25) == meter->highColor);
}

static void testValueFormat ()
{
	CHECK (ModernValueEdit::formatValue ("2.00", 1, "dB") == "2.0 dB");
	CHECK (ModernValueEdit::formatValue ("-1.00", 1, "dB") == "-1.0 dB");
	CHECK (ModernValueEdit::formatValue ("-0.01", 1, "dB") == "0.0 dB");
	CHECK (ModernValueEdit::formatValue ("50", 1, "%") == "50.0 %");
	CHECK (ModernValueEdit::formatValue ("25.00", 1, "") == "25.0");
	CHECK (ModernValueEdit::formatValue ("50.0 %", 1, "%") == "50.0 %");
	CHECK (ModernValueEdit::formatValue ("abc", 1, "dB") == "abc");
	CHECK (ModernValueEdit::formatValue ("", 1, "dB") == "");
}

int main ()
{
	testFaderDetent ();
	testFaderMagnet ();
	testMeter ();
	testValueFormat ();
	printf ("%d checks, %d failures\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
