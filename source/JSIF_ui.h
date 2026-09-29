//------------------------------------------------------------------------
// Copyright(c) 2024 yg331.
//------------------------------------------------------------------------
// Views for the "Modern" editor template. Everything is drawn in code so the
// skin stays sharp at every zoom factor and colours can be changed in the uidesc.
//------------------------------------------------------------------------

#pragma once

#include "vstgui/lib/controls/ccontrol.h"
#include "vstgui/lib/controls/cbuttons.h"
#include "vstgui/lib/controls/ctextedit.h"
#include "vstgui/lib/controls/cvumeter.h"
#include "vstgui/lib/cviewcontainer.h"

#include <string>
#include <vector>

namespace VSTGUI {

//------------------------------------------------------------------------
//  Vertical fader with a Sonnox-style cap.
//  Dragging is relative (Shift = fine). Double-click or Cmd-click resets.
//  With a detent set, the fader holds at that value while the mouse moves
//  through a dead zone of 2 * detentZone, so 0 dB is easy to hit exactly.
//  With snapStep set, normal drags are pulled onto multiples of snapStep
//  when within snapStrength * snapStep of one (whole numbers feel magnetic).
//  Shift-drags, typed values and the wheel are not snapped.
//------------------------------------------------------------------------
class ModernFader : public CControl, protected CMouseWheelEditingSupport
{
public:
	explicit ModernFader (const CRect& size);

	// normalized positions, negative = none
	void setDetent (float value) { detent = value; invalid (); }
	float getDetent () const { return detent; }
	void setMarker (float value) { marker = value; invalid (); }
	float getMarker () const { return marker; }
	bool hasDetent () const { return detent >= 0.f; }
	bool isAtDetent () const;

	CCoord capWidth {72.};
	CCoord capHeight {116.};
	CCoord trackWidth {12.};
	CCoord detentZone {18.};
	float snapStep {0.f};
	float snapStrength {0.2f};
	CColor trackColor {16, 17, 19, 255};
	CColor indicatorColor {232, 232, 234, 255};
	CColor markerColor {169, 170, 174, 255};
	CColor activeColor {61, 160, 245, 255};

	// drag geometry, in view coordinates
	CCoord getTravel () const;
	CCoord valueToDrag (float value) const;
	float dragToValue (CCoord drag) const;
	float stepValue (float from, float delta) const;
	float applyMagnet (float value) const;
	CRect getCapRect () const;

	void draw (CDrawContext* context) override;
	void onMouseDownEvent (MouseDownEvent& event) override;
	void onMouseMoveEvent (MouseMoveEvent& event) override;
	void onMouseUpEvent (MouseUpEvent& event) override;
	void onMouseCancelEvent (MouseCancelEvent& event) override;
	void onMouseWheelEvent (MouseWheelEvent& event) override;
	void onKeyboardEvent (KeyboardEvent& event) override;

	CLASS_METHODS (ModernFader, CControl)

private:
	void changeValue (float value);
	void drawCap (CDrawContext* context, const CRect& cap);

	float detent {-1.f};
	float marker {-1.f};
	bool dragging {false};
	bool fineMode {false};
	CCoord dragStartY {0.};
	CCoord dragStartPos {0.};
};

//------------------------------------------------------------------------
//  Segmented LED meter. Derives from CVuMeter so VuMeterController picks it up.
//  Segments at or above zoneMid use midColor, at or above zoneHigh use highColor
//  (1-based segment numbers). "bars" draws the same value on several bars.
//------------------------------------------------------------------------
class ModernMeter : public CVuMeter
{
public:
	ModernMeter (const CRect& size, int32_t numLed);

	int32_t bars {1};
	CCoord barGap {16.};
	CCoord segmentGap {6.};
	CCoord segmentRadius {5.};
	int32_t zoneMid {10};
	int32_t zoneHigh {19};
	CColor lowColor {182, 92, 242, 255};
	CColor midColor {61, 160, 245, 255};
	CColor highColor {242, 71, 107, 255};
	CColor offColor {55, 56, 60, 255};

	int32_t getLitSegments () const;
	CColor segmentColor (int32_t segment) const;

	void draw (CDrawContext* context) override;

	CLASS_METHODS (ModernMeter, CVuMeter)
};

//------------------------------------------------------------------------
//  dB labels drawn on the segment centres of a ModernMeter with the same
//  height, num-led and segment-gap. labels: "25:+6,19:0,10:-12,..."
//------------------------------------------------------------------------
class ModernMeterScale : public CView
{
public:
	explicit ModernMeterScale (const CRect& size);

	void setLabels (const std::string& spec);
	const std::string& getLabels () const { return labelSpec; }

	int32_t numLed {25};
	CCoord segmentGap {6.};
	int32_t tickEvery {3};
	SharedPointer<CFontDesc> font;
	CColor fontColor {191, 192, 196, 255};
	CColor tickColor {85, 87, 92, 255};

	void draw (CDrawContext* context) override;

	CLASS_METHODS (ModernMeterScale, CView)

private:
	std::string labelSpec;
	std::vector<std::pair<int32_t, std::string>> labels;
};

//------------------------------------------------------------------------
//  Two-line toggle button filled with its colour when on, tinted when off.
//------------------------------------------------------------------------
class ModernButton : public COnOffButton
{
public:
	explicit ModernButton (const CRect& size);

	std::string title;
	std::string subTitle;
	CColor color {61, 160, 245, 255};
	CColor onTextColor {21, 22, 26, 255};
	CColor baseColor {35, 36, 39, 255};
	CCoord radius {18.};
	SharedPointer<CFontDesc> font;
	SharedPointer<CFontDesc> subFont;

	void draw (CDrawContext* context) override;

	CLASS_METHODS (ModernButton, COnOffButton)
};

//------------------------------------------------------------------------
//  Container with a rounded, outlined background.
//------------------------------------------------------------------------
class ModernPanel : public CViewContainer
{
public:
	explicit ModernPanel (const CRect& size);

	CCoord cornerRadius {24.};
	CColor frameColor {53, 54, 58, 255};
	CCoord frameWidth {2.};

	void drawBackgroundRect (CDrawContext* context, const CRect& updateRect) override;
};

//------------------------------------------------------------------------
//  Parameter readout that can be clicked to type a value. Shows the value
//  with a fixed number of decimals and a unit ("2.0 dB").
//------------------------------------------------------------------------
class ModernValueEdit : public CTextEdit
{
public:
	explicit ModernValueEdit (const CRect& size);

	std::string unit;
	int32_t decimals {1};
	CColor hoverColor {32, 33, 36, 255};
	CColor hoverFrameColor {53, 54, 58, 255};

	static std::string formatValue (const UTF8String& text, int32_t decimals, const std::string& unit);

	void setText (const UTF8String& txt) override;
	void draw (CDrawContext* context) override;
	void onMouseEnterEvent (MouseEnterEvent& event) override;
	void onMouseExitEvent (MouseExitEvent& event) override;

private:
	bool hovered {false};
};

} // namespace VSTGUI
