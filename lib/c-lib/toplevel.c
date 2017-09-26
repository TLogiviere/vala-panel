#include "toplevel.h"
#include "css.h"
#include "misc.h"
#include "panel-layout.h"
#include "vala-panel-enums.h"

#include "vala-panel-compat.h"

#include <glib.h>
#include <math.h>
#include <stdbool.h>

static const int PERIOD = 200;

static ValaPanelPlatform *platform   = NULL;
static ValaPanelAppletHolder *holder = NULL;

static void activate_new_panel(GSimpleAction *act, GVariant *param, void *data);
static void activate_remove_panel(GSimpleAction *act, GVariant *param, void *data);
static void activate_panel_settings(GSimpleAction *act, GVariant *param, void *data);
static void vala_panel_toplevel_unit_update_geometry(ValaPanelToplevelUnit *self);

static const GActionEntry panel_entries[] =
    { { "new-panel", activate_new_panel, NULL, NULL, NULL, { 0 } },
      { "remove-panel", activate_remove_panel, NULL, NULL, NULL, { 0 } },
      { "panel-settings", activate_panel_settings, "s", NULL, NULL, { 0 } } };

enum
{
	VALA_PANEL_TOPLEVEL_DUMMY,
	VALA_PANEL_TOPLEVEL_UUID,
	VALA_PANEL_TOPLEVEL_HEIGHT,
	VALA_PANEL_TOPLEVEL_WIDTH,
	VALA_PANEL_TOPLEVEL_USE_FONT,
	VALA_PANEL_TOPLEVEL_USE_BACKGROUND_COLOR,
	VALA_PANEL_TOPLEVEL_USE_FOREGROUND_COLOR,
	VALA_PANEL_TOPLEVEL_USE_BACKGROUND_FILE,
	VALA_PANEL_TOPLEVEL_FONT_SIZE_ONLY,
	VALA_PANEL_TOPLEVEL_FONT_SIZE,
	VALA_PANEL_TOPLEVEL_ROUND_CORNERS_SIZE,
	VALA_PANEL_TOPLEVEL_FONT,
	VALA_PANEL_TOPLEVEL_BACKGROUND_COLOR,
	VALA_PANEL_TOPLEVEL_FOREGROUND_COLOR,
	VALA_PANEL_TOPLEVEL_ICON_SIZE,
	VALA_PANEL_TOPLEVEL_BACKGROUND_FILE,
	VALA_PANEL_TOPLEVEL_PANEL_GRAVITY,
	VALA_PANEL_TOPLEVEL_ORIENTATION,
	VALA_PANEL_TOPLEVEL_MONITOR,
	VALA_PANEL_TOPLEVEL_DOCK,
	VALA_PANEL_TOPLEVEL_STRUT,
	VALA_PANEL_TOPLEVEL_IS_DYNAMIC,
	VALA_PANEL_TOPLEVEL_AUTOHIDE,
	VALA_PANEL_TOPLEVEL_LAST
};
static GParamSpec *vala_panel_toplevel_properties[VALA_PANEL_TOPLEVEL_LAST];

struct _ValaPanelToplevelUnit
{
	GtkApplicationWindow __parent__;
	ValaPanelLayout *layout;
	GtkRevealer *ah_rev;
	GtkSeparator *ah_sep;
	PanelAutohideState ah_state;
	ValaPanelUnitSettings *settings;
	GtkCssProvider *provider;
	bool initialized;
	bool dock;
	bool autohide;
	bool strut;
	bool use_background_color;
	bool use_foreground_color;
	bool use_background_file;
	bool use_font;
	bool font_size_only;
	uint round_corners_size;
	GdkRGBA background_color;
	GdkRGBA foreground_color;
	char *background_file;
	char *uuid;
	int mon;
	int height;
	int width;
	GtkOrientation orientation;
	PanelGravity gravity;
	GtkDialog *pref_dialog;
	char *font;
};

G_DEFINE_TYPE(ValaPanelToplevelUnit, vala_panel_toplevel_unit, GTK_TYPE_APPLICATION_WINDOW)
/*****************************************************************************************
 *                                   Common functions
 *****************************************************************************************/

static void stop_ui(ValaPanelToplevelUnit *self)
{
	if (self->pref_dialog != NULL)
		gtk_dialog_response(self->pref_dialog, GTK_RESPONSE_CLOSE);
	if (self->initialized)
	{
		gdk_flush();
		self->initialized = false;
	}
	if (gtk_bin_get_child(GTK_BIN(self)))
	{
		gtk_widget_hide(GTK_WIDGET(self->layout));
	}
}

static void start_ui(ValaPanelToplevelUnit *self)
{
	css_apply_from_resource(GTK_WIDGET(self),
	                        "/org/vala-panel/lib/style.css",
	                        "-panel-transparent");
	css_toggle_class(GTK_WIDGET(self), "-panel-transparent", false);
	gtk_application_add_window(gtk_window_get_application(GTK_WINDOW(self)), GTK_WINDOW(self));
	gtk_widget_add_events(GTK_WIDGET(self),
	                      GDK_BUTTON_PRESS_MASK | GDK_ENTER_NOTIFY_MASK |
	                          GDK_LEAVE_NOTIFY_MASK);
	gtk_widget_realize(GTK_WIDGET(self));
	self->ah_rev = GTK_REVEALER(gtk_revealer_new());
	self->layout = VALA_PANEL_LAYOUT(
	    vala_panel_layout_new((ValaPanelToplevel *)self, self->orientation, 0));
	gtk_revealer_set_transition_type(self->ah_rev, GTK_REVEALER_TRANSITION_TYPE_CROSSFADE);
	g_signal_connect_swapped(self->ah_rev,
	                         "notify::child-revealed",
	                         G_CALLBACK(gtk_widget_queue_draw),
	                         self->layout);
	gtk_container_add(GTK_CONTAINER(self->ah_rev), GTK_WIDGET(self->layout));
	gtk_container_add(GTK_CONTAINER(self), GTK_WIDGET(self->ah_rev));
	vala_panel_layout_init_applets(self->layout);
	gtk_widget_show(GTK_WIDGET(self->ah_rev));
	gtk_widget_show(GTK_WIDGET(self->layout));
	gtk_revealer_set_reveal_child(self->ah_rev, true);
	gtk_window_set_type_hint(GTK_WINDOW(self),
	                         (self->dock) ? GDK_WINDOW_TYPE_HINT_DOCK
	                                      : GDK_WINDOW_TYPE_HINT_NORMAL);
	// End To Layout
	gtk_widget_show(GTK_WIDGET(self));
	gtk_window_stick(GTK_WINDOW(self));
	vala_panel_layout_update_applet_positions(self->layout);
	gtk_window_present(GTK_WINDOW(self));
	self->autohide =
	    g_settings_get_boolean(self->settings->default_settings, VALA_PANEL_KEY_AUTOHIDE);
	vala_panel_toplevel_unit_update_geometry(self);
	self->initialized = true;
}

static void setup(ValaPanelToplevelUnit *self, bool use_internal_values)
{
	if (use_internal_values)
	{
		g_settings_set_int(self->settings->default_settings,
		                   VALA_PANEL_KEY_MONITOR,
		                   self->mon);
		g_settings_set_enum(self->settings->default_settings,
		                    VALA_PANEL_KEY_GRAVITY,
		                    self->gravity);
	}
	vala_panel_add_gsettings_as_action(G_ACTION_MAP(self),
	                                   self->settings->default_settings,
	                                   VALA_PANEL_KEY_GRAVITY);
	vala_panel_add_gsettings_as_action(G_ACTION_MAP(self),
	                                   self->settings->default_settings,
	                                   VALA_PANEL_KEY_HEIGHT);
	vala_panel_add_gsettings_as_action(G_ACTION_MAP(self),
	                                   self->settings->default_settings,
	                                   VALA_PANEL_KEY_WIDTH);
	vala_panel_add_gsettings_as_action(G_ACTION_MAP(self),
	                                   self->settings->default_settings,
	                                   VALA_PANEL_KEY_DYNAMIC);
	vala_panel_add_gsettings_as_action(G_ACTION_MAP(self),
	                                   self->settings->default_settings,
	                                   VALA_PANEL_KEY_AUTOHIDE);
	vala_panel_add_gsettings_as_action(G_ACTION_MAP(self),
	                                   self->settings->default_settings,
	                                   VALA_PANEL_KEY_STRUT);
	vala_panel_add_gsettings_as_action(G_ACTION_MAP(self),
	                                   self->settings->default_settings,
	                                   VALA_PANEL_KEY_DOCK);
	vala_panel_add_gsettings_as_action(G_ACTION_MAP(self),
	                                   self->settings->default_settings,
	                                   VALA_PANEL_KEY_MARGIN);
	vala_panel_bind_gsettings(self, self->settings->default_settings, VALA_PANEL_KEY_MONITOR);
	vala_panel_add_gsettings_as_action(G_ACTION_MAP(self),
	                                   self->settings->default_settings,
	                                   VALA_PANEL_KEY_SHOW_HIDDEN);
	vala_panel_add_gsettings_as_action(G_ACTION_MAP(self),
	                                   self->settings->default_settings,
	                                   VALA_PANEL_KEY_ICON_SIZE);
	vala_panel_add_gsettings_as_action(G_ACTION_MAP(self),
	                                   self->settings->default_settings,
	                                   VALA_PANEL_KEY_BACKGROUND_COLOR);
	vala_panel_add_gsettings_as_action(G_ACTION_MAP(self),
	                                   self->settings->default_settings,
	                                   VALA_PANEL_KEY_FOREGROUND_COLOR);
	vala_panel_add_gsettings_as_action(G_ACTION_MAP(self),
	                                   self->settings->default_settings,
	                                   VALA_PANEL_KEY_BACKGROUND_FILE);
	vala_panel_add_gsettings_as_action(G_ACTION_MAP(self),
	                                   self->settings->default_settings,
	                                   VALA_PANEL_KEY_FONT);
	vala_panel_add_gsettings_as_action(G_ACTION_MAP(self),
	                                   self->settings->default_settings,
	                                   VALA_PANEL_KEY_CORNERS_SIZE);
	vala_panel_add_gsettings_as_action(G_ACTION_MAP(self),
	                                   self->settings->default_settings,
	                                   VALA_PANEL_KEY_FONT_SIZE_ONLY);
	vala_panel_add_gsettings_as_action(G_ACTION_MAP(self),
	                                   self->settings->default_settings,
	                                   VALA_PANEL_KEY_USE_BACKGROUND_COLOR);
	vala_panel_add_gsettings_as_action(G_ACTION_MAP(self),
	                                   self->settings->default_settings,
	                                   VALA_PANEL_KEY_USE_FOREGROUND_COLOR);
	vala_panel_add_gsettings_as_action(G_ACTION_MAP(self),
	                                   self->settings->default_settings,
	                                   VALA_PANEL_KEY_USE_FONT);
	vala_panel_add_gsettings_as_action(G_ACTION_MAP(self),
	                                   self->settings->default_settings,
	                                   VALA_PANEL_KEY_USE_BACKGROUND_FILE);
	if (self->mon < gdk_display_get_n_monitors(gtk_widget_get_display(GTK_WIDGET(self))))
		start_ui(self);
}

/**************************************************************************************
 *                                     Actions stuff
 **************************************************************************************/

G_GNUC_INTERNAL bool panel_edge_available(ValaPanelToplevelUnit *self, uint edge, int monitor,
                                          bool include_this)
{
	g_autoptr(GtkApplication) app;
	g_object_get(self, "application", &app, NULL);
	for (g_autoptr(GList) w = gtk_application_get_windows(app); w != NULL; w = w->next)
		if (VALA_PANEL_IS_TOPLEVEL_UNIT(w))
		{
			ValaPanelToplevelUnit *pl = VALA_PANEL_TOPLEVEL_UNIT(w->data);
			if (((pl != self) || include_this) &&
			    (vala_panel_edge_from_gravity(self->gravity) == edge) &&
			    ((monitor == self->mon) || self->mon < 0))
				return false;
		}
	return true;
}

static void activate_new_panel(GSimpleAction *act, GVariant *param, void *data)
{
	ValaPanelToplevelUnit *self = VALA_PANEL_TOPLEVEL_UNIT(data);
	int new_mon                 = -2;
	GtkPositionType new_edge    = GTK_POS_TOP;
	bool found                  = false;
	/* Allocate the edge. */
	g_assert(gtk_widget_get_display(GTK_WIDGET(self)) != NULL);
	int monitors = gdk_display_get_n_monitors(gtk_widget_get_display(GTK_WIDGET(self)));
	/* try to allocate edge on current monitor first */
	int m = self->mon;
	for (int e = GTK_POS_BOTTOM; e >= GTK_POS_LEFT; e--)
	{
		if (panel_edge_available(self, (GtkPositionType)e, m, true))
		{
			new_edge = (GtkPositionType)e;
			new_mon  = m;
			found    = true;
		}
	}
	/* try all monitors */
	if (!found)
		for (m = 0; m < monitors; ++m)
		{
			/* try each of the four edges */
			for (int e = GTK_POS_BOTTOM; e >= GTK_POS_LEFT; e--)
			{
				if (panel_edge_available(self, (GtkPositionType)e, m, true))
				{
					new_edge = (GtkPositionType)e;
					new_mon  = m;
					found    = true;
				}
			}
		}
	if (!found)
	{
		g_warning(
		    "Error adding panel: There is no room for another panel. All the edges are "
		    "taken.");
		g_autoptr(GtkWidget) msg = gtk_message_dialog_new(
		    GTK_WINDOW(self),
		    GTK_DIALOG_DESTROY_WITH_PARENT,
		    GTK_MESSAGE_ERROR,
		    GTK_BUTTONS_CLOSE,
		    N_("There is no room for another panel. All the edges are taken."));
		vala_panel_apply_window_icon(GTK_WINDOW(msg));
		gtk_window_set_title(GTK_WINDOW(msg), _("Error"));
		gtk_dialog_run(GTK_DIALOG(msg));
		gtk_widget_destroy(GTK_WIDGET(msg));
		return;
	}
	g_autofree char *new_name = vala_panel_core_settings_get_uuid();
	// FIXME: Translate after adding constructors
	ValaPanelToplevelUnit *new_toplevel =
	    vala_panel_toplevel_unit_new_from_position(gtk_window_get_application(GTK_WINDOW(self)),
	                                               new_name,
	                                               new_mon,
	                                               new_edge);
	//        new_toplevel.configure("position");
	gtk_widget_show_all(GTK_WIDGET(new_toplevel));
	gtk_widget_queue_resize(GTK_WIDGET(new_toplevel));
}
static void activate_remove_panel(GSimpleAction *act, GVariant *param, void *data)
{
	ValaPanelToplevelUnit *self     = VALA_PANEL_TOPLEVEL_UNIT(data);
	g_autoptr(GtkMessageDialog) dlg = GTK_MESSAGE_DIALOG(
	    gtk_message_dialog_new_with_markup(GTK_WINDOW(self),
	                                       GTK_DIALOG_MODAL,
	                                       GTK_MESSAGE_QUESTION,
	                                       GTK_BUTTONS_OK_CANCEL,
	                                       N_("Really delete this panel?\n<b>Warning:"
	                                          "This can not be recovered.</b>")));
	vala_panel_apply_window_icon(GTK_WINDOW(dlg));
	gtk_window_set_title(GTK_WINDOW(dlg), _("Confirm"));
	bool ok = (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_OK);
	gtk_widget_destroy(GTK_WIDGET(dlg));
	if (ok)
	{
		g_autofree char *uid  = g_strdup(self->uuid);
		g_autofree char *path = NULL;
		g_object_get(self->settings->default_settings, "path", &path, NULL);
		stop_ui(self);
		gtk_widget_destroy(GTK_WIDGET(self));
		/* delete the config file of this panel */
		ValaPanelCoreSettings *st = vala_panel_platform_get_settings(platform);
		vala_panel_core_settings_remove_unit_settings_full(st, uid, true);
	}
}
static void activate_panel_settings(GSimpleAction *act, GVariant *param, void *data)
{
	//    this.configure(param.get_string());
}
/**************************************************************************
 * Appearance -------------------------------------------------------------
 **************************************************************************/
G_GNUC_INTERNAL void update_appearance(ValaPanelToplevelUnit *self)
{
	if (self->provider)
		gtk_style_context_remove_provider(gtk_widget_get_style_context(GTK_WIDGET(self)),
		                                  GTK_STYLE_PROVIDER(self->provider));
	if (!self->font)
		return;
	g_autoptr(GString) str            = g_string_new("");
	g_autofree char *background_color = gdk_rgba_to_string(&self->background_color);
	g_string_append_printf(str, ".-vala-panel-background {\n");
	if (self->use_background_color)
		g_string_append_printf(str, " background-color: %s;\n", background_color);
	else
		g_string_append_printf(str, " background-color: transparent;\n");
	if (self->use_background_file)
	{
		g_string_append_printf(str,
		                       " background-image: url('%s');\n",
		                       self->background_file);
		/* Feature proposed: Background repeat */
		//~                 if (false)
		//~                     g_string_append_printf(str," background-repeat:
		// no-repeat;\n");
	}
	else
		g_string_append_printf(str, " background-image: none;\n");
	g_string_append_printf(str, "}\n");
	/* Feature proposed: Panel Layout and Shadow */
	//~             g_string_append_printf(str,".-vala-panel-shadow {\n");
	//~             g_string_append_printf(str," box-shadow: 0 0 0 3px alpha(0.3,
	//%s);\n",foreground_color);
	//~             g_string_append_printf(str," border-style: none;\n margin: 3px;\n");
	//~             g_string_append_printf(str,"}\n");
	g_string_append_printf(str, ".-vala-panel-round-corners {\n");
	g_string_append_printf(str, " border-radius: %upx;\n", self->round_corners_size);
	g_string_append_printf(str, "}\n");
	PangoFontDescription *desc = pango_font_description_from_string(self->font);
	g_string_append_printf(str, ".-vala-panel-font-size {\n");
	g_string_append_printf(str,
	                       " font-size: %dpx;\n",
	                       pango_font_description_get_size(desc) / PANGO_SCALE);
	g_string_append_printf(str, "}\n");
	g_string_append_printf(str, ".-vala-panel-font {\n");
	const char *family   = pango_font_description_get_family(desc);
	PangoWeight weight   = pango_font_description_get_weight(desc);
	PangoStyle style     = pango_font_description_get_style(desc);
	PangoVariant variant = pango_font_description_get_variant(desc);
	g_string_append_printf(str,
	                       " font-style: %s;\n",
	                       (style == PANGO_STYLE_ITALIC)
	                           ? "italic"
	                           : ((style == PANGO_STYLE_OBLIQUE) ? "oblique" : "normal"));
	g_string_append_printf(str,
	                       " font-variant: %s;\n",
	                       (variant == PANGO_VARIANT_SMALL_CAPS) ? "small-caps" : "normal");
	g_string_append_printf(str,
	                       " font-weight: %s;\n",
	                       (weight <= PANGO_WEIGHT_SEMILIGHT)
	                           ? "light"
	                           : (weight >= PANGO_WEIGHT_SEMIBOLD ? "bold" : "normal"));
	g_string_append_printf(str, " font-family: %s;\n", family);
	g_string_append_printf(str, "}\n");
	g_string_append_printf(str, ".-vala-panel-foreground-color {\n");
	g_autofree char *foreground_color = gdk_rgba_to_string(&self->foreground_color);
	g_string_append_printf(str, " color: %s;\n", foreground_color);
	g_string_append_printf(str, "}\n");
	g_autofree char *css = str->str;
	self->provider       = css_add_css_to_widget(GTK_WIDGET(self), css);
	css_toggle_class(GTK_WIDGET(self),
	                 "-vala-panel-background",
	                 self->use_background_color || self->use_background_file);
	css_toggle_class(GTK_WIDGET(self), "-vala-panel-shadow", false);
	css_toggle_class(GTK_WIDGET(self),
	                 "-vala-panel-round-corners",
	                 self->round_corners_size > 0);
	css_toggle_class(GTK_WIDGET(self), "-vala-panel-font-size", self->use_font);
	css_toggle_class(GTK_WIDGET(self),
	                 "-vala-panel-font",
	                 self->use_font && !self->font_size_only);
	css_toggle_class(GTK_WIDGET(self),
	                 "-vala-panel-foreground-color",
	                 self->use_foreground_color);
}

ValaPanelToplevelUnit *vala_panel_toplevel_unit_new_from_position(GtkApplication *app,
                                                                  const char *uid, int mon,
                                                                  PanelGravity edge)
{
	ValaPanelToplevelUnit *ret =
	    VALA_PANEL_TOPLEVEL_UNIT(g_object_new(vala_panel_toplevel_unit_get_type(),
	                                          "border-width",
	                                          0,
	                                          "decorated",
	                                          false,
	                                          "name",
	                                          "ValaPanel",
	                                          "resizable",
	                                          false,
	                                          "title",
	                                          "ValaPanel",
	                                          "type-hint",
	                                          GDK_WINDOW_TYPE_HINT_DOCK,
	                                          "window-position",
	                                          GTK_WIN_POS_NONE,
	                                          "skip-taskbar-hint",
	                                          true,
	                                          "skip-pager-hint",
	                                          true,
	                                          "accept-focus",
	                                          false,
	                                          "application",
	                                          app,
	                                          "uuid",
	                                          uid));
	ret->mon     = mon;
	ret->gravity = edge;
	setup(ret, true);
	return ret;
}
ValaPanelToplevelUnit *vala_panel_toplevel_unit_new_from_uuid(GtkApplication *app,
                                                              ValaPanelPlatform *plt,
                                                              const char *uid)
{
	ValaPanelToplevelUnit *ret =
	    VALA_PANEL_TOPLEVEL_UNIT(g_object_new(vala_panel_toplevel_unit_get_type(),
	                                          "border-width",
	                                          0,
	                                          "decorated",
	                                          false,
	                                          "name",
	                                          "ValaPanel",
	                                          "resizable",
	                                          false,
	                                          "title",
	                                          "ValaPanel",
	                                          "type-hint",
	                                          GDK_WINDOW_TYPE_HINT_DOCK,
	                                          "window-position",
	                                          GTK_WIN_POS_NONE,
	                                          "skip-taskbar-hint",
	                                          true,
	                                          "skip-pager-hint",
	                                          true,
	                                          "accept-focus",
	                                          false,
	                                          "application",
	                                          app,
	                                          "uuid",
	                                          uid));
	if (platform == NULL)
	{
		platform = plt;
	}
	setup(ret, false);
	return ret;
}
/*********************************************************************************************
 * Positioning
 *********************************************************************************************/

static void monitors_changed_cb(GdkDisplay *scr, GdkMonitor *mon, void *data)
{
	GtkApplication *app   = (GtkApplication *)data;
	int mons              = gdk_display_get_n_monitors(scr);
	g_autofree GList *win = gtk_application_get_windows(app);
	for (GList *il = win; il != NULL; il = il->next)
	{
		if (VALA_PANEL_IS_TOPLEVEL_UNIT(il->data))
		{
			ValaPanelToplevelUnit *panel = (ValaPanelToplevelUnit *)il->data;
			if (panel->mon < mons && !panel->initialized)
				start_ui(panel);
			else if (panel->mon >= mons && panel->initialized)
				stop_ui(panel);
			else
			{
				vala_panel_toplevel_unit_update_geometry(panel);
			}
		}
	}
}

static int calc_width(int scrw, int panel_width, int panel_margin)
{
	int effective_width = (int)round(scrw * panel_width / 100.0);
	if ((effective_width + panel_margin) > scrw)
		effective_width = scrw - panel_margin;
	return effective_width;
}
static void measure(GtkWidget *w, GtkOrientation orient, int for_size, int *min, int *nat,
                    int *base_min, int *base_nat)
{
	ValaPanelToplevelUnit *self = (ValaPanelToplevelUnit *)w;
	GdkDisplay *screen          = gtk_widget_get_display(w);
	GdkRectangle marea          = { 0, 0, 0, 0 };
	if (self->mon < 0)
		gdk_monitor_get_geometry(gdk_display_get_primary_monitor(screen), &marea);
	else if (self->mon < gdk_display_get_n_monitors(screen))
		gdk_monitor_get_geometry(gdk_display_get_monitor(screen, self->mon), &marea);
	int scrw = self->orientation == GTK_ORIENTATION_HORIZONTAL ? marea.width : marea.height;
	if (self->orientation != orient)
		*min = *nat = (!self->autohide || (self->ah_rev != NULL &&
		                                   gtk_revealer_get_reveal_child(self->ah_rev)))
		                  ? self->height
		                  : GAP;
	else
		*min = *nat = *base_min = *base_nat = calc_width(scrw, self->width, 0);
}
static void get_preferred_width_for_height(GtkWidget *w, int height, int *min, int *nat)
{
	int *x = NULL, *y = NULL;
	GtkOrientation eff_ori = GTK_ORIENTATION_HORIZONTAL;
	GTK_WIDGET_CLASS(vala_panel_toplevel_unit_parent_class)
	    ->get_preferred_width_for_height(w, height, x, y);
	measure(w, eff_ori, height, min, nat, x, y);
}
static void get_preferred_height_for_width(GtkWidget *w, int height, int *min, int *nat)
{
	int *x = NULL, *y = NULL;
	GtkOrientation eff_ori = GTK_ORIENTATION_VERTICAL;
	GTK_WIDGET_CLASS(vala_panel_toplevel_unit_parent_class)
	    ->get_preferred_width_for_height(w, height, x, y);
	measure(w, eff_ori, height, min, nat, x, y);
}
static void get_preferred_width(GtkWidget *w, int *min, int *nat)
{
	ValaPanelToplevelUnit *self = (ValaPanelToplevelUnit *)w;
	*min = *nat = (!self->autohide ||
	               (self->ah_rev != NULL && gtk_revealer_get_reveal_child(self->ah_rev)))
	                  ? self->height
	                  : GAP;
}
static void get_preferred_height(GtkWidget *w, int *min, int *nat)
{
	ValaPanelToplevelUnit *self = (ValaPanelToplevelUnit *)w;
	*min = *nat = (!self->autohide ||
	               (self->ah_rev != NULL && gtk_revealer_get_reveal_child(self->ah_rev)))
	                  ? self->height
	                  : GAP;
}
static GtkSizeRequestMode get_request_mode(GtkWidget *w)
{
	ValaPanelToplevelUnit *self = (ValaPanelToplevelUnit *)w;
	return (self->orientation == GTK_ORIENTATION_HORIZONTAL)
	           ? GTK_SIZE_REQUEST_WIDTH_FOR_HEIGHT
	           : GTK_SIZE_REQUEST_HEIGHT_FOR_WIDTH;
}

static void vala_panel_toplevel_unit_update_geometry(ValaPanelToplevelUnit *self)
{
	GdkDisplay *screen = gtk_widget_get_display(GTK_WIDGET(self));
	GdkRectangle marea = { 0 };
	if (self->mon < 0)
		gdk_monitor_get_geometry(gdk_display_get_primary_monitor(screen), &marea);
	else if (self->mon < gdk_display_get_n_monitors(screen))
		gdk_monitor_get_geometry(gdk_display_get_monitor(screen, self->mon), &marea);
	int effective_height = self->orientation == GTK_ORIENTATION_HORIZONTAL
	                           ? self->height
	                           : (self->width / 100) * marea.height - marea.y;
	int effective_width = self->orientation == GTK_ORIENTATION_HORIZONTAL
	                          ? (self->width / 100) * marea.width - marea.x
	                          : self->height;
	gtk_widget_set_size_request(GTK_WIDGET(self), effective_width, effective_height);
	gtk_widget_queue_resize(GTK_WIDGET(self));
	while (gtk_events_pending())
		gtk_main_iteration();
	vala_panel_platform_move_to_side(platform, GTK_WINDOW(self), self->gravity, self->mon);
	vala_panel_platform_update_strut(platform, GTK_WINDOW(self));
	g_object_notify(G_OBJECT(self), "orientation");
}
/****************************************************
 *         autohide : new version                   *
 ****************************************************/
static bool timeout_func(ValaPanelToplevelUnit *self)
{
	if (self->autohide && self->ah_state == AH_WAITING)
	{
		css_toggle_class(GTK_WIDGET(self), "-panel-transparent", true);
		gtk_revealer_set_reveal_child(self->ah_rev, false);
		self->ah_state = AH_HIDDEN;
	}
	return false;
}

static void ah_show(ValaPanelToplevelUnit *self)
{
	css_toggle_class(GTK_WIDGET(self), "-panel-transparent", false);
	gtk_revealer_set_reveal_child(self->ah_rev, true);
	self->ah_state = AH_VISIBLE;
}

static void ah_hide(ValaPanelToplevelUnit *self)
{
	self->ah_state = AH_WAITING;
	g_timeout_add(PERIOD, (GSourceFunc)timeout_func, self);
}

static bool enter_notify_event(GtkWidget *w, GdkEventCrossing *event)
{
	ValaPanelToplevelUnit *self = (ValaPanelToplevelUnit *)w;
	ah_show(self);
	return false;
}

static bool leave_notify_event(GtkWidget *w, GdkEventCrossing *event)
{
	ValaPanelToplevelUnit *self = (ValaPanelToplevelUnit *)w;
	if (self->autohide &&
	    (event->detail != GDK_NOTIFY_INFERIOR && event->detail != GDK_NOTIFY_VIRTUAL))
		ah_hide(self);
	return false;
}

static void grab_notify(ValaPanelToplevelUnit *self, bool was_grabbed, gpointer data)
{
	if (!was_grabbed)
		self->ah_state = AH_GRAB;
	else if (self->autohide)
		ah_hide(self);
}

static void vala_panel_toplevel_unit_get_property(GObject *object, guint property_id, GValue *value,
                                                  GParamSpec *pspec)
{
	ValaPanelToplevelUnit *self = NULL;
	PangoFontDescription *desc  = pango_font_description_from_string(self->font);
	char *str = NULL, *str2 = NULL;
	self = G_TYPE_CHECK_INSTANCE_CAST(object,
	                                  vala_panel_toplevel_unit_get_type(),
	                                  ValaPanelToplevelUnit);
	switch (property_id)
	{
	case VALA_PANEL_TOPLEVEL_UUID:
		g_value_set_string(value, self->uuid);
		break;
	case VALA_PANEL_TOPLEVEL_HEIGHT:
		g_value_set_int(value, self->height);
		break;
	case VALA_PANEL_TOPLEVEL_WIDTH:
		g_value_set_int(value, self->width);
		break;
	case VALA_PANEL_TOPLEVEL_USE_FONT:
		g_value_set_boolean(value, self->use_font);
		break;
	case VALA_PANEL_TOPLEVEL_USE_BACKGROUND_COLOR:
		g_value_set_boolean(value, self->use_background_color);
		break;
	case VALA_PANEL_TOPLEVEL_USE_FOREGROUND_COLOR:
		g_value_set_boolean(value, self->use_foreground_color);
		break;
	case VALA_PANEL_TOPLEVEL_USE_BACKGROUND_FILE:
		g_value_set_boolean(value, self->use_background_file);
		break;
	case VALA_PANEL_TOPLEVEL_FONT_SIZE_ONLY:
		g_value_set_boolean(value, self->font_size_only);
		break;
	case VALA_PANEL_TOPLEVEL_FONT_SIZE:
		g_value_set_int(value, pango_font_description_get_size(desc));
		break;
	case VALA_PANEL_TOPLEVEL_ROUND_CORNERS_SIZE:
		g_value_set_uint(value, self->round_corners_size);
		break;
	case VALA_PANEL_TOPLEVEL_FONT:
		g_value_set_string(value, self->font);
		break;
	case VALA_PANEL_TOPLEVEL_BACKGROUND_COLOR:
		str = gdk_rgba_to_string(&self->background_color);
		g_value_take_string(value, str);
		break;
	case VALA_PANEL_TOPLEVEL_FOREGROUND_COLOR:
		str2 = gdk_rgba_to_string(&self->foreground_color);
		g_value_take_string(value, str2);
		break;
	//        case VALA_PANEL_TOPLEVEL_ICON_SIZE:
	//        g_value_set_uint (value, self->);
	//        break;
	case VALA_PANEL_TOPLEVEL_BACKGROUND_FILE:
		g_value_set_string(value, self->background_file);
		break;
	case VALA_PANEL_TOPLEVEL_PANEL_GRAVITY:
		g_value_set_enum(value, self->gravity);
		break;
	case VALA_PANEL_TOPLEVEL_ORIENTATION:
		g_value_set_enum(value, self->orientation);
		break;
	case VALA_PANEL_TOPLEVEL_MONITOR:
		g_value_set_int(value, self->mon);
		break;
	case VALA_PANEL_TOPLEVEL_DOCK:
		g_value_set_boolean(value, self->dock);
		break;
	case VALA_PANEL_TOPLEVEL_STRUT:
		g_value_set_boolean(value, self->strut);
		break;
	//        case VALA_PANEL_TOPLEVEL_IS_DYNAMIC:
	//        g_value_set_boolean (value, self->);
	//        break;
	case VALA_PANEL_TOPLEVEL_AUTOHIDE:
		g_value_set_boolean(value, self->autohide);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
		break;
	}
}

static void vala_panel_toplevel_unit_set_property(GObject *object, guint property_id,
                                                  const GValue *value, GParamSpec *pspec)
{
	ValaPanelToplevelUnit *self   = VALA_PANEL_TOPLEVEL_UNIT(object);
	bool geometry_update_required = false, appearance_update_required = false;
	PangoFontDescription *desc = pango_font_description_from_string(self->font);
	self                       = G_TYPE_CHECK_INSTANCE_CAST(object,
	                                  vala_panel_toplevel_unit_get_type(),
	                                  ValaPanelToplevelUnit);
	switch (property_id)
	{
	case VALA_PANEL_TOPLEVEL_UUID:
		if (self->uuid != NULL)
			g_free(self->uuid);
		self->uuid = g_value_dup_string(value);
		break;
	case VALA_PANEL_TOPLEVEL_HEIGHT:
		self->height             = g_value_get_int(value);
		geometry_update_required = true;
		break;
	case VALA_PANEL_TOPLEVEL_WIDTH:
		self->width              = g_value_get_int(value);
		geometry_update_required = true;
		break;
	case VALA_PANEL_TOPLEVEL_USE_FONT:
		self->use_font             = g_value_get_boolean(value);
		appearance_update_required = true;
		break;
	case VALA_PANEL_TOPLEVEL_USE_BACKGROUND_COLOR:
		self->use_background_color = g_value_get_boolean(value);
		appearance_update_required = true;
		break;
	case VALA_PANEL_TOPLEVEL_USE_FOREGROUND_COLOR:
		self->use_foreground_color = g_value_get_boolean(value);
		appearance_update_required = true;
		break;
	case VALA_PANEL_TOPLEVEL_USE_BACKGROUND_FILE:
		self->use_background_file  = g_value_get_boolean(value);
		appearance_update_required = true;
		break;
	case VALA_PANEL_TOPLEVEL_FONT_SIZE_ONLY:
		self->font_size_only       = g_value_get_boolean(value);
		appearance_update_required = true;
		break;
	case VALA_PANEL_TOPLEVEL_FONT_SIZE:
		pango_font_description_set_size(desc, g_value_get_int(value));
		appearance_update_required = true;
		break;
	case VALA_PANEL_TOPLEVEL_ROUND_CORNERS_SIZE:
		self->round_corners_size   = g_value_get_uint(value);
		appearance_update_required = true;
		break;
	case VALA_PANEL_TOPLEVEL_FONT:
		self->font                 = g_value_dup_string(value);
		appearance_update_required = true;
		break;
	case VALA_PANEL_TOPLEVEL_BACKGROUND_COLOR:
		gdk_rgba_parse(&self->background_color, g_value_get_string(value));
		appearance_update_required = true;
		break;
	case VALA_PANEL_TOPLEVEL_FOREGROUND_COLOR:
		gdk_rgba_parse(&self->foreground_color, g_value_get_string(value));
		appearance_update_required = true;
		break;
	//        case VALA_PANEL_TOPLEVEL_ICON_SIZE:
	//        vala_panel_toplevel_set_icon_size (self, g_value_get_uint (value));
	//        appearance_update_required = true;
	//        break;
	case VALA_PANEL_TOPLEVEL_BACKGROUND_FILE:
		self->background_file      = g_value_dup_string(value);
		appearance_update_required = true;
		break;
	case VALA_PANEL_TOPLEVEL_PANEL_GRAVITY:
		self->gravity            = (PanelGravity)g_value_get_enum(value);
		geometry_update_required = true;
		break;
	case VALA_PANEL_TOPLEVEL_MONITOR:
		self->mon                = g_value_get_int(value);
		geometry_update_required = true;
		break;
	case VALA_PANEL_TOPLEVEL_DOCK:
		self->dock                 = g_value_get_boolean(value);
		appearance_update_required = true;
		break;
	case VALA_PANEL_TOPLEVEL_STRUT:
		self->strut                = g_value_get_boolean(value);
		appearance_update_required = true;
		break;
	//        case VALA_PANEL_TOPLEVEL_IS_DYNAMIC:
	//        vala_panel_toplevel_set_is_dynamic (self, g_value_get_boolean (value));
	//        break;
	case VALA_PANEL_TOPLEVEL_AUTOHIDE:
		self->autohide           = g_value_get_boolean(value);
		geometry_update_required = true;
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
		break;
	}
	if (geometry_update_required)
		vala_panel_toplevel_unit_update_geometry(self);
	if (appearance_update_required)
		update_appearance(self);
}

void vala_panel_toplevel_unit_init(ValaPanelToplevelUnit *self)
{
}

void vala_panel_toplevel_unit_class_init(ValaPanelToplevelUnitClass *parent)
{
	GTK_WIDGET_CLASS(parent)->enter_notify_event             = enter_notify_event;
	GTK_WIDGET_CLASS(parent)->leave_notify_event             = leave_notify_event;
	GTK_WIDGET_CLASS(parent)->get_preferred_height           = get_preferred_height;
	GTK_WIDGET_CLASS(parent)->get_preferred_width            = get_preferred_width;
	GTK_WIDGET_CLASS(parent)->get_preferred_height_for_width = get_preferred_height_for_width;
	GTK_WIDGET_CLASS(parent)->get_preferred_width_for_height = get_preferred_width_for_height;
	GTK_WIDGET_CLASS(parent)->get_request_mode               = get_request_mode;
	G_OBJECT_CLASS(parent)->set_property = vala_panel_toplevel_unit_set_property;
	G_OBJECT_CLASS(parent)->get_property = vala_panel_toplevel_unit_get_property;
	g_object_class_install_property(G_OBJECT_CLASS(parent),
	                                VALA_PANEL_TOPLEVEL_UUID,
	                                vala_panel_toplevel_properties[VALA_PANEL_TOPLEVEL_UUID] =
	                                    g_param_spec_string("uuid",
	                                                        "uuid",
	                                                        "uuid",
	                                                        NULL,
	                                                        G_PARAM_STATIC_NAME |
	                                                            G_PARAM_STATIC_NICK |
	                                                            G_PARAM_STATIC_BLURB |
	                                                            G_PARAM_READABLE |
	                                                            G_PARAM_WRITABLE |
	                                                            G_PARAM_CONSTRUCT_ONLY));
	g_object_class_install_property(G_OBJECT_CLASS(parent),
	                                VALA_PANEL_TOPLEVEL_HEIGHT,
	                                vala_panel_toplevel_properties[VALA_PANEL_TOPLEVEL_HEIGHT] =
	                                    g_param_spec_int("height",
	                                                     "height",
	                                                     "height",
	                                                     G_MININT,
	                                                     G_MAXINT,
	                                                     0,
	                                                     G_PARAM_STATIC_NAME |
	                                                         G_PARAM_STATIC_NICK |
	                                                         G_PARAM_STATIC_BLURB |
	                                                         G_PARAM_READABLE |
	                                                         G_PARAM_WRITABLE));
	g_object_class_install_property(G_OBJECT_CLASS(parent),
	                                VALA_PANEL_TOPLEVEL_WIDTH,
	                                vala_panel_toplevel_properties[VALA_PANEL_TOPLEVEL_WIDTH] =
	                                    g_param_spec_int("width",
	                                                     "width",
	                                                     "width",
	                                                     G_MININT,
	                                                     G_MAXINT,
	                                                     0,
	                                                     G_PARAM_STATIC_NAME |
	                                                         G_PARAM_STATIC_NICK |
	                                                         G_PARAM_STATIC_BLURB |
	                                                         G_PARAM_READABLE |
	                                                         G_PARAM_WRITABLE));
	g_object_class_install_property(
	    G_OBJECT_CLASS(parent),
	    VALA_PANEL_TOPLEVEL_USE_FONT,
	    vala_panel_toplevel_properties[VALA_PANEL_TOPLEVEL_USE_FONT] =
	        g_param_spec_boolean("use-font",
	                             "use-font",
	                             "use-font",
	                             FALSE,
	                             G_PARAM_STATIC_NAME | G_PARAM_STATIC_NICK |
	                                 G_PARAM_STATIC_BLURB | G_PARAM_READABLE |
	                                 G_PARAM_WRITABLE));
	g_object_class_install_property(
	    G_OBJECT_CLASS(parent),
	    VALA_PANEL_TOPLEVEL_USE_BACKGROUND_COLOR,
	    vala_panel_toplevel_properties[VALA_PANEL_TOPLEVEL_USE_BACKGROUND_COLOR] =
	        g_param_spec_boolean("use-background-color",
	                             "use-background-color",
	                             "use-background-color",
	                             FALSE,
	                             G_PARAM_STATIC_NAME | G_PARAM_STATIC_NICK |
	                                 G_PARAM_STATIC_BLURB | G_PARAM_READABLE |
	                                 G_PARAM_WRITABLE));
	g_object_class_install_property(
	    G_OBJECT_CLASS(parent),
	    VALA_PANEL_TOPLEVEL_USE_FOREGROUND_COLOR,
	    vala_panel_toplevel_properties[VALA_PANEL_TOPLEVEL_USE_FOREGROUND_COLOR] =
	        g_param_spec_boolean("use-foreground-color",
	                             "use-foreground-color",
	                             "use-foreground-color",
	                             FALSE,
	                             G_PARAM_STATIC_NAME | G_PARAM_STATIC_NICK |
	                                 G_PARAM_STATIC_BLURB | G_PARAM_READABLE |
	                                 G_PARAM_WRITABLE));
	g_object_class_install_property(
	    G_OBJECT_CLASS(parent),
	    VALA_PANEL_TOPLEVEL_USE_BACKGROUND_FILE,
	    vala_panel_toplevel_properties[VALA_PANEL_TOPLEVEL_USE_BACKGROUND_FILE] =
	        g_param_spec_boolean("use-background-file",
	                             "use-background-file",
	                             "use-background-file",
	                             FALSE,
	                             G_PARAM_STATIC_NAME | G_PARAM_STATIC_NICK |
	                                 G_PARAM_STATIC_BLURB | G_PARAM_READABLE |
	                                 G_PARAM_WRITABLE));
	g_object_class_install_property(
	    G_OBJECT_CLASS(parent),
	    VALA_PANEL_TOPLEVEL_FONT_SIZE_ONLY,
	    vala_panel_toplevel_properties[VALA_PANEL_TOPLEVEL_FONT_SIZE_ONLY] =
	        g_param_spec_boolean("font-size-only",
	                             "font-size-only",
	                             "font-size-only",
	                             FALSE,
	                             G_PARAM_STATIC_NAME | G_PARAM_STATIC_NICK |
	                                 G_PARAM_STATIC_BLURB | G_PARAM_READABLE |
	                                 G_PARAM_WRITABLE));
	g_object_class_install_property(
	    G_OBJECT_CLASS(parent),
	    VALA_PANEL_TOPLEVEL_FONT_SIZE,
	    vala_panel_toplevel_properties[VALA_PANEL_TOPLEVEL_FONT_SIZE] =
	        g_param_spec_uint("font-size",
	                          "font-size",
	                          "font-size",
	                          0,
	                          G_MAXUINT,
	                          0U,
	                          G_PARAM_STATIC_NAME | G_PARAM_STATIC_NICK | G_PARAM_STATIC_BLURB |
	                              G_PARAM_READABLE | G_PARAM_WRITABLE));
	g_object_class_install_property(
	    G_OBJECT_CLASS(parent),
	    VALA_PANEL_TOPLEVEL_ROUND_CORNERS_SIZE,
	    vala_panel_toplevel_properties[VALA_PANEL_TOPLEVEL_ROUND_CORNERS_SIZE] =
	        g_param_spec_uint("round-corners-size",
	                          "round-corners-size",
	                          "round-corners-size",
	                          0,
	                          G_MAXUINT,
	                          0U,
	                          G_PARAM_STATIC_NAME | G_PARAM_STATIC_NICK | G_PARAM_STATIC_BLURB |
	                              G_PARAM_READABLE | G_PARAM_WRITABLE));
	g_object_class_install_property(G_OBJECT_CLASS(parent),
	                                VALA_PANEL_TOPLEVEL_FONT,
	                                vala_panel_toplevel_properties[VALA_PANEL_TOPLEVEL_FONT] =
	                                    g_param_spec_string("font",
	                                                        "font",
	                                                        "font",
	                                                        NULL,
	                                                        G_PARAM_STATIC_NAME |
	                                                            G_PARAM_STATIC_NICK |
	                                                            G_PARAM_STATIC_BLURB |
	                                                            G_PARAM_READABLE |
	                                                            G_PARAM_WRITABLE));
	g_object_class_install_property(
	    G_OBJECT_CLASS(parent),
	    VALA_PANEL_TOPLEVEL_BACKGROUND_COLOR,
	    vala_panel_toplevel_properties[VALA_PANEL_TOPLEVEL_BACKGROUND_COLOR] =
	        g_param_spec_string("background-color",
	                            "background-color",
	                            "background-color",
	                            NULL,
	                            G_PARAM_STATIC_NAME | G_PARAM_STATIC_NICK |
	                                G_PARAM_STATIC_BLURB | G_PARAM_READABLE |
	                                G_PARAM_WRITABLE));
	g_object_class_install_property(
	    G_OBJECT_CLASS(parent),
	    VALA_PANEL_TOPLEVEL_FOREGROUND_COLOR,
	    vala_panel_toplevel_properties[VALA_PANEL_TOPLEVEL_FOREGROUND_COLOR] =
	        g_param_spec_string("foreground-color",
	                            "foreground-color",
	                            "foreground-color",
	                            NULL,
	                            G_PARAM_STATIC_NAME | G_PARAM_STATIC_NICK |
	                                G_PARAM_STATIC_BLURB | G_PARAM_READABLE |
	                                G_PARAM_WRITABLE));
	g_object_class_install_property(
	    G_OBJECT_CLASS(parent),
	    VALA_PANEL_TOPLEVEL_ICON_SIZE,
	    vala_panel_toplevel_properties[VALA_PANEL_TOPLEVEL_ICON_SIZE] =
	        g_param_spec_uint("icon-size",
	                          "icon-size",
	                          "icon-size",
	                          0,
	                          G_MAXUINT,
	                          0U,
	                          G_PARAM_STATIC_NAME | G_PARAM_STATIC_NICK | G_PARAM_STATIC_BLURB |
	                              G_PARAM_READABLE | G_PARAM_WRITABLE));
	g_object_class_install_property(
	    G_OBJECT_CLASS(parent),
	    VALA_PANEL_TOPLEVEL_BACKGROUND_FILE,
	    vala_panel_toplevel_properties[VALA_PANEL_TOPLEVEL_BACKGROUND_FILE] =
	        g_param_spec_string("background-file",
	                            "background-file",
	                            "background-file",
	                            NULL,
	                            G_PARAM_STATIC_NAME | G_PARAM_STATIC_NICK |
	                                G_PARAM_STATIC_BLURB | G_PARAM_READABLE |
	                                G_PARAM_WRITABLE));
	g_object_class_install_property(
	    G_OBJECT_CLASS(parent),
	    VALA_PANEL_TOPLEVEL_PANEL_GRAVITY,
	    vala_panel_toplevel_properties[VALA_PANEL_TOPLEVEL_PANEL_GRAVITY] =
	        g_param_spec_enum("panel-gravity",
	                          "panel-gravity",
	                          "panel-gravity",
	                          VALA_PANEL_TYPE_GRAVITY,
	                          0,
	                          G_PARAM_STATIC_NAME | G_PARAM_STATIC_NICK | G_PARAM_STATIC_BLURB |
	                              G_PARAM_READABLE | G_PARAM_WRITABLE | G_PARAM_CONSTRUCT));
	g_object_class_install_property(
	    G_OBJECT_CLASS(parent),
	    VALA_PANEL_TOPLEVEL_ORIENTATION,
	    vala_panel_toplevel_properties[VALA_PANEL_TOPLEVEL_ORIENTATION] =
	        g_param_spec_enum("orientation",
	                          "orientation",
	                          "orientation",
	                          GTK_TYPE_ORIENTATION,
	                          0,
	                          G_PARAM_STATIC_NAME | G_PARAM_STATIC_NICK | G_PARAM_STATIC_BLURB |
	                              G_PARAM_READABLE));
	g_object_class_install_property(
	    G_OBJECT_CLASS(parent),
	    VALA_PANEL_TOPLEVEL_MONITOR,
	    vala_panel_toplevel_properties[VALA_PANEL_TOPLEVEL_MONITOR] =
	        g_param_spec_int("monitor",
	                         "monitor",
	                         "monitor",
	                         G_MININT,
	                         G_MAXINT,
	                         0,
	                         G_PARAM_STATIC_NAME | G_PARAM_STATIC_NICK | G_PARAM_STATIC_BLURB |
	                             G_PARAM_READABLE | G_PARAM_WRITABLE | G_PARAM_CONSTRUCT));
	g_object_class_install_property(G_OBJECT_CLASS(parent),
	                                VALA_PANEL_TOPLEVEL_DOCK,
	                                vala_panel_toplevel_properties[VALA_PANEL_TOPLEVEL_DOCK] =
	                                    g_param_spec_boolean("dock",
	                                                         "dock",
	                                                         "dock",
	                                                         FALSE,
	                                                         G_PARAM_STATIC_NAME |
	                                                             G_PARAM_STATIC_NICK |
	                                                             G_PARAM_STATIC_BLURB |
	                                                             G_PARAM_READABLE |
	                                                             G_PARAM_WRITABLE));
	g_object_class_install_property(G_OBJECT_CLASS(parent),
	                                VALA_PANEL_TOPLEVEL_STRUT,
	                                vala_panel_toplevel_properties[VALA_PANEL_TOPLEVEL_STRUT] =
	                                    g_param_spec_boolean("strut",
	                                                         "strut",
	                                                         "strut",
	                                                         FALSE,
	                                                         G_PARAM_STATIC_NAME |
	                                                             G_PARAM_STATIC_NICK |
	                                                             G_PARAM_STATIC_BLURB |
	                                                             G_PARAM_READABLE |
	                                                             G_PARAM_WRITABLE));
	g_object_class_install_property(
	    G_OBJECT_CLASS(parent),
	    VALA_PANEL_TOPLEVEL_IS_DYNAMIC,
	    vala_panel_toplevel_properties[VALA_PANEL_TOPLEVEL_IS_DYNAMIC] =
	        g_param_spec_boolean("is-dynamic",
	                             "is-dynamic",
	                             "is-dynamic",
	                             FALSE,
	                             G_PARAM_STATIC_NAME | G_PARAM_STATIC_NICK |
	                                 G_PARAM_STATIC_BLURB | G_PARAM_READABLE |
	                                 G_PARAM_WRITABLE));
	g_object_class_install_property(
	    G_OBJECT_CLASS(parent),
	    VALA_PANEL_TOPLEVEL_AUTOHIDE,
	    vala_panel_toplevel_properties[VALA_PANEL_TOPLEVEL_AUTOHIDE] =
	        g_param_spec_boolean("autohide",
	                             "autohide",
	                             "autohide",
	                             FALSE,
	                             G_PARAM_STATIC_NAME | G_PARAM_STATIC_NICK |
	                                 G_PARAM_STATIC_BLURB | G_PARAM_READABLE |
	                                 G_PARAM_WRITABLE));
}
