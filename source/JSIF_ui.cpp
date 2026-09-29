//------------------------------------------------------------------------
// Copyright(c) 2024 yg331.
//------------------------------------------------------------------------

#include "JSIF_ui.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cgradient.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/events.h"
#include "vstgui/uidescription/iviewcreator.h"
#include "vstgui/uidescription/uiattributes.h"
#include "vstgui/uidescription/uiviewcreator.h"
#include "vstgui/uidescription/uiviewfactory.h"
#include "vstgui/uidescription/detail/uiviewcreatorattributes.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>

namespace VSTGUI {

namespace {

constexpr float kValueEpsilon = 1e-6f;

CColor mix (const CColor& a, const CColor& b, double t)
{
	auto m = [t] (uint8_t x, uint8_t y) {
		return static_cast<uint8_t> (std::lround (x * t + y * (1. - t)));
	};
	return {m (a.red, b.red), m (a.green, b.green), m (a.blue, b.blue), m (a.alpha, b.alpha)};
}

CColor withAlpha (CColor c, uint8_t alpha)
{
	c.alpha = alpha;
	return c;
}

void fillRoundRect (CDrawContext* context, const CRect& r, CCoord radius, const CColor& color)
{
	auto path = owned (context->createGraphicsPath ());
	if (!path)
		return;
	path->addRoundRect (r, radius);
	context->setFillColor (color);
	context->drawGraphicsPath (path, CDrawContext::kPathFilled);
}

void strokeRoundRect (CDrawContext* context, const CRect& r, CCoord radius, const CColor& color,
                      CCoord width)
{
	auto path = owned (context->createGraphicsPath ());
	if (!path)
		return;
	path->addRoundRect (r, radius);
	context->setFrameColor (color);
	context->setLineWidth (width);
	context->setLineStyle (kLineSolid);
	context->drawGraphicsPath (path, CDrawContext::kPathStroked);
}

void fillGradientRoundRect (CDrawContext* context, const CRect& r, CCoord radius,
                            const GradientColorStopMap& stops)
{
	auto path = owned (context->createGraphicsPath ());
	auto gradient = owned (CGradient::create (stops));
	if (!path || !gradient)
		return;
	path->addRoundRect (r, radius);
	context->fillLinearGradient (path, *gradient, CPoint (r.left, r.top), CPoint (r.left, r.bottom));
}

void fillTriangle (CDrawContext* context, const CPoint& a, const CPoint& b, const CPoint& c,
                   const CColor& color)
{
	auto path = owned (context->createGraphicsPath ());
	if (!path)
		return;
	path->beginSubpath (a);
	path->addLine (b);
	path->addLine (c);
	path->closeSubpath ();
	context->setFillColor (color);
	context->drawGraphicsPath (path, CDrawContext::kPathFilled);
}

} // namespace

//------------------------------------------------------------------------
// ModernFader
//------------------------------------------------------------------------
ModernFader::ModernFader (const CRect& size) : CControl (size)
{
	setWantsFocus (true);
}

bool ModernFader::isAtDetent () const
{
	return hasDetent () && std::fabs (getValueNormalized () - detent) < kValueEpsilon;
}

CCoord ModernFader::getTravel () const
{
	return std::max<CCoord> (1., getViewSize ().getHeight () - capHeight);
}

CCoord ModernFader::valueToDrag (float value) const
{
	const CCoord travel = getTravel ();
	const CCoord pos = value * travel;
	if (!hasDetent () || value < detent - kValueEpsilon)
		return pos;
	if (value > detent + kValueEpsilon)
		return pos + 2. * detentZone;
	return detent * travel + detentZone;
}

float ModernFader::dragToValue (CCoord drag) const
{
	const CCoord travel = getTravel ();
	if (hasDetent ())
	{
		const CCoord d = detent * travel;
		if (drag >= d && drag <= d + 2. * detentZone)
			return detent;
		if (drag > d + 2. * detentZone)
			drag -= 2. * detentZone;
	}
	return static_cast<float> (std::clamp<CCoord> (drag / travel, 0., 1.));
}

float ModernFader::stepValue (float from, float delta) const
{
	float to = std::clamp (from + delta, 0.f, 1.f);
	// stepping across the detent stops on it
	if (hasDetent () && std::fabs (from - detent) > kValueEpsilon &&
	    (from - detent) * (to - detent) < 0.f)
		to = detent;
	return to;
}

float ModernFader::applyMagnet (float value) const
{
	if (snapStep <= 0.f || isAtDetent () || (hasDetent () && std::fabs (value - detent) < kValueEpsilon))
		return value;
	const double step = snapStep;
	const double snapped = std::round (value / step) * step;
	if (std::fabs (value - snapped) < snapStrength * step)
		return static_cast<float> (std::clamp (snapped, 0., 1.));
	return value;
}

CRect ModernFader::getCapRect () const
{
	const CRect& r = getViewSize ();
	const CCoord cy = r.top + capHeight / 2. + (1. - getValueNormalized ()) * getTravel ();
	const CCoord cx = r.getCenter ().x;
	return CRect (cx - capWidth / 2., cy - capHeight / 2., cx + capWidth / 2., cy + capHeight / 2.);
}

void ModernFader::changeValue (float value)
{
	value = std::clamp (value, 0.f, 1.f);
	if (std::fabs (value - getValueNormalized ()) < kValueEpsilon * 0.1f)
		return;
	setValueNormalized (value);
	valueChanged ();
	invalid ();
}

void ModernFader::onMouseDownEvent (MouseDownEvent& event)
{
	if (!event.buttonState.isLeft ())
		return;

	if (event.clickCount == 2 || event.modifiers.has (ModifierKey::Control))
	{
		beginEdit ();
		changeValue (getDefaultValue ());
		endEdit ();
		event.consumed = true;
		event.ignoreFollowUpMoveAndUpEvents (true);
		return;
	}

	beginEdit ();
	dragging = true;
	fineMode = event.modifiers.has (ModifierKey::Shift);

	if (!getCapRect ().pointInside (event.mousePosition))
	{
		// click on the track jumps there, landing on the detent when close to it
		const CCoord travel = getTravel ();
		const CCoord pos =
		    travel - (event.mousePosition.y - getViewSize ().top - capHeight / 2.);
		if (hasDetent () && std::fabs (pos - detent * travel) < detentZone)
			changeValue (detent);
		else
			changeValue (applyMagnet (static_cast<float> (std::clamp<CCoord> (pos / travel, 0., 1.))));
	}
	dragStartY = event.mousePosition.y;
	dragStartPos = valueToDrag (getValueNormalized ());
	event.consumed = true;
}

void ModernFader::onMouseMoveEvent (MouseMoveEvent& event)
{
	if (!dragging)
		return;
	const bool fine = event.modifiers.has (ModifierKey::Shift);
	if (fine != fineMode)
	{
		// re-anchor so switching to fine mode does not jump
		fineMode = fine;
		dragStartY = event.mousePosition.y;
		dragStartPos = valueToDrag (getValueNormalized ());
	}
	const CCoord delta = (dragStartY - event.mousePosition.y) * (fine ? 0.1 : 1.);
	const float value = dragToValue (dragStartPos + delta);
	changeValue (fine ? value : applyMagnet (value));
	event.consumed = true;
}

void ModernFader::onMouseUpEvent (MouseUpEvent& event)
{
	if (dragging)
	{
		dragging = false;
		endEdit ();
	}
	event.consumed = true;
}

void ModernFader::onMouseCancelEvent (MouseCancelEvent& event)
{
	if (dragging)
	{
		dragging = false;
		endEdit ();
	}
	event.consumed = true;
}

void ModernFader::onMouseWheelEvent (MouseWheelEvent& event)
{
	if (event.deltaY == 0.)
		return;
	const float step =
	    static_cast<float> (event.deltaY) * 0.01f * (event.modifiers.has (ModifierKey::Shift) ? 0.1f : 1.f);
	onMouseWheelEditing (this);
	changeValue (stepValue (getValueNormalized (), step));
	event.consumed = true;
}

void ModernFader::onKeyboardEvent (KeyboardEvent& event)
{
	if (event.type != EventType::KeyDown)
		return;
	float step = event.modifiers.has (ModifierKey::Shift) ? 0.001f : 0.01f;
	switch (event.virt)
	{
		case VirtualKey::Up:
		case VirtualKey::Right: break;
		case VirtualKey::Down:
		case VirtualKey::Left: step = -step; break;
		default: return;
	}
	beginEdit ();
	changeValue (stepValue (getValueNormalized (), step));
	endEdit ();
	event.consumed = true;
}

void ModernFader::draw (CDrawContext* context)
{
	context->setDrawMode (kAntiAliasing | kNonIntegralMode);
	const CRect& r = getViewSize ();
	const CCoord cx = r.getCenter ().x;
	const CCoord travel = getTravel ();

	// track
	CRect track (cx - trackWidth / 2., r.top + capHeight / 2. - 6., cx + trackWidth / 2.,
	             r.bottom - capHeight / 2. + 6.);
	fillRoundRect (context, track, trackWidth / 2., trackColor);
	context->setFrameColor (CColor (255, 255, 255, 12));
	context->setLineWidth (1.);
	context->drawLine (CPoint (track.left + 2., track.bottom + 1.), CPoint (track.right - 2., track.bottom + 1.));

	// unity markers either side of the cap
	if (marker >= 0.f)
	{
		const CCoord y = r.top + capHeight / 2. + (1. - marker) * travel;
		const bool active = isAtDetent () && std::fabs (marker - detent) < kValueEpsilon;
		const CColor color = active ? activeColor : markerColor;
		fillTriangle (context, CPoint (r.left, y - 10.), CPoint (r.left + 14., y), CPoint (r.left, y + 10.), color);
		fillTriangle (context, CPoint (r.right, y - 10.), CPoint (r.right - 14., y), CPoint (r.right, y + 10.), color);
	}

	drawCap (context, getCapRect ());
	setDirty (false);
}

void ModernFader::drawCap (CDrawContext* context, const CRect& cap)
{
	// soft drop shadow
	for (int i = 4; i >= 1; --i)
	{
		CRect s (cap);
		s.offset (0., i * 3.);
		s.extend (i * 1.5, i * 1.5);
		fillRoundRect (context, s, 10. + i, CColor (0, 0, 0, 30));
	}

	// metal body: the rim catches light at the top and falls off towards the bottom
	fillGradientRoundRect (context, cap, 9.,
	                       {{0.00, CColor (178, 182, 188)},
	                        {0.06, CColor (132, 136, 142)},
	                        {0.50, CColor (96, 99, 105)},
	                        {0.94, CColor (70, 73, 78)},
	                        {1.00, CColor (48, 50, 54)}});

	// two concave scoops either side of the centre crest: in a dish lit from above the
	// upper wall is in shadow and the lower wall catches the light
	const CCoord cy = cap.getCenter ().y;
	const CCoord inset = 6.;
	const CRect upper (cap.left + inset, cap.top + inset, cap.right - inset, cy - 4.);
	const CRect lower (cap.left + inset, cy + 4., cap.right - inset, cap.bottom - inset);
	for (const CRect& scoop : {upper, lower})
	{
		fillGradientRoundRect (context, scoop, 6.,
		                       {{0.00, CColor (44, 46, 50)},
		                        {0.30, CColor (70, 73, 78)},
		                        {0.80, CColor (120, 124, 130)},
		                        {1.00, CColor (150, 154, 160)}});
		// shadowed top lip, lit bottom lip
		context->setLineWidth (1.5);
		context->setFrameColor (CColor (0, 0, 0, 110));
		context->drawLine (CPoint (scoop.left + 5., scoop.top + 1.), CPoint (scoop.right - 5., scoop.top + 1.));
		context->setFrameColor (CColor (255, 255, 255, 70));
		context->drawLine (CPoint (scoop.left + 5., scoop.bottom - 1.), CPoint (scoop.right - 5., scoop.bottom - 1.));
	}

	// curved grip ridges following the dish
	const CCoord ridgeHalf = 14.;
	const CCoord cx = cap.getCenter ().x;
	auto drawRidge = [&] (CCoord y, CCoord bow) {
		for (int pass = 0; pass < 2; ++pass)
		{
			const CCoord dy = pass == 0 ? 1.5 : 0.;
			auto path = owned (context->createGraphicsPath ());
			if (!path)
				return;
			path->beginSubpath (CPoint (cx - ridgeHalf, y + dy));
			path->addBezierCurve (CPoint (cx - ridgeHalf / 2., y + dy + bow), CPoint (cx + ridgeHalf / 2., y + dy + bow),
			                      CPoint (cx + ridgeHalf, y + dy));
			context->setFrameColor (pass == 0 ? CColor (0, 0, 0, 120) : CColor (255, 255, 255, 80));
			context->setLineWidth (1.5);
			context->drawGraphicsPath (path, CDrawContext::kPathStroked);
		}
	};
	for (int i = 0; i < 4; ++i)
	{
		drawRidge (upper.top + 12. + i * 8., -5.); // arcs bow up in the upper scoop
		drawRidge (lower.top + 9. + i * 8., 5.);   // and down in the lower one
	}

	// crest highlight and outline
	context->setFrameColor (CColor (255, 255, 255, 60));
	context->setLineWidth (2.);
	context->drawLine (CPoint (cap.left + 8., cap.top + 2.), CPoint (cap.right - 8., cap.top + 2.));
	strokeRoundRect (context, cap, 9., CColor (20, 21, 24), 2.);

	// value indicator on the centre crest
	CRect line (cap.left + 5., cy - 2., cap.right - 5., cy + 2.);
	if (isAtDetent ())
	{
		CRect glow (line);
		glow.extend (4., 3.);
		fillRoundRect (context, glow, 4., withAlpha (activeColor, 120));
		fillRoundRect (context, line, 2., mix (activeColor, CColor (255, 255, 255), 0.25));
	}
	else
	{
		fillRoundRect (context, line, 2., indicatorColor);
	}
}

//------------------------------------------------------------------------
// ModernMeter
//------------------------------------------------------------------------
ModernMeter::ModernMeter (const CRect& size, int32_t numLed)
: CVuMeter (size, nullptr, nullptr, numLed, Style::kVertical)
{
}

int32_t ModernMeter::getLitSegments () const
{
	const int32_t n = getNbLed ();
	const auto lit = static_cast<int32_t> (std::floor (getValueNormalized () * n + 0.5f));
	return std::clamp (lit, 0, n);
}

CColor ModernMeter::segmentColor (int32_t segment) const
{
	if (segment >= zoneHigh)
		return highColor;
	if (segment >= zoneMid)
		return midColor;
	return lowColor;
}

void ModernMeter::draw (CDrawContext* context)
{
	context->setDrawMode (kAntiAliasing | kNonIntegralMode);
	const CRect& r = getViewSize ();
	const int32_t n = std::max (1, getNbLed ());
	const int32_t numBars = std::max (1, bars);
	const CCoord segH = (r.getHeight () - (n - 1) * segmentGap) / n;
	const CCoord barW = (r.getWidth () - (numBars - 1) * barGap) / numBars;
	const int32_t lit = getLitSegments ();

	for (int32_t b = 0; b < numBars; ++b)
	{
		const CCoord x = r.left + b * (barW + barGap);
		for (int32_t k = 1; k <= n; ++k)
		{
			const CCoord bottom = r.bottom - (k - 1) * (segH + segmentGap);
			CRect seg (x, bottom - segH, x + barW, bottom);
			if (k <= lit)
			{
				const CColor color = segmentColor (k);
				fillRoundRect (context, seg, segmentRadius, color);
				context->setFillColor (CColor (255, 255, 255, 64));
				context->drawRect (CRect (seg.left + 2., seg.top + 1., seg.right - 2., seg.top + 3.), kDrawFilled);
			}
			else
			{
				fillRoundRect (context, seg, segmentRadius, offColor);
			}
		}
	}
	setDirty (false);
}

//------------------------------------------------------------------------
// ModernMeterScale
//------------------------------------------------------------------------
ModernMeterScale::ModernMeterScale (const CRect& size) : CView (size)
{
	setMouseEnabled (false);
}

void ModernMeterScale::setLabels (const std::string& spec)
{
	labelSpec = spec;
	labels.clear ();
	size_t start = 0;
	while (start < spec.size ())
	{
		size_t end = spec.find (',', start);
		if (end == std::string::npos)
			end = spec.size ();
		const std::string item = spec.substr (start, end - start);
		const size_t colon = item.find (':');
		if (colon != std::string::npos)
			labels.emplace_back (std::atoi (item.substr (0, colon).c_str ()), item.substr (colon + 1));
		start = end + 1;
	}
	invalid ();
}

void ModernMeterScale::draw (CDrawContext* context)
{
	context->setDrawMode (kAntiAliasing | kNonIntegralMode);
	const CRect& r = getViewSize ();
	const int32_t n = std::max (1, numLed);
	const CCoord segH = (r.getHeight () - (n - 1) * segmentGap) / n;
	auto centreOf = [&] (int32_t k) { return r.bottom - (k - 1) * (segH + segmentGap) - segH / 2.; };

	if (font)
		context->setFont (font);
	context->setFontColor (fontColor);
	for (int32_t k = 1; k <= n; ++k)
	{
		auto it = std::find_if (labels.begin (), labels.end (), [k] (const auto& l) { return l.first == k; });
		const CCoord y = centreOf (k);
		if (it != labels.end ())
		{
			context->drawString (it->second.c_str (), CRect (r.left, y - 20., r.right, y + 20.), kCenterText);
		}
		else if (tickEvery > 0 && k % tickEvery == 0)
		{
			context->setFrameColor (tickColor);
			context->setLineWidth (2.);
			const CCoord cx = r.getCenter ().x;
			context->drawLine (CPoint (cx - 7., y), CPoint (cx + 7., y));
		}
	}
	setDirty (false);
}

//------------------------------------------------------------------------
// ModernButton
//------------------------------------------------------------------------
ModernButton::ModernButton (const CRect& size) : COnOffButton (size)
{
}

void ModernButton::draw (CDrawContext* context)
{
	context->setDrawMode (kAntiAliasing | kNonIntegralMode);
	const bool on = getValueNormalized () >= 0.5f;
	CRect r (getViewSize ());
	r.inset (6., 6.);

	if (on)
	{
		for (int i = 3; i >= 1; --i)
		{
			CRect g (r);
			g.extend (i * 2., i * 2.);
			fillRoundRect (context, g, radius + i * 2., withAlpha (color, static_cast<uint8_t> (36 / i)));
		}
		fillGradientRoundRect (context, r, radius,
		                       {{0.00, mix (color, CColor (255, 255, 255), 0.88)},
		                        {0.55, color},
		                        {1.00, mix (color, CColor (0, 0, 0), 0.85)}});
		strokeRoundRect (context, r, radius, mix (color, CColor (0, 0, 0), 0.7), 2.);
		context->setFrameColor (CColor (255, 255, 255, 90));
		context->setLineWidth (2.);
		context->drawLine (CPoint (r.left + radius, r.top + 2.), CPoint (r.right - radius, r.top + 2.));
	}
	else
	{
		CRect s (r);
		s.offset (0., 4.);
		fillRoundRect (context, s, radius, CColor (0, 0, 0, 80));
		fillRoundRect (context, r, radius, mix (color, baseColor, 0.20));
		strokeRoundRect (context, r, radius, mix (color, baseColor, 0.35), 2.);
	}

	const CColor textColor = on ? onTextColor : mix (color, CColor (232, 232, 234), 0.7);
	context->setFontColor (textColor);
	const CCoord cy = r.getCenter ().y;
	if (subTitle.empty ())
	{
		if (font)
			context->setFont (font);
		context->drawString (title.c_str (), r, kCenterText);
	}
	else
	{
		if (font)
			context->setFont (font);
		context->drawString (title.c_str (), CRect (r.left, cy - 30., r.right, cy + 2.), kCenterText);
		if (subFont)
			context->setFont (subFont);
		context->drawString (subTitle.c_str (), CRect (r.left, cy + 2., r.right, cy + 30.), kCenterText);
	}
	setDirty (false);
}

//------------------------------------------------------------------------
// ModernPanel
//------------------------------------------------------------------------
ModernPanel::ModernPanel (const CRect& size) : CViewContainer (size)
{
}

void ModernPanel::drawBackgroundRect (CDrawContext* context, const CRect& /*updateRect*/)
{
	context->setDrawMode (kAntiAliasing | kNonIntegralMode);
	CRect r (0., 0., getWidth (), getHeight ());
	r.inset (frameWidth / 2., frameWidth / 2.);
	fillRoundRect (context, r, cornerRadius, getBackgroundColor ());
	if (frameWidth > 0.)
		strokeRoundRect (context, r, cornerRadius, frameColor, frameWidth);
}

//------------------------------------------------------------------------
// ModernValueEdit
//------------------------------------------------------------------------
ModernValueEdit::ModernValueEdit (const CRect& size) : CTextEdit (size, nullptr, -1)
{
}

std::string ModernValueEdit::formatValue (const UTF8String& text, int32_t decimals,
                                          const std::string& unit)
{
	const char* str = text.data ();
	if (!str)
		return {};
	char* end = nullptr;
	double value = std::strtod (str, &end);
	if (end == str)
		return text.getString ();
	char buffer[64];
	snprintf (buffer, sizeof (buffer), "%.*f", std::clamp (decimals, 0, 6), value);
	std::string result (buffer);
	if (result.find_first_not_of ("-0.") == std::string::npos && result[0] == '-')
		result.erase (0, 1); // "-0.0" -> "0.0"
	if (!unit.empty ())
		result += " " + unit;
	return result;
}

void ModernValueEdit::setText (const UTF8String& txt)
{
	CTextEdit::setText (UTF8String (formatValue (txt, decimals, unit)));
}

void ModernValueEdit::draw (CDrawContext* context)
{
	if (hovered && !getPlatformTextEdit ())
	{
		context->setDrawMode (kAntiAliasing | kNonIntegralMode);
		CRect r (getViewSize ());
		r.inset (1., 1.);
		fillRoundRect (context, r, 10., hoverColor);
		strokeRoundRect (context, r, 10., hoverFrameColor, 2.);
	}
	CTextEdit::draw (context);
}

void ModernValueEdit::onMouseEnterEvent (MouseEnterEvent& event)
{
	hovered = true;
	invalid ();
	CTextEdit::onMouseEnterEvent (event);
}

void ModernValueEdit::onMouseExitEvent (MouseExitEvent& event)
{
	hovered = false;
	invalid ();
	CTextEdit::onMouseExitEvent (event);
}

//------------------------------------------------------------------------
// View creators
//------------------------------------------------------------------------
namespace {

using AttrType = IViewCreator::AttrType;

// Declarative attribute table so each creator only lists its properties.
template <typename ViewT>
struct Attr
{
	std::string name;
	AttrType type;
	std::function<void (ViewT*, const std::string&, const IUIDescription*)> set;
	std::function<void (ViewT*, std::string&, const IUIDescription*)> get;
};

template <typename ViewT>
Attr<ViewT> colorAttr (const std::string& name, CColor ViewT::*member)
{
	return {name, IViewCreator::kColorType,
	        [member] (ViewT* v, const std::string& s, const IUIDescription* d) {
		        CColor c;
		        if (UIViewCreator::stringToColor (&s, c, d))
			        v->*member = c;
	        },
	        [member] (ViewT* v, std::string& s, const IUIDescription* d) {
		        UIViewCreator::colorToString (v->*member, s, d);
	        }};
}

template <typename ViewT>
Attr<ViewT> numberAttr (const std::string& name, CCoord ViewT::*member)
{
	return {name, IViewCreator::kFloatType,
	        [member] (ViewT* v, const std::string& s, const IUIDescription*) {
		        v->*member = std::strtod (s.c_str (), nullptr);
	        },
	        [member] (ViewT* v, std::string& s, const IUIDescription*) {
		        s = UIAttributes::doubleToString (v->*member);
	        }};
}

template <typename ViewT>
Attr<ViewT> intAttr (const std::string& name, int32_t ViewT::*member)
{
	return {name, IViewCreator::kIntegerType,
	        [member] (ViewT* v, const std::string& s, const IUIDescription*) {
		        v->*member = std::atoi (s.c_str ());
	        },
	        [member] (ViewT* v, std::string& s, const IUIDescription*) {
		        s = std::to_string (v->*member);
	        }};
}

template <typename ViewT>
Attr<ViewT> stringAttr (const std::string& name, std::string ViewT::*member)
{
	return {name, IViewCreator::kStringType,
	        [member] (ViewT* v, const std::string& s, const IUIDescription*) { v->*member = s; },
	        [member] (ViewT* v, std::string& s, const IUIDescription*) { s = v->*member; }};
}

template <typename ViewT>
Attr<ViewT> fontAttr (const std::string& name, SharedPointer<CFontDesc> ViewT::*member)
{
	return {name, IViewCreator::kFontType,
	        [member] (ViewT* v, const std::string& s, const IUIDescription* d) {
		        if (auto font = d->getFont (s.c_str ()))
			        v->*member = font;
	        },
	        [member] (ViewT* v, std::string& s, const IUIDescription* d) {
		        if (v->*member)
			        if (auto name = d->lookupFontName (v->*member))
				        s = name;
	        }};
}

template <typename ViewT>
class ModernViewCreator : public ViewCreatorAdapter
{
public:
	using CreateFunc = std::function<CView*()>;

	ModernViewCreator (IdStringPtr name, IdStringPtr baseName, CreateFunc create,
	                   std::vector<Attr<ViewT>> attributes)
	: name (name), baseName (baseName), createFunc (std::move (create)), attrs (std::move (attributes))
	{
		UIViewFactory::registerViewCreator (*this);
	}

	IdStringPtr getViewName () const override { return name; }
	IdStringPtr getBaseViewName () const override { return baseName; }
	CView* create (const UIAttributes&, const IUIDescription*) const override { return createFunc (); }

	bool apply (CView* view, const UIAttributes& attributes, const IUIDescription* description) const override
	{
		auto* v = dynamic_cast<ViewT*> (view);
		if (!v)
			return false;
		for (const auto& a : attrs)
			if (auto value = attributes.getAttributeValue (a.name))
				a.set (v, *value, description);
		v->invalid ();
		return true;
	}

	bool getAttributeNames (StringList& names) const override
	{
		for (const auto& a : attrs)
			names.emplace_back (a.name);
		return true;
	}

	AttrType getAttributeType (const std::string& attributeName) const override
	{
		for (const auto& a : attrs)
			if (a.name == attributeName)
				return a.type;
		return kUnknownType;
	}

	bool getAttributeValue (CView* view, const std::string& attributeName, std::string& stringValue,
	                        const IUIDescription* description) const override
	{
		auto* v = dynamic_cast<ViewT*> (view);
		if (!v)
			return false;
		for (const auto& a : attrs)
		{
			if (a.name == attributeName)
			{
				a.get (v, stringValue, description);
				return true;
			}
		}
		return false;
	}

private:
	IdStringPtr name;
	IdStringPtr baseName;
	CreateFunc createFunc;
	std::vector<Attr<ViewT>> attrs;
};

const CRect kDefaultRect (0, 0, 100, 100);

ModernViewCreator<ModernFader> gFaderCreator (
    "Modern Fader", UIViewCreator::kCControl,
    [] { return new ModernFader (kDefaultRect); },
    {
        {"detent-value", IViewCreator::kFloatType,
         [] (ModernFader* v, const std::string& s, const IUIDescription*) { v->setDetent (static_cast<float> (std::strtod (s.c_str (), nullptr))); },
         [] (ModernFader* v, std::string& s, const IUIDescription*) { s = UIAttributes::doubleToString (v->getDetent ()); }},
        {"marker-value", IViewCreator::kFloatType,
         [] (ModernFader* v, const std::string& s, const IUIDescription*) { v->setMarker (static_cast<float> (std::strtod (s.c_str (), nullptr))); },
         [] (ModernFader* v, std::string& s, const IUIDescription*) { s = UIAttributes::doubleToString (v->getMarker ()); }},
        numberAttr ("cap-width", &ModernFader::capWidth),
        numberAttr ("cap-height", &ModernFader::capHeight),
        numberAttr ("track-width", &ModernFader::trackWidth),
        numberAttr ("detent-zone", &ModernFader::detentZone),
        {"snap-step", IViewCreator::kFloatType,
         [] (ModernFader* v, const std::string& s, const IUIDescription*) { v->snapStep = static_cast<float> (std::strtod (s.c_str (), nullptr)); },
         [] (ModernFader* v, std::string& s, const IUIDescription*) { s = UIAttributes::doubleToString (v->snapStep); }},
        {"snap-strength", IViewCreator::kFloatType,
         [] (ModernFader* v, const std::string& s, const IUIDescription*) { v->snapStrength = static_cast<float> (std::strtod (s.c_str (), nullptr)); },
         [] (ModernFader* v, std::string& s, const IUIDescription*) { s = UIAttributes::doubleToString (v->snapStrength); }},
        colorAttr ("track-color", &ModernFader::trackColor),
        colorAttr ("indicator-color", &ModernFader::indicatorColor),
        colorAttr ("marker-color", &ModernFader::markerColor),
        colorAttr ("active-color", &ModernFader::activeColor),
    });

ModernViewCreator<ModernMeter> gMeterCreator (
    "Modern Meter", UIViewCreator::kCVuMeter,
    [] { return new ModernMeter (kDefaultRect, 25); },
    {
        intAttr ("bars", &ModernMeter::bars),
        numberAttr ("bar-gap", &ModernMeter::barGap),
        numberAttr ("segment-gap", &ModernMeter::segmentGap),
        numberAttr ("segment-radius", &ModernMeter::segmentRadius),
        intAttr ("zone-mid", &ModernMeter::zoneMid),
        intAttr ("zone-high", &ModernMeter::zoneHigh),
        colorAttr ("low-color", &ModernMeter::lowColor),
        colorAttr ("mid-color", &ModernMeter::midColor),
        colorAttr ("high-color", &ModernMeter::highColor),
        colorAttr ("off-color", &ModernMeter::offColor),
    });

ModernViewCreator<ModernMeterScale> gScaleCreator (
    "Modern Meter Scale", UIViewCreator::kCView,
    [] { return new ModernMeterScale (kDefaultRect); },
    {
        {"labels", IViewCreator::kStringType,
         [] (ModernMeterScale* v, const std::string& s, const IUIDescription*) { v->setLabels (s); },
         [] (ModernMeterScale* v, std::string& s, const IUIDescription*) { s = v->getLabels (); }},
        intAttr ("num-led", &ModernMeterScale::numLed),
        numberAttr ("segment-gap", &ModernMeterScale::segmentGap),
        intAttr ("tick-every", &ModernMeterScale::tickEvery),
        fontAttr ("font", &ModernMeterScale::font),
        colorAttr ("font-color", &ModernMeterScale::fontColor),
        colorAttr ("tick-color", &ModernMeterScale::tickColor),
    });

ModernViewCreator<ModernButton> gButtonCreator (
    "Modern Button", UIViewCreator::kCControl,
    [] { return new ModernButton (kDefaultRect); },
    {
        stringAttr ("title", &ModernButton::title),
        stringAttr ("sub-title", &ModernButton::subTitle),
        colorAttr ("color", &ModernButton::color),
        colorAttr ("on-text-color", &ModernButton::onTextColor),
        colorAttr ("base-color", &ModernButton::baseColor),
        numberAttr ("corner-radius", &ModernButton::radius),
        fontAttr ("font", &ModernButton::font),
        fontAttr ("sub-font", &ModernButton::subFont),
    });

ModernViewCreator<ModernPanel> gPanelCreator (
    "Modern Panel", UIViewCreator::kCViewContainer,
    [] { return new ModernPanel (kDefaultRect); },
    {
        numberAttr ("corner-radius", &ModernPanel::cornerRadius),
        colorAttr ("frame-color", &ModernPanel::frameColor),
        numberAttr ("frame-width", &ModernPanel::frameWidth),
    });

ModernViewCreator<ModernValueEdit> gValueEditCreator (
    "Modern Value Edit", UIViewCreator::kCTextEdit,
    [] { return new ModernValueEdit (kDefaultRect); },
    {
        stringAttr ("unit", &ModernValueEdit::unit),
        intAttr ("decimals", &ModernValueEdit::decimals),
        colorAttr ("hover-color", &ModernValueEdit::hoverColor),
        colorAttr ("hover-frame-color", &ModernValueEdit::hoverFrameColor),
    });

} // namespace
} // namespace VSTGUI
