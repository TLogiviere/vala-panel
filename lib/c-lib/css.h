#ifndef __VALA_PANEL_CSS_H__
#define __VALA_PANEL_CSS_H__

#include <glib.h>
#include <glib-object.h>
#include <gtk/gtk.h>

void css_apply_with_class (GtkWidget* widget, const gchar* css, const gchar *klass , gboolean remove);
gchar* css_generate_background(const char *filename, GdkRGBA color,gboolean no_image);
gchar* css_generate_font_color(GdkRGBA color);
gchar* css_generate_font_size(gint size);
gchar* css_generate_font_label(gfloat size, gboolean is_bold);
gchar* css_apply_from_file (GtkWidget* widget, const gchar *file);
gchar* css_apply_from_resource (GtkWidget* widget, const char *file, const char* klass);
gchar* css_apply_from_file_to_app (const gchar *file);
//inline gchar* css_generate_flat_button(GtkWidget* widget,ValaPanel* panel);

#endif