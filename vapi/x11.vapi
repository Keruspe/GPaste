/* x11.vapi
 *
 * Copyright (C) 2009  Jürg Billeter
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
 *
 * Author:
 * 	Jürg Billeter <j@bitron.ch>
 */

[CCode (gir_namespace = "xlib", gir_version = "2.0", cprefix = "", lower_case_cprefix = "", cheader_filename = "X11/Xlib.h,X11/Xatom.h,X11/Xutil.h,X11/Xregion.h")]
namespace X {
	// Note: must be called before opening a display or calling any other Xlib function,
	// see http://tronche.com/gui/x/xlib/display/XInitThreads.html
	[CCode (cname = "XInitThreads")]
	public Status init_threads ();

	[Compact]
	[CCode (cname = "Display", free_function = "XCloseDisplay")]
	public class Display {
		[CCode (cname = "XOpenDisplay")]
		public Display (string? name = null);

		[CCode (cname = "XAllPlanes")]
		public static ulong get_all_planes ();

		[CCode (cname = "XActivateScreenSaver")]
		public void activate_screensaver ();

		[CCode (cname = "XAddToSaveSet")]
		public int add_to_save_set (Window w);

		[CCode (cname = "XAllowEvents")]
		public int allow_events (int event_mode, int time);

		[CCode (cname = "XBitmapBitOrder")]
		public int bitmap_bit_order ();

		[CCode (cname = "XBitmapUnit")]
		public int bitmap_scanline_unit ();

		[CCode (cname = "XBitmapPad")]
		public int bitmap_scanline_padding ();

		[CCode (cname = "XChangeProperty")]
		public int change_property (Window w, Atom property, Atom type, int format, int mode, [CCode (array_length = false)] uchar[] data, int nelements);

		[CCode (cname = "XChangeWindowAttributes")]
		public int change_window_attributes (Window w, ulong valuemask, SetWindowAttributes attributes);

		[CCode (cname = "XConfigureWindow")]
		public int configure_window (Window w, uint value_mask, WindowChanges values);

		[CCode (cname = "ConnectionNumber")]
		public int connection_number ();

		[CCode (cname = "DefaultRootWindow")]
		public Window default_root_window ();

		[CCode (cname = "XDefaultScreenOfDisplay")]
		public unowned Screen default_screen ();

		[CCode (cname = "XScreenOfDisplay")]
		public unowned Screen screen_by_id (int screen_number);

		[CCode (cname = "DisplayString")]
		public unowned string display_string ();

		[CCode (cname = "XQLength")]
		public int event_queue_length ();

		[CCode (cname = "XFlush")]
		public int flush ();

		[CCode (cname = "XForceScreenSaver")]
		public void force_screensaver (int mode);

		[CCode (cname = "XGetKeyboardMapping", array_length = false)]
		public ulong[] get_keyboard_mapping (uchar first_keycode, int keycode_count, ref int keysyms_per_keycode_return);

		[CCode (cname = "XGetModifierMapping")]
		public ModifierKeymap get_modifier_mapping ();

		[CCode (cname = "XGetScreenSaver")]
		public void get_screensaver (out int timeout, out int interval, out int prefer_blanking, out int allow_exposures);

		[CCode (cname = "XGetSelectionOwner")]
		public Window get_selection_owner (Atom selection);

		[CCode (cname = "XGetInputFocus")]
		public void get_input_focus (out Window focus_return, out int revert_to_return);

		[CCode (cname = "XGetWindowAttributes")]
		public void get_window_attributes (Window w, out WindowAttributes window_attributes_return);

		[CCode (cname = "XGetWindowProperty")]
		public int get_window_property (Window w, Atom property, long long_offset, long long_length, bool delete, Atom req_type, out Atom actual_type_return, out int actual_format_return, out ulong nitems_return, out ulong bytes_after_return, [CCode (type = "unsigned char **")] out void* prop_return);

		[CCode (cname = "XGrabButton")]
		public int grab_button (uint button, uint modifiers, Window grab_window, bool owner_events, uint event_mask, int pointer_mode, int keyboard_mode, Window confine_to, uint cursor);

		[CCode (cname = "XGrabKey")]
		public int grab_key (int keycode, uint modifiers, Window grab_window, bool owner_events, int pointer_mode, int keyboard_mode);

		[CCode (cname = "XGrabPointer")]
		public int grab_pointer (Window grab_window, bool owner_events, uint event_mask, int pointer_mode, int keyboard_mode, Window confine_to, uint cursor, int time);

		[CCode (cname = "XGrabServer")]
		public int grab_server ();

		[CCode (cname = "XImageByteOrder")]
		public int image_byte_order ();

		[CCode (cname = "XInternAtom")]
		public Atom intern_atom (string atom_name, bool only_if_exists);

		[CCode (cname = "XInternAtoms")]
		public void intern_atoms (string[] names, bool only_if_exists, [CCode (array_length = false)] Atom[] atoms_return);

		[CCode (cname = "XGetAtomName")]
		public string get_atom_name (X.Atom atom);

		[CCode (cname = "XGetAtomNames")]
		public string get_atom_names (Atom[] atoms, [CCode (array_length = false)] out string[] names);

		[CCode (cname = "XDeleteProperty")]
		public int delete_property (Window w, X.Atom property);

		[CCode (cname = "XGetGeometry")]
		public void get_geometry (Drawable d, out Window root_return, out int x_return, out int y_return, out uint width_return, out uint height_return, out uint border_width_return, out uint depth_return);

		[CCode (cname = "XInternalConnectionNumbers")]
		public Status internal_connection_numbers (ref int[] fd_return);

		[CCode (cname = "XDisplayKeycodes")]
		public int keycodes (ref int min_keycodes_return, ref int max_keycodes_return);

		[CCode (cname = "XKeysymToKeycode")]
		public uchar keysym_to_keycode (ulong keysym);

		[CCode (cname = "XKeycodeToKeysym")]
		public ulong keycode_to_keysym (uchar keycode, int index);

		[CCode (cname = "XLastKnownRequestProcessed")]
		public ulong last_known_request_processed ();

		[CCode (cname = "XLockDisplay")]
		public void lock_display ();

		[CCode (cname = "XMapWindow")]
		public int map_window (Window w);

		[CCode (cname = "XMapRaised")]
		public int map_raised (Window w);

		[CCode (cname = "XMaxRequestSize")]
		public long max_request_size ();

		[CCode (cname = "XExtendedMaxRequestSize")]
		public long max_extended_request_size ();

		[CCode (cname = "XEventsQueued")]
		public int events_queued (int mode);

		[CCode (cname = "XNextEvent")]
		public int next_event (ref Event event_return);

		[CCode (cname = "XNextRequest")]
		public ulong next_request ();

		[CCode (cname = "XNoOp")]
		public void no_operation ();

		[CCode (cname = "XScreenCount")]
		public int number_of_screens ();

		[CCode (cname = "XPending")]
		public int pending ();

		[CCode (cname = "XProcessInternalConnection")]
		public void process_internal_connection (int fd);

		[CCode (cname = "XProtocolVersion")]
		public int protocol_version ();

		[CCode (cname = "XProtocolRevision")]
		public int protocol_revision ();

		[CCode (cname = "XIconifyWindow")]
		public Status iconify_window (Window w, int screen_number);

		[CCode (cname = "XWithdrawWindow")]
		public Status withdraw_window (Window w, int screen_number);

		[CCode (cname = "XLowerWindow")]
		public int lower_window (Window w);

		[CCode (cname = "XRaiseWindow")]
		public int raise_window (Window w);

		[CCode (cname = "XReparentWindow")]
		public int reparent_window (Window w, Window parent, int x, int y);

		[CCode (cname = "XResetScreenSaver")]
		public void reset_screensaver ();

		[CCode (cname = "XResizeWindow")]
		public int resize_window (Window w, uint width, uint height);

		[CCode (cname = "XRootWindow")]
		public Window root_window (int screen_number);

		[CCode (cname = "ScreenCount")]
		public int screen_count ();

		[CCode (cname = "XScreenOfDisplay")]
		public unowned Screen screen_of_display (int screen_number);

		[CCode (cname = "XSelectInput")]
		public int select_input (Window w, long event_mask);

		[CCode (cname = "XSendEvent")]
		public void send_event (Window w, bool propagate, long event_mask, ref Event event_send);

		[CCode (cname = "XSetCloseDownMode")]
		public void set_close_down_mode (int close_mode);

		[CCode (cname = "XSetScreenSaver")]
		public void set_screensaver (int timeout, int interval, int prefer_blanking, int allow_exposures);

		[CCode (cname = "XSetSelectionOwner")]
		public Window set_selection_owner (Atom selection, Window owner, int time);

		[CCode (cname = "XSetInputFocus")]
		public int set_input_focus (Window focus, int revert_to, int time);

		[CCode (cname = "XUngrabButton")]
		public int ungrab_button (uint button, uint modifiers, Window grab_window);

		[CCode (cname = "XUngrabKey")]
		public int ungrab_key (int keycode, uint modifiers, Window grab_window);

		[CCode (cname = "XUngrabPointer")]
		public int ungrab_pointer (int time);

		[CCode (cname = "XUngrabKeyboard")]
		public int ungrab_keyboard (int time);

		[CCode (cname = "XUngrabServer")]
		public int ungrab_server ();

                [CCode (cname = "XSync")]
                public int x_sync (bool discard);

		[CCode (cname = "XUnlockDisplay")]
		public void unlock_display ();

		[CCode (cname = "XUnmapWindow")]
		public int unmap_window (Window w);

		[CCode (cname = "XQueryTree")]
		public void query_tree (Window w, out Window root_return, out Window parent_return, out Window[] children_return);

		[CCode (cname = "XTranslateCoordinates")]
		public bool translate_coordinates (Window src_w, Window dest_w, int src_x, int src_y, out int dest_x_return, out int dest_y_return, out Window child_return);		

		[CCode (cname = "XQueryPointer")]
		public bool query_pointer (Window w, out Window root_retur, out Window child_retur, out int root_x_return, out int root_y_return, out int win_x_return, out int win_y_return, out uint mask_return);	

		[CCode (cname = "XSetWMNormalHints")]
		public void set_wm_normal_hints (Window w, SizeHints hints);

		[CCode (cname = "XSetWMProtocols")]
		public void set_wm_protocols (Window w, Atom[] protocols);

		[CCode (cname = "XSetWMProtocols")]
		public void set_wm_protocols_n (Window w, Atom[] protocols);

		[CCode (cname = "XSetTransientForHint")]
		public int set_transient_for_hint (Window w, Window prop_window);

		[CCode (cname = "XGetTransientForHint")]
		public int get_transient_for_hint (Window w, out Window prop_window);

		[CCode (cname = "XGetWMProtocols")]
		public void get_wm_protocols (Window w, out Atom[] protocols);

		[CCode (cname = "XMoveResizeWindow")]
		public void move_resize_window (Window window, int x, int y, uint width, uint height);

		[CCode (cname = "XWindowEvent")]
		public int window_event (Window w, EventMask event_mask, out Event event_return);

		[CCode (cname = "XServerVendor")]
		public string xserver_vendor_name ();

		[CCode (cname = "XVendorRelease")]
		public string xserver_vendor_release ();

		[CCode (cname = "XMoveWindow")]
		public void move_window (Window window, int x, int y);
	}

	[Compact]
	[CCode (cname = "XModifierKeymap", free_function = "XFreeModifiermap")]
	public class ModifierKeymap {
		// The server's max # of keys per modifier
		public int max_keypermod;
		// An 8 by max_keypermod array of modifiers
		public uchar[] modifiermap;
	}

	[SimpleType]
	[IntegerType (rank = 9)]
	[CCode (cname = "Atom", type_id = "G_TYPE_LONG",
		marshaller_type_name = "LONG",
		get_value_function = "g_value_get_long",
		set_value_function = "g_value_set_long")]
	public struct Atom {
	}

	[SimpleType]
	[IntegerType (rank = 9)]
	[CCode (cname = "Colormap")]
	public struct Colormap {
	}

	[SimpleType]
	[CCode (cname = "GC")]
	public struct GC {
	}

	[SimpleType]
	[IntegerType (rank = 9)]
	[CCode (cname = "Status")]
	public struct Status {
	}

	[SimpleType]
	[IntegerType (rank = 9)]
	[CCode (cname = "XID", type_id = "G_TYPE_INT",
		marshaller_type_name = "INT",
		get_value_function = "g_value_get_int",
		set_value_function = "g_value_set_int", default_value = "0",
		type_signature = "i")]
	public struct ID {
	}

	[SimpleType]
	[IntegerType (rank = 9)]
	[CCode (cname = "Drawable", type_id = "G_TYPE_INT",
		marshaller_type_name = "INT",
		get_value_function = "g_value_get_int",
		set_value_function = "g_value_set_int", default_value = "0",
		type_signature = "i")]
	public struct Drawable : ID
	{
	}

	[SimpleType]
	[IntegerType (rank = 9)]
	[CCode (cname = "Window", type_id = "G_TYPE_INT",
		marshaller_type_name = "INT",
		get_value_function = "g_value_get_int",
		set_value_function = "g_value_set_int", default_value = "0",
		type_signature = "i")]
	public struct Window : Drawable {
	}


	[SimpleType]
	[IntegerType (rank = 9)]
	[CCode (cname = "Pixmap", type_id = "G_TYPE_INT",
		marshaller_type_name = "INT",
		get_value_function = "g_value_get_int",
		set_value_function = "g_value_set_int", default_value = "0",
		type_signature = "i")]
	public struct Pixmap : Drawable	{
	}

	[CCode (ref_function = "", unref_function = "")]
	[Compact]
	public class Visual {
	}

	public struct WindowChanges {
		public int x;
		public int y;
		public int width;
		public int height;
		public int border_width;
		public Window sibling;
		public int stack_mode;
	}
	public struct SizeHints {
		public long @flags;
		public int x;
		public int y;
		public int width;
		public int height;
	}

	[CCode (cname = "XCreateWindow")]
	public Window create_window (Display display, Window parent, int x, int y, uint width, uint height, uint border_width, int depth, uint @class, Visual? visual, X.CW valuemask, ref SetWindowAttributes attributes);

	[CCode (cname = "XSetWindowAttributes")]
	public struct SetWindowAttributes {
		// public Pixmap background_pixmap;	/* background or None or ParentRelative */
		public ulong background_pixel;	/* background pixel */
		// public Pixmap border_pixmap;	/* border of the window */
		public ulong border_pixel;	/* border pixel value */
		public int bit_gravity;		/* one of bit gravity values */
		public int win_gravity;		/* one of the window gravity values */
		public int backing_store;		/* NotUseful, WhenMapped, Always */
		public ulong backing_planes;/* planes to be preseved if possible */
		public ulong backing_pixel;/* value to use in restoring planes */
		public bool save_under;		/* should bits under be saved? (popups) */
		public long event_mask;		/* set of events that should be saved */
		public long do_not_propagate_mask;	/* set of events that should not propagate */
		public bool override_redirect;	/* boolean value for override-redirect */
		// public Colormap colormap;		/* color map to be associated with window */
		// public Cursor cursor;		/* cursor to be displayed (or None) */
	}

	[CCode(cname = "XWindowAttributes",
	       cheader_filename = "X11/Xlib.h,X11/Xatom.h,X11/Xutil.h")]
	public struct WindowAttributes {
		public int x;
		public int y;			/* location of window */
		public int width;
		public int height;		/* width and height of window */
		public int border_width;		/* border width of window */
		public int depth;          	/* depth of window */
		public Visual visual;		/* the associated visual structure */
		public Window root;        	/* root of screen containing window */
		public int @class;			/* InputOutput, InputOnly*/
		public int bit_gravity;		/* one of bit gravity values */
		public int win_gravity;		/* one of the window gravity values */
		public int backing_store;		/* NotUseful, WhenMapped, Always */
		public ulong backing_planes;/* planes to be preserved if possible */
		public ulong backing_pixel;/* value to be used when restoring planes */
		public bool save_under;		/* boolean, should bits under be saved? */
		// public Colormap colormap;		/* color map to be associated with window */
		public bool map_installed;		/* boolean, is color map currently installed*/
		public int map_state;		/* IsUnmapped, IsUnviewable, IsViewable */
		public long all_event_masks;	/* set of events all people have interest in*/
		public long your_event_mask;	/* my event mask */
		public long do_not_propagate_mask; /* set of events that should not propagate */
		public bool override_redirect;	/* boolean value for override-redirect */
		// public Screen screen;		/* back pointer to correct screen */
	}

	[CCode (cname = "CopyFromParent")]
	public const int COPY_FROM_PARENT;

	[CCode (cname = "CurrentTime")]
	public const ulong CURRENT_TIME;

	[CCode (cname = "Success")]
	public int Success;

	[CCode (cname = "XFree")]
	public int free (void* data);

	[CCode (cprefix = "", cname = "int")]
	public enum ErrorCode {
		[CCode (cname = "Success")]
		SUCCESS,
		[CCode (cname = "BadRequest")]
		BAD_REQUEST,
		[CCode (cname = "BadValue")]
		BAD_VALUE,
		[CCode (cname = "BadWindow")]
		BAD_WINDOW,
		[CCode (cname = "BadPixmap")]
		BAD_PIXMAP,
		[CCode (cname = "BadAtom")]
		BAD_ATOM,
		[CCode (cname = "BadCursor")]
		BAD_CURSOR,
		[CCode (cname = "BadFont")]
		BAD_FONT,
		[CCode (cname = "BadMatch")]
		BAD_MATCH,
		[CCode (cname = "BadDrawable")]
		BAD_DRAWABLE,
		[CCode (cname = "BadAccess")]
		BAD_ACCESS,
		[CCode (cname = "BadAlloc")]
		BAD_ALLOC,
		[CCode (cname = "BadColor")]
		BAD_COLOR,
		[CCode (cname = "BadGC")]
		BAD_GC,
		[CCode (cname = "BadIDChoice")]
		BAD_ID_CHOICE,
		[CCode (cname = "BadName")]
		BAD_NAME,
		[CCode (cname = "BadLength")]
		BAD_LENGTH,
		[CCode (cname = "BadImplementation")]
		BAD_IMPLEMENTATION,
		[CCode (cname = "FirstExtensionError")]
		FIRST_EXTENSION_ERROR,
		[CCode (cname = "LastExtensionError")]
		LAST_EXTENSION_ERROR
	}

	[CCode (cprefix = "", cname = "int")]
	public enum WindowClass {
		[CCode (cname = "InputOutput")]
		INPUT_OUTPUT,
		[CCode (cname = "InputOnly")]
		INPUT_ONLY
	}

	[CCode (cprefix = "CW", cname = "int")]
	public enum CW {
		BackPixmap,
		BackPixel,
		BackingStore,
		BackingPlanes,
		BackingPixel,
		BitGravity,
		BorderPixmap,
		BorderPixel,
		BorderWidth,
		Colormap,
		Cursor,
		DontPropagate,
		EventMask,
		Height,
		OverrideRedirect,
		SaveUnder,
		Sibling,
		StackMode,
		X,
		Y,
		Width,
		WinGravity
	}

	[CCode (cprefix = "GrabMode")]
	public enum GrabMode {
		Sync,
		Async
	}

	[CCode (cprefix = "")]
	public enum EventMask {
		NoEventMask,
		KeyPressMask,
		KeyReleaseMask,
		ButtonPressMask,
		ButtonReleaseMask,
		EnterWindowMask,
		LeaveWindowMask,
		PointerMotionMask,
		PointerMotionHintMask,
		Button1MotionMask,
		Button2MotionMask,
		Button3MotionMask,
		Button4MotionMask,
		Button5MotionMask,
		ButtonMotionMask,
		KeymapStateMask,
		ExposureMask,
		VisibilityChangeMask,
		StructureNotifyMask,
		ResizeRedirectMask,
		SubstructureNotifyMask,
		SubstructureRedirectMask,
		FocusChangeMask,
		PropertyChangeMask,
		ColormapChangeMask,
		OwnerGrabButtonMask
	}

	[CCode (cprefix = "")]
	public enum KeyMask {
		ShiftMask,
		LockMask,
		ControlMask,
		Mod1Mask,
		Mod2Mask,
		Mod3Mask,
		Mod4Mask,
		Mod5Mask
	}

	[CCode (cprefix = "")]
	public enum EventType {
		KeyPress,
		KeyRelease,
		ButtonPress,
		ButtonRelease,
		MotionNotify,
		EnterNotify,
		LeaveNotify,
		FocusIn,
		FocusOut,
		KeymapNotify,
		Expose,
		GraphicsExpose,
		NoExpose,
		VisibilityNotify,
		CreateNotify,
		DestroyNotify,
		UnmapNotify,
		MapNotify,
		MapRequest,
		ReparentNotify,
		ConfigureNotify,
		ConfigureRequest,
		GravityNotify,
		ResizeRequest,
		CirculateNotify,
		CirculateRequest,
		PropertyNotify,
		SelectionClear,
		SelectionRequest,
		SelectionNotify,
		ColormapNotify,
		ClientMessage,
		MappingNotify
	}

	// union
	[CCode (cname = "XEvent")]
	public struct Event {
		public int type;
		public AnyEvent xany;
		public KeyEvent xkey;
		public ButtonEvent xbutton;
		public MotionEvent xmotion;
		public CrossingEvent xcrossing;
		public CreateWindowEvent xcreatewindow;
		public DestroyWindowEvent xdestroywindow;
		public UnmapEvent xunmap;
		public MapEvent xmap;
		public MapRequestEvent xmaprequest;
		public ReparentEvent xreparent;
		public ConfigureEvent xconfigure;
		public GravityEvent xgravity;
		public ConfigureRequestEvent xconfigurerequest;
		public CirculateEvent xcirculate;
		public CirculateRequestEvent xcirculaterequest;
		public PropertyEvent xproperty;
		public SelectionEvent xselection;
		public ClientMessageEvent xclient;
	}

	[CCode (cname = "XAnyEvent")]
	public struct AnyEvent {
		public int type;
		public ulong serial;
		public bool send_event;
		public unowned Display display;
		public Window window;
	}

	[CCode (cname = "XKeyEvent")]
	public struct KeyEvent {
		public int type;
		public ulong serial;
		public bool send_event;
		public unowned Display display;
		public Window window;
		public Window root;
		public Window subwindow;
		public ulong time;
		public int x;
		public int y;
		public int x_root;
		public int y_root;
		public uint state;
		public uint keycode;
		public bool same_screen;
	}

	[CCode (cname = "XButtonEvent")]
	public struct ButtonEvent {
		public int type;
		public ulong serial;
		public bool send_event;
		public unowned Display display;
		public Window window;
		public Window subwindow;
		public ulong time;
		public int x;
		public int y;
		public int x_root;
		public int y_root;
		public uint state;
		public uint button;
		public bool same_screen;
	}

	[CCode (cname = "XMotionEvent")]
	public struct MotionEvent {
		public int type;
		public ulong serial;
		public bool send_event;
		public unowned Display display;
		public Window window;
		public Window subwindow;
		public ulong time;
		public int x;
		public int y;
		public int x_root;
		public int y_root;
		public uint state;
		public char is_hint;
		public bool same_screen;
	}

	[CCode (cname = "XCrossingEvent")]
	public struct CrossingEvent {
		public int type;
		public ulong serial;
		public bool send_event;
		public unowned Display display;
		public Window window;
		public Window root;
		public Window subwindow;
		public ulong time;
		public int x;
		public int y;
		public int x_root;
		public int y_root;
		public int mode;
		public int detail;
		public bool same_screen;
		public bool focus;
		public uint state;
	}

	[CCode (cname = "XCreateWindowEvent")]
	public struct CreateWindowEvent {
		public int type;
		public ulong serial;
		public bool send_event;
		public unowned Display display;
		public Window parent;
		public Window window;
		public int x;
		public int y;
		public int width;
		public int height;
		public int border_width;
		public bool override_redirect;
	}

	[CCode (cname = "XDestroyWindowEvent")]
	public struct DestroyWindowEvent {
		public int type;
		public ulong serial;
		public bool send_event;
		public unowned Display display;
		public Window event;
		public Window window;
	}

	[CCode (cname = "XUnmapEvent")]
	public struct UnmapEvent {
		public int type;
		public ulong serial;
		public bool send_event;
		public unowned Display display;
		public Window event;
		public Window window;
		public bool from_configure;
	}

	[CCode (cname = "XMapEvent")]
	public struct MapEvent {
		public int type;
		public ulong serial;
		public bool send_event;
		public unowned Display display;
		public Window event;
		public Window window;
		public bool override_redirect;
	}

	[CCode (cname = "XMapRequestEvent")]
	public struct MapRequestEvent {
		public int type;
		public ulong serial;
		public bool send_event;
		public unowned Display display;
		public Window parent;
		public Window window;
	}

	[CCode (cname = "XReparentEvent")]
	public struct ReparentEvent {
		public int type;
		public ulong serial;
		public bool send_event;
		public unowned Display display;
		public Window event;
		public Window window;
		public Window parent;
		public int x;
		public int y;
		public bool override_redirect;
	}

	[CCode (cname = "XConfigureEvent")]
	public struct ConfigureEvent {
		public int type;
		public ulong serial;
		public bool send_event;
		public unowned Display display;
		public Window event;
		public Window window;
		public int x;
		public int y;
		public int width;
		public int height;
		public int border_width;
		public Window above;
		public bool override_redirect;
	}

	[CCode (cname = "XGravityEvent")]
	public struct GravityEvent {
		public int type;
		public ulong serial;
		public bool send_event;
		public unowned Display display;
		public Window event;
		public Window window;
		public int x;
		public int y;
	}

	[CCode (cname = "XConfigureRequestEvent")]
	public struct ConfigureRequestEvent {
		public int type;
		public ulong serial;
		public bool send_event;
		public unowned Display display;
		public Window parent;
		public Window window;
		public int x;
		public int y;
		public int width;
		public int height;
		public int border_width;
		public Window above;
		public int detail;
		public ulong value_mask;
	}

	[CCode (cname = "XCirculateEvent")]
	public struct CirculateEvent {
		public int type;
		public ulong serial;
		public bool send_event;
		public unowned Display display;
		public Window event;
		public Window window;
		public int place;
	}

	[CCode (cname = "XCirculateRequestEvent")]
	public struct CirculateRequestEvent {
		public int type;
		public ulong serial;
		public bool send_event;
		public unowned Display display;
		public Window parent;
		public Window window;
		public int place;
	}

	[CCode (cname = "XPropertyEvent")]
	public struct PropertyEvent {
		public int type;
		public ulong serial;
		public bool send_event;
		public unowned Display display;
		public Window window;
		public Atom atom;
		public ulong time;
		public int state;
	}

	[CCode (cname = "XSelectionEvent")]
	public struct SelectionEvent {
		public int type;
		public ulong serial;
		public bool send_event;
		public unowned Display display;
		public Window requestor;
		public Atom selection;
		public Atom target;
		public Atom property;
		public ulong time;
	}

	[CCode (cname = "XClientMessageEvent")]
	public struct ClientMessageEvent {
		public int type;
		public ulong serial;	/* # of last request processed by server */
		public bool send_event;	/* true if this came from a SendEvent request */
		public unowned Display display;	/* Display the event was read from */
		public Window window;
		public Atom message_type;
		public int format;
		public ClientMessageEventData data;
	}

	[CCode (cname = "RECTANGLE", has_type_id = false)]
	public struct Rectangle {
		public short x;
		public short y;
		public short width;
		public short height;
	}

	// union
	public struct ClientMessageEventData {
		public unowned char[] b;
		public unowned short[] s;
		public unowned long[] l;
	}

	[CCode (cprefix = "Queued")]
	public enum QueuedMode {
		Already,
		AfterFlush,
		AfterReading
	}

	[CCode (cprefix = "PropMode")]
	public enum PropMode {
		Replace,
		Prepend,
		Append
	}

	[CCode (cprefix = "")]
	public enum AllowEventsMode {
		AsyncPointer,
		SyncPointer,
		ReplayPointer,
		AsyncKeyboard,
		SyncKeyboard,
		ReplayKeyboard,
		AsyncBoth,
		SyncBoth
	}

	[CCode (cprefix = "")]
	public enum MapState {
		IsUnmapped,
		IsUnviewable,
		IsViewable
	}

	[CCode (cprefix = "RevertTo")]
	public enum RevertTo {
		None,
		PointerRoot,
		Parent
	}

	[Compact]
	[CCode (cname = "Screen")]
	public class Screen {
		public Display display;
		public Window root;
		public int width;
		public int height;

		[CCode (cname = "ScreenOfDisplay")]
		public static unowned Screen get_screen (Display disp, int screen_number);

		[CCode (cname = "BlackPixelOfScreen")]
		public ulong black_pixel_of_screen ();

		[CCode (cname = "CellsOfScreen")]
		public int cells_of_screen ();

		[CCode (cname = "DefaultColormapOfScreen")]
		public Colormap default_colormap_of_screen ();

		[CCode (cname = "DefaultDepthOfScreen")]
		public int default_depth_of_screen ();

		[CCode (cname = "DefaultGCOfScreen")]
		public GC default_gc_of_screen ();

		[CCode (cname = "DefaultVisualOfScreen")]
		public Visual default_visual_of_screen ();

		[CCode (cname = "DisplayOfScreen")]
		public unowned Display display_of_screen ();

		[CCode (cname = "DoesBackingStore")]
		public int does_backing_store ();

		[CCode (cname = "DoesSaveUnders")]
		public bool does_save_unders ();

		[CCode (cname = "EventMaskOfScreen")]
		public long event_mask_of_Screen ();

		[CCode (cname = "HeightMMOfScreen")]
		public int height_in_mm_of_screen ();

		[CCode (cname = "HeightOfScreen")]
		public int height_of_screen ();

		[CCode (cname = "MaxCmapsOfScreen")]
		public int max_colormaps_of_screen ();

		[CCode (cname = "MinCmapsOfScreen")]
		public int min_colormaps_of_screen ();

		[CCode (cname = "PlanesOfScreen")]
		public int planes_of_screen ();

		[CCode (cname = "RootWindowOfScreen")]
		public Window root_window_of_screen ();

		[CCode (cname = "ScreenNumberOfScreen")]
		public int screen_number_of_screen ();

		[CCode (cname = "WhitePixelOfScreen")]
		public ulong white_pixel_of_screen ();

		[CCode (cname = "WidthMMOfScreen")]
		public int width_in_mm_of_screen ();

		[CCode (cname = "WidthOfScreen")]
		public int width_of_screen ();
	}

	public const X.ID None;

	public const X.Atom XA_ATOM;
	public const X.Atom XA_CARDINAL;
	public const X.Atom XA_WINDOW;
	public const X.Atom XA_WM_CLASS;
	public const X.Atom XA_WM_HINTS;
	public const X.Atom XA_WM_ICON_NAME;
	public const X.Atom XA_WM_NAME;
	public const X.Atom XA_WM_NORMAL_HINTS;
	public const X.Atom XA_WM_TRANSIENT_FOR;

	public const uint XK_Num_Lock;
	public const uint XK_Scroll_Lock;
	public const uint XK_Super_L;
	public const uint XK_Super_R;

	[CCode (cname = "XStringToKeysym")]
	public ulong string_to_keysym (string key);

	[CCode (cname = "XKeysymToString")]
	public unowned string keysym_to_string (ulong keysym);

	[CCode (cname = "XConvertCase")]
	public void convert_case (ulong keysym, out ulong lower_return, out ulong upper_return);
}

