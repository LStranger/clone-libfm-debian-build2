/*
 *      fm-nav-history.h
 *      
 *      Copyright 2009 PCMan <pcman.tw@gmail.com>
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


#ifndef __FM_NAV_HISTORY_H__
#define __FM_NAV_HISTORY_H__

#include <glib-object.h>
#include "fm-path.h"

G_BEGIN_DECLS

/* FIXME: Do we really need GObject for such a simple data structure? */

#define FM_NAV_HISTORY_TYPE				(fm_nav_history_get_type())
#define FM_NAV_HISTORY(obj)				(G_TYPE_CHECK_INSTANCE_CAST((obj),\
			FM_NAV_HISTORY_TYPE, FmNavHistory))
#define FM_NAV_HISTORY_CLASS(klass)		(G_TYPE_CHECK_CLASS_CAST((klass),\
			FM_NAV_HISTORY_TYPE, FmNavHistoryClass))
#define FM_IS_NAV_HISTORY(obj)			(G_TYPE_CHECK_INSTANCE_TYPE((obj),\
			FM_NAV_HISTORY_TYPE))
#define FM_IS_NAV_HISTORY_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE((klass),\
			FM_NAV_HISTORY_TYPE))

typedef struct _FmNavHistory			FmNavHistory;
typedef struct _FmNavHistoryItem		FmNavHistoryItem;
typedef struct _FmNavHistoryClass		FmNavHistoryClass;

/**
 * FmNavHistoryItem
 * @path: active path to folder
 * @scroll_pos: how much folder was scrolled in view
 */
struct _FmNavHistoryItem
{
    FmPath* path;
    int scroll_pos;
};

struct _FmNavHistory
{
	GObject parent;
    GQueue items;
    GList* cur;
    guint n_max;
};

struct _FmNavHistoryClass
{
    /*< private >*/
	GObjectClass parent_class;
};

GType		fm_nav_history_get_type		(void);
FmNavHistory*	fm_nav_history_new			(void);

/* The returned GList belongs to FmNavHistory and shouldn't be freed. */
const GList* fm_nav_history_list(FmNavHistory* nh);
const FmNavHistoryItem* fm_nav_history_get_cur(FmNavHistory* nh);
const GList* fm_nav_history_get_cur_link(FmNavHistory* nh);
gboolean fm_nav_history_can_back(FmNavHistory* nh);
void fm_nav_history_back(FmNavHistory* nh, int old_scroll_pos);
gboolean fm_nav_history_can_forward(FmNavHistory* nh);
void fm_nav_history_forward(FmNavHistory* nh, int old_scroll_pos);
void fm_nav_history_chdir(FmNavHistory* nh, FmPath* path, int old_scroll_pos);
void fm_nav_history_jump(FmNavHistory* nh, GList* l, int old_scroll_pos);
void fm_nav_history_clear(FmNavHistory* nh);
void fm_nav_history_set_max(FmNavHistory* nh, guint num);

G_END_DECLS

#endif /* __FM_NAV_HISTORY_H__ */
