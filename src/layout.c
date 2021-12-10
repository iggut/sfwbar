/* This entire file is licensed under GNU General Public License v3.0
 *
 * Copyright 2020 Lev Babiev
 */

#include "sfwbar.h"
#include "config.h"
#include <gtk/gtk.h>
#include <gio/gdesktopappinfo.h>

GList *widget_list;

gboolean widget_action ( GtkWidget *widget, gchar *cmd )
{
  if(cmd)
    g_spawn_command_line_async(cmd,NULL);
  return TRUE;
}

gboolean widget_ebox_action ( GtkWidget *w, GdkEventButton *ev, gchar *cmd )
{
  if(ev->type == GDK_BUTTON_PRESS && ev->button == 1)
    widget_action(w,cmd);
  return TRUE;
}

gpointer layout_scanner_thread ( gpointer data )
{
  GList *iter;
  gint64 timer;

  while ( TRUE )
  {
    scanner_expire();
    layout_widgets_update(data);
    if(!widget_list)
      g_thread_exit(NULL);
    timer = G_MAXINT64;
    for(iter=widget_list;iter!=NULL;iter=g_list_next(iter))
      timer = MIN(timer,((struct layout_widget *)iter->data)->next_poll);
    timer -= g_get_monotonic_time();
    if(timer>0)
      usleep(timer);
  }
}

struct layout_widget *layout_widget_new ( void )
{
  struct layout_widget *lw;
  lw = g_malloc(sizeof(struct layout_widget));
  lw->widget = NULL;
  lw->style = NULL;
  lw->css = NULL;
  lw->value = NULL;
  lw->action = NULL;
  lw->eval = NULL;
  lw->wtype = 0;
  lw->interval = 0;
  lw->next_poll = 0;
  lw->dir = GTK_POS_RIGHT;
  lw->ready = FALSE;
  lw->rect.x = 0;
  lw->rect.y = 0;
  lw->rect.w = 1;
  lw->rect.h = 1;
  return lw;
}

GtkWidget *layout_widget_config ( struct layout_widget *lw, GtkWidget *parent,
    GtkWidget *sibling )
{
  gint dir;
  lw->lobject = lw->widget;

  if(lw->style)
  {
    gtk_widget_set_name(lw->widget,lw->style);
    g_free(lw->style);
    lw->style = NULL;
  }

  if(lw->css)
  {
    GtkStyleContext *context = gtk_widget_get_style_context (lw->widget);
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider,lw->css,strlen(lw->css),NULL);
    gtk_style_context_add_provider (context,
      GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_USER);
    g_free(lw->css);
    lw->css = NULL;
  }

  if(GTK_IS_PROGRESS_BAR(lw->widget))
  {
    gtk_widget_style_get(lw->widget,"direction",&(lw->dir),NULL);
    if((lw->dir==GTK_POS_LEFT)||(lw->dir==GTK_POS_RIGHT))
      gtk_orientable_set_orientation(GTK_ORIENTABLE(lw->widget),GTK_ORIENTATION_HORIZONTAL);
    else
      gtk_orientable_set_orientation(GTK_ORIENTABLE(lw->widget),GTK_ORIENTATION_VERTICAL);
    if((lw->dir==GTK_POS_TOP)||(lw->dir==GTK_POS_LEFT))
      gtk_progress_bar_set_inverted(GTK_PROGRESS_BAR(lw->widget),TRUE);
    else
      gtk_progress_bar_set_inverted(GTK_PROGRESS_BAR(lw->widget),FALSE);
  }

  if(lw->wtype==G_TOKEN_TASKBAR)
    taskbar_init(lw->widget);

  if(lw->wtype==G_TOKEN_PAGER)
    pager_init(lw->widget);

  if(lw->wtype==G_TOKEN_TRAY)
    sni_init(lw->widget);

  if(GTK_IS_LABEL(lw->widget))
  {
    gdouble xalign;
    gtk_widget_style_get(lw->widget,"align",&xalign,NULL);
    gtk_label_set_xalign(GTK_LABEL(lw->widget),xalign);
  }

  if((lw->action)&&(GTK_IS_BUTTON(lw->widget)))
    g_signal_connect(G_OBJECT(lw->widget),"clicked",
      G_CALLBACK(widget_action),g_strdup(lw->action));

  if((lw->action)&&(!GTK_IS_BUTTON(lw->widget)))
  {
    lw->lobject = gtk_event_box_new();
    gtk_container_add(GTK_CONTAINER(lw->lobject),lw->widget);
    g_signal_connect(G_OBJECT(lw->lobject),"button_press_event",
        G_CALLBACK(widget_ebox_action),g_strdup(lw->action));
  }

  widget_set_css(lw->widget);

  if(GTK_IS_BUTTON(lw->widget))
  {
    lw->widget = scale_image_new();
    gtk_container_add(GTK_CONTAINER(lw->lobject),lw->widget);
  }

  if(parent)
  {
    gtk_widget_style_get(parent,"direction",&dir,NULL);
    if( (lw->rect.x < 1) || (lw->rect.y < 1 ) )
      gtk_grid_attach_next_to(GTK_GRID(parent),lw->lobject,sibling,dir,1,1);
    else
      gtk_grid_attach(GTK_GRID(parent),lw->lobject,
          lw->rect.x,lw->rect.y,lw->rect.w,lw->rect.h);
  }

  return lw->lobject;
}

void layout_widget_free ( struct layout_widget *lw )
{
  if(!lw)
    return;
  g_free(lw->style);
  g_free(lw->css);
  g_free(lw->value);
  g_free(lw->action);
  g_free(lw->eval);
  g_free(lw);
}

gboolean layout_widget_draw ( struct layout_widget *lw )
{
  if(GTK_IS_LABEL(GTK_WIDGET(lw->widget)))
    gtk_label_set_markup(GTK_LABEL(lw->widget),lw->eval);
  if(GTK_IS_PROGRESS_BAR(lw->widget))
    if(!g_strrstr(lw->eval,"nan"))
      gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(lw->widget),
          g_ascii_strtod(lw->eval,NULL));

  if(GTK_IS_IMAGE(lw->widget))
  {
    scale_image_set_image(GTK_WIDGET(lw->widget),lw->eval);
    scale_image_update(GTK_WIDGET(lw->widget));
  }

  return FALSE;
}

void layout_widgets_update ( GMainContext *gmc )
{
  GSource *src;
  GList *iter;
  gint64 ctime;
  struct layout_widget *lw;
  gchar *eval;

  ctime = g_get_monotonic_time();

  for(iter=widget_list;iter!=NULL;iter=g_list_next(iter))
  {
    lw = iter->data;

    if(lw->next_poll > ctime)
      continue;
    lw->next_poll = ctime+lw->interval;
    if((lw->value!=NULL)&&(GTK_IS_LABEL(lw->widget)||
        GTK_IS_PROGRESS_BAR(lw->widget)||GTK_IS_IMAGE(lw->widget)))
    {
      eval = expr_parse(lw->value, NULL);
      if(g_strcmp0(eval,lw->eval))
      {
        g_free(lw->eval);
        lw->eval = eval;
        src = g_idle_source_new();
        g_source_set_callback(src,(GSourceFunc)layout_widget_draw,lw,NULL);
        g_source_attach(src,gmc);
      }
      else
        g_free(eval);
    }
  }
}

void layout_widget_attach ( struct layout_widget *lw )
{
  guint vcount;

  if(!lw->value)
    return layout_widget_free(lw);

  lw->eval = expr_parse(lw->value, &vcount);
  if(!vcount)
  {
    layout_widget_draw(lw);
    return layout_widget_free(lw);
  }
  g_free(lw->eval);
  lw->eval = NULL;

  widget_list = g_list_append(widget_list,lw);
}

void widget_set_css ( GtkWidget *widget )
{
  gboolean expand;
  GList *l;
  gtk_widget_style_get(widget,"hexpand",&expand,NULL);
  gtk_widget_set_hexpand(GTK_WIDGET(widget),expand);
  gtk_widget_style_get(widget,"vexpand",&expand,NULL);
  gtk_widget_set_vexpand(GTK_WIDGET(widget),expand);
  if(GTK_IS_CONTAINER(widget))
  {
    l = gtk_container_get_children(GTK_CONTAINER(widget));
    for(;l!=NULL;l=g_list_next(l))
      widget_set_css(l->data);
  }

}
