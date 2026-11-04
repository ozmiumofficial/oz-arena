/*
Oz: the native UI (ENGINE_PLAN.md P4): RmlUi 6 with FreeType, pages in RML and RCSS from the
pk3s (engine/ui -> oz/zz-oz-ui.pk3, under ui/), logic in C++, no scripting language.

- Pages: ui/pages/<name>.rml, or generated (the Settings page, from ui/settings.json). Opening
  the first one takes KEYCATCH_UI and frees the mouse cursor; closing the last gives them back.
- Input: the engine's keys and characters go here while a page is open. The mouse uses the
  system cursor (SE_MOUSE_ABS); hovering a control focuses it, as q3_ui's cursor does. Arrow keys,
  the d-pad and the left stick move the focus by RCSS nav properties; A activates, B (Escape,
  Backspace, the right mouse button) goes back, Y is the Godot menu's social key, the shoulder
  buttons switch tabs.
- Sounds: sound/misc/menu1-4.wav, when ui_qmenu.c plays them (PlaySound).
- Test hooks: ui_page, ui_capture, ui_focus, ui_key, ui_focuswalk.
*/
#include "oz_ui_internal.h"

#include <RmlUi/Debugger.h>
#include <RmlUi/Core/Factory.h>
#include <RmlUi/Core/StyleSheetContainer.h>

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <deque>
#include <set>
#include <vector>

namespace OzUI {

void Printf( const char *fmt, ... ) {
	char text[4096];
	va_list ap;

	va_start( ap, fmt );
	vsnprintf( text, sizeof( text ), fmt, ap );
	va_end( ap );
	OzE_Print( text );
}

/*
================
SystemInterface: time, the log, paths and the clipboard
================
*/
class SystemInterface : public Rml::SystemInterface {
public:
	double GetElapsedTime() override {
		return OzE_Milliseconds() * 0.001;
	}

	bool LogMessage( Rml::Log::Type type, const Rml::String &message ) override {
		if ( type == Rml::Log::LT_ERROR || type == Rml::Log::LT_ASSERT ) {
			Printf( "^1UI: %s\n", message.c_str() );
		} else if ( type == Rml::Log::LT_WARNING ) {
			Printf( "^3UI: %s\n", message.c_str() );
		} else {
			OzE_DPrint( ( "UI: " + message + "\n" ).c_str() );
		}
		return true;
	}

	// "/x" is from the pk3s' root, anything else beside the page; the file system knows no "." or ".."
	void JoinPath( Rml::String &translated, const Rml::String &document, const Rml::String &path ) override {
		Rml::String joined;
		if ( !path.empty() && path[0] == '/' ) {
			joined = path.substr( 1 );
		} else {
			const size_t slash = document.rfind( '/' );
			joined = ( slash == Rml::String::npos ? Rml::String() : document.substr( 0, slash + 1 ) ) + path;
		}
		std::vector<Rml::String> parts;
		size_t start = 0;
		while ( start <= joined.size() ) {
			size_t end = joined.find( '/', start );
			if ( end == Rml::String::npos ) {
				end = joined.size();
			}
			const Rml::String part = joined.substr( start, end - start );
			if ( part == ".." ) {
				if ( !parts.empty() ) {
					parts.pop_back();
				}
			} else if ( !part.empty() && part != "." ) {
				parts.push_back( part );
			}
			start = end + 1;
		}
		translated.clear();
		for ( size_t i = 0; i < parts.size(); i++ ) {
			translated += ( i ? "/" : "" ) + parts[i];
		}
	}

	void GetClipboardText( Rml::String &text ) override {
		char *clip = OzE_GetClipboard();
		text = clip ? clip : "";
		if ( clip ) {
			OzE_Free( clip );
		}
	}
};

/*
================
FileInterface: the engine's file system, so pages, styles, fonts and images come from the pk3s
================
*/
class FileInterface : public Rml::FileInterface {
	struct File {
		char	*data;
		size_t	size, pos;
	};

public:
	Rml::FileHandle Open( const Rml::String &path ) override {
		const char *p = path.c_str();
		void *buffer = nullptr;
		while ( *p == '/' ) {
			p++;
		}
		const int len = OzE_ReadFile( p, &buffer );
		if ( len < 0 ) {
			return 0;
		}
		return (Rml::FileHandle) new File{ (char *)buffer, (size_t)len, 0 };
	}

	void Close( Rml::FileHandle handle ) override {
		File *f = (File *)handle;
		OzE_FreeFile( f->data );
		delete f;
	}

	size_t Read( void *buffer, size_t size, Rml::FileHandle handle ) override {
		File *f = (File *)handle;
		const size_t n = size < f->size - f->pos ? size : f->size - f->pos;
		memcpy( buffer, f->data + f->pos, n );
		f->pos += n;
		return n;
	}

	bool Seek( Rml::FileHandle handle, long offset, int origin ) override {
		File *f = (File *)handle;
		const long base = origin == SEEK_SET ? 0 : origin == SEEK_CUR ? (long)f->pos : (long)f->size;
		if ( base + offset < 0 || base + offset > (long)f->size ) {
			return false;
		}
		f->pos = (size_t)( base + offset );
		return true;
	}

	size_t Tell( Rml::FileHandle handle ) override {
		return ( (File *)handle )->pos;
	}

	size_t Length( Rml::FileHandle handle ) override {
		return ( (File *)handle )->size;
	}
};

static SystemInterface systemInterface;
static FileInterface fileInterface;
static Rml::Context *context;
static bool initialized, commandsAdded, debugger, warnedRenderer, pagesLoaded;
static bool emojiFont;		// Segoe UI Emoji is a fallback face

Rml::Context *GetContext() {
	return context;
}

/*
================
Sounds, as ui_qmenu.c plays them:
  in    after a new page's first frame (UI_Refresh's m_entersound)
  move  the focus moves to another control (keys, d-pad or hover), a control is activated, a value changes
  out   a page closes (Escape, Backspace, the right mouse button, B)
  buzz  a slider pushed past its end, a refused action
================
*/
static bool quiet;		// programmatic focus is silent
static bool inPending;	// the in sound, after the next frame
static bool mouseMoving;	// inside ProcessMouseMove: only a moving cursor focuses what it hovers

void PlaySound( Sound sound ) {
	static const char *const names[] = { "sound/misc/menu1.wav", "sound/misc/menu2.wav", "sound/misc/menu3.wav", "sound/misc/menu4.wav" };

	if ( sound == Sound::Move && quiet ) {
		return;
	}
	if ( atoi( OzE_CvarString( "ui_logSounds" ) ) ) {	// for engine/tests/ui_test.py
		Printf( "ui sound: %s\n", names[(int)sound] );
	}
	OzE_StartLocalSound( names[(int)sound] );
}

void FocusQuietly( Rml::Element *element ) {
	const bool was = quiet;
	quiet = true;
	if ( element ) {
		element->Focus();
	}
	quiet = was;
}

static bool Focusable( Rml::Element *e ) {
	return e && e->GetComputedValues().tab_index() == Rml::Style::TabIndex::Auto && e->GetComputedValues().focus() != Rml::Style::Focus::None;
}

// controls that take Left and Right themselves
static bool TakesLeftRight( Rml::Element *e ) {
	if ( !e ) {
		return false;
	}
	const Rml::String tag = e->GetTagName(), type = e->GetAttribute<Rml::String>( "type", "" );
	return ( tag == "input" && ( type == "range" || type == "text" || type == "password" ) ) || tag == "textarea" || e->IsClassSet( "spinner" ) || e->IsClassSet( "toggle" );
}

static bool Typing() {
	Rml::Element *e = context ? context->GetFocusElement() : nullptr;
	if ( !e ) {
		return false;
	}
	const Rml::String tag = e->GetTagName(), type = e->GetAttribute<Rml::String>( "type", "text" );
	return tag == "textarea" || ( tag == "input" && ( type == "text" || type == "password" ) );
}

/*
================
PageListener

On every page, in the capture phase: the generic sounds, and hover focusing.
================
*/
class PageListener : public Rml::EventListener {
public:
	void ProcessEvent( Rml::Event &event ) override {
		Rml::Element *target = event.GetTargetElement();

		switch ( event.GetId() ) {
		case Rml::EventId::Focus:
			if ( target && target != target->GetOwnerDocument() && Focusable( target ) ) {
				PlaySound( Sound::Move );
			}
			break;

		case Rml::EventId::Mouseover:
			// hovering a control focuses it (q3_ui's Menu_SetCursor from UI_MouseEvent), when the
			// cursor moves; a list scrolling under a still cursor leaves the focus where it is
			if ( !mouseMoving ) {
				break;
			}
			for ( Rml::Element *e = target; e; e = e->GetParentNode() ) {
				if ( Focusable( e ) ) {
					if ( context->GetFocusElement() != e ) {
						e->Focus();
					}
					break;
				}
			}
			break;

		case Rml::EventId::Click:
			if ( target && Focusable( target ) && target->GetTagName() != "input" ) {
				PlaySound( Sound::Move );	// Menu_ActivateItem
			} else if ( target && target->GetTagName() == "input" && target->GetAttribute<Rml::String>( "type", "" ) == "checkbox" ) {
				PlaySound( Sound::Move );
			}
			break;

		case Rml::EventId::Keydown: {
			const int key = event.GetParameter<int>( "key_identifier", 0 );
			if ( ( key != Rml::Input::KI_LEFT && key != Rml::Input::KI_RIGHT ) || !target ) {
				break;
			}
			if ( target->GetTagName() == "input" && target->GetAttribute<Rml::String>( "type", "" ) == "range" ) {
				// Slider_Key: a step, or a buzz at the end
				const float value = target->GetAttribute<float>( "value", 0.0f );
				const float min = target->GetAttribute<float>( "min", 0.0f ), max = target->GetAttribute<float>( "max", 100.0f );
				const bool atEnd = key == Rml::Input::KI_LEFT ? value <= min : value >= max;
				PlaySound( atEnd ? Sound::Buzz : Sound::Move );
			} else if ( target->IsClassSet( "spinner" ) || target->IsClassSet( "toggle" ) ) {
				PlaySound( Sound::Move );	// SpinControl_Key and RadioButton_Key wrap and toggle
			}
			break;
		}
		default:
			break;
		}
	}
};

static PageListener pageListener;

/*
================
Pages
================
*/
// a page drawn over the one below, which stays (a panel over the main menu, a confirmation);
// any other page replaces the one below until it closes
#define PAGE_OVERLAY	1
// a page of the menus (P5): its own data model and actions (oz_ui_menu.cpp)
#define PAGE_MENU		2

struct Page {
	const char				*name;
	const char				*file;				// in the pk3s, or NULL for a generated page
	Rml::String				(*generate)( void );
	int						flags;
	Rml::ElementDocument	*doc;
};

static Page pages[] = {
	{ "test", "ui/pages/test.rml", nullptr, 0, nullptr },
	{ "uifx", "ui/pages/uifx.rml", nullptr, 0, nullptr },
	{ "settings", nullptr, Settings_PageRml, PAGE_OVERLAY, nullptr },
	{ "splash", "ui/pages/splash.rml", nullptr, PAGE_MENU, nullptr },
	{ "menu", "ui/pages/menu.rml", nullptr, PAGE_MENU, nullptr },
	{ "quit", "ui/pages/quit.rml", nullptr, PAGE_MENU | PAGE_OVERLAY, nullptr },
	{ "error", "ui/pages/error.rml", nullptr, PAGE_MENU | PAGE_OVERLAY, nullptr },
	{ "display_confirm", "ui/pages/display_confirm.rml", nullptr, PAGE_OVERLAY, nullptr },
	{ "apply_confirm", "ui/pages/apply_confirm.rml", nullptr, PAGE_OVERLAY, nullptr },
	{ "color_picker", "ui/pages/color_picker.rml", nullptr, PAGE_OVERLAY, nullptr },
	{ "friends", "ui/pages/friends.rml", nullptr, PAGE_MENU | PAGE_OVERLAY, nullptr },
	{ "quick_join", "ui/pages/quick_join.rml", nullptr, PAGE_MENU | PAGE_OVERLAY, nullptr },
	{ "ranked", "ui/pages/ranked.rml", nullptr, PAGE_MENU | PAGE_OVERLAY, nullptr },
	{ "server_browser", "ui/pages/server_browser.rml", nullptr, PAGE_MENU | PAGE_OVERLAY, nullptr },
	{ "dedicated", "ui/pages/dedicated.rml", nullptr, PAGE_MENU | PAGE_OVERLAY, nullptr },
	{ "offline_bots", "ui/pages/offline_bots.rml", nullptr, PAGE_MENU | PAGE_OVERLAY, nullptr },
	{ "mods", "ui/pages/mods.rml", nullptr, PAGE_MENU | PAGE_OVERLAY, nullptr },	// PH step 2: the Mod Library (oz_ui_mods.cpp)
	{ "create_match", "ui/pages/create_match.rml", nullptr, PAGE_MENU | PAGE_OVERLAY, nullptr },
	{ "campaign", "ui/pages/campaign.rml", nullptr, PAGE_MENU | PAGE_OVERLAY, nullptr },
	{ "training", "ui/pages/training.rml", nullptr, PAGE_MENU | PAGE_OVERLAY, nullptr },
	{ "profile", "ui/pages/profile.rml", nullptr, PAGE_MENU | PAGE_OVERLAY, nullptr },
	{ "leaderboards", "ui/pages/leaderboards.rml", nullptr, PAGE_MENU | PAGE_OVERLAY, nullptr },
	{ "replays", "ui/pages/replays.rml", nullptr, PAGE_MENU | PAGE_OVERLAY, nullptr },
	{ "cinematics", "ui/pages/cinematics.rml", nullptr, PAGE_MENU | PAGE_OVERLAY, nullptr },
	{ "credits", "ui/pages/credits.rml", nullptr, PAGE_MENU | PAGE_OVERLAY, nullptr },
	{ "pause", "ui/pages/pause.rml", nullptr, PAGE_MENU, nullptr },
	{ "connect", "ui/pages/connect.rml", nullptr, PAGE_MENU, nullptr },
	{ "crosshair", "ui/pages/crosshair.rml", nullptr, PAGE_MENU | PAGE_OVERLAY, nullptr },
	{ "editor", "ui/pages/editor.rml", nullptr, 0, nullptr },	// P13: the map editor (oz_ui_editor.cpp)
};

// always on top while any page is open, never focused: the toasts (Chrome.tsx's Toasts)
static Rml::ElementDocument *toastsDoc;

static std::vector<Page *> stack;

static Page *FindPage( const char *name ) {
	for ( Page &p : pages ) {
		if ( !StrICmp( p.name, name ) ) {
			return &p;
		}
	}
	return nullptr;
}

/*
================
Settings > Accessibility: each page's access sheet (engine/tools/menu/gen_access.py) is merged into
it as it loads, and its body gets the classes the settings set: hc (High-contrast menus) and calm
(Reduced flashing).
================
*/
static int accessClasses = -1;

static void SetAccessClasses( Rml::ElementDocument *doc, int classes ) {
	doc->SetClass( "hc", ( classes & 1 ) != 0 );
	doc->SetClass( "calm", ( classes & 2 ) != 0 );
}

static void AddAccessSheet( Rml::ElementDocument *doc, const char *name ) {
	const Rml::String file = Rml::String( "ui/rcss/access/" ) + name + ".rcss";
	if ( OzE_FileExists( file.c_str() ) && doc->GetStyleSheetContainer() ) {
		const Rml::SharedPtr<Rml::StyleSheetContainer> sheet = Rml::Factory::InstanceStyleSheetFile( file );
		if ( sheet ) {
			doc->SetStyleSheetContainer( doc->GetStyleSheetContainer()->CombineStyleSheetContainer( *sheet ) );
		}
	}
	SetAccessClasses( doc, accessClasses < 0 ? 0 : accessClasses );
}

static void AccessFrame() {
	const int classes = ( atoi( OzE_CvarString( "oz_accessibility_high_contrast" ) ) ? 1 : 0 )
		| ( atoi( OzE_CvarString( "oz_accessibility_reduced_flashing" ) ) ? 2 : 0 );
	if ( classes == accessClasses ) {
		return;
	}
	accessClasses = classes;
	for ( Page &p : pages ) {
		if ( p.doc ) {
			SetAccessClasses( p.doc, classes );
		}
	}
	if ( toastsDoc ) {
		SetAccessClasses( toastsDoc, classes );
	}
}

static bool LoadPage( Page *p ) {
	if ( p->doc ) {
		return true;
	}
	if ( p->generate ) {
		p->doc = context->LoadDocumentFromMemory( p->generate(), Rml::String( "ui/pages/" ) + p->name + ".rml" );
	} else {
		p->doc = context->LoadDocument( p->file );
	}
	if ( !p->doc ) {
		Printf( "^3UI: page %s didn't load\n", p->name );
		return false;
	}
	AddAccessSheet( p->doc, p->name );
	p->doc->AddEventListener( Rml::EventId::Focus, &pageListener, true );
	p->doc->AddEventListener( Rml::EventId::Mouseover, &pageListener, true );
	p->doc->AddEventListener( Rml::EventId::Click, &pageListener, true );
	p->doc->AddEventListener( Rml::EventId::Keydown, &pageListener, true );
	if ( !strcmp( p->name, "settings" ) ) {
		Settings_Attach( p->doc );
	}
	if ( p->flags & PAGE_MENU ) {
		Menu_Attach( p->name, p->doc );
	}
	return true;
}

// keep: back to a page that was covered, its focus where it was; else its first or autofocus control.
// An overlay is modal, so the focus stays in it.
static void ShowPage( Page *p, bool keep ) {
	quiet = true;
	p->doc->Show( ( p->flags & PAGE_OVERLAY ) ? Rml::ModalFlag::Modal : Rml::ModalFlag::None, keep ? Rml::FocusFlag::Keep : Rml::FocusFlag::Auto );
	quiet = false;
}

static int splashTime;	// when the splash opened

static void Covered() {
	Menu_Covered( stack.empty() ? "" : stack.back()->name );
	if ( toastsDoc && !stack.empty() ) {
		toastsDoc->Show( Rml::ModalFlag::None, Rml::FocusFlag::None );
		toastsDoc->PullToFront();
	} else if ( toastsDoc ) {
		toastsDoc->Hide();
	}
}

static bool OpenPage( Page *p ) {
	if ( !OzE_Renderer() ) {
		Printf( "^3UI: the renderer can't draw the native UI\n" );
		PlaySound( Sound::Buzz );
		return false;
	}
	Renderer().Acquire();	// its images load with the page
	if ( !LoadPage( p ) ) {
		return false;
	}
	for ( Page *open : stack ) {
		if ( open == p ) {
			return true;	// open already (a page can be in the stack once)
		}
	}
	if ( !stack.empty() ) {
		// an overlay keeps the page under it drawn, unless both are panels (the Dedicated page from
		// the Server Browser): then the new panel takes the old one's place until it closes
		const bool dialog = !strcmp( p->name, "friends" ) || !strcmp( p->name, "quit" ) ||
			!strcmp( p->name, "display_confirm" ) || !strcmp( p->name, "apply_confirm" ) || !strcmp( p->name, "color_picker" );	// over Settings
		const bool base = !strcmp( stack.back()->name, "menu" ) || !strcmp( stack.back()->name, "pause" );
		if ( !( p->flags & PAGE_OVERLAY ) || ( !dialog && !base && ( stack.back()->flags & PAGE_OVERLAY ) ) ) {
			stack.back()->doc->Hide();
		}
	} else {
		OzE_CatchKeys( 1 );
	}
	stack.push_back( p );
	Menu_Opened( p->name );
	ShowPage( p, false );
	p->doc->PullToFront();
	p->doc->SetClass( "shown", true );	// starts the page's entrance animations (body.shown ...)
	if ( !strcmp( p->name, "editor" ) ) {
		Editor_Opened( p->doc );
	}
	if ( !strcmp( p->name, "splash" ) ) {
		splashTime = OzE_Milliseconds();
	}
	Covered();
	inPending = true;
	return true;
}

static void ClosePage( bool sound ) {
	if ( stack.empty() ) {
		return;
	}
	stack.back()->doc->Hide();
	stack.back()->doc->SetClass( "shown", false );
	stack.pop_back();
	if ( sound ) {
		PlaySound( Sound::Out );	// UI_PopMenu
	}
	if ( stack.empty() ) {
		OzE_CatchKeys( 0 );
	} else {
		ShowPage( stack.back(), true );	// its focus where it was
	}
	Covered();
}

static void CloseAll() {
	while ( !stack.empty() ) {
		ClosePage( false );
	}
}

static Rml::ElementDocument *TopDoc() {
	return stack.empty() ? nullptr : stack.back()->doc;
}

bool OpenPageByName( const char *name ) {
	Page *p = FindPage( name );
	if ( !p ) {
		Printf( "^3UI: no page %s\n", name );
		PlaySound( Sound::Buzz );
		return false;
	}
	return OpenPage( p );
}

static void Back();

void GoBack() {
	Back();
}

void CloseAllPages() {
	CloseAll();
}

bool IsPageOpen( const char *name ) {
	for ( const Page *p : stack ) {
		if ( !strcmp( p->name, name ) ) {
			return true;
		}
	}
	return false;
}

const char *TopPageName() {
	return stack.empty() ? "" : stack.back()->name;
}

Rml::ElementDocument *TopDocument() {
	return TopDoc();
}

/*
================
Input
================
*/
static int Modifiers() {
	int m = 0;
	if ( OzE_KeyDown( K_SHIFT ) ) m |= Rml::Input::KM_SHIFT;
	if ( OzE_KeyDown( K_CTRL ) ) m |= Rml::Input::KM_CTRL;
	if ( OzE_KeyDown( K_ALT ) ) m |= Rml::Input::KM_ALT;
	return m;
}

static Rml::Input::KeyIdentifier MapKey( int key ) {
	using namespace Rml::Input;

	if ( key >= 'a' && key <= 'z' ) return (KeyIdentifier)( KI_A + key - 'a' );
	if ( key >= '0' && key <= '9' ) return (KeyIdentifier)( KI_0 + key - '0' );
	if ( key >= K_F1 && key <= K_F12 ) return (KeyIdentifier)( KI_F1 + key - K_F1 );
	switch ( key ) {
	case K_TAB: return KI_TAB;
	case K_ENTER: return KI_RETURN;
	case K_KP_ENTER: return KI_NUMPADENTER;
	case K_SPACE: return KI_SPACE;
	case K_BACKSPACE: return KI_BACK;
	case K_DEL: case K_KP_DEL: return KI_DELETE;
	case K_INS: return KI_INSERT;
	case K_HOME: case K_KP_HOME: return KI_HOME;
	case K_END: case K_KP_END: return KI_END;
	case K_PGUP: case K_KP_PGUP: return KI_PRIOR;
	case K_PGDN: case K_KP_PGDN: return KI_NEXT;
	case K_UPARROW: case K_KP_UPARROW: case K_PAD0_DPAD_UP: case K_PAD0_LEFTSTICK_UP: return KI_UP;
	case K_DOWNARROW: case K_KP_DOWNARROW: case K_PAD0_DPAD_DOWN: case K_PAD0_LEFTSTICK_DOWN: return KI_DOWN;
	case K_LEFTARROW: case K_KP_LEFTARROW: case K_PAD0_DPAD_LEFT: case K_PAD0_LEFTSTICK_LEFT: return KI_LEFT;
	case K_RIGHTARROW: case K_KP_RIGHTARROW: case K_PAD0_DPAD_RIGHT: case K_PAD0_LEFTSTICK_RIGHT: return KI_RIGHT;
	case K_PAD0_A: case K_PAD0_START: return KI_RETURN;
	default: return KI_UNKNOWN;
	}
}

static bool PadNavKey( int key ) {
	return ( key >= K_PAD0_DPAD_UP && key <= K_PAD0_DPAD_RIGHT ) || ( key >= K_PAD0_LEFTSTICK_LEFT && key <= K_PAD0_LEFTSTICK_DOWN );
}

// the pads send no repeats of their own: a held direction repeats as a held arrow key does
static int padHeld, padNextRepeat;

static void Back() {
	// Escape first, so an open select box closes instead of the page
	if ( context->ProcessKeyDown( Rml::Input::KI_ESCAPE, Modifiers() ) && !stack.empty() && !Menu_Back( stack.back()->name ) ) {
		ClosePage( true );
	}
	context->ProcessKeyUp( Rml::Input::KI_ESCAPE, Modifiers() );
}

static void SwitchTab( int dir ) {
	if ( !stack.empty() && !strcmp( stack.back()->name, "settings" ) && Settings_Tab( dir ) ) {
		PlaySound( Sound::Move );
	} else if ( !stack.empty() && ( stack.back()->flags & PAGE_MENU ) && Menu_Tab( stack.back()->doc, dir ) ) {
		PlaySound( Sound::Move );
	} else {
		PlaySound( Sound::Buzz );
	}
}

// the splash goes on after 1.5 s with any key, click or pad button (splash.gd's SKIP_AFTER)
static bool SplashKey( int key, bool down ) {
	if ( stack.empty() || strcmp( stack.back()->name, "splash" ) ) {
		return false;
	}
	if ( down && OzE_Milliseconds() - splashTime >= 1500 && key != K_MWHEELUP && key != K_MWHEELDOWN ) {
		Menu_SplashDone();
	}
	return true;
}

static void CollectControls( Rml::Element *e, std::vector<Rml::Element *> &out );

// the box with class "modal" an element is in (the Server Browser's server card): the d-pad stays inside it
static Rml::Element *ModalBox( Rml::Element *e ) {
	for ( ; e && e != e->GetOwnerDocument(); e = e->GetParentNode() ) {
		if ( e->IsClassSet( "modal" ) ) {
			return e;
		}
	}
	return nullptr;
}

/*
The menus' pages: when RmlUi's nav leaves the focus where it was (it doesn't cross a scrolling
list's edge, and a data-for row has no fixed nav targets), the nearest control on that side, by
the distance between centers with the sideways distance counting double.
*/
static void NavFallback( Rml::Element *before, Rml::Input::KeyIdentifier ki ) {
	using namespace Rml::Input;
	if ( !before || stack.empty() || !( stack.back()->flags & PAGE_MENU ) || context->GetFocusElement() != before
		|| before->GetOwnerDocument() != stack.back()->doc || before == stack.back()->doc ) {
		return;
	}
	const bool horizontal = ki == KI_LEFT || ki == KI_RIGHT;
	if ( ( ki != KI_UP && ki != KI_DOWN && !horizontal ) || ( horizontal && TakesLeftRight( before ) ) ) {
		return;
	}
	const Rml::Vector2f from = before->GetAbsoluteOffset( Rml::BoxArea::Border ) + before->GetBox().GetSize( Rml::BoxArea::Border ) * 0.5f;
	std::vector<Rml::Element *> all;
	CollectControls( stack.back()->doc, all );
	Rml::Element *best = nullptr, *modal = ModalBox( before );
	float bestScore = 1e30f;
	for ( Rml::Element *e : all ) {
		if ( e == before || ModalBox( e ) != modal ) {
			continue;
		}
		const Rml::Vector2f to = e->GetAbsoluteOffset( Rml::BoxArea::Border ) + e->GetBox().GetSize( Rml::BoxArea::Border ) * 0.5f;
		const float dx = to.x - from.x, dy = to.y - from.y;
		const float along = ki == KI_UP ? -dy : ki == KI_DOWN ? dy : ki == KI_LEFT ? -dx : dx;
		const float across = horizontal ? fabsf( dy ) : fabsf( dx );
		if ( along < 2.0f ) {
			continue;
		}
		const float score = along + 2.0f * across;
		if ( score < bestScore ) {
			bestScore = score;
			best = e;
		}
	}
	if ( best ) {
		best->Focus();
		best->ScrollIntoView( Rml::ScrollIntoViewOptions( Rml::ScrollAlignment::Nearest ) );
	}
}

// the scrolling box (overflow-y auto or scroll) an element is in, if any
static Rml::Element *ScrollingBox( Rml::Element *e ) {
	for ( Rml::Element *p = e->GetParentNode(); p && p != p->GetOwnerDocument(); p = p->GetParentNode() ) {
		const Rml::Style::Overflow o = p->GetComputedValues().overflow_y();
		if ( o == Rml::Style::Overflow::Auto || o == Rml::Style::Overflow::Scroll ) {
			return p;
		}
	}
	return nullptr;
}

/*
The menus' pages: Up or Down that RmlUi's nav took past a scrolling list (it doesn't look inside
one: the Server Browser's rows sit between its filters and its foot) lands on the list's nearest
row in view instead, one that shares the focus's column.
*/
static void NavIntoList( Rml::Element *before, Rml::Element *after, Rml::Input::KeyIdentifier ki ) {
	using namespace Rml::Input;
	if ( !before || !after || before == after || ( ki != KI_UP && ki != KI_DOWN ) || stack.empty() || !( stack.back()->flags & PAGE_MENU )
		|| before->GetOwnerDocument() != stack.back()->doc || before == stack.back()->doc || ModalBox( before ) ) {
		return;
	}
	const float sign = ki == KI_DOWN ? 1.0f : -1.0f;
	const Rml::Vector2f bp = before->GetAbsoluteOffset( Rml::BoxArea::Border ), bs = before->GetBox().GetSize( Rml::BoxArea::Border );
	const float from = bp.y + bs.y * 0.5f;
	const float to = after->GetAbsoluteOffset( Rml::BoxArea::Border ).y + after->GetBox().GetSize( Rml::BoxArea::Border ).y * 0.5f;
	std::vector<Rml::Element *> all;
	CollectControls( stack.back()->doc, all );
	Rml::Element *best = nullptr;
	float bestAlong = 1e30f;
	for ( Rml::Element *e : all ) {
		Rml::Element *box = e != before && e != after ? ScrollingBox( e ) : nullptr;
		if ( !box || box == ScrollingBox( before ) ) {
			continue;
		}
		const Rml::Vector2f p = e->GetAbsoluteOffset( Rml::BoxArea::Border ), s = e->GetBox().GetSize( Rml::BoxArea::Border );
		const float top = box->GetAbsoluteOffset( Rml::BoxArea::Padding ).y, y = p.y + s.y * 0.5f;
		const float along = ( y - from ) * sign;
		if ( along < 2.0f || along >= ( to - from ) * sign || y < top || y > top + box->GetClientHeight()	// between, and in view
			|| p.x + s.x <= bp.x || p.x >= bp.x + bs.x ) {	// in the focus's column
			continue;
		}
		if ( along < bestAlong ) {
			bestAlong = along;
			best = e;
		}
	}
	if ( best ) {
		best->Focus();
		best->ScrollIntoView( Rml::ScrollIntoViewOptions( Rml::ScrollAlignment::Nearest ) );
	}
}

static void KeyEvent( int key, bool down ) {
	if ( !context || stack.empty() ) {
		return;
	}
	if ( down && keyHook && keyHook( key ) ) {
		return;
	}
	if ( Editor_Key( key, down ) ) {
		return;	// the map editor's view and keys (oz_ui_editor.cpp)
	}
	if ( SplashKey( key, down ) ) {
		return;
	}
	const int mods = Modifiers();

	if ( key >= K_MOUSE1 && key <= K_MOUSE5 ) {
		if ( key == K_MOUSE2 ) {	// q3_ui: the right button goes back
			if ( down ) {
				Back();
			}
			return;
		}
		const int button = key == K_MOUSE1 ? 0 : key == K_MOUSE3 ? 2 : key - K_MOUSE1;
		if ( down ) {
			context->ProcessMouseButtonDown( button, mods );
		} else {
			context->ProcessMouseButtonUp( button, mods );
		}
		return;
	}
	if ( key == K_MWHEELUP || key == K_MWHEELDOWN ) {
		if ( down ) {
			context->ProcessMouseWheel( key == K_MWHEELUP ? -1.0f : 1.0f, mods );
		}
		return;
	}

	if ( key == K_ESCAPE || key == K_PAD0_B || ( key == K_BACKSPACE && !Typing() ) ) {
		if ( down ) {
			Back();
		}
		return;
	}
	if ( key == K_PAD0_LEFTSHOULDER || key == K_PAD0_RIGHTSHOULDER || ( ( key == 'q' || key == 'e' ) && !Typing() && !mods ) ) {
		if ( down ) {
			SwitchTab( ( key == K_PAD0_RIGHTSHOULDER || key == 'e' ) ? 1 : -1 );
		}
		return;
	}
	if ( key == K_PAD0_Y || ( key == 'f' && !Typing() && !mods && ( stack.back()->flags & PAGE_MENU ) ) ) {	// the friends panel
		if ( down ) {
			Rml::ElementDocument *doc = TopDoc();
			Rml::Dictionary params;
			if ( !doc || doc->DispatchEvent( "social", params ) ) {
				Menu_Social();	// buzzes away from the main menu (the pause menu has no friends panel)
			}
		}
		return;
	}

	const Rml::Input::KeyIdentifier ki = MapKey( key );
	if ( ki == Rml::Input::KI_UNKNOWN ) {
		return;
	}
	if ( PadNavKey( key ) ) {
		padHeld = down ? key : ( padHeld == key ? 0 : padHeld );
		padNextRepeat = OzE_Milliseconds() + 400;
	}
	if ( down ) {
		Rml::Element *before = context->GetFocusElement();
		context->ProcessKeyDown( ki, mods );
		NavIntoList( before, context->GetFocusElement(), ki );
		NavFallback( before, ki );
		Settings_AfterKey();
	} else {
		context->ProcessKeyUp( ki, mods );
	}
}

static void PadRepeat() {
	if ( padHeld && OzE_Milliseconds() >= padNextRepeat ) {
		const Rml::Input::KeyIdentifier ki = MapKey( padHeld );
		context->ProcessKeyDown( ki, 0 );
		Settings_AfterKey();
		context->ProcessKeyUp( ki, 0 );
		padNextRepeat = OzE_Milliseconds() + 110;
	}
}

/*
================
Test model: the test page's data, bound to a C++ struct
================
*/
static struct {
	int				clicks = 0;
	float			level = 50.0f;
	bool			enabled = true;
	Rml::String		name = "Oz";
	Rml::Vector<Rml::String> items = { "Rocket launcher", "Railgun", "Lightning gun" };
	Rml::DataModelHandle handle;
} testModel;

static void BindTestModel() {
	Rml::DataModelConstructor c = context->CreateDataModel( "test" );
	if ( !c ) {
		return;
	}
	c.Bind( "clicks", &testModel.clicks );
	c.Bind( "level", &testModel.level );
	c.Bind( "enabled", &testModel.enabled );
	c.Bind( "name", &testModel.name );
	c.RegisterArray<Rml::Vector<Rml::String>>();
	c.Bind( "items", &testModel.items );
	c.BindEventCallback( "press", []( Rml::DataModelHandle h, Rml::Event &, const Rml::VariantList & ) {
		testModel.clicks++;
		h.DirtyVariable( "clicks" );
	} );
	testModel.handle = c.GetModelHandle();
}

/*
================
ui_capture: a screenshot of the next frames' UI. The screenshot command runs inside the frame,
right after the UI is composited, so the renderer reads that frame (rend2 reads the framebuffer
when the command is executed, opengl and vulkan at the frame's end).
================
*/
static char captureName[128];
static int captureFrames;

static void CaptureTick() {
	char cmd[192];

	if ( !captureName[0] || --captureFrames > 0 ) {
		return;
	}
	const size_t len = strlen( captureName );
	if ( len > 4 && !StrICmp( captureName + len - 4, ".tga" ) ) {
		captureName[len - 4] = '\0';
		snprintf( cmd, sizeof( cmd ), "screenshot %s\n", captureName );
	} else {
		if ( len > 4 && !StrICmp( captureName + len - 4, ".jpg" ) ) {
			captureName[len - 4] = '\0';
		}
		snprintf( cmd, sizeof( cmd ), "screenshotJPEG %s\n", captureName );
	}
	captureName[0] = '\0';
	OzE_ExecuteNow( cmd );
}

/*
================
The focus walk (ui_focuswalk): from a page's first control, every control the d-pad reaches,
direction by direction (a breadth-first search over the navigation graph), against every
visible control of the page. Each settings tab is walked as its own page. Left and Right are
skipped on controls that take them (sliders, spinners, toggles, text), so nothing changes.
================
*/
static void CollectControls( Rml::Element *e, std::vector<Rml::Element *> &out ) {
	if ( !e->IsVisible() ) {
		return;
	}
	if ( Focusable( e ) && !e->IsClassSet( "mouseonly" ) ) {	// mouseonly: a color's hex field (the keys reach its swatch)
		out.push_back( e );
	}
	for ( int i = 0; i < e->GetNumChildren(); i++ ) {
		CollectControls( e->GetChild( i ), out );
	}
}

static Rml::String Describe( Rml::Element *e ) {
	if ( !e->GetId().empty() ) {
		return "#" + e->GetId();
	}
	Rml::String text = e->GetInnerRML();
	if ( text.size() > 24 ) {
		text = text.substr( 0, 24 ) + "...";
	}
	return e->GetTagName() + "(" + text + ")";
}

static bool walkVerbose;	// ui_focuswalk <page> <tab>: every step

// a step of the walk runs inside one frame (a few seconds on the biggest pages): without the window's
// messages taken now and then, Windows shows the game as not responding after 5 s
static void KeepAnswering() {
	static int last;
	const int now = OzE_Milliseconds();
	if ( now - last >= 100 ) {
		last = now;
		OzE_PumpEvents();
	}
}

static FocusWalkResult FocusWalk( Rml::ElementDocument *doc, const Rml::String &label ) {
	static const int dirs[4] = { K_PAD0_DPAD_UP, K_PAD0_DPAD_DOWN, K_PAD0_DPAD_LEFT, K_PAD0_DPAD_RIGHT };
	FocusWalkResult result;
	std::vector<Rml::Element *> all;
	std::set<Rml::Element *> seen;
	std::deque<Rml::Element *> queue;

	result.page = label;
	context->Update();
	CollectControls( doc, all );
	result.total = (int)all.size();
	if ( all.empty() ) {
		return result;
	}

	Rml::Element *start = context->GetFocusElement();
	if ( !start || start->GetOwnerDocument() != doc || !Focusable( start ) ) {
		start = all[0];
	}
	seen.insert( start );
	queue.push_back( start );
	quiet = true;
	while ( !queue.empty() ) {
		Rml::Element *e = queue.front();
		queue.pop_front();
		KeepAnswering();
		for ( int d = 0; d < 4; d++ ) {
			if ( d >= 2 && TakesLeftRight( e ) ) {
				continue;
			}
			e->Focus();
			e->ScrollIntoView( Rml::ScrollIntoViewOptions( Rml::ScrollAlignment::Nearest ) );	// as a player's focus always is
			context->Update();
			KeyEvent( dirs[d], true );
			KeyEvent( dirs[d], false );
			padHeld = 0;
			context->Update();
			Rml::Element *f = context->GetFocusElement();
			if ( walkVerbose ) {
				const char *const names[4] = { "up", "down", "left", "right" };
				const Rml::Vector2f pe = e->GetAbsoluteOffset( Rml::BoxArea::Border ), pf = f ? f->GetAbsoluteOffset( Rml::BoxArea::Border ) : Rml::Vector2f();
				Printf( "  %s (%.0f,%.0f) %s -> %s (%.0f,%.0f)", Describe( e ).c_str(), pe.x, pe.y, names[d], f ? Describe( f ).c_str() : "none", pf.x, pf.y );
				Printf( "%c", 10 );
			}
			if ( f && f != e && Focusable( f ) && seen.insert( f ).second ) {
				queue.push_back( f );
			}
		}
	}
	quiet = false;

	for ( Rml::Element *e : all ) {
		if ( seen.count( e ) ) {
			result.reached++;
		} else {
			result.missed.push_back( Describe( e ) );
		}
	}
	return result;
}

// the walk goes a step at a time, about 30 ms of steps a frame (a step is one of a page's submenus or tabs,
// or the page), so the window draws as it goes and the engine takes the window's messages between steps;
// the commands queued after ui_focuswalk wait, as each slice puts a wait and the next slice in front of them
static struct {
	bool active;
	Rml::String only, onlyTab;	// ui_focuswalk <page> <tab>
	size_t page;				// the page being walked
	bool open;					// it's open and its steps are listed
	bool menu, tabs;			// a menu page (its submenus or tab bar), the settings page's tabs
	Rml::StringList steps;		// the submenus' or tabs' ids, the settings tabs' indices, or "" (the page)
	size_t step;
	int pagesWalked, failed, reached, total;
} walk;

// opens the page and lists its steps; false when it's skipped
static bool WalkOpen( Page &p ) {
	int tabs = 0;

	if ( !walk.only.empty() && StrICmp( walk.only.c_str(), p.name ) ) {
		return false;
	}
	if ( !strcmp( p.name, "uifx" ) ) {
		return false;	// ui_test.py's measured page: no controls
	}
	CloseAll();
	if ( !OpenPage( &p ) ) {
		walk.failed++;
		return false;
	}
	walk.menu = ( p.flags & PAGE_MENU ) != 0;
	walk.tabs = !walk.menu && !strcmp( p.name, "settings" ) && Settings_TabCount( &tabs );
	walk.steps.clear();
	walk.step = 0;
	// the menus' pages: each submenu of the main menu, each tab of a page's tab bar
	if ( walk.menu && !strcmp( p.name, "menu" ) ) {
		walk.steps = { "", "play", "profile_menu", "extras" };
	} else if ( walk.menu ) {
		Rml::ElementList bars;
		p.doc->GetElementsByClassName( bars, "tabs" );
		for ( Rml::Element *bar : bars ) {
			for ( int i = 0; i < bar->GetNumChildren(); i++ ) {
				if ( bar->GetChild( i )->IsClassSet( "tab" ) ) {
					walk.steps.push_back( bar->GetChild( i )->GetId() );
				}
			}
		}
	} else {
		for ( int t = 0; t < tabs; t++ ) {
			walk.steps.push_back( std::to_string( t ) );	// each settings tab walked as its own page
		}
	}
	if ( walk.steps.empty() ) {
		walk.steps.push_back( "" );
	}
	return true;
}

static void WalkStep( Page &p, const Rml::String &v ) {
	const char *onlyTab = walk.onlyTab.empty() ? nullptr : walk.onlyTab.c_str();
	Rml::String label = p.name;

	if ( walk.menu ) {
		if ( onlyTab && strcmp( onlyTab, "*" ) && v != onlyTab ) {
			return;
		}
		if ( !strcmp( p.name, "menu" ) ) {
			Menu_Set( "state.sub", v.c_str() );
			context->Update();
			FocusQuietly( p.doc->QuerySelector( v.empty() ? Rml::String( "#main_play" ) : "#sub_" + v + " button" ) );
		} else if ( !v.empty() ) {
			Rml::Element *tab = p.doc->GetElementById( v );
			if ( tab ) {
				tab->Click();
				context->Update();
				FocusQuietly( tab );
			}
		}
		if ( !v.empty() ) {
			label += "/" + v;
		}
	} else if ( walk.tabs ) {
		Settings_SetTab( atoi( v.c_str() ) );
		if ( onlyTab && strcmp( onlyTab, "*" ) && StrICmp( onlyTab, Settings_Get( "@tab" ) ) ) {
			return;
		}
		label += "/" + Rml::String( Settings_Get( "@tab" ) );
		FocusQuietly( p.doc->GetElementById( "tab_" + Rml::String( Settings_Get( "@tab" ) ) ) );
	}
	walkVerbose = onlyTab != nullptr;
	const FocusWalkResult r = FocusWalk( p.doc, label );
	walkVerbose = false;
	const bool ok = r.reached == r.total && ( walk.menu || r.total > 0 );	// the splash has no controls
	walk.pagesWalked++;
	walk.reached += r.reached;
	walk.total += r.total;
	walk.failed += ok ? 0 : 1;
	Printf( "ui_focuswalk: %-24s %3d/%-3d %s", r.page.c_str(), r.reached, r.total, ok ? "ok" : "MISSED" );
	for ( size_t i = 0; i < r.missed.size() && i < 8; i++ ) {
		Printf( " %s", r.missed[i].c_str() );
	}
	Printf( r.missed.size() > 8 ? " ...\n" : "\n" );
}

static void WalkSlice() {
	const size_t numPages = sizeof( pages ) / sizeof( pages[0] );
	const int start = OzE_Milliseconds();

	while ( walk.page < numPages && OzE_Milliseconds() - start < 30 ) {
		Page &p = pages[walk.page];
		if ( !walk.open ) {
			walk.open = WalkOpen( p );
			walk.page += walk.open ? 0 : 1;
			if ( walk.open ) {
				// its first step in the frame it opened, wherever the slice's time runs out: a frame between lets
				// the page put up what it shows by itself, the map editor's getting started card in a home
				// that never closed it, which takes every key
				WalkStep( p, walk.steps[walk.step++] );
			}
		} else if ( walk.step < walk.steps.size() ) {
			WalkStep( p, walk.steps[walk.step++] );
		} else {
			if ( walk.menu && !strcmp( p.name, "menu" ) ) {
				Menu_Set( "state.sub", "" );
			}
			if ( walk.tabs ) {
				Settings_SetTab( 0 );
			}
			walk.open = false;
			walk.page++;
		}
	}
	if ( walk.page < numPages ) {
		OzE_InsertText( "wait\nui_focuswalk_next\n" );
		return;
	}
	walk.active = false;
	CloseAll();
	Printf( "ui_focuswalk: %s, %d pages, %d of %d controls reached\n", walk.failed ? "FAIL" : "PASS", walk.pagesWalked, walk.reached, walk.total );
}

static void FocusWalk_f() {
	if ( !initialized ) {
		return;
	}
	walk.active = true;
	walk.only = OzE_Argc() > 1 ? OzE_Argv( 1 ) : "";
	walk.onlyTab = OzE_Argc() > 2 ? OzE_Argv( 2 ) : "";
	walk.page = 0;
	walk.open = false;
	walk.pagesWalked = walk.failed = walk.reached = walk.total = 0;
	WalkSlice();
}

static void FocusWalkNext_f() {
	if ( walk.active ) {
		WalkSlice();
	}
}

/*
================
Commands
================
*/
static void Page_f() {
	if ( !initialized ) {
		return;
	}
	if ( OzE_Argc() < 2 ) {
		Printf( "ui_page <name>: open a native UI page. Pages:" );
		for ( const Page &p : pages ) {
			Printf( " %s", p.name );
		}
		Printf( "\nOpen:" );
		for ( const Page *p : stack ) {
			Printf( " %s", p->name );
		}
		Printf( "\n" );
		return;
	}
	Page *p = FindPage( OzE_Argv( 1 ) );
	if ( !p ) {
		Printf( "ui_page: no page %s\n", OzE_Argv( 1 ) );
		return;
	}
	OpenPage( p );
}

static void Close_f() {
	CloseAll();
}

static void Back_f() {
	if ( initialized && !stack.empty() ) {
		Back();
	}
}

static void Capture_f() {
	if ( OzE_Argc() < 2 ) {
		Printf( "ui_capture <file> [frames]: a screenshot (screenshots/<file>.jpg or .tga) after the UI has drawn\n" );
		return;
	}
	snprintf( captureName, sizeof( captureName ), "%s", OzE_Argv( 1 ) );
	captureFrames = OzE_Argc() > 2 ? atoi( OzE_Argv( 2 ) ) : 3;
	if ( captureFrames < 1 ) {
		captureFrames = 1;
	}
}

static void Focus_f() {
	Rml::ElementDocument *doc = TopDoc();
	Rml::Element *e = doc && OzE_Argc() > 1 ? doc->GetElementById( OzE_Argv( 1 ) ) : nullptr;

	if ( OzE_Argc() < 2 && context ) {	// where the focus is, and its box
		Rml::Element *f = context->GetFocusElement();
		if ( f ) {
			const Rml::Vector2f p = f->GetAbsoluteOffset( Rml::BoxArea::Border ), size = f->GetBox().GetSize( Rml::BoxArea::Border );
			Printf( "ui_focus: %s at %.0f,%.0f size %.0fx%.0f\n", Describe( f ).c_str(), p.x, p.y, size.x, size.y );
		}
		return;
	}

	if ( !e ) {
		Printf( "ui_focus <id>: no element %s on the open page\n", OzE_Argc() > 1 ? OzE_Argv( 1 ) : "" );
		return;
	}
	FocusQuietly( e );
	e->ScrollIntoView();
}

// ui_key <key> [up|down]: a key as the engine sends it (a press and release by default)
static void Key_f() {
	const int key = OzE_Argc() > 1 ? OzE_KeyNum( OzE_Argv( 1 ) ) : -1;

	if ( key < 0 ) {
		Printf( "ui_key <key name> [down|up]\n" );
		return;
	}
	const char *how = OzE_Argc() > 2 ? OzE_Argv( 2 ) : "";
	if ( strcmp( how, "up" ) ) {
		KeyEvent( key, true );
	}
	if ( strcmp( how, "down" ) ) {
		KeyEvent( key, false );
	}
	if ( context ) {
		context->Update();
	}
}

// ui_type <text>: characters typed into the focused element (a test's password or search)
static void Type_f() {
	if ( context ) {
		context->ProcessTextInput( Rml::String( OzE_ArgsFrom( 1 ) ) );
		context->Update();
	}
}

// ui_click <id or selector> [<x> <y>]: the mouse clicks the element at that point of its box
// (fractions, the middle by default), through RmlUi's own hit test, and prints what it hit
static void Click_f() {
	Rml::ElementDocument *doc = TopDoc();
	Rml::Element *e = doc && OzE_Argc() > 1 ? doc->GetElementById( OzE_Argv( 1 ) ) : nullptr;

	if ( !e && doc && OzE_Argc() > 1 ) {
		e = doc->QuerySelector( OzE_Argv( 1 ) );	// a part without an id: "#graphics_shadows .prev"
	}
	if ( !e ) {
		Printf( "ui_click <id or selector> [<x> <y>]: no element %s on the open page\n", OzE_Argc() > 1 ? OzE_Argv( 1 ) : "" );
		return;
	}
	const Rml::Vector2f p = e->GetAbsoluteOffset( Rml::BoxArea::Border ), size = e->GetBox().GetSize( Rml::BoxArea::Border );
	const float fx = OzE_Argc() > 3 ? (float)atof( OzE_Argv( 2 ) ) : 0.5f, fy = OzE_Argc() > 3 ? (float)atof( OzE_Argv( 3 ) ) : 0.5f;
	const int x = (int)( p.x + size.x * fx ), y = (int)( p.y + size.y * fy );
	mouseMoving = true;
	context->ProcessMouseMove( x, y, Modifiers() );
	mouseMoving = false;
	Rml::Element *hit = context->GetHoverElement();
	Printf( "ui_click: %d,%d hits %s\n", x, y, hit ? Describe( hit ).c_str() : "nothing" );
	context->ProcessMouseButtonDown( 0, Modifiers() );
	context->ProcessMouseButtonUp( 0, Modifiers() );
	context->Update();
}

static void Debugger_f() {
	static bool started;	// only when asked for: it puts a warning marker on screen for any logged warning
	if ( initialized && !started ) {
		started = Rml::Debugger::Initialise( context );
	}
	if ( initialized ) {
		debugger = !debugger;
		Rml::Debugger::SetVisible( debugger );
	}
}

static void DisplayFont_f();

static void AddCommands() {
	if ( commandsAdded ) {
		return;
	}
	commandsAdded = true;
	OzE_AddCommand( "ui_page", Page_f );
	OzE_AddCommand( "ui_close", Close_f );
	OzE_AddCommand( "ui_top", []() { Printf( "ui_top: %s\n", TopPageName() ); } );	// the page on top, for tests
	OzE_AddCommand( "ui_back", Back_f );
	OzE_AddCommand( "ui_capture", Capture_f );
	OzE_AddCommand( "ui_focus", Focus_f );
	OzE_AddCommand( "ui_key", Key_f );
	OzE_AddCommand( "ui_type", Type_f );
	OzE_AddCommand( "ui_click", Click_f );
	OzE_AddCommand( "ui_focuswalk", FocusWalk_f );
	OzE_AddCommand( "ui_focuswalk_next", FocusWalkNext_f );	// the walk's next slice
	OzE_AddCommand( "ui_debugger", Debugger_f );
	OzE_AddCommand( "ui_displayfont", DisplayFont_f );
	Settings_Commands();
	Menu_Commands();
	Editor_Commands();
}

// Barlow, the web menu's body face; the display face is traced from the player's pak0 as the UI
// starts (oz_ui_font.cpp; without it the display text falls back to Barlow)
static const char *const fonts[] = {
	"ui/fonts/Barlow-Regular.ttf", "ui/fonts/Barlow-Medium.ttf", "ui/fonts/Barlow-SemiBold.ttf", "ui/fonts/Barlow-Bold.ttf"
};

// ui_displayfont <file>: the traced display font, written to the home path (tests compare it)
static void DisplayFont_f() {
	Rml::String how;
	const std::vector<unsigned char> &ttf = Font_MakeDisplay( how );
	Printf( "ui_displayfont: %s\n", how.c_str() );
	if ( OzE_Argc() > 1 && !ttf.empty() ) {
		OzE_WriteFile( OzE_Argv( 1 ), ttf.data(), (int)ttf.size() );
	}
}

/*
================
The menus' takeover (P5): Oz's own game on a renderer with the UI, unless ui_nativeMenu is 0
================
*/
static bool NativeMenus() {
	return initialized && atoi( OzE_CvarString( "ui_nativeMenu" ) ) && OzE_Renderer() && OzE_IsOzGame();
}

// a connection under way, before the map loads (the connection screen's time)
static bool Connecting() {
	char server[8], message[8];
	int tries, downloadMs;
	return OzE_Connection( server, sizeof( server ), &tries, message, sizeof( message ), &downloadMs ) != 0;
}

// the main menu and its pages belong to the main menu screen, the pause menu to a match, the
// connection screen to connecting: when the client leaves that state (a match starts, the player
// leaves it, the map loads) they close
static void FollowClientState() {
	if ( stack.empty() ) {
		return;
	}
	const int state = OzE_ClientState();
	const char *first = stack[0]->name;
	if ( ( ( !strcmp( first, "menu" ) || !strcmp( first, "splash" ) || !strcmp( first, "editor" ) ) && state != 0 ) || ( !strcmp( first, "pause" ) && state != 2 )
		|| ( !strcmp( first, "connect" ) && !Connecting() ) ) {
		CloseAll();
	}
}

// why the last connection or match ended (a drop, a kick, a timeout: com_errorMessage), in a dialog
// over the main menu, as the game code's menu showed it; closing it clears the message (Menu_Back)
static void ShowError() {
	const char *message = OzE_CvarString( "com_errorMessage" );
	if ( !message[0] || stack.empty() || strcmp( stack[0]->name, "menu" ) || IsPageOpen( "error" ) ) {
		return;
	}
	Menu_Set( "error.text", message );
	OpenPage( FindPage( "error" ) );
}

} // namespace OzUI

using namespace OzUI;

extern "C" {

void OzUI_Init( void ) {
	if ( initialized ) {
		return;
	}
	Settings_Init();
	AddCommands();
	OzE_CvarCreate( "ui_nativeMenu", "1", 0, "Oz Arena's own menus: the main menu and the pause menu (0: the game code's menus).", "0", "1", 1 );

	Rml::SetSystemInterface( &systemInterface );
	Rml::SetFileInterface( &fileInterface );
	Rml::SetRenderInterface( &Renderer() );
	if ( !Rml::Initialise() ) {
		Printf( "^1UI: RmlUi didn't start\n" );
		return;
	}
	Menu_Init();
	int loaded = 0;
	for ( const char *f : fonts ) {
		loaded += Rml::LoadFontFace( f, loaded == 0 ) ? 1 : 0;
	}
	Rml::String how;
	const std::vector<unsigned char> &display = Font_MakeDisplay( how );
	if ( !display.empty() && Rml::LoadFontFace( Rml::Span<const Rml::byte>( display.data(), display.size() ), "OzDisplay",
		Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Normal ) ) {
		loaded++;
	}
	Printf( "UI: display font: %s\n", how.c_str() );
	// emoji: after Barlow among the faces asked for what a font lacks
	const std::vector<unsigned char> &emoji = Font_Emoji( how );
	emojiFont = !emoji.empty() && Rml::LoadFontFace( Rml::Span<const Rml::byte>( emoji.data(), emoji.size() ), "OzEmoji",
		Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Normal, true );
	if ( emojiFont ) {
		loaded++;
	}
	Printf( "UI: emoji: %s\n", emojiFont ? how.c_str() : "none (chat shows emoji codes)" );
	context = Rml::CreateContext( "oz", Rml::Vector2i( 1280, 720 ), &Renderer() );
	if ( !context ) {
		Printf( "^1UI: no RmlUi context\n" );
		Rml::Shutdown();
		return;
	}
	BindTestModel();
	Settings_BindModel( context );
	Menu_BindModel( context );
	initialized = true;

	Printf( "UI: RmlUi %s, %d fonts, %d settings\n", Rml::GetVersion().c_str(), loaded, Settings_Loaded() );
}

void OzUI_Shutdown( void ) {
	if ( !initialized ) {
		return;
	}
	CloseAll();
	Hud_Shutdown();
	Renderer().RendererShutdown();
	Rml::Shutdown();
	Menu_Shutdown();
	for ( Page &p : pages ) {
		p.doc = nullptr;
	}
	toastsDoc = nullptr;
	pagesLoaded = false;
	context = nullptr;
	initialized = false;
}

void OzUI_RendererShutdown( void ) {
	OzE_RendererRestarted();	// the showcase's models and shaders go with it
	if ( initialized ) {
		Hud_RendererShutdown();
		Renderer().RendererShutdown();
	}
}

int OzUI_IsActive( void ) {
	return initialized && !stack.empty();
}

// The game code's UI going (CL_ShutdownUI: a vid_restart, the map editor's lit preview restarting
// the client's systems) takes KEYCATCH_UI from under the pages, which stay open: without it, while
// disconnected, the console drew full screen over them and couldn't be closed. The page takes it
// back, as its own now that the game code's menu is gone.
void OzUI_KeepKeys( void ) {
	if ( OzUI_IsActive() ) {
		OzE_CatchKeys( 1 );
	}
}

int OzUI_EditorOpen( void ) {
	return initialized && IsPageOpen( "editor" );
}

int OzUI_EmojiFont( void ) {
	return initialized && emojiFont;
}

int OzUI_MainMenu( void ) {
	static bool splashShown;

	if ( !NativeMenus() ) {
		return 0;
	}
	if ( !stack.empty() ) {
		return 1;
	}
	const bool menu = splashShown;
	OpenPage( FindPage( splashShown ? "menu" : "splash" ) );
	splashShown = true;
	const char *after = Prog::TakePendingPage();	// a campaign match goes back to the campaign (P12)
	if ( menu && after[0] && FindPage( after ) ) {
		OpenPage( FindPage( after ) );
	}
	return !stack.empty();
}

int OzUI_NativeMenus( void ) {
	return NativeMenus();
}

int OzUI_PauseMenu( void ) {
	if ( !NativeMenus() || OzE_PlayingDemo() ) {
		return 0;
	}
	OpenPage( FindPage( "pause" ) );
	return !stack.empty();
}

// under any mod, as the HUD (decision 21): the menus' pages close as the client leaves the main menu,
// and the connection screen takes their place, without the page sound (the game code's was silent)
int OzUI_ConnectScreen( void ) {
	if ( !initialized || !atoi( OzE_CvarString( "ui_nativeMenu" ) ) || !OzE_Renderer() || !pagesLoaded || !Connecting() ) {
		return 0;	// a demo starting passes through here too: it stays the game code's
	}
	FollowClientState();
	if ( stack.empty() && OpenPage( FindPage( "connect" ) ) ) {
		inPending = false;
	}
	return IsPageOpen( "connect" );
}

void OzUI_Draw( void ) {
	int w, h;

	if ( !initialized ) {
		return;
	}
	OzE_ScreenSize( &w, &h );
	if ( w <= 0 || h <= 0 ) {
		return;
	}
	// 1dp is a pixel at 1080p, as the web menu's rem (16 px at 1080p), times Settings' menu size
	// (accessibility/ui_scale, as the web menu multiplied its rem). The pages lay out in as little as
	// 1280x720 dp (narrower windows reflow, menu.rcss's @media), where the web menu needed 1700 dp
	// across. A small window keeps 1rem at 10 px, and the size grows the menus only as far as that
	// layout still fits. Set every frame: the context starts at 1280x720 with a ratio of 1, so a
	// 1280x720 window never saw a change of size and kept 1080p's layout. Both calls return early
	// when nothing changed.
	float size = (float)atof( OzE_CvarString( "oz_accessibility_ui_scale" ) );
	size = size > 0.0f ? Rml::Math::Clamp( size, 0.75f, 1.5f ) : 1.0f;
	const float fit = Rml::Math::Min( h / 1080.0f, w / 1280.0f );
	const float scale = Rml::Math::Min( Rml::Math::Max( fit, 0.625f ) * size, Rml::Math::Min( w / 1280.0f, h / 720.0f ) );
	context->SetDimensions( Rml::Vector2i( w, h ) );
	context->SetDensityIndependentPixelRatio( scale );
	Renderer().Acquire();	// images a page shows load while RmlUi lays it out
	if ( !pagesLoaded && Renderer().r ) {
		// every page at the first frame with a renderer (a broken one shows in the log at startup);
		// earlier, their images would fail to load and RmlUi wouldn't try them again
		pagesLoaded = true;
		for ( Page &p : pages ) {
			LoadPage( &p );
		}
		toastsDoc = context->LoadDocument( "ui/pages/toasts.rml" );
		if ( toastsDoc ) {
			AddAccessSheet( toastsDoc, "toasts" );
		}
	}
	FollowClientState();
	ShowError();
	AccessFrame();
	Settings_Frame();	// also with every page closed: a renderer setting's restart when Settings closes
	if ( !stack.empty() ) {
		PadRepeat();
		Menu_Frame();
		if ( !strcmp( stack.back()->name, "splash" ) && OzE_Milliseconds() - splashTime > 5000 ) {
			Menu_SplashDone();	// splash.gd's MIN_SECONDS
		}
	}
	long long prof = OzE_ProfStart();
	if ( !stack.empty() || debugger ) {
		// with no page open nothing in the context shows (the toasts hide with the last page), and
		// updating every element of the pages loaded hidden cost 2.5 ms a frame in a match
		context->Update();
	}
	OzE_ProfEnd( 0, prof );

	// the map editor's view under its page (P13); else the showcase under the main menu and the pages
	// over it (Godot drew it under the web view)
	if ( Editor_Draw( w, h ) ) {
	} else if ( IsPageOpen( "menu" ) && OzE_ClientState() == 0 && OzE_Renderer() ) {
		float accent[3];
		Menu_Accent( accent );
		OzE_ShowcaseDraw( w, h, accent );
	}

	const bool hud = Hud_Pending();
	prof = 0;
	if ( !stack.empty() || debugger || hud ) {
		prof = OzE_ProfStart();
		const bool began = Renderer().BeginFrame( w, h );
		OzE_ProfEnd( 8, prof );	// COM_PROF_WORLD_FLUSH: BeginFrame first runs the renderer's queued world view
		prof = 0;
		if ( began ) {
			prof = OzE_ProfStart();
			long long part = OzE_ProfStart();
			Hud_Render( w, h );		// P6: cgame's HUD, under the pages
			OzE_ProfDetail( "ui hud render", part );
			if ( !stack.empty() || debugger ) {
				part = OzE_ProfStart();
				Renderer().LoadTiles( captureName[0] != 0 );	// a capture shows every tile
				OzE_ProfDetail( "ui tiles", part );
				part = OzE_ProfStart();
				context->Render();
				OzE_ProfDetail( "ui pages render", part );
			}
			part = OzE_ProfStart();
			Renderer().EndFrame();
			OzE_ProfDetail( "ui end frame", part );
			if ( inPending ) {
				inPending = false;
				PlaySound( Sound::In );		// once the page has drawn, as UI_Refresh plays it
			}
		} else if ( !warnedRenderer ) {
			warnedRenderer = true;
			Printf( "^3UI: the renderer can't draw the native UI\n" );
		}
	}
	OzE_ProfEnd( 1, prof );
	Hud_Clear();		// drawn or not, a frame's HUD is that frame's
	CaptureTick();
}

void OzUI_KeyEvent( int key, int down ) {
	KeyEvent( key, down != 0 );
}

void OzUI_CharEvent( int ch ) {
	if ( initialized && !stack.empty() && ch >= 32 && ch != 127 ) {
		context->ProcessTextInput( (Rml::Character)ch );
	}
}

void OzUI_MouseEvent( int x, int y ) {
	if ( initialized && !stack.empty() ) {
		mouseMoving = true;
		context->ProcessMouseMove( x, y, Modifiers() );
		mouseMoving = false;
		Editor_Mouse( x, y );	// the map editor's pointer over its view
	}
}

} // extern "C"
