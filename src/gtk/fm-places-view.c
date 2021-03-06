/*
 *      fm-places-view.c
 *
 *      Copyright 2009 - 2012 Hong Jen Yee (PCMan) <pcman.tw@gmail.com>
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with this program; if not, write to the Free Software
 *      Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 *      MA 02110-1301, USA.
 */

/**
 * SECTION:fm-places-view
 * @short_description: A widget for side panel with places list.
 * @title: FmPlacesView
 *
 * @include: libfm/fm-places-view.h
 *
 * The #FmPlacesView displays list of pseudo-folders which contains
 * such items as Home directory, Trash bin, mounted removable drives,
 * bookmarks, etc.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib/gi18n-lib.h>
#include "fm-places-view.h"
#include "fm-config.h"
#include "fm-gtk-utils.h"
#include "fm-bookmarks.h"
#include "fm-file-menu.h"
#include "fm-cell-renderer-pixbuf.h"
#include "fm-dnd-auto-scroll.h"
#include "fm-places-model.h"

enum
{
    CHDIR,
    N_SIGNALS
};

static void activate_row(FmPlacesView* view, guint button, GtkTreePath* tree_path);
static void on_row_activated( GtkTreeView* view, GtkTreePath* tree_path, GtkTreeViewColumn *col);
static gboolean on_button_press(GtkWidget* view, GdkEventButton* evt);
static gboolean on_button_release(GtkWidget* view, GdkEventButton* evt);

static void on_mount(GtkAction* act, gpointer user_data);
static void on_umount(GtkAction* act, gpointer user_data);
static void on_eject(GtkAction* act, gpointer user_data);

static void on_remove_bm(GtkAction* act, gpointer user_data);
static void on_rename_bm(GtkAction* act, gpointer user_data);
static void on_empty_trash(GtkAction* act, gpointer user_data);

static gboolean on_dnd_dest_files_dropped(FmDndDest* dd, int x, int y, GdkDragAction action,
                                       int info_type, FmFileInfoList* files, FmPlacesView* view);

//static void on_trash_changed(GFileMonitor *monitor, GFile *gf, GFile *other, GFileMonitorEvent evt, gpointer user_data);
//static void on_use_trash_changed(FmConfig* cfg, gpointer unused);
//static void on_pane_icon_size_changed(FmConfig* cfg, gpointer unused);

G_DEFINE_TYPE(FmPlacesView, fm_places_view, GTK_TYPE_TREE_VIEW);

/** One common FmPlacesModel for all FmPlacesView instances */
static FmPlacesModel* model = NULL;

static guint signals[N_SIGNALS];

static const char vol_menu_xml[]=
"<popup>"
  "<placeholder name='ph3'>"
  "<menuitem action='Mount'/>"
  "<menuitem action='Unmount'/>"
  "<menuitem action='Eject'/>"
  "</placeholder>"
"</popup>";

static const char mount_menu_xml[]=
"<popup>"
  "<placeholder name='ph3'>"
  "<menuitem action='Unmount'/>"
  "</placeholder>"
"</popup>";

static GtkActionEntry vol_menu_actions[]=
{
    {"Mount", NULL, N_("Mount Volume"), NULL, NULL, G_CALLBACK(on_mount)},
    {"Unmount", NULL, N_("Unmount Volume"), NULL, NULL, G_CALLBACK(on_umount)},
    {"Eject", NULL, N_("Eject Removable Media"), NULL, NULL, G_CALLBACK(on_eject)}
};

static const char bookmark_menu_xml[]=
"<popup>"
  "<placeholder name='ph3'>"
  "<menuitem action='RenameBm'/>"
  "<menuitem action='RemoveBm'/>"
  "</placeholder>"
"</popup>";

static GtkActionEntry bm_menu_actions[]=
{
    {"RenameBm", GTK_STOCK_EDIT, N_("Rename Bookmark Item"), NULL, NULL, G_CALLBACK(on_rename_bm)},
    {"RemoveBm", GTK_STOCK_REMOVE, N_("Remove from Bookmark"), NULL, NULL, G_CALLBACK(on_remove_bm)}
};

static const char trash_menu_xml[]=
"<popup>"
  "<placeholder name='ph3'>"
  "<menuitem action='EmptyTrash'/>"
  "</placeholder>"
"</popup>";

static GtkActionEntry trash_menu_actions[]=
{
    {"EmptyTrash", NULL, N_("Empty Trash"), NULL, NULL, G_CALLBACK(on_empty_trash)}
};

enum {
    FM_DND_DEST_TARGET_BOOOKMARK = N_FM_DND_DEST_DEFAULT_TARGETS + 1
};

static const GtkTargetEntry dnd_dest_targets[] =
{
    {"GTK_TREE_MODEL_ROW", GTK_TARGET_SAME_WIDGET, FM_DND_DEST_TARGET_BOOOKMARK}
};

/* Target types for dragging from the shortcuts list */
static const GtkTargetEntry dnd_src_targets[] = {
    { "GTK_TREE_MODEL_ROW", GTK_TARGET_SAME_WIDGET, FM_DND_DEST_TARGET_BOOOKMARK }
};

static void fm_places_view_dispose(GObject *object)
{
    FmPlacesView* self;

    g_return_if_fail(object != NULL);
    g_return_if_fail(FM_IS_PLACES_VIEW(object));
    self = (FmPlacesView*)object;

    if(self->dnd_dest)
    {
        g_signal_handlers_disconnect_by_func(self->dnd_dest, on_dnd_dest_files_dropped, self);
        g_object_unref(self->dnd_dest);
        self->dnd_dest = NULL;
    }

    G_OBJECT_CLASS(fm_places_view_parent_class)->dispose(object);
}

static void fm_places_view_finalize(GObject *object)
{
    FmPlacesView* self;

    g_return_if_fail(object != NULL);
    g_return_if_fail(FM_IS_PLACES_VIEW(object));
    self = (FmPlacesView*)object;

    if(self->clicked_row)
        gtk_tree_path_free(self->clicked_row);

    G_OBJECT_CLASS(fm_places_view_parent_class)->finalize(object);
}

static gboolean sep_func( GtkTreeModel* model, GtkTreeIter* it, gpointer data )
{
    return fm_places_model_iter_is_separator(FM_PLACES_MODEL(model), it);
}

static void on_renderer_icon_size_changed(FmConfig* cfg, gpointer user_data)
{
    FmCellRendererPixbuf* render = FM_CELL_RENDERER_PIXBUF(user_data);
    fm_cell_renderer_pixbuf_set_fixed_size(render, fm_config->pane_icon_size, fm_config->pane_icon_size);
}

static void on_cell_renderer_pixbuf_destroy(gpointer user_data, GObject* render)
{
    g_signal_handler_disconnect(fm_config, GPOINTER_TO_UINT(user_data));
}

/* Given a drop path retrieved by gtk_tree_view_get_dest_row_at_pos, this function
 * determines whether dropping a bookmark item at the specified path is allow.
 * If dropping is not allowed, this function tries to choose an alternative position
 * for the bookmark item and modified the tree path @tp passed into this function. */
static gboolean get_bookmark_drag_dest(FmPlacesView* view, GtkTreePath** tp, GtkTreeViewDropPosition* pos)
{
    gboolean ret = TRUE;
    if(*tp)
    {
        /* if the drop site is below the separator (in the bookmark area) */
        if(fm_places_model_path_is_bookmark(model, *tp))
        {
            /* we cannot drop into a item */
            if(*pos == GTK_TREE_VIEW_DROP_INTO_OR_BEFORE ||
               *pos == GTK_TREE_VIEW_DROP_INTO_OR_AFTER)
                ret = FALSE;
            else
                ret = TRUE;
        }
        else /* the drop site is above the separator (in the places area containing volumes) */
        {
            GtkTreePath* sep = fm_places_model_get_separator_path(model);
            /* set drop site at the first bookmark item */
            gtk_tree_path_get_indices(*tp)[0] = gtk_tree_path_get_indices(sep)[0] + 1;
            gtk_tree_path_free(sep);
            *pos = GTK_TREE_VIEW_DROP_BEFORE;
            ret = TRUE;
        }
    }
    else
    {
        /* drop at end of the bookmarks list instead */
        *tp = gtk_tree_path_new_from_indices(gtk_tree_model_iter_n_children(GTK_TREE_MODEL(model), NULL) - 1, -1);
        *pos = GTK_TREE_VIEW_DROP_AFTER;
        ret = TRUE;
    }
    /* g_debug("path: %s", gtk_tree_path_to_string(*tp)); */
    return ret;
}

static gboolean on_drag_motion (GtkWidget *dest_widget,
                    GdkDragContext *drag_context, gint x, gint y, guint time)
{
    FmPlacesView* view = FM_PLACES_VIEW(dest_widget);
    /* fm_drag_context_has_target_name(drag_context, "GTK_TREE_MODEL_ROW"); */
    GdkAtom target;
    GtkTreeViewDropPosition pos;
    GtkTreePath* tp;
    gboolean ret = FALSE;
    GdkDragAction action = 0;

    target = gtk_drag_dest_find_target(dest_widget, drag_context, NULL);
    if(target == GDK_NONE)
        return FALSE;

    gtk_tree_view_get_dest_row_at_pos(&view->parent, x, y, &tp, &pos);

    /* handle reordering bookmark items first */
    if(target == gdk_atom_intern_static_string("GTK_TREE_MODEL_ROW"))
    {
        /* bookmark item is being dragged */
        ret = get_bookmark_drag_dest(view, &tp, &pos);
        action = ret ? GDK_ACTION_MOVE : 0; /* bookmark items can only be moved */
    }
    /* try FmDndDest */
    else if(fm_dnd_dest_is_target_supported(view->dnd_dest, target))
    {
        /* query default action (this may trigger drag-data-received signal)
         * FIXME: this is a dirty and bad API design definitely requires refactor. */
        action = fm_dnd_dest_get_default_action(view->dnd_dest, drag_context, target);

        /* the user is dragging files. get FmFileInfo of drop site. */
        if(pos == GTK_TREE_VIEW_DROP_INTO_OR_BEFORE || pos == GTK_TREE_VIEW_DROP_INTO_OR_AFTER) /* drag into items */
        {
            FmPlacesItem* item = NULL;
            GtkTreeIter it;
            FmFileInfo* fi;
            /* FIXME: handle adding bookmarks with Dnd */
            if(tp && gtk_tree_model_get_iter(GTK_TREE_MODEL(model), &it, tp))
                gtk_tree_model_get(GTK_TREE_MODEL(model), &it, FM_PLACES_MODEL_COL_INFO, &item, -1);

            fi = item ? fm_places_item_get_info(item) : NULL;
            fm_dnd_dest_set_dest_file(view->dnd_dest, fi);
            ret = action != 0;
        }
        else /* drop between items, create bookmark items for dragged files */
        {
            fm_dnd_dest_set_dest_file(view->dnd_dest, NULL);
            if( (!tp || fm_places_model_path_is_bookmark(model, tp))
               && get_bookmark_drag_dest(view, &tp, &pos)) /* tp is after separator */
            {
                action = GDK_ACTION_LINK;
                ret = TRUE;
            }
            else
            {
                action = 0;
                ret = FALSE;
            }
        }
    }
    gdk_drag_status(drag_context, action, time);

    if(ret)
        gtk_tree_view_set_drag_dest_row(&view->parent, tp, pos);
    else
        gtk_tree_view_set_drag_dest_row(&view->parent, NULL, 0);

    if(tp)
        gtk_tree_path_free(tp);

    return ret;
}

static void on_drag_leave ( GtkWidget *dest_widget,
                    GdkDragContext *drag_context, guint time)
{
    FmPlacesView* view = FM_PLACES_VIEW(dest_widget);
    gtk_tree_view_set_drag_dest_row(&view->parent, NULL, 0);
    /* g_debug("drag_leave"); */
    fm_dnd_dest_drag_leave(view->dnd_dest, drag_context, time);
}

static gboolean on_drag_drop ( GtkWidget *dest_widget,
                    GdkDragContext *drag_context, gint x, gint y, guint time)
{
    FmPlacesView* view = FM_PLACES_VIEW(dest_widget);
    gboolean ret = FALSE;

    GdkAtom target = gtk_drag_dest_find_target(dest_widget, drag_context, NULL);
    /* this is to reorder bookmark */
    if(target == gdk_atom_intern_static_string("GTK_TREE_MODEL_ROW"))
    {
        gtk_drag_get_data(dest_widget, drag_context, target, time);
        ret = TRUE;
    }
    else
    {
        /* try FmDndDest */
        ret = fm_dnd_dest_drag_drop(view->dnd_dest, drag_context, target, x, y, time);
        if(!ret)
            gtk_drag_finish(drag_context, FALSE, FALSE, time);
    }
    return ret;
}

static void on_drag_data_received ( GtkWidget *dest_widget,
                GdkDragContext *drag_context, gint x, gint y,
                GtkSelectionData *sel_data, guint info, guint time)
{
    FmPlacesView* view = FM_PLACES_VIEW(dest_widget);
    GtkTreePath* dest_tp = NULL;
    GtkTreeViewDropPosition pos;

    gtk_tree_view_get_dest_row_at_pos(&view->parent, x, y, &dest_tp, &pos);
    switch(info)
    {
    case FM_DND_DEST_TARGET_BOOOKMARK:
        if(get_bookmark_drag_dest(view, &dest_tp, &pos)) /* got the drop position */
        {
            GtkTreePath* src_tp;
            /* get the source row */
            gboolean ret = gtk_tree_get_row_drag_data(sel_data, NULL, &src_tp);
            if(ret)
            {
                /* don't do anything if source and dest are the same row */
                if(G_UNLIKELY(gtk_tree_path_compare(src_tp, dest_tp) == 0))
                    ret = FALSE;
                else
                {
                    /* don't do anything if this is not a bookmark item */
                    if(!fm_places_model_path_is_bookmark(model, src_tp))
                        ret = FALSE;
                }
                if(ret)
                {
                    GtkTreeIter src_it, dest_it;
                    FmPlacesItem* item = NULL;
                    ret = FALSE;
                    /* get the source bookmark item */
                    if(gtk_tree_model_get_iter(GTK_TREE_MODEL(model), &src_it, src_tp))
                        gtk_tree_model_get(GTK_TREE_MODEL(model), &src_it, FM_PLACES_MODEL_COL_INFO, &item, -1);
                    if(item)
                    {
                        /* move it to destination position */
                        if(gtk_tree_model_get_iter(GTK_TREE_MODEL(model), &dest_it, dest_tp))
                        {
                            int new_pos, sep_pos;
                            /* get index of the separator */
                            GtkTreePath* sep_tp = fm_places_model_get_separator_path(model);
                            sep_pos = gtk_tree_path_get_indices(sep_tp)[0];

                            if(pos == GTK_TREE_VIEW_DROP_BEFORE)
                                gtk_list_store_move_before(GTK_LIST_STORE(model), &src_it, &dest_it);
                            else
                                gtk_list_store_move_after(GTK_LIST_STORE(model), &src_it, &dest_it);
                            new_pos = gtk_tree_path_get_indices(dest_tp)[0] - sep_pos - 1;
                            /* reorder the bookmark item */
                            fm_bookmarks_reorder(fm_places_model_get_bookmarks(model), fm_places_item_get_bookmark_item(item), new_pos);
                            gtk_tree_path_free(sep_tp);
                            ret = TRUE;
                        }
                    }
                }
                gtk_tree_path_free(src_tp);
            }
            gtk_drag_finish(drag_context, ret, FALSE, time);
        }
        break;
    default:
        /* check if files are received. */
        fm_dnd_dest_drag_data_received(view->dnd_dest, drag_context, x, y, sel_data, info, time);
        break;
    }
    if(dest_tp)
        gtk_tree_path_free(dest_tp);
}

static void fm_places_view_init(FmPlacesView *self)
{
    GtkTreeViewColumn* col;
    GtkCellRenderer* renderer;
    GtkTargetList* targets;
    guint handler;

    if(G_UNLIKELY(!model))
    {
        model = fm_places_model_new();
        g_object_add_weak_pointer(G_OBJECT(model), (gpointer*)&model);
    }
    else
        g_object_ref(model);

    gtk_tree_view_set_model(GTK_TREE_VIEW(self), GTK_TREE_MODEL(model));
    g_object_unref(model);

    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(self), FALSE);
    gtk_tree_view_set_row_separator_func(GTK_TREE_VIEW(self), sep_func, NULL, NULL);

    col = gtk_tree_view_column_new();
    renderer = (GtkCellRenderer*)fm_cell_renderer_pixbuf_new();
    handler = g_signal_connect(fm_config, "changed::pane_icon_size", G_CALLBACK(on_renderer_icon_size_changed), renderer);
    g_object_weak_ref(G_OBJECT(renderer), on_cell_renderer_pixbuf_destroy, GUINT_TO_POINTER(handler));
    fm_cell_renderer_pixbuf_set_fixed_size((FmCellRendererPixbuf*)renderer, fm_config->pane_icon_size, fm_config->pane_icon_size);

    gtk_tree_view_column_pack_start( col, renderer, FALSE );
    gtk_tree_view_column_set_attributes( col, renderer,
                                         "pixbuf", FM_PLACES_MODEL_COL_ICON, NULL );

    renderer = gtk_cell_renderer_text_new();
    gtk_tree_view_column_pack_start( col, renderer, TRUE );
    g_object_set(renderer, "ellipsize", PANGO_ELLIPSIZE_END, NULL);
    gtk_tree_view_column_set_attributes( col, renderer,
                                         "text", FM_PLACES_MODEL_COL_LABEL, NULL );

    renderer = gtk_cell_renderer_pixbuf_new();
    self->mount_indicator_renderer = GTK_CELL_RENDERER_PIXBUF(renderer);
    gtk_tree_view_column_pack_start( col, renderer, FALSE );
    gtk_cell_layout_set_cell_data_func(GTK_CELL_LAYOUT(col), renderer,
                                       fm_places_model_mount_indicator_cell_data_func,
                                       NULL, NULL);

    gtk_tree_view_append_column ( GTK_TREE_VIEW(self), col );

    gtk_tree_view_enable_model_drag_source(GTK_TREE_VIEW(self), GDK_BUTTON1_MASK,
                      dnd_src_targets, G_N_ELEMENTS(dnd_src_targets), GDK_ACTION_MOVE);

    gtk_drag_dest_set(GTK_WIDGET(self), 0,
            fm_default_dnd_dest_targets, N_FM_DND_DEST_DEFAULT_TARGETS,
            GDK_ACTION_COPY|GDK_ACTION_MOVE|GDK_ACTION_LINK|GDK_ACTION_ASK);
    targets = gtk_drag_dest_get_target_list(GTK_WIDGET(self));
    /* add our own targets */
    gtk_target_list_add_table(targets, dnd_dest_targets, G_N_ELEMENTS(dnd_dest_targets));

    self->dnd_dest = fm_dnd_dest_new(GTK_WIDGET(self));
    g_signal_connect(self->dnd_dest, "files_dropped", G_CALLBACK(on_dnd_dest_files_dropped), self);
}

/**
 * fm_places_view_new
 *
 * Creates new #FmPlacesView widget.
 *
 * Returns: (transfer full): a new #FmPlacesView object.
 *
 * Since: 0.1.0
 */
FmPlacesView *fm_places_view_new(void)
{
    return g_object_new(FM_PLACES_VIEW_TYPE, NULL);
}

static void activate_row(FmPlacesView* view, guint button, GtkTreePath* tree_path)
{
    GtkTreeIter it;
    if(gtk_tree_model_get_iter(GTK_TREE_MODEL(model), &it, tree_path))
    {
        FmPlacesItem* item;
        FmPath* path;
        gtk_tree_model_get(GTK_TREE_MODEL(model), &it, FM_PLACES_MODEL_COL_INFO, &item, -1);
        if(!item)
            return;
        switch(fm_places_item_get_type(item))
        {
        case FM_PLACES_ITEM_PATH:
        case FM_PLACES_ITEM_MOUNT:
            path = fm_path_ref(fm_places_item_get_path(item));
            break;
        case FM_PLACES_ITEM_VOLUME:
        {
            GFile* gf;
            GMount* mnt = g_volume_get_mount(fm_places_item_get_volume(item));
            if(!mnt)
            {
                GtkWindow* parent = GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(view)));
                if(!fm_mount_volume(parent, fm_places_item_get_volume(item), TRUE))
                    return;
                mnt = g_volume_get_mount(fm_places_item_get_volume(item));
                if(!mnt)
                {
                    g_debug("GMount is invalid after successful g_volume_mount().\nThis is quite possibly a gvfs bug.\nSee https://bugzilla.gnome.org/show_bug.cgi?id=552168");
                    return;
                }
            }
            gf = g_mount_get_root(mnt);
            g_object_unref(mnt);
            if(gf)
            {
                path = fm_path_new_for_gfile(gf);
                g_object_unref(gf);
            }
            else
                path = NULL;
            break;
        }
        default:
            return;
        }

        if(path)
        {
            g_signal_emit(view, signals[CHDIR], 0, button, path);
            fm_path_unref(path);
        }
    }
}

static void on_row_activated(GtkTreeView* view, GtkTreePath* tree_path, GtkTreeViewColumn *col)
{
    activate_row(FM_PLACES_VIEW(view), 1, tree_path);
}

/**
 * fm_places_view_chdir
 * @pv: a widget to apply
 * @path: the new path
 *
 * Changes active path and eventually sends the #FmPlacesView::chdir signal.
 *
 * Before 1.0.0 this call had name fm_places_chdir.
 * Before 0.1.12 this call had name fm_places_select.
 *
 * Since: 0.1.0
 */
void fm_places_view_chdir(FmPlacesView* pv, FmPath* path)
{
    GtkTreeIter it;
    GtkTreeModel* model = gtk_tree_view_get_model(GTK_TREE_VIEW(pv));
    GtkTreeSelection* ts = gtk_tree_view_get_selection(GTK_TREE_VIEW(pv));
    if(fm_places_model_get_iter_by_fm_path(FM_PLACES_MODEL(model), &it, path))
    {
        gtk_tree_selection_select_iter(ts, &it);
    }
    else
        gtk_tree_selection_unselect_all(ts);
}

static gboolean on_button_release(GtkWidget* widget, GdkEventButton* evt)
{
    FmPlacesView* view = FM_PLACES_VIEW(widget);
    if(view->clicked_row)
    {
        if(evt->button == 1)
        {
            GtkTreePath* tp;
            GtkTreeViewColumn* col;
            int cell_x;
            if(gtk_tree_view_get_path_at_pos(&view->parent, evt->x, evt->y, &tp, &col, &cell_x, NULL))
            {
                /* check if we release the button on the row we previously clicked. */
                if(gtk_tree_path_compare(tp, view->clicked_row) == 0)
                {
                    /* check if we click on the "eject" icon. */
                    int start, cell_w;
                    gtk_tree_view_column_cell_get_position(col, GTK_CELL_RENDERER(view->mount_indicator_renderer),
                                                           &start, &cell_w);
                    if(cell_x > start && cell_x < (start + cell_w)) /* click on eject icon */
                    {
                        GtkTreeIter it;
                        /* do eject if needed */
                        if(gtk_tree_model_get_iter(GTK_TREE_MODEL(model), &it, tp))
                        {
                            FmPlacesItem* item;
                            gtk_tree_model_get(GTK_TREE_MODEL(model), &it, FM_PLACES_MODEL_COL_INFO, &item, -1);
                            if(item && fm_places_item_is_mounted(item))
                            {
                                GtkWindow* toplevel = GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(view)));
                                GMount* mount;
                                GVolume* volume;
                                switch(fm_places_item_get_type(item))
                                {
                                case FM_PLACES_ITEM_VOLUME:
                                    volume = fm_places_item_get_volume(item);
                                    /* eject the volume */
                                    if(g_volume_can_eject(volume))
                                        fm_eject_volume(toplevel, volume, TRUE);
                                    else /* not ejectable, do unmount */
                                    {
                                        mount = g_volume_get_mount(volume);
                                        if(mount)
                                        {
                                            fm_unmount_mount(toplevel, mount, TRUE);
                                            g_object_unref(mount);
                                        }
                                    }
                                    break;
                                case FM_PLACES_ITEM_MOUNT:
                                    mount = fm_places_item_get_mount(item);
                                    if(g_mount_can_unmount(mount))
                                        fm_unmount_mount(toplevel, mount, TRUE);
                                    break;
                                default:
                                    break;
                                }
                                gtk_tree_path_free(tp);

                                gtk_tree_path_free(view->clicked_row);
                                view->clicked_row = NULL;
                                goto _out;
                            }
                        }
                    }
                    /* activate the clicked row. */
                    gtk_tree_view_row_activated(&view->parent, view->clicked_row, col);
                }
                gtk_tree_path_free(tp);
            }
        }

        gtk_tree_path_free(view->clicked_row);
        view->clicked_row = NULL;
    }
_out:
    return GTK_WIDGET_CLASS(fm_places_view_parent_class)->button_release_event(widget, evt);
}

static void place_item_menu_unref(gpointer ui, GObject *menu)
{
    g_object_unref(ui);
}

static GtkWidget* place_item_get_menu(FmPlacesItem* item)
{
    GtkWidget* menu = NULL;
    GtkUIManager* ui = gtk_ui_manager_new();
    GtkActionGroup* act_grp = gtk_action_group_new("Popup");
    gtk_action_group_set_translation_domain(act_grp, GETTEXT_PACKAGE);

    /* FIXME: merge with FmFileMenu when possible */
    if(fm_places_item_get_type(item) == FM_PLACES_ITEM_PATH)
    {
        if(fm_places_item_get_bookmark_item(item))
        {
            gtk_action_group_add_actions(act_grp, bm_menu_actions, G_N_ELEMENTS(bm_menu_actions), item);
            gtk_ui_manager_add_ui_from_string(ui, bookmark_menu_xml, -1, NULL);
        }
        else if(fm_path_is_trash_root(fm_places_item_get_path(item)))
        {
            gtk_action_group_add_actions(act_grp, trash_menu_actions, G_N_ELEMENTS(trash_menu_actions), item);
            gtk_ui_manager_add_ui_from_string(ui, trash_menu_xml, -1, NULL);
        }
    }
    else if(fm_places_item_get_type(item) == FM_PLACES_ITEM_VOLUME)
    {
        GtkAction* act;
        GMount* mnt;
        gtk_action_group_add_actions(act_grp, vol_menu_actions, G_N_ELEMENTS(vol_menu_actions), item);
        gtk_ui_manager_add_ui_from_string(ui, vol_menu_xml, -1, NULL);

        mnt = g_volume_get_mount(fm_places_item_get_volume(item));
        if(mnt) /* mounted */
        {
            g_object_unref(mnt);
            act = gtk_action_group_get_action(act_grp, "Mount");
            gtk_action_set_sensitive(act, FALSE);
        }
        else /* not mounted */
        {
            act = gtk_action_group_get_action(act_grp, "Unmount");
            gtk_action_set_sensitive(act, FALSE);
        }

        if(g_volume_can_eject(fm_places_item_get_volume(item)))
            act = gtk_action_group_get_action(act_grp, "Unmount");
        else
            act = gtk_action_group_get_action(act_grp, "Eject");
        gtk_action_set_visible(act, FALSE);
    }
    else if(fm_places_item_get_type(item) == FM_PLACES_ITEM_MOUNT)
    {
        GtkAction* act;
        GMount* mnt;
        gtk_action_group_add_actions(act_grp, vol_menu_actions, G_N_ELEMENTS(vol_menu_actions), item);
        gtk_ui_manager_add_ui_from_string(ui, mount_menu_xml, -1, NULL);

        mnt = fm_places_item_get_mount(item);
        if(mnt) /* mounted */
        {
            act = gtk_action_group_get_action(act_grp, "Mount");
            gtk_action_set_sensitive(act, FALSE);
        }
        else /* not mounted */
        {
            act = gtk_action_group_get_action(act_grp, "Unmount");
            gtk_action_set_sensitive(act, FALSE);
        }
    }
    else
        goto _out;
    gtk_ui_manager_insert_action_group(ui, act_grp, 0);

    menu = gtk_ui_manager_get_widget(ui, "/popup");
    if(menu)
    {
        g_signal_connect(menu, "selection-done", G_CALLBACK(gtk_widget_destroy), NULL);
        g_object_weak_ref(G_OBJECT(menu), place_item_menu_unref, g_object_ref(ui));
    }

_out:
    g_object_unref(act_grp);
    g_object_unref(ui);
    return menu;
}

static gboolean on_button_press(GtkWidget* widget, GdkEventButton* evt)
{
    FmPlacesView* view = FM_PLACES_VIEW(widget);
    GtkTreePath* tp;
    GtkTreeViewColumn* col;
    gboolean ret = GTK_WIDGET_CLASS(fm_places_view_parent_class)->button_press_event(widget, evt);

    gtk_tree_view_get_path_at_pos(&view->parent, evt->x, evt->y, &tp, &col, NULL, NULL);
    if(view->clicked_row) /* what? more than one botton clicked? */
        gtk_tree_path_free(view->clicked_row);
    view->clicked_row = tp;
    if(tp)
    {
        switch(evt->button) /* middle click */
        {
        case 1: /* left click */
            break;
        case 2: /* middle click */
            activate_row(view, 2, tp);
            break;
        case 3: /* right click */
            {
                GtkTreeIter it;
                if(gtk_tree_model_get_iter(GTK_TREE_MODEL(model), &it, tp)
                    && !fm_places_model_iter_is_separator(model, &it) )
                {
                    FmPlacesItem* item;
                    GtkMenu* menu;
                    gtk_tree_model_get(GTK_TREE_MODEL(model), &it, FM_PLACES_MODEL_COL_INFO, &item, -1);
                    menu = GTK_MENU(place_item_get_menu(item));
                    if(menu)
                    {
                        gtk_menu_attach_to_widget(menu, widget, NULL);
                        gtk_menu_popup(menu, NULL, NULL, NULL, NULL, 3, evt->time);
                    }
                }
            }
            break;
        }
    }
    return ret;
}

static void on_mount(GtkAction* act, gpointer user_data)
{
    FmPlacesItem* item = (FmPlacesItem*)user_data;
    if(fm_places_item_get_type(item) == FM_PLACES_ITEM_VOLUME)
    {
        GMount* mnt = g_volume_get_mount(fm_places_item_get_volume(item));
        if(!mnt)
        {
            if(!fm_mount_volume(NULL, fm_places_item_get_volume(item), TRUE))
                return;
        }
        else
            g_object_unref(mnt);
    }
}

static void on_umount(GtkAction* act, gpointer user_data)
{
    FmPlacesItem* item = (FmPlacesItem*)user_data;
    GMount* mnt;
    switch(fm_places_item_get_type(item))
    {
    case FM_PLACES_ITEM_VOLUME:
        mnt = g_volume_get_mount(fm_places_item_get_volume(item));
        break;
    case FM_PLACES_ITEM_MOUNT:
        /* FIXME: this seems to be broken. */
        mnt = g_object_ref(fm_places_item_get_mount(item));
        break;
    default:
        mnt = NULL;
    }

    if(mnt)
    {
        fm_unmount_mount(NULL, mnt, TRUE);
        g_object_unref(mnt);
    }
}

static void on_eject(GtkAction* act, gpointer user_data)
{
    FmPlacesItem* item = (FmPlacesItem*)user_data;
    if(fm_places_item_get_type(item) == FM_PLACES_ITEM_VOLUME)
    {
        /* FIXME: get the toplevel window here */
        fm_eject_volume(NULL, fm_places_item_get_volume(item), TRUE);
    }
}

static void on_remove_bm(GtkAction* act, gpointer user_data)
{
    FmPlacesItem* item = (FmPlacesItem*)user_data;
    fm_bookmarks_remove(fm_places_model_get_bookmarks(model), fm_places_item_get_bookmark_item(item));
}

static void on_rename_bm(GtkAction* act, gpointer user_data)
{
    FmPlacesItem* item = (FmPlacesItem*)user_data;
    /* FIXME: we need to set a proper parent window for the dialog */
    char* new_name = fm_get_user_input(NULL, _("Rename Bookmark Item"),
                                       _("Enter a new name:"),
                                       fm_places_item_get_bookmark_item(item)->name);
    if(new_name)
    {
        if(strcmp(new_name, fm_places_item_get_bookmark_item(item)->name))
        {
            fm_bookmarks_rename(fm_places_model_get_bookmarks(model), fm_places_item_get_bookmark_item(item), new_name);
        }
        g_free(new_name);
    }
}

static void on_empty_trash(GtkAction* act, gpointer user_data)
{
    /* FIXME: This is very dirty, but it's inevitable. :-( */
    GSList* proxies = gtk_action_get_proxies(act);
    GtkWidget* menu_item = proxies->data ? GTK_WIDGET(proxies->data) : NULL;
    GtkWidget* menu = gtk_widget_get_parent(menu_item);
    GtkWidget* view = gtk_menu_get_attach_widget(GTK_MENU(menu));
    fm_empty_trash(view ? GTK_WINDOW(gtk_widget_get_toplevel(view)) : NULL);
}

static gboolean on_dnd_dest_files_dropped(FmDndDest* dd, int x, int y, GdkDragAction action,
                               int info_type, FmFileInfoList* files, FmPlacesView* view)
{
    FmPath* dest;
    GList* l;
    gboolean ret = FALSE;

    dest = fm_dnd_dest_get_dest_path(dd);
    /* g_debug("action= %d, %d files-dropped!, info_type: %d", action, fm_list_get_length(files), info_type); */

    if(!dest && action == GDK_ACTION_LINK) /* add bookmarks */
    {
        GtkTreePath* tp;
        GtkTreeViewDropPosition pos;
        gtk_tree_view_get_dest_row_at_pos(GTK_TREE_VIEW(view), x, y, &tp, &pos);

        if(get_bookmark_drag_dest(view, &tp, &pos))
        {
            GtkTreePath* sep = fm_places_model_get_separator_path(model);
            int idx = gtk_tree_path_get_indices(tp)[0] - gtk_tree_path_get_indices(sep)[0];
            if(pos == GTK_TREE_VIEW_DROP_BEFORE)
                --idx;
            for( l=fm_file_info_list_peek_head_link(files); l; l=l->next, ++idx )
            {
                FmFileInfo* fi = FM_FILE_INFO(l->data);
                if(fm_file_info_is_dir(fi))
                    fm_bookmarks_insert(fm_places_model_get_bookmarks(model), fm_file_info_get_path(fi), fm_file_info_get_disp_name(fi), idx);
                /* we don't need to add item to places view. Later the bookmarks will be reloaded. */
            }
            gtk_tree_path_free(sep);
        }
        if(tp)
            gtk_tree_path_free(tp);
        ret = TRUE;
    }

    return ret;
}

static void on_set_scroll_adjustments(GtkTreeView* view, GtkAdjustment* hadj, GtkAdjustment* vadj)
{
    /* we don't want scroll horizontally, so we pass NULL instead of hadj. */
    fm_dnd_set_dest_auto_scroll(GTK_WIDGET(view), NULL, vadj);
    GTK_TREE_VIEW_CLASS(fm_places_view_parent_class)->set_scroll_adjustments(view, hadj, vadj);
}

static void fm_places_view_class_init(FmPlacesViewClass *klass)
{
    GObjectClass *g_object_class;
    GtkWidgetClass* widget_class;
    GtkTreeViewClass* tv_class;
    g_object_class = G_OBJECT_CLASS(klass);
    g_object_class->dispose = fm_places_view_dispose;
    g_object_class->finalize = fm_places_view_finalize;

    widget_class = GTK_WIDGET_CLASS(klass);
    widget_class->button_press_event = on_button_press;
    widget_class->button_release_event = on_button_release;
    widget_class->drag_motion = on_drag_motion;
    widget_class->drag_leave = on_drag_leave;
    widget_class->drag_drop = on_drag_drop;
    widget_class->drag_data_received = on_drag_data_received;

    tv_class = GTK_TREE_VIEW_CLASS(klass);
    tv_class->row_activated = on_row_activated;
    tv_class->set_scroll_adjustments = on_set_scroll_adjustments;

    /**
     * FmPlacesView::chdir:
     * @view: a view instance that emitted the signal
     * @button: the button row was activated with
     * @path: (#FmPath *) new directory path
     *
     * The #FmPlacesView::chdir signal is emitted when current selected
     * directory in view is changed.
     *
     * Since: 0.1.0
     */
    signals[CHDIR] =
        g_signal_new("chdir",
                     G_TYPE_FROM_CLASS(klass),
                     G_SIGNAL_RUN_LAST,
                     G_STRUCT_OFFSET(FmPlacesViewClass, chdir),
                     NULL, NULL,
                     g_cclosure_marshal_VOID__UINT_POINTER,
                     G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_POINTER);
}
