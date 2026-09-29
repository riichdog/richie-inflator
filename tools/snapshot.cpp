//------------------------------------------------------------------------
// UI snapshot tool: renders a template from JSIF_editor.uidesc to a PNG
// without a host, so the editor can be checked from the command line.
//
// usage: jsif_snapshot <plugin.vst3> <template> <out.png> [scale] [args ...]
//   tag=value   set a control's normalized value
//   t:tag=text  set the text of a label or readout (what the host would show)
//   m:tag=text  add a menu entry to an option menu and select it
//------------------------------------------------------------------------

#include "vstgui/lib/vstguiinit.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/cbitmap.h"
#include "vstgui/lib/coffscreencontext.h"
#include "vstgui/lib/controls/ccontrol.h"
#include "vstgui/lib/controls/coptionmenu.h"
#include "vstgui/lib/controls/ctextlabel.h"
#include "vstgui/lib/platform/platformfactory.h"
#include "vstgui/uidescription/uidescription.h"

#include <CoreFoundation/CoreFoundation.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>

using namespace VSTGUI;

struct Args
{
	std::map<int32_t, float> values;
	std::map<int32_t, std::string> texts;
	std::map<int32_t, std::string> menus;
};

static void applyValues (CView* view, const Args& args)
{
	if (auto control = dynamic_cast<CControl*> (view))
	{
		const auto tag = control->getTag ();
		auto it = args.values.find (tag);
		if (it != args.values.end ())
			control->setValueNormalized (it->second);
		auto menu = args.menus.find (tag);
		if (auto optionMenu = dynamic_cast<COptionMenu*> (control); optionMenu && menu != args.menus.end ())
		{
			optionMenu->removeAllEntry ();
			optionMenu->addEntry (menu->second.c_str ());
			optionMenu->setCurrent (0);
		}
		auto text = args.texts.find (tag);
		if (auto label = dynamic_cast<CTextLabel*> (control); label && text != args.texts.end ())
			label->setText (text->second.c_str ());
		control->invalid ();
	}
	if (auto container = view->asViewContainer ())
		container->forEachChild ([&] (CView* child) { applyValues (child, args); });
}

int main (int argc, char** argv)
{
	if (argc < 4)
	{
		fprintf (stderr, "usage: %s <plugin.vst3> <template> <out.png> [scale] [tag=value ...]\n", argv[0]);
		return 2;
	}
	const char* bundlePath = argv[1];
	const char* templateName = argv[2];
	const char* outPath = argv[3];
	double scale = argc > 4 ? atof (argv[4]) : 1.0;

	Args args;
	for (int i = 5; i < argc; ++i)
	{
		const char* arg = argv[i];
		const char* eq = strchr (arg, '=');
		if (!eq)
			continue;
		if (strncmp (arg, "t:", 2) == 0)
			args.texts[atoi (arg + 2)] = eq + 1;
		else if (strncmp (arg, "m:", 2) == 0)
			args.menus[atoi (arg + 2)] = eq + 1;
		else
			args.values[atoi (arg)] = static_cast<float> (atof (eq + 1));
	}

	auto url = CFURLCreateFromFileSystemRepresentation (
	    nullptr, reinterpret_cast<const UInt8*> (bundlePath), strlen (bundlePath), true);
	auto bundle = CFBundleCreate (nullptr, url);
	CFRelease (url);
	if (!bundle)
	{
		fprintf (stderr, "cannot open bundle %s\n", bundlePath);
		return 1;
	}
	VSTGUI::init (bundle);

	int result = 0;
	{
		UIDescription description (CResourceDescription ("JSIF_editor.uidesc"));
		if (!description.parse ())
		{
			fprintf (stderr, "cannot parse JSIF_editor.uidesc\n");
			return 1;
		}
		CView* view = description.createView (templateName, nullptr);
		if (!view)
		{
			fprintf (stderr, "cannot create template %s\n", templateName);
			return 1;
		}
		CRect size = view->getViewSize ();
		auto frame = makeOwned<CFrame> (size, nullptr);
		frame->addView (view);
		applyValues (view, args);

		CPoint pixels (size.getWidth () * scale, size.getHeight () * scale);
		auto bitmap = renderBitmapOffscreen (pixels, 1., [&] (CDrawContext& context) {
			CDrawContext::Transform transform (context, CGraphicsTransform ().scale (scale, scale));
			frame->drawRect (&context, size);
		});
		auto png = bitmap ? getPlatformFactory ().createBitmapMemoryPNGRepresentation (
		                        bitmap->getPlatformBitmap ())
		                  : PNGBitmapBuffer ();
		if (png.empty ())
		{
			fprintf (stderr, "rendering failed\n");
			result = 1;
		}
		else if (FILE* f = fopen (outPath, "wb"))
		{
			fwrite (png.data (), 1, png.size (), f);
			fclose (f);
			printf ("wrote %s (%.0fx%.0f @ %.2fx)\n", outPath, size.getWidth (), size.getHeight (), scale);
		}
		else
		{
			fprintf (stderr, "cannot write %s\n", outPath);
			result = 1;
		}
		frame->removeAll ();
	}
	VSTGUI::exit ();
	CFRelease (bundle);
	return result;
}
