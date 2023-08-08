/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* Copyright 2012 Mozilla Foundation and Mozilla contributors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <QGuiApplication>
#include <QFont>
#include <QScreen>
#include <QPalette>

#include "nsLookAndFeel.h"
#include "nsStyleConsts.h"
#include "gfxFont.h"
#include "gfxFontConstants.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/FontPropertyTypes.h"
#include "mozilla/StaticPrefs_widget.h"
#include "mozilla/Services.h"
#include "mozilla/Logging.h"
#include "nsIObserver.h"

using namespace mozilla;

LazyLogModule sLookAndFeel("LookAndFeel");

class nsLookAndFeel::Observer final : public nsIObserver
{
public:
    NS_DECL_ISUPPORTS

    explicit Observer() : mDarkAmbience(false) {}

    NS_IMETHOD Observe(nsISupports*, const char* aTopic,
                       const char16_t* aData) override;

    bool GetDarkAmbience();
private:
    virtual ~Observer() = default;

public:
    bool mDarkAmbience;
};

NS_IMPL_ISUPPORTS(nsLookAndFeel::Observer, nsIObserver)

static const char16_t UNICODE_BULLET = 0x2022;

#define QCOLOR_TO_NS_RGB(c)                     \
  ((nscolor)NS_RGB(c.red(),c.green(),c.blue()))

nsLookAndFeel::nsLookAndFeel()
    : nsXPLookAndFeel()
{
    mObserver = new nsLookAndFeel::Observer();
    nsCOMPtr<nsIObserverService> os = mozilla::services::GetObserverService();
    if (os) {
        os->AddObserver(mObserver, "ambience-theme-changed", false);
    }
}

nsLookAndFeel::~nsLookAndFeel()
{
    nsCOMPtr<nsIObserverService> os = mozilla::services::GetObserverService();
    if (os) {
        os->RemoveObserver(mObserver, "ambience-theme-changed");
    }
    mObserver = nullptr;
}

NS_IMETHODIMP nsLookAndFeel::Observer::Observe(nsISupports*, const char* aTopic, const char16_t* aData) {
    MOZ_ASSERT(!strcmp(aTopic, "ambience-theme-changed"));

    bool darkAmbience = false;
    nsDependentString data(aData);
    if (data.EqualsLiteral("dark")) {
        darkAmbience = true;
    }

    if (mDarkAmbience != darkAmbience) {
        mDarkAmbience = darkAmbience;
        MOZ_LOG(sLookAndFeel, LogLevel::Info, ("Ambience set to %s", mDarkAmbience ? "dark" : "light"));
        if (nsCOMPtr<nsIObserverService> obs = services::GetObserverService()) {
            obs->NotifyObservers(nullptr, "look-and-feel-changed", nullptr);
        }
    }

    return NS_OK;
}

bool nsLookAndFeel::Observer::GetDarkAmbience()
{
    return mDarkAmbience;
}


void nsLookAndFeel::NativeInit() { }

nsresult
nsLookAndFeel::NativeGetColor(ColorID aID, nscolor &aColor)
{
    nsresult rv = NS_OK;

#define BG_PRELIGHT_COLOR     NS_RGB(0xee,0xee,0xee)
#define FG_PRELIGHT_COLOR     NS_RGB(0x77,0x77,0x77)
#define RED_COLOR             NS_RGB(0xff,0x00,0x00)

    QPalette palette = QGuiApplication::palette();

    switch (aID) {
        // These colors don't seem to be used for anything anymore in Mozilla
        // (except here at least TextSelectBackground and TextSelectForeground)
        // The CSS2 colors below are used.
    case ColorID::WindowBackground:
        aColor = QCOLOR_TO_NS_RGB(palette.color(QPalette::Normal, QPalette::Window));
        break;
    case ColorID::WindowForeground:
        aColor = QCOLOR_TO_NS_RGB(palette.color(QPalette::Normal, QPalette::WindowText));
        break;
    case ColorID::WidgetBackground:
        aColor = QCOLOR_TO_NS_RGB(palette.color(QPalette::Normal, QPalette::Window));
        break;
    case ColorID::WidgetForeground:
        aColor = QCOLOR_TO_NS_RGB(palette.color(QPalette::Normal, QPalette::WindowText));
        break;
    case ColorID::WidgetSelectBackground:
        aColor = QCOLOR_TO_NS_RGB(palette.color(QPalette::Normal, QPalette::Window));
        break;
    case ColorID::WidgetSelectForeground:
        aColor = QCOLOR_TO_NS_RGB(palette.color(QPalette::Normal, QPalette::WindowText));
        break;
    case ColorID::Widget3DHighlight:
        aColor = NS_RGB(0xa0,0xa0,0xa0);
        break;
    case ColorID::Widget3DShadow:
        aColor = NS_RGB(0x40,0x40,0x40);
        break;
    case ColorID::TextBackground:
        // not used?
        aColor = QCOLOR_TO_NS_RGB(palette.color(QPalette::Normal, QPalette::Window));
        break;
    case ColorID::TextForeground:
        // not used?
        aColor = QCOLOR_TO_NS_RGB(palette.color(QPalette::Normal, QPalette::WindowText));
        break;
    case ColorID::TextSelectBackground:
    case ColorID::IMESelectedRawTextBackground:
    case ColorID::IMESelectedConvertedTextBackground:
        // still used
        aColor = QCOLOR_TO_NS_RGB(palette.color(QPalette::Normal, QPalette::Highlight));
        break;
    case ColorID::TextSelectForeground:
    case ColorID::IMESelectedRawTextForeground:
    case ColorID::IMESelectedConvertedTextForeground:
        // still used
        aColor = QCOLOR_TO_NS_RGB(palette.color(QPalette::Normal, QPalette::HighlightedText));
        break;
    case ColorID::IMERawInputBackground:
    case ColorID::IMEConvertedTextBackground:
        aColor = NS_TRANSPARENT;
        break;
    case ColorID::IMERawInputForeground:
    case ColorID::IMEConvertedTextForeground:
        aColor = NS_SAME_AS_FOREGROUND_COLOR;
        break;
    case ColorID::IMERawInputUnderline:
    case ColorID::IMEConvertedTextUnderline:
        aColor = NS_SAME_AS_FOREGROUND_COLOR;
        break;
    case ColorID::IMESelectedRawTextUnderline:
    case ColorID::IMESelectedConvertedTextUnderline:
        aColor = NS_TRANSPARENT;
        break;
    case ColorID::SpellCheckerUnderline:
        aColor = RED_COLOR;
        break;

        // css2  http://www.w3.org/TR/REC-CSS2/ui.html#system-colors
    case ColorID::Activeborder:
        // active window border
        aColor = QCOLOR_TO_NS_RGB(palette.color(QPalette::Normal, QPalette::Window));
        break;
    case ColorID::Activecaption:
        // active window caption background
        aColor = QCOLOR_TO_NS_RGB(palette.color(QPalette::Normal, QPalette::Window));
        break;
    case ColorID::Appworkspace:
        // MDI background color
        aColor = QCOLOR_TO_NS_RGB(palette.color(QPalette::Normal, QPalette::Window));
        break;
    case ColorID::Background:
        // desktop background
        aColor = QCOLOR_TO_NS_RGB(palette.color(QPalette::Normal, QPalette::Window));
        break;
    case ColorID::Captiontext:
        // text in active window caption, size box, and scrollbar arrow box (!)
        aColor = QCOLOR_TO_NS_RGB(palette.color(QPalette::Normal, QPalette::Text));
        break;
    case ColorID::Graytext:
        // disabled text in windows, menus, etc.
        aColor = QCOLOR_TO_NS_RGB(palette.color(QPalette::Disabled, QPalette::Text));
        break;
    case ColorID::Highlight:
        // background of selected item
        aColor = QCOLOR_TO_NS_RGB(palette.color(QPalette::Normal, QPalette::Highlight));
        break;
    case ColorID::Highlighttext:
        // text of selected item
        aColor = QCOLOR_TO_NS_RGB(palette.color(QPalette::Normal, QPalette::HighlightedText));
        break;
    case ColorID::Inactiveborder:
        // inactive window border
        aColor = QCOLOR_TO_NS_RGB(palette.color(QPalette::Disabled, QPalette::Window));
        break;
    case ColorID::Inactivecaption:
        // inactive window caption
        aColor = QCOLOR_TO_NS_RGB(palette.color(QPalette::Disabled, QPalette::Window));
        break;
    case ColorID::Inactivecaptiontext:
        // text in inactive window caption
        aColor = QCOLOR_TO_NS_RGB(palette.color(QPalette::Disabled, QPalette::Text));
        break;
    case ColorID::Infobackground:
        // tooltip background color
        aColor = QCOLOR_TO_NS_RGB(palette.color(QPalette::Normal, QPalette::ToolTipBase));
        break;
    case ColorID::Infotext:
        // tooltip text color
        aColor = QCOLOR_TO_NS_RGB(palette.color(QPalette::Normal, QPalette::ToolTipText));
        break;
    case ColorID::Menu:
        // menu background
        aColor = QCOLOR_TO_NS_RGB(palette.color(QPalette::Normal, QPalette::Window));
        break;
    case ColorID::Menutext:
        // menu text
        aColor = QCOLOR_TO_NS_RGB(palette.color(QPalette::Normal, QPalette::Text));
        break;
    case ColorID::Scrollbar:
        // scrollbar gray area
        aColor = QCOLOR_TO_NS_RGB(palette.color(QPalette::Normal, QPalette::Mid));
        break;

    case ColorID::Threedface:
    case ColorID::Buttonface:
        // 3-D face color
        aColor = QCOLOR_TO_NS_RGB(palette.color(QPalette::Normal, QPalette::Button));
        break;

    case ColorID::Buttontext:
        // text on push buttons
        aColor = QCOLOR_TO_NS_RGB(palette.color(QPalette::Normal, QPalette::ButtonText));
        break;

    case ColorID::Buttonhighlight:
        // 3-D highlighted edge color
    case ColorID::Threedhighlight:
        // 3-D highlighted outer edge color
        aColor = QCOLOR_TO_NS_RGB(palette.color(QPalette::Normal, QPalette::Dark));
        break;

    case ColorID::Threedlightshadow:
        // 3-D highlighted inner edge color
        aColor = QCOLOR_TO_NS_RGB(palette.color(QPalette::Normal, QPalette::Light));
        break;

    case ColorID::Buttonshadow:
        // 3-D shadow edge color
    case ColorID::Threedshadow:
        // 3-D shadow inner edge color
        aColor = QCOLOR_TO_NS_RGB(palette.color(QPalette::Normal, QPalette::Dark));
        break;

    case ColorID::Threeddarkshadow:
        // 3-D shadow outer edge color
        aColor = QCOLOR_TO_NS_RGB(palette.color(QPalette::Normal, QPalette::Shadow));
        break;

    case ColorID::Window:
    case ColorID::Windowframe:
        aColor = QCOLOR_TO_NS_RGB(palette.color(QPalette::Normal, QPalette::Window));
        break;

    case ColorID::Windowtext:
        aColor = QCOLOR_TO_NS_RGB(palette.color(QPalette::Normal, QPalette::Text));
        break;

    case ColorID::MozEventreerow:
    case ColorID::Field:
        aColor = QCOLOR_TO_NS_RGB(palette.color(QPalette::Normal, QPalette::Base));
        break;
    case ColorID::Fieldtext:
        aColor = QCOLOR_TO_NS_RGB(palette.color(QPalette::Normal, QPalette::Text));
        break;
    case ColorID::MozDialog:
        aColor = QCOLOR_TO_NS_RGB(palette.color(QPalette::Normal, QPalette::Window));
        break;
    case ColorID::MozDialogtext:
        aColor = QCOLOR_TO_NS_RGB(palette.color(QPalette::Normal, QPalette::WindowText));
        break;
    case ColorID::MozDragtargetzone:
        aColor = QCOLOR_TO_NS_RGB(palette.color(QPalette::Normal, QPalette::Window));
        break;
    case ColorID::MozButtondefault:
        // default button border color
        aColor = QCOLOR_TO_NS_RGB(palette.color(QPalette::Normal, QPalette::Button));
        break;
    case ColorID::MozButtonhoverface:
        aColor = BG_PRELIGHT_COLOR;
        break;
    case ColorID::MozButtonhovertext:
        aColor = FG_PRELIGHT_COLOR;
        break;
    case ColorID::MozCellhighlight:
    case ColorID::MozHtmlCellhighlight:
        aColor = QCOLOR_TO_NS_RGB(palette.color(QPalette::Normal, QPalette::Highlight));
        break;
    case ColorID::MozCellhighlighttext:
    case ColorID::MozHtmlCellhighlighttext:
        aColor = QCOLOR_TO_NS_RGB(palette.color(QPalette::Normal, QPalette::HighlightedText));
        break;
    case ColorID::MozMenuhover:
        aColor = BG_PRELIGHT_COLOR;
        break;
    case ColorID::MozMenuhovertext:
        aColor = QCOLOR_TO_NS_RGB(palette.color(QPalette::Normal, QPalette::Text));
        break;
    case ColorID::MozOddtreerow:
        aColor = NS_TRANSPARENT;
        break;
    case ColorID::MozNativehyperlinktext:
        aColor = NS_SAME_AS_FOREGROUND_COLOR;
        break;
    case ColorID::MozComboboxtext:
        aColor = QCOLOR_TO_NS_RGB(palette.color(QPalette::Normal, QPalette::Text));
        break;
    case ColorID::MozCombobox:
        aColor = QCOLOR_TO_NS_RGB(palette.color(QPalette::Normal, QPalette::Base));
        break;
    case ColorID::MozMenubartext:
        aColor = QCOLOR_TO_NS_RGB(palette.color(QPalette::Normal, QPalette::Text));
        break;
    case ColorID::MozMenubarhovertext:
        aColor = QCOLOR_TO_NS_RGB(palette.color(QPalette::Normal, QPalette::Text));
        break;
    default:
        /* default color is BLACK */
        aColor = 0;
        rv = NS_ERROR_FAILURE;
        break;
    }

    return rv;
}

nsresult
nsLookAndFeel::GetIntImpl(IntID aID, int32_t &aResult)
{
    nsresult rv = nsXPLookAndFeel::GetIntImpl(aID, aResult);
    // Make an exception for eIntID_SystemUsesDarkTheme as this is handled below
    if (NS_SUCCEEDED(rv) && ((aID != eIntID_SystemUsesDarkTheme) || (aResult != 2)))
        return rv;

    rv = NS_OK;

    switch (aID) {
        case eIntID_CaretBlinkTime:
            aResult = 500;
            break;

        case eIntID_CaretWidth:
            aResult = 1;
            break;

        case eIntID_ShowCaretDuringSelection:
            aResult = 0;
            break;

        case eIntID_SelectTextfieldsOnKeyFocus:
            // Select textfield content when focused by kbd
            // used by EventStateManager::sTextfieldSelectModel
            aResult = 1;
            break;

        case eIntID_SubmenuDelay:
            aResult = 200;
            break;

        case eIntID_TooltipDelay:
            aResult = 500;
            break;

        case eIntID_MenusCanOverlapOSBar:
            // we want XUL popups to be able to overlap the task bar.
            aResult = 1;
            break;

        case eIntID_ScrollArrowStyle:
            aResult = eScrollArrowStyle_Single;
            break;

        case eIntID_ScrollSliderStyle:
            aResult = eScrollThumbStyle_Proportional;
            break;

        case eIntID_TouchEnabled:
            aResult = 1;
            break;

        case eIntID_WindowsDefaultTheme:
        case eIntID_WindowsThemeIdentifier:
        case eIntID_OperatingSystemVersionIdentifier:
            aResult = 0;
            rv = NS_ERROR_NOT_IMPLEMENTED;
            break;

        case eIntID_IMERawInputUnderlineStyle:
        case eIntID_IMEConvertedTextUnderlineStyle:
            aResult = NS_STYLE_TEXT_DECORATION_STYLE_SOLID;
            break;

        case eIntID_IMESelectedRawTextUnderlineStyle:
        case eIntID_IMESelectedConvertedTextUnderline:
            aResult = NS_STYLE_TEXT_DECORATION_STYLE_NONE;
            break;

        case eIntID_SpellCheckerUnderlineStyle:
            aResult = NS_STYLE_TEXT_DECORATION_STYLE_WAVY;
            break;

        case eIntID_ScrollbarButtonAutoRepeatBehavior:
            aResult = 0;
            break;

        case eIntID_ContextMenuOffsetVertical:
        case eIntID_ContextMenuOffsetHorizontal:
            aResult = 2;
            break;

        case eIntID_SystemUsesDarkTheme:
            // Choose theme based on ambience
            aResult = mObserver->GetDarkAmbience() ? 1 : 0;
            break;

        default:
            aResult = 0;
            rv = NS_ERROR_FAILURE;
    }

    return rv;
}

nsresult
nsLookAndFeel::GetFloatImpl(FloatID aID, float &aResult)
{
  nsresult res = nsXPLookAndFeel::GetFloatImpl(aID, aResult);
  if (NS_SUCCEEDED(res))
    return res;
  res = NS_OK;

  switch (aID) {
    case eFloatID_IMEUnderlineRelativeSize:
        aResult = 1.0f;
        break;
    case eFloatID_SpellCheckerUnderlineRelativeSize:
        aResult = 1.0f;
        break;
    default:
        aResult = -1.0;
        res = NS_ERROR_FAILURE;
    }
  return res;
}

/*virtual*/
bool
nsLookAndFeel::GetFontImpl(FontID aID, nsString& aFontName,
                           gfxFontStyle& aFontStyle)
{
  QFont qFont = QGuiApplication::font();

  NS_NAMED_LITERAL_STRING(quote, "\"");
  nsString family((char16_t*)qFont.family().data());
  aFontName = quote + family + quote;

  aFontStyle.systemFont = true;
  switch (qFont.style()) {
      case QFont::StyleNormal:
          aFontStyle.style = mozilla::FontSlantStyle::Normal();
          break;
      case QFont::StyleItalic:
          aFontStyle.style = mozilla::FontSlantStyle::Italic();
          break;
      case QFont::StyleOblique:
          aFontStyle.style = mozilla::FontSlantStyle::Oblique();
          break;
      default:
          aFontStyle.style = mozilla::FontSlantStyle::Normal();
          break;
  }

  aFontStyle.weight = mozilla::FontWeight::Normal();
  aFontStyle.stretch = mozilla::FontStretch((float)qFont.stretch());
  // Scaling to CSS pixels needed in esr78 (323b5be77a649)
  float scaleFactor = mozilla::StaticPrefs::layout_css_devPixelsPerPx();
  // use pixel size directly if it is set, otherwise compute from point size
  if (qFont.pixelSize() != -1) {
    aFontStyle.size = qFont.pixelSize() / scaleFactor;
  } else {
    aFontStyle.size = qFont.pointSizeF() * qApp->primaryScreen()->logicalDotsPerInch() / 72.0f / scaleFactor;
  }

  return true;
}

/*virtual*/
bool
nsLookAndFeel::GetEchoPasswordImpl() {
    return true;
}

/*virtual*/
uint32_t
nsLookAndFeel::GetPasswordMaskDelayImpl()
{
    // Same value on Android framework
    return 1500;
}

/* virtual */
char16_t
nsLookAndFeel::GetPasswordCharacterImpl()
{
    return UNICODE_BULLET;
}
