/* This entire file is licensed under GNU General Public License v3.0
 *
 * Copyright 2020 Lev Babiev
 */

#include <gtk/gtk.h>
#include <gtk-layer-shell.h>
#include "sfwbar.h"

gchar *confname=NULL, *cssname=NULL, *sockname=NULL;
static GOptionEntry entries[] = {
  {"config",'f',0,G_OPTION_ARG_FILENAME,&confname,"Specify config file"},
  {"css",'c',0,G_OPTION_ARG_FILENAME,&cssname,"Specify css file"},
  {"socket",'s',0,G_OPTION_ARG_FILENAME,&sockname,"Specify sway socket file"},
  {NULL}};

void parse_command_line ( struct context *context, int argc, char **argv)
{
  GOptionContext *optc;
  optc = g_option_context_new(" - Sway Floating Window Bar");
  g_option_context_add_main_entries(optc,entries,NULL);
  g_option_context_add_group (optc, gtk_get_option_group (TRUE));
  g_option_context_parse(optc,&argc,&argv,NULL);
}

void dispatch_event ( struct ipc_event *ev, struct context *context )
{
  if ((ev->event == 0)&&(context->features & F_PLACEMENT))
    place_window(ev->pid, context);
  if(context->features & F_TASKBAR)
  {
    if (ev->event == 99)
      ev->event = 0;
    if (ev->event == 0 || ev->event == 3)
      taskbar_update_window (ev,context);
    if (ev->event == 1)
      taskbar_delete_window(ev->pid,context);
    if (ev->event == 2)
      context->tb_focus = ev->pid;
    taskbar_refresh(context);
  }
  g_free(ev->title);
  g_free(ev->appid);
}

void init_context ( struct context *context )
{
  context->scan_list=NULL;
  context->file_list=NULL;
  context->widgets=NULL;
  context->buttons = NULL;
  context->features=0;
  context->default_dec=4;
  context->buff_len = 1024;
  context->read_buff = g_malloc(context->buff_len);
}

void placement_init ( struct context *context, json_object *obj )
{
  json_object *ptr;
  if(!json_object_object_get_ex(obj,"placement",&ptr))
    return;
  context->features |= F_PLACEMENT;
  context->wp_x= json_int_by_name(ptr,"xcascade",10);
  context->wp_y= json_int_by_name(ptr,"ycascade",10);
  if(context->wp_x<1)
    context->wp_x=1;
  if(context->wp_y<1)
    context->wp_y=1;

}

GtkWidget *load_config ( struct context *context )
{
  gchar *json, *fname;
  json_object *obj;
  GtkWidget *root;
  
  if(confname!=NULL)
    fname = g_strdup(confname);
  else
    fname = get_xdg_config_file("sfwbar.config");
  obj = json_object_from_file(fname);
  g_free(fname);
  json = (gchar *)json_util_get_last_err();
  if(json!=NULL)
    printf("%s\n",json);
  
  placement_init(context,obj);
  scanner_init(context,obj);
  root = layout_init(context,obj);

  json_object_put(obj);
  return root;
}

gint shell_timer ( struct context *context )
{
  json_object *obj;
  struct ipc_event ev;
  gint32 etype;

  scanner_expire(context->scan_list);
  widget_update_all(context);
  obj = ipc_poll(context->ipc,&etype);
  while (obj != NULL)
  {
    ev = ipc_parse_event(obj);
    if(etype==0x80000003)
      dispatch_event(&ev,context);
    if(etype==0x80000000)
      pager_update(context);
    json_object_put(obj);
    obj = ipc_poll(context->ipc,&etype);
  }
  return TRUE;
}

static void activate (GtkApplication* app, struct context *context)
{
  GtkWindow *window;
  GtkWidget *root;
  GtkCssProvider *css;
  gchar *cssf;

  window = (GtkWindow *)gtk_application_window_new (app);
  gtk_layer_init_for_window (window);
  gtk_layer_auto_exclusive_zone_enable (window);
  gtk_layer_set_keyboard_interactivity(window,FALSE);

  cssf = get_xdg_config_file("sfwbar.css");
  if(cssf!=NULL)
  {
    css = gtk_css_provider_new();
    gtk_css_provider_load_from_path(css,cssf,NULL);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
      GTK_STYLE_PROVIDER(css),GTK_STYLE_PROVIDER_PRIORITY_USER);
    g_free(cssf);
  }
  
  root = load_config(context);
  gtk_layer_set_anchor (window,GTK_LAYER_SHELL_EDGE_LEFT,TRUE);
  gtk_layer_set_anchor (window,GTK_LAYER_SHELL_EDGE_RIGHT,TRUE);
  gtk_layer_set_anchor (window,GTK_LAYER_SHELL_EDGE_BOTTOM,TRUE);
  gtk_layer_set_anchor (window,GTK_LAYER_SHELL_EDGE_TOP,FALSE);

  if(root != NULL)
  {
    gtk_container_add(GTK_CONTAINER(window), root);

    gtk_widget_show_all ((GtkWidget *)window);
    gtk_widget_set_size_request (GTK_WIDGET (window), -1, gtk_widget_get_allocated_height(context->box));
    gtk_window_resize (window, 1, 1);
  }

  if(context->features & F_TASKBAR)
    taskbar_populate(context);

  if((context->features & F_TASKBAR)||(context->features & F_PLACEMENT)||(context->features & F_PAGER))
  {
    context->ipc = ipc_open(10);
    ipc_subscribe(context->ipc);
  }

  g_timeout_add (100,(GSourceFunc )shell_timer,context);
  gtk_widget_show_all ((GtkWidget *)window);
}


int main (int argc, char **argv)
{
  GtkApplication *app;
  int status;
  struct context context;

  init_context(&context);

  parse_command_line(&context,argc,argv);

  app = gtk_application_new ("org.gtk.sfwbar", G_APPLICATION_FLAGS_NONE);
  g_signal_connect (app, "activate", G_CALLBACK (activate), &context);
  status = g_application_run (G_APPLICATION (app), argc, argv);
  g_object_unref (app);

  return status;
}
