#define _POSIX_C_SOURCE 200809L

#include <dirent.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <gtk/gtk.h>

typedef struct {
  GtkApplication *app;
  GtkWindow *window;
  GtkPicture *picture;
  GtkScrolledWindow *scroller;
  GtkDrawingArea *overlay;
  GtkLabel *label;
  GtkButton *prev_btn;
  GtkButton *next_btn;
  GtkButton *zoom_out_btn;
  GtkButton *zoom_in_btn;
  GtkButton *zoom_reset_btn;
  GtkButton *crop_btn;
  GtkButton *crop_apply_btn;
  GtkLabel *zoom_label;

  char *dir;
  char **names;
  size_t count;
  size_t idx;

  double zoom;
  int img_w;
  int img_h;
  bool crop_mode;
  bool has_sel;
  double sel_x0, sel_y0, sel_x1, sel_y1;
} Viewer;

static void die(const char *msg) {
  perror(msg);
  exit(1);
}

static bool is_image_ext(const char *name) {
  const char *dot = strrchr(name, '.');
  if (!dot || dot[1] == '\0')
    return false;
  const char *ext = dot + 1;

  static const char *k[] = {"png",  "jpg",  "jpeg", "webp", "bmp",
                            "gif",  "tif",  "tiff", "avif", "heic"};
  for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); i++) {
    if (strcasecmp(ext, k[i]) == 0)
      return true;
  }
  return false;
}

static int cmp_strp(const void *a, const void *b) {
  const char *const *sa = (const char *const *)a;
  const char *const *sb = (const char *const *)b;
  return strcasecmp(*sa, *sb);
}

static char *path_dirname_dup(const char *path) {
  const char *slash = strrchr(path, '/');
  if (!slash)
    return strdup(".");
  size_t len = (size_t)(slash - path);
  if (len == 0)
    return strdup("/");
  char *out = (char *)malloc(len + 1);
  if (!out)
    return NULL;
  memcpy(out, path, len);
  out[len] = '\0';
  return out;
}

static char *path_basename_dup(const char *path) {
  const char *slash = strrchr(path, '/');
  return strdup(slash ? slash + 1 : path);
}

static char *path_join(const char *dir, const char *name) {
  size_t dlen = strlen(dir);
  size_t nlen = strlen(name);
  bool need_slash = (dlen > 0 && dir[dlen - 1] != '/');
  size_t len = dlen + (need_slash ? 1 : 0) + nlen;
  char *out = (char *)malloc(len + 1);
  if (!out)
    return NULL;
  memcpy(out, dir, dlen);
  size_t off = dlen;
  if (need_slash)
    out[off++] = '/';
  memcpy(out + off, name, nlen);
  out[len] = '\0';
  return out;
}

static char **list_images_in_dir(const char *dir, size_t *out_count) {
  *out_count = 0;
  DIR *d = opendir(dir);
  if (!d)
    return NULL;

  size_t cap = 64;
  char **items = (char **)calloc(cap, sizeof(char *));
  if (!items) {
    closedir(d);
    return NULL;
  }

  struct dirent *de;
  while ((de = readdir(d)) != NULL) {
    if (de->d_name[0] == '.')
      continue;
    if (!is_image_ext(de->d_name))
      continue;
    if (*out_count == cap) {
      cap *= 2;
      char **nitems = (char **)realloc(items, cap * sizeof(char *));
      if (!nitems)
        break;
      items = nitems;
    }
    items[*out_count] = strdup(de->d_name);
    if (!items[*out_count])
      break;
    (*out_count)++;
  }

  closedir(d);
  qsort(items, *out_count, sizeof(char *), cmp_strp);
  return items;
}

static void free_strv(char **v, size_t n) {
  if (!v)
    return;
  for (size_t i = 0; i < n; i++)
    free(v[i]);
  free(v);
}

static void viewer_set_index(Viewer *v, size_t new_idx) {
  if (!v || v->count == 0)
    return;
  v->idx = new_idx % v->count;

  char *full = path_join(v->dir, v->names[v->idx]);
  if (!full)
    die("malloc");

  GFile *f = g_file_new_for_path(full);
  gtk_picture_set_file(v->picture, f);
  g_object_unref(f);

  // Load dimensions for zoom/crop mapping.
  GError *err = NULL;
  GdkPixbuf *pb = gdk_pixbuf_new_from_file(full, &err);
  if (pb) {
    v->img_w = gdk_pixbuf_get_width(pb);
    v->img_h = gdk_pixbuf_get_height(pb);
    g_object_unref(pb);
  } else {
    v->img_w = 0;
    v->img_h = 0;
    if (err)
      g_error_free(err);
  }

  v->zoom = 1.0;
  v->crop_mode = false;
  v->has_sel = false;

  gtk_label_set_text(v->label, v->names[v->idx]);
  gtk_window_set_title(v->window, v->names[v->idx]);

  gtk_widget_set_sensitive(GTK_WIDGET(v->prev_btn), v->count > 1);
  gtk_widget_set_sensitive(GTK_WIDGET(v->next_btn), v->count > 1);

  free(full);
}

static void on_prev_clicked(GtkButton *btn, gpointer user_data) {
  (void)btn;
  Viewer *v = (Viewer *)user_data;
  if (!v || v->count == 0)
    return;
  viewer_set_index(v, (v->idx + v->count - 1) % v->count);
}

static void on_next_clicked(GtkButton *btn, gpointer user_data) {
  (void)btn;
  Viewer *v = (Viewer *)user_data;
  if (!v || v->count == 0)
    return;
  viewer_set_index(v, (v->idx + 1) % v->count);
}

static void viewer_update_zoom(Viewer *v) {
  if (!v)
    return;
  if (v->zoom < 0.05)
    v->zoom = 0.05;
  if (v->zoom > 16.0)
    v->zoom = 16.0;

  if (v->img_w > 0 && v->img_h > 0) {
    int zw = (int)((double)v->img_w * v->zoom);
    int zh = (int)((double)v->img_h * v->zoom);
    if (zw < 1)
      zw = 1;
    if (zh < 1)
      zh = 1;
    gtk_widget_set_size_request(GTK_WIDGET(v->picture), zw, zh);
    gtk_widget_set_size_request(GTK_WIDGET(v->overlay), zw, zh);
  }

  char buf[64];
  snprintf(buf, sizeof(buf), "%.0f%%", v->zoom * 100.0);
  gtk_label_set_text(v->zoom_label, buf);
  gtk_widget_queue_draw(GTK_WIDGET(v->overlay));
}

static void on_zoom_out(GtkButton *btn, gpointer user_data) {
  (void)btn;
  Viewer *v = (Viewer *)user_data;
  v->zoom /= 1.25;
  viewer_update_zoom(v);
}

static void on_zoom_in(GtkButton *btn, gpointer user_data) {
  (void)btn;
  Viewer *v = (Viewer *)user_data;
  v->zoom *= 1.25;
  viewer_update_zoom(v);
}

static void on_zoom_reset(GtkButton *btn, gpointer user_data) {
  (void)btn;
  Viewer *v = (Viewer *)user_data;
  v->zoom = 1.0;
  viewer_update_zoom(v);
}

static void on_crop_toggle(GtkButton *btn, gpointer user_data) {
  (void)btn;
  Viewer *v = (Viewer *)user_data;
  v->crop_mode = !v->crop_mode;
  v->has_sel = false;
  gtk_widget_set_sensitive(GTK_WIDGET(v->crop_apply_btn), v->crop_mode);
  gtk_widget_queue_draw(GTK_WIDGET(v->overlay));
}

static char *viewer_current_path(Viewer *v) {
  if (!v || v->count == 0)
    return NULL;
  return path_join(v->dir, v->names[v->idx]);
}

static char *viewer_crop_output_path(Viewer *v) {
  if (!v)
    return NULL;
  const char *name = v->names[v->idx];
  const char *dot = strrchr(name, '.');
  size_t base_len = dot ? (size_t)(dot - name) : strlen(name);
  char out_name[PATH_MAX];
  snprintf(out_name, sizeof(out_name), "%.*s_crop.png", (int)base_len, name);
  return path_join(v->dir, out_name);
}

static void on_crop_apply(GtkButton *btn, gpointer user_data) {
  (void)btn;
  Viewer *v = (Viewer *)user_data;
  if (!v || !v->crop_mode || !v->has_sel || v->zoom <= 0.0)
    return;

  double x0 = v->sel_x0 < v->sel_x1 ? v->sel_x0 : v->sel_x1;
  double y0 = v->sel_y0 < v->sel_y1 ? v->sel_y0 : v->sel_y1;
  double x1 = v->sel_x0 < v->sel_x1 ? v->sel_x1 : v->sel_x0;
  double y1 = v->sel_y0 < v->sel_y1 ? v->sel_y1 : v->sel_y0;

  int ix = (int)(x0 / v->zoom);
  int iy = (int)(y0 / v->zoom);
  int iw = (int)((x1 - x0) / v->zoom);
  int ih = (int)((y1 - y0) / v->zoom);
  if (iw < 2 || ih < 2)
    return;

  char *in_path = viewer_current_path(v);
  char *out_path = viewer_crop_output_path(v);
  if (!in_path || !out_path) {
    free(in_path);
    free(out_path);
    return;
  }

  GError *err = NULL;
  GdkPixbuf *pb = gdk_pixbuf_new_from_file(in_path, &err);
  if (!pb) {
    if (err) {
      g_warning("Failed to load for crop: %s", err->message);
      g_error_free(err);
    }
    free(in_path);
    free(out_path);
    return;
  }

  int pw = gdk_pixbuf_get_width(pb);
  int ph = gdk_pixbuf_get_height(pb);
  if (ix < 0)
    ix = 0;
  if (iy < 0)
    iy = 0;
  if (ix + iw > pw)
    iw = pw - ix;
  if (iy + ih > ph)
    ih = ph - iy;
  if (iw < 2 || ih < 2) {
    g_object_unref(pb);
    free(in_path);
    free(out_path);
    return;
  }

  GdkPixbuf *sub = gdk_pixbuf_new_subpixbuf(pb, ix, iy, iw, ih);
  gboolean ok = gdk_pixbuf_save(sub, out_path, "png", &err, NULL);
  if (!ok) {
    if (err) {
      g_warning("Failed to save crop: %s", err->message);
      g_error_free(err);
    }
  } else {
    // Open the newly created crop (and refresh directory list would be more
    // work; keep it minimal and just load the crop itself).
    gtk_window_set_title(v->window, out_path);
  }

  g_object_unref(sub);
  g_object_unref(pb);
  free(in_path);
  free(out_path);
}

static void overlay_draw(GtkDrawingArea *area, cairo_t *cr, int width,
                         int height, gpointer user_data) {
  (void)area;
  Viewer *v = (Viewer *)user_data;
  if (!v || !v->crop_mode || !v->has_sel)
    return;

  double x0 = v->sel_x0;
  double y0 = v->sel_y0;
  double x1 = v->sel_x1;
  double y1 = v->sel_y1;
  if (x0 > x1) {
    double t = x0;
    x0 = x1;
    x1 = t;
  }
  if (y0 > y1) {
    double t = y0;
    y0 = y1;
    y1 = t;
  }

  if (x1 - x0 < 2.0 || y1 - y0 < 2.0)
    return;

  cairo_save(cr);
  cairo_set_source_rgba(cr, 0, 0, 0, 0.35);
  cairo_rectangle(cr, 0, 0, width, height);
  cairo_rectangle(cr, x0, y0, x1 - x0, y1 - y0);
  cairo_set_fill_rule(cr, CAIRO_FILL_RULE_EVEN_ODD);
  cairo_fill(cr);

  cairo_set_source_rgba(cr, 1, 1, 1, 0.9);
  cairo_set_line_width(cr, 2.0);
  cairo_rectangle(cr, x0 + 1, y0 + 1, (x1 - x0) - 2, (y1 - y0) - 2);
  cairo_stroke(cr);
  cairo_restore(cr);
}

static void on_drag_begin(GtkGestureDrag *gesture, double start_x,
                          double start_y, gpointer user_data) {
  (void)gesture;
  Viewer *v = (Viewer *)user_data;
  if (!v || !v->crop_mode)
    return;
  v->has_sel = true;
  v->sel_x0 = start_x;
  v->sel_y0 = start_y;
  v->sel_x1 = start_x;
  v->sel_y1 = start_y;
  gtk_widget_queue_draw(GTK_WIDGET(v->overlay));
}

static void on_drag_update(GtkGestureDrag *gesture, double offset_x,
                           double offset_y, gpointer user_data) {
  Viewer *v = (Viewer *)user_data;
  if (!v || !v->crop_mode || !v->has_sel)
    return;
  double start_x = 0, start_y = 0;
  gtk_gesture_drag_get_start_point(gesture, &start_x, &start_y);
  v->sel_x1 = start_x + offset_x;
  v->sel_y1 = start_y + offset_y;
  gtk_widget_queue_draw(GTK_WIDGET(v->overlay));
}

static gboolean on_key_pressed(GtkEventControllerKey *controller,
                               guint keyval, guint keycode,
                               GdkModifierType state, gpointer user_data) {
  (void)controller;
  (void)keycode;
  (void)state;
  Viewer *v = (Viewer *)user_data;
  if (!v)
    return GDK_EVENT_PROPAGATE;

  switch (keyval) {
  case GDK_KEY_Left:
  case GDK_KEY_p:
    on_prev_clicked(NULL, v);
    return GDK_EVENT_STOP;
  case GDK_KEY_Right:
  case GDK_KEY_n:
  case GDK_KEY_space:
    on_next_clicked(NULL, v);
    return GDK_EVENT_STOP;
  case GDK_KEY_plus:
  case GDK_KEY_equal:
    on_zoom_in(NULL, v);
    return GDK_EVENT_STOP;
  case GDK_KEY_minus:
  case GDK_KEY_underscore:
    on_zoom_out(NULL, v);
    return GDK_EVENT_STOP;
  case GDK_KEY_0:
    on_zoom_reset(NULL, v);
    return GDK_EVENT_STOP;
  case GDK_KEY_c:
    on_crop_toggle(NULL, v);
    return GDK_EVENT_STOP;
  case GDK_KEY_Return:
  case GDK_KEY_KP_Enter:
    on_crop_apply(NULL, v);
    return GDK_EVENT_STOP;
  case GDK_KEY_Escape:
  case GDK_KEY_q:
    gtk_window_close(v->window);
    return GDK_EVENT_STOP;
  default:
    return GDK_EVENT_PROPAGATE;
  }
}

static void on_activate(GtkApplication *app, gpointer user_data) {
  Viewer *v = (Viewer *)user_data;

  v->window = GTK_WINDOW(gtk_application_window_new(app));
  gtk_window_set_default_size(v->window, 980, 720);

  GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_window_set_child(v->window, root);

  GtkWidget *toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_widget_set_margin_top(toolbar, 8);
  gtk_widget_set_margin_bottom(toolbar, 8);
  gtk_widget_set_margin_start(toolbar, 8);
  gtk_widget_set_margin_end(toolbar, 8);
  gtk_box_append(GTK_BOX(root), toolbar);

  v->prev_btn = GTK_BUTTON(gtk_button_new_with_label("Prev"));
  v->next_btn = GTK_BUTTON(gtk_button_new_with_label("Next"));
  v->zoom_out_btn = GTK_BUTTON(gtk_button_new_with_label("-"));
  v->zoom_in_btn = GTK_BUTTON(gtk_button_new_with_label("+"));
  v->zoom_reset_btn = GTK_BUTTON(gtk_button_new_with_label("100%"));
  v->crop_btn = GTK_BUTTON(gtk_button_new_with_label("Crop"));
  v->crop_apply_btn = GTK_BUTTON(gtk_button_new_with_label("Save crop"));
  v->label = GTK_LABEL(gtk_label_new(""));
  v->zoom_label = GTK_LABEL(gtk_label_new("100%"));
  gtk_widget_set_hexpand(GTK_WIDGET(v->label), TRUE);
  gtk_label_set_xalign(v->label, 0.0f);
  gtk_widget_set_margin_start(GTK_WIDGET(v->label), 8);

  gtk_box_append(GTK_BOX(toolbar), GTK_WIDGET(v->prev_btn));
  gtk_box_append(GTK_BOX(toolbar), GTK_WIDGET(v->next_btn));
  gtk_box_append(GTK_BOX(toolbar), GTK_WIDGET(v->zoom_out_btn));
  gtk_box_append(GTK_BOX(toolbar), GTK_WIDGET(v->zoom_in_btn));
  gtk_box_append(GTK_BOX(toolbar), GTK_WIDGET(v->zoom_reset_btn));
  gtk_box_append(GTK_BOX(toolbar), GTK_WIDGET(v->crop_btn));
  gtk_box_append(GTK_BOX(toolbar), GTK_WIDGET(v->crop_apply_btn));
  gtk_box_append(GTK_BOX(toolbar), GTK_WIDGET(v->zoom_label));
  gtk_box_append(GTK_BOX(toolbar), GTK_WIDGET(v->label));

  g_signal_connect(v->prev_btn, "clicked", G_CALLBACK(on_prev_clicked), v);
  g_signal_connect(v->next_btn, "clicked", G_CALLBACK(on_next_clicked), v);
  g_signal_connect(v->zoom_out_btn, "clicked", G_CALLBACK(on_zoom_out), v);
  g_signal_connect(v->zoom_in_btn, "clicked", G_CALLBACK(on_zoom_in), v);
  g_signal_connect(v->zoom_reset_btn, "clicked", G_CALLBACK(on_zoom_reset), v);
  g_signal_connect(v->crop_btn, "clicked", G_CALLBACK(on_crop_toggle), v);
  g_signal_connect(v->crop_apply_btn, "clicked", G_CALLBACK(on_crop_apply), v);
  gtk_widget_set_sensitive(GTK_WIDGET(v->crop_apply_btn), FALSE);

  v->scroller = GTK_SCROLLED_WINDOW(gtk_scrolled_window_new());
  gtk_widget_set_vexpand(GTK_WIDGET(v->scroller), TRUE);
  gtk_box_append(GTK_BOX(root), GTK_WIDGET(v->scroller));

  GtkWidget *overlay = gtk_overlay_new();
  gtk_scrolled_window_set_child(v->scroller, overlay);

  v->picture = GTK_PICTURE(gtk_picture_new());
  gtk_picture_set_can_shrink(v->picture, TRUE);
  gtk_picture_set_content_fit(v->picture, GTK_CONTENT_FIT_CONTAIN);
  gtk_overlay_set_child(GTK_OVERLAY(overlay), GTK_WIDGET(v->picture));

  v->overlay = GTK_DRAWING_AREA(gtk_drawing_area_new());
  gtk_widget_set_hexpand(GTK_WIDGET(v->overlay), FALSE);
  gtk_widget_set_vexpand(GTK_WIDGET(v->overlay), FALSE);
  gtk_overlay_add_overlay(GTK_OVERLAY(overlay), GTK_WIDGET(v->overlay));
  gtk_widget_set_halign(GTK_WIDGET(v->overlay), GTK_ALIGN_START);
  gtk_widget_set_valign(GTK_WIDGET(v->overlay), GTK_ALIGN_START);
  gtk_widget_set_can_focus(GTK_WIDGET(v->overlay), FALSE);
  gtk_drawing_area_set_draw_func(v->overlay, overlay_draw, v, NULL);

  GtkGesture *drag = gtk_gesture_drag_new();
  gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(drag), GDK_BUTTON_PRIMARY);
  g_signal_connect(drag, "drag-begin", G_CALLBACK(on_drag_begin), v);
  g_signal_connect(drag, "drag-update", G_CALLBACK(on_drag_update), v);
  gtk_widget_add_controller(GTK_WIDGET(v->overlay),
                            GTK_EVENT_CONTROLLER(drag));

  GtkEventController *keys = gtk_event_controller_key_new();
  g_signal_connect(keys, "key-pressed", G_CALLBACK(on_key_pressed), v);
  gtk_widget_add_controller(GTK_WIDGET(v->window), keys);

  viewer_set_index(v, v->idx);
  viewer_update_zoom(v);
  gtk_window_present(v->window);
}

int main(int argc, char **argv) {
  if (argc != 2) {
    fprintf(stderr, "Usage: %s /path/to/image\n", argv[0]);
    return 2;
  }

  char *dir = path_dirname_dup(argv[1]);
  char *base = path_basename_dup(argv[1]);
  if (!dir || !base)
    die("malloc");

  size_t count = 0;
  char **names = list_images_in_dir(dir, &count);
  if (!names || count == 0) {
    fprintf(stderr, "No images found in: %s\n", dir);
    free_strv(names, count);
    free(dir);
    free(base);
    return 1;
  }

  size_t idx = 0;
  for (size_t i = 0; i < count; i++) {
    if (strcmp(names[i], base) == 0) {
      idx = i;
      break;
    }
  }

  Viewer v = {0};
  v.dir = dir;
  v.names = names;
  v.count = count;
  v.idx = idx;

  v.app = gtk_application_new("dev.tiv.viewer", G_APPLICATION_DEFAULT_FLAGS);
  g_signal_connect(v.app, "activate", G_CALLBACK(on_activate), &v);
  // We handle the file argument ourselves; passing it to GApplication without an
  // "open" handler triggers: "This application can not open files."
  char *argv0[] = {argv[0], NULL};
  int rc = g_application_run(G_APPLICATION(v.app), 1, argv0);
  g_object_unref(v.app);

  free_strv(names, count);
  free(dir);
  free(base);
  return rc;
}
