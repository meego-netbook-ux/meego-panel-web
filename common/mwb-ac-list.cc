/*
 * Meego-Web-Browser: The web browser for Meego
 * Copyright (c) 2009, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU Lesser General Public License,
 * version 2.1, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
 * License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
extern "C" {
#include <cogl/cogl-pango.h>
}
#include <string.h>
#include <glib/gi18n.h>
#include <math.h>
#include "mwb-ac-list.h"
#include "mwb-separator.h"
#include "mwb-utils.h"

G_DEFINE_TYPE (MwbAcList, mwb_ac_list, MX_TYPE_WIDGET);

#define MWB_AC_LIST_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), MWB_TYPE_AC_LIST, \
   MwbAcListPrivate))

enum
{
  PROP_0,

  PROP_SEARCH_TEXT,
  PROP_SELECTION,
  PROP_COMPLETION_ENABLED,
  PROP_SEARCH_ENABLED
};

#define MWB_AC_LIST_MAX_ENTRIES 15
#define MWB_AC_LIST_ICON_SIZE 16

#define MWB_AC_LIST_SUGGESTED_TLD_PREF "suggested_tld."
#define MWB_AC_LIST_TLD_FROM_PREF(pref) \
  ((pref) + sizeof (MWB_AC_LIST_SUGGESTED_TLD_PREF) - 2)

struct _MwbAcListPrivate
{
  gint           prefs_branch;
  guint          prefs_branch_changed_handler;
  gboolean       complete_domains;
  gboolean       search_in_automagic;

  guint          result_received_handler;
  guint32        search_id;

  GArray        *entries;
  guint          n_visible_entries;

  gint           tallest_entry;

  guint            clear_timeout;
  gfloat           last_height;
  gdouble          anim_progress;
  ClutterTimeline *timeline;

  MxWidget    *separator;

  ClutterColor   match_color;

  GString       *search_text;
  /* Keeps track of the old search text length so that we can clear
     the favicon cache when it shrinks */
  guint          old_search_length;
  gint           selection;

  CoglHandle     search_engine_icon;
  gchar         *search_engine_name;
  gchar         *search_engine_url;

  sqlite3       *dbcon;
  sqlite3_stmt  *search_stmt;

  /* List of suggested TLD completions */
  GHashTable    *tld_suggestions;
  /* Pointer to a key in the hash table which has the highest score so
     we can quickly complete the common case */
  const gchar   *best_tld_suggestion;
};

typedef struct _MwbAcListEntry MwbAcListEntry;

struct _MwbAcListEntry
{
  MxWidget *label_actor;
  gchar *label_text;
  gchar *url;
  gint type;
  gint match_start, match_end;
  CoglHandle texture;

  /* This is used for drawing the highlight and also for picking. Its
     color gets set to the highlight color but it will not be painted
     if the row is not highlighted */
  MxWidget *highlight_widget;
  guint highlight_motion_handler;
  guint highlight_clicked_handler;
};

typedef struct _MwbAcListCachedFavicon MwbAcListCachedFavicon;

struct _MwbAcListCachedFavicon
{
  gchar *url;

  /* May be COGL_INVALID_HANDLE if the favicon isn't received yet */
  CoglHandle texture;

  guint favicon_handler;

  MwbAcList *ac_list;
};

static const struct
{
  const gchar *name;
  guint offset;
  const gchar *notify;
}
mwb_ac_list_boolean_prefs[] =
  {
    { "complete_domains",
      G_STRUCT_OFFSET (MwbAcListPrivate, complete_domains),
      "completion-enabled" },
    { "search_in_automagic",
      G_STRUCT_OFFSET (MwbAcListPrivate, search_in_automagic),
      "search-enabled" }
  };

static void mwb_ac_list_clear_entries (MwbAcList *self);

static void mwb_ac_list_add_default_entries (MwbAcList *self);

static void mwb_ac_list_forget_search_engine (MwbAcList *self);

#define MWB_AC_LIST_SEARCH_ENTRY    0
#define MWB_AC_LIST_HOSTNAME_ENTRY  1
#define MWB_AC_LIST_N_FIXED_ENTRIES 2

enum
{
  ACTIVATE_SIGNAL,

  LAST_SIGNAL
};

static guint ac_list_signals[LAST_SIGNAL] = { 0, };

static void
mwb_ac_list_get_property (GObject *object, guint property_id,
                          GValue *value, GParamSpec *pspec)
{
  MwbAcList *ac_list = MWB_AC_LIST (object);

  switch (property_id)
    {
    case PROP_SEARCH_TEXT:
      g_value_set_string (value, mwb_ac_list_get_search_text (ac_list));
      break;

    case PROP_SELECTION:
      g_value_set_int (value, mwb_ac_list_get_selection (ac_list));
      break;

    case PROP_COMPLETION_ENABLED:
      g_value_set_boolean (value, ac_list->priv->complete_domains);
      break;

    case PROP_SEARCH_ENABLED:
      g_value_set_boolean (value, ac_list->priv->search_in_automagic);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
mwb_ac_list_set_property (GObject *object, guint property_id,
                          const GValue *value, GParamSpec *pspec)
{
  MwbAcList *ac_list = MWB_AC_LIST (object);

  switch (property_id)
    {
    case PROP_SEARCH_TEXT:
      mwb_ac_list_set_search_text (ac_list, g_value_get_string (value));
      break;

    case PROP_SELECTION:
      mwb_ac_list_set_selection (ac_list, g_value_get_int (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
mwb_ac_list_dispose (GObject *object)
{
  MwbAcListPrivate *priv = MWB_AC_LIST (object)->priv;

  if (priv->clear_timeout)
    {
      g_source_remove (priv->clear_timeout);
      priv->clear_timeout = 0;
    }

  if (priv->timeline)
    {
      clutter_timeline_stop (priv->timeline);
      g_object_unref (priv->timeline);
      priv->timeline = NULL;
    }

  if (priv->separator)
    {
      clutter_actor_unparent (CLUTTER_ACTOR (priv->separator));
      priv->separator = NULL;
    }

  mwb_ac_list_clear_entries (MWB_AC_LIST (object));

  mwb_ac_list_forget_search_engine (MWB_AC_LIST (object));

  G_OBJECT_CLASS (mwb_ac_list_parent_class)->dispose (object);
}

static void
mwb_ac_list_finalize (GObject *object)
{
  MwbAcListPrivate *priv = MWB_AC_LIST (object)->priv;

  g_array_free (priv->entries, TRUE);

  g_string_free (priv->search_text, TRUE);

  g_hash_table_unref (priv->tld_suggestions);

  if (priv->search_engine_name)
    g_free (priv->search_engine_name);
  if (priv->search_engine_url)
    g_free (priv->search_engine_url);

  G_OBJECT_CLASS (mwb_ac_list_parent_class)->finalize (object);
}

static gboolean
mwb_ac_list_stristr (const gchar *haystack, const gchar *needle,
                     gint *start_ret, gint *end_ret)
{
  const gchar *search_start;

  /* Looks for the first occurence of needle in haystack both of which
     are UTF-8 strings. Case is ignored as far as allowed by
     g_unichar_tolower. */

  /* Try each position in haystack */
  for (search_start = haystack;
       *search_start;
       search_start = g_utf8_next_char (search_start))
    {
      const gchar *haystack_ptr = search_start;
      const gchar *needle_ptr = needle;

      while (TRUE)
        {
          if (*needle_ptr == 0)
            {
              /* If we've reached the end of the needle then we have
                 found a match */
              if (start_ret)
                *start_ret = search_start - haystack;
              if (end_ret)
                *end_ret = haystack_ptr - haystack;
              return TRUE;
            }
          else if (*haystack_ptr == 0)
            break;
          else if (g_unichar_tolower (g_utf8_get_char (haystack_ptr))
                   != g_unichar_tolower (g_utf8_get_char (needle_ptr)))
            break;
          else
            {
              haystack_ptr = g_utf8_next_char (haystack_ptr);
              needle_ptr = g_utf8_next_char (needle_ptr);
            }
        }
    }

  return FALSE;
}

static gfloat
mwb_ac_list_get_height (MwbAcList *self,
                        gfloat     max_height)
{
  MwbAcListPrivate *priv = self->priv;
  gint n_entries = MIN (MWB_AC_LIST_MAX_ENTRIES, priv->entries->len);
  gfloat separator_height = 0.f;
  gfloat total_height;

  if (n_entries <= 0)
    return 0.0;

  if (priv->separator)
    clutter_actor_get_preferred_height (CLUTTER_ACTOR (priv->separator), -1,
                                        NULL, &separator_height);

  total_height = ((n_entries * priv->tallest_entry) +
                  ((n_entries - 1) * separator_height));

  if (total_height > max_height)
    {
      n_entries = max_height / (priv->tallest_entry + separator_height);
      total_height = ((n_entries * priv->tallest_entry) +
                      ((n_entries - 1) * separator_height));
    }

  if (priv->timeline)
    return ((1.0 - priv->anim_progress) * priv->last_height +
            priv->anim_progress * total_height);
  else
    return total_height;
}

static void
mwb_ac_list_get_preferred_height (ClutterActor *actor,
                                  gfloat        for_width,
                                  gfloat       *min_height_p,
                                  gfloat       *natural_height_p)
{
  mwb_ac_list_get_preferred_height_with_max (MWB_AC_LIST (actor),
                                             for_width,
                                             G_MAXFLOAT,
                                             min_height_p,
                                             natural_height_p);
}

/* This is the same as clutter_actor_get_preferred_height except that
   it will try to prefer a size that is a multiple of the row height
   but no greater than 'max_height' */
void
mwb_ac_list_get_preferred_height_with_max (MwbAcList *self,
                                           gfloat     for_width,
                                           gfloat     max_height,
                                           gfloat    *min_height_p,
                                           gfloat    *natural_height_p)
{
  if (min_height_p)
    *min_height_p = 0;

  if (natural_height_p)
    {
      MxPadding padding;
      gfloat height;

      mx_widget_get_padding (MX_WIDGET (self), &padding);

      height = mwb_ac_list_get_height (self,
                                       max_height -
                                       padding.top -
                                       padding.bottom);
      if (height > 0)
        *natural_height_p = height + padding.top + padding.bottom;
      else
        *natural_height_p = 0;
    }
}

static void
mwb_ac_list_paint (ClutterActor *actor)
{
  MwbAcListPrivate *priv = MWB_AC_LIST (actor)->priv;
  MxPadding padding;
  ClutterGeometry geom;
  gfloat separator_height = 0.;
  gfloat ypos;
  guint i;

  /* Chain up to get the background */
  CLUTTER_ACTOR_CLASS (mwb_ac_list_parent_class)->paint (actor);

  mx_widget_get_padding (MX_WIDGET (actor), &padding);

  clutter_actor_get_allocation_geometry (actor, &geom);

  ypos = padding.top;
  geom.height -= (gint)padding.bottom;

  if (priv->separator)
    clutter_actor_get_preferred_height (CLUTTER_ACTOR (priv->separator), -1,
                                        NULL, &separator_height);

  for (i = 0;
       i < priv->entries->len
         && ypos + priv->tallest_entry <= geom.height + 1e-6;
       i++)
    {
      MwbAcListEntry *entry = &g_array_index (priv->entries, MwbAcListEntry, i);

      /* Only paint the highlight widget if the row is selected */
      if (entry->highlight_widget && (guint)priv->selection == i
          && CLUTTER_ACTOR_IS_MAPPED (CLUTTER_ACTOR (entry->highlight_widget)))
        clutter_actor_paint (CLUTTER_ACTOR (entry->highlight_widget));

      if (entry->texture)
        {
          int y = ((int) ypos + priv->tallest_entry / 2
                   - MWB_AC_LIST_ICON_SIZE / 2);

          cogl_set_source_texture (entry->texture);
          cogl_rectangle (padding.left,
                          y,
                          padding.left + MWB_AC_LIST_ICON_SIZE,
                          y + MWB_AC_LIST_ICON_SIZE);
        }

      if (entry->label_actor
          && CLUTTER_ACTOR_IS_MAPPED (CLUTTER_ACTOR (entry->label_actor)))
        clutter_actor_paint (CLUTTER_ACTOR (entry->label_actor));

      /* Temporarily move the separator with cogl_translate so that we can
         paint it in the right place */
      if (ypos + priv->tallest_entry + separator_height <= geom.height + 1e-6)
        {
          cogl_push_matrix ();
          cogl_translate (0.0f, ypos + priv->tallest_entry, 0.0f);
          clutter_actor_paint (CLUTTER_ACTOR (priv->separator));
          cogl_pop_matrix ();
        }

      ypos += priv->tallest_entry + separator_height;
    }
}

static void
mwb_ac_list_pick (ClutterActor *actor, const ClutterColor *color)
{
  MwbAcListPrivate *priv = MWB_AC_LIST (actor)->priv;
  gfloat separator_height = 0;
  gfloat ypos;
  ClutterGeometry geom;
  MxPadding padding;
  guint i;

  /* Chain up so we get a bounding box painted */
  CLUTTER_ACTOR_CLASS (mwb_ac_list_parent_class)->pick (actor, color);

  if (priv->separator)
    clutter_actor_get_preferred_height (CLUTTER_ACTOR (priv->separator), -1,
                                        NULL, &separator_height);

  mx_widget_get_padding (MX_WIDGET (actor), &padding);

  clutter_actor_get_allocation_geometry (actor, &geom);

  ypos = padding.top;
  geom.height -= (gint)padding.bottom;

  /* We only want to pick on the highlight boxes. We will paint the
     highlight box regardless of whether the row is selected so that
     we can pick on all rows */
  for (i = 0;
       i < priv->entries->len
         && ypos + priv->tallest_entry <= geom.height + 1e-6;
       i++)
    {
      MwbAcListEntry *entry = &g_array_index (priv->entries, MwbAcListEntry, i);

      /* Only paint the highlight widget if the row is selected */
      if (entry->highlight_widget
          && CLUTTER_ACTOR_IS_MAPPED (CLUTTER_ACTOR (entry->highlight_widget)))
        clutter_actor_paint (CLUTTER_ACTOR (entry->highlight_widget));

      ypos += priv->tallest_entry + separator_height;
    }
}

static gboolean
mwb_ac_list_set_selection_for_highlight (MwbAcList *ac_list,
                                         ClutterActor *actor)
{
  MwbAcListPrivate *priv = ac_list->priv;
  guint i;

  for (i = 0; i < priv->entries->len; i++)
    {
      MwbAcListEntry *entry = &g_array_index (priv->entries, MwbAcListEntry, i);

      if (CLUTTER_ACTOR (entry->highlight_widget) == actor)
        {
          mwb_ac_list_set_selection (ac_list, i);

          return TRUE;
        }
    }

  return FALSE;
}

static gboolean
mwb_ac_list_highlight_motion_cb (ClutterActor *actor,
                                 ClutterMotionEvent *event,
                                 MwbAcList *ac_list)
{
  return mwb_ac_list_set_selection_for_highlight (ac_list, actor);
}

static gboolean
mwb_ac_list_highlight_clicked_cb (ClutterActor *actor,
                                  ClutterMotionEvent *event,
                                  MwbAcList *ac_list)
{
  if (mwb_ac_list_set_selection_for_highlight (ac_list, actor))
    {
      g_signal_emit (ac_list, ac_list_signals[ACTIVATE_SIGNAL], 0);
      return TRUE;
    }
  else
    return FALSE;
}

static void
mwb_ac_list_forget_search_engine (MwbAcList *self)
{
  MwbAcListPrivate *priv = self->priv;

  if (priv->search_engine_name)
    {
      g_free (priv->search_engine_name);
      priv->search_engine_name = NULL;
    }
  if (priv->search_engine_url)
    {
      g_free (priv->search_engine_url);
      priv->search_engine_url = NULL;
    }

  if (priv->search_engine_icon != COGL_INVALID_HANDLE)
    {
      cogl_handle_unref (priv->search_engine_icon);
      priv->search_engine_icon = COGL_INVALID_HANDLE;
    }
}

struct BestTldData
{
  const gchar *best_tld;
  gint best_score;
  gint best_overlap_length;
  const gchar *search_string;
};

static void
mwb_ac_list_update_entry (MwbAcList *ac_list,
                          MwbAcListEntry *entry)
{
  MwbAcListPrivate *priv = ac_list->priv;
  gfloat label_height;
  ClutterActor *text;

  if (entry->highlight_widget == NULL)
    {
      entry->highlight_widget = (MxWidget*)g_object_new (MX_TYPE_FRAME,
                                                         "reactive", TRUE,
                                                         NULL);
      entry->highlight_motion_handler
        = g_signal_connect (entry->highlight_widget, "motion-event",
                            G_CALLBACK (mwb_ac_list_highlight_motion_cb),
                            ac_list);
      entry->highlight_clicked_handler
        = g_signal_connect (entry->highlight_widget, "button-press-event",
                            G_CALLBACK (mwb_ac_list_highlight_clicked_cb),
                            ac_list);
      clutter_actor_set_parent (CLUTTER_ACTOR (entry->highlight_widget),
                                CLUTTER_ACTOR (ac_list));
    }

  if (entry->label_actor)
    clutter_actor_unparent (CLUTTER_ACTOR (entry->label_actor));

  entry->label_actor = MX_WIDGET (mx_label_new_with_text (entry->label_text));
  clutter_actor_set_parent (CLUTTER_ACTOR (entry->label_actor),
                            CLUTTER_ACTOR (ac_list));

  text = mx_label_get_clutter_text (MX_LABEL (entry->label_actor));

  if (entry->match_end > 0)
    {
      PangoAttrList *attr_list = pango_attr_list_new ();
      PangoAttribute *bold_attr = pango_attr_weight_new (PANGO_WEIGHT_BOLD);
      PangoAttribute *color_attr
        = pango_attr_foreground_new (priv->match_color.red * 65535 / 255,
                                     priv->match_color.green * 65535 / 255,
                                     priv->match_color.blue * 65535 / 255);
      bold_attr->start_index = entry->match_start;
      bold_attr->end_index = entry->match_end;
      pango_attr_list_insert (attr_list, bold_attr);
      color_attr->start_index = entry->match_start;
      color_attr->end_index = entry->match_end;
      pango_attr_list_insert (attr_list, color_attr);
      clutter_text_set_attributes (CLUTTER_TEXT (text), attr_list);
      pango_attr_list_unref (attr_list);
    }

  clutter_text_set_ellipsize (CLUTTER_TEXT (text), PANGO_ELLIPSIZE_MIDDLE);

  clutter_actor_get_preferred_height (CLUTTER_ACTOR (entry->label_actor), -1,
                                      NULL, &label_height);
  if (label_height > priv->tallest_entry)
    priv->tallest_entry = label_height;
}

static void
mwb_ac_list_allocate (ClutterActor           *actor,
                      const ClutterActorBox  *box,
                      ClutterAllocationFlags  flags)
{
  MwbAcList *self = MWB_AC_LIST (actor);
  MwbAcListPrivate *priv = self->priv;
  MxPadding padding;
  guint i;
  gfloat ypos;
  gfloat separator_height = 0;

  CLUTTER_ACTOR_CLASS (mwb_ac_list_parent_class)->allocate (actor, box, flags);

  mx_widget_get_padding (MX_WIDGET (actor), &padding);

  ypos = padding.top;

  if (priv->separator)
    {
      ClutterActorBox separator_box;

      clutter_actor_get_preferred_height (CLUTTER_ACTOR (priv->separator), -1,
                                          NULL, &separator_height);

      /* The separator is actually going to be draw multiple times at
         different locations using cogl_translate, so we just want to
         allocate it with the right width at the top */
      separator_box.x1 = 0;
      separator_box.x2 = box->x2 - box->x1;
      separator_box.y1 = 0;
      separator_box.y2 = separator_height;

      clutter_actor_allocate (CLUTTER_ACTOR (priv->separator),
                              &separator_box, flags);
    }

  /* If we're not in the middle of a transition already then store the
     last allocated height so we can use it to start an animation */
  if (priv->timeline == NULL)
    {
      priv->last_height = box->y2 - box->y1 - padding.top - padding.bottom;
      /* Store the number of visible entries so that we know not to
         move the selection off the end of the list */
      if (priv->tallest_entry <= 0)
        priv->n_visible_entries = 0;
      else
        priv->n_visible_entries = rint ((priv->last_height -
                                         padding.top -
                                         padding.bottom) /
                                        (separator_height +
                                         priv->tallest_entry));
    }

  /* Allocate all of the labels and highlight widgets */
  for (i = 0; i < priv->entries->len; i++)
    {
      MwbAcListEntry *entry = &g_array_index (priv->entries, MwbAcListEntry, i);

      /* Put the label to the right of the icon and vertically
         centered within the row size */
      if (entry->label_actor)
        {
          ClutterActorBox label_box;
          gfloat height;
          ClutterActor *label_actor = CLUTTER_ACTOR (entry->label_actor);

          clutter_actor_get_preferred_height (label_actor, -1, NULL, &height);

          label_box.x1 = (padding.left + (gfloat)MWB_AC_LIST_ICON_SIZE);
          label_box.x2 = box->x2 - box->x1 - padding.right;
          label_box.y1
            = (MWB_PIXBOUND (priv->tallest_entry / 2.0f - height / 2.0f)
               + ypos);
          label_box.y2 = (label_box.y1 + (gfloat)priv->tallest_entry);

          clutter_actor_allocate (label_actor, &label_box, flags);
        }

      /* Let the highlight widget fill the entire row */
      if (entry->highlight_widget)
        {
          ClutterActorBox highlight_box;
          const int BORDER_PAD = 1;  /* yes, this is a temporary hack */

          highlight_box.x1 = BORDER_PAD;
          highlight_box.x2 = box->x2 - box->x1 - BORDER_PAD;
          highlight_box.y1 = ypos;
          highlight_box.y2 = ypos + (gfloat)priv->tallest_entry;

          clutter_actor_allocate (CLUTTER_ACTOR (entry->highlight_widget),
                                  &highlight_box, flags);
        }

      ypos += (gfloat)priv->tallest_entry + separator_height;
    }
}

static void
mwb_ac_list_update_all_entries (MwbAcList *self)
{
  MwbAcListPrivate *priv = self->priv;
  guint i;

  for (i = 0; i < priv->entries->len; i++)
    {
      MwbAcListEntry *entry
        = &g_array_index (priv->entries, MwbAcListEntry, i);
      mwb_ac_list_update_entry (self, entry);
    }
}

static void
mwb_ac_list_style_changed_cb (MxWidget *widget)
{
  MwbAcList *self = MWB_AC_LIST (widget);
  MwbAcListPrivate *priv = self->priv;
  ClutterColor *color = NULL;

  mx_stylable_get (MX_STYLABLE (self),
                     "color", &color,
                     NULL);

  /* The 'color' property is used to set the search match color. This
     should really be done with a different property but it will be
     left as a FIXME until Mx/libccss gains the ability to add
     custom color properties */
  if (color)
    {
      if (!clutter_color_equal (&priv->match_color, color))
        {
          priv->match_color = *color;
          /* The color has changed so we need to reset the pango
             attributes on all of the entries */
          mwb_ac_list_update_all_entries (self);
        }

      clutter_color_free (color);
    }
}

static void
mwb_ac_list_class_init (MwbAcListClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  ClutterActorClass *actor_class = (ClutterActorClass *) klass;
  GParamSpec *pspec;

  g_type_class_add_private (klass, sizeof (MwbAcListPrivate));

  object_class->dispose = mwb_ac_list_dispose;
  object_class->finalize = mwb_ac_list_finalize;
  object_class->get_property = mwb_ac_list_get_property;
  object_class->set_property = mwb_ac_list_set_property;

  actor_class->get_preferred_height = mwb_ac_list_get_preferred_height;
  actor_class->paint = mwb_ac_list_paint;
  actor_class->pick = mwb_ac_list_pick;
  actor_class->allocate = mwb_ac_list_allocate;

  pspec = g_param_spec_string ("search-text", "Search Text",
                               "The text to auto complete on",
                               "",
                               GParamFlags(G_PARAM_READWRITE |
                               G_PARAM_STATIC_NAME |
                               G_PARAM_STATIC_NICK |
                               G_PARAM_STATIC_BLURB));
  g_object_class_install_property (object_class, PROP_SEARCH_TEXT, pspec);

  pspec = g_param_spec_int ("selection", "Selection",
                            "The currently selected row or -1 for no selection",
                            -1, G_MAXINT, -1,
                            GParamFlags(G_PARAM_READWRITE |
                            G_PARAM_STATIC_NAME |
                            G_PARAM_STATIC_NICK |
                            G_PARAM_STATIC_BLURB));
  g_object_class_install_property (object_class, PROP_SELECTION, pspec);

  pspec = g_param_spec_boolean ("completion-enabled", "Completion enabled",
                                "Whether top-level domain completion is enabled",
                                TRUE,
                                GParamFlags(G_PARAM_READABLE |
                                G_PARAM_STATIC_NAME |
                                G_PARAM_STATIC_NICK |
                                G_PARAM_STATIC_BLURB));
  g_object_class_install_property (object_class, PROP_COMPLETION_ENABLED, pspec);

  pspec = g_param_spec_boolean ("search-enabled", "Search enabled",
                                "Whether radical-bar search is enabled",
                                TRUE,
                                GParamFlags(G_PARAM_READABLE |
                                G_PARAM_STATIC_NAME |
                                G_PARAM_STATIC_NICK |
                                G_PARAM_STATIC_BLURB));
  g_object_class_install_property (object_class, PROP_SEARCH_ENABLED, pspec);

  ac_list_signals[ACTIVATE_SIGNAL] =
    g_signal_new ("activate",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (MwbAcListClass, activate),
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);
}

static void
mwb_ac_list_init (MwbAcList *self)
{
  MwbAcListPrivate *priv = self->priv = MWB_AC_LIST_PRIVATE (self);

  priv->entries = g_array_new (FALSE, TRUE, sizeof (MwbAcListEntry));

  priv->search_text = g_string_new ("");

  priv->selection = -1;

  priv->separator = mwb_separator_new ();
  clutter_actor_set_parent (CLUTTER_ACTOR (priv->separator),
                            CLUTTER_ACTOR (self));

  g_object_set (G_OBJECT (self), "clip-to-allocation", TRUE, NULL);

  priv->tld_suggestions = g_hash_table_new_full (g_str_hash,
                                                 g_str_equal,
                                                 g_free,
                                                 NULL);

  g_signal_connect (self, "style-changed",
                    G_CALLBACK (mwb_ac_list_style_changed_cb), NULL);

  priv->dbcon = NULL;
  priv->search_stmt = NULL;
}

MxWidget*
mwb_ac_list_new (void)
{
  return MX_WIDGET (g_object_new (MWB_TYPE_AC_LIST, NULL));
}

#define FAVICON_SQL "SELECT url FROM favicons WHERE id='%d'"

#define THEMEDIR "/usr/share/meego-panel-web/netpanel/"
static void
mwb_ac_list_set_icon (MwbAcList *self, MwbAcListEntry *entry)
{

  MwbAcListPrivate *priv = self->priv;

  GError *texture_error = NULL;

  if (!entry)
    return;

  gchar *favi_url = NULL;
  gchar *icon_path = NULL;

  sqlite3_stmt *favicon_stmt;
  gchar *stmt = g_strdup_printf(FAVICON_SQL, entry->type);
  int rc = sqlite3_prepare_v2 (priv->dbcon,
                           stmt,
                           -1,
                           &favicon_stmt, NULL);
  if (rc)
    g_warning ("[netpanel] sqlite3_prepare_v2():tab_stmt %s",
               sqlite3_errmsg(priv->dbcon));

  if (priv->dbcon && favicon_stmt)
    {
      if (sqlite3_step (favicon_stmt) == SQLITE_ROW)
        {
          favi_url = (gchar*)sqlite3_column_text(favicon_stmt, 0);
        }
    }

  if(favi_url)
    {
      gchar *csum = g_compute_checksum_for_string (G_CHECKSUM_MD5, favi_url, -1);
      gchar *thumbnail_filename = g_strconcat (csum, ".ico", NULL);
      icon_path = g_build_filename (g_get_home_dir (),
                                    ".config",
                                    "internet-panel",
                                    "favicons",
                                    thumbnail_filename,
                                    NULL);
      if (!g_file_test (icon_path, G_FILE_TEST_EXISTS))
        {
          g_free (icon_path);
          icon_path = g_strdup_printf ("%s%s", THEMEDIR, "o2_globe.png");
        }
      g_free(csum);
      g_free(thumbnail_filename);
    }
  else
    {
      icon_path = g_strdup_printf("%s%s", THEMEDIR, "o2_globe.png");
    }
  sqlite3_finalize(favicon_stmt);

  if (icon_path)
    {
      entry->texture =  cogl_texture_new_from_file (icon_path,
                                                    COGL_TEXTURE_NONE,
                                                    COGL_PIXEL_FORMAT_ANY,
                                                    &texture_error);
      if (texture_error)
        {
          g_warning ("[netpanel] unable to open ac-list icon: %s\n",
                     texture_error->message);
          g_error_free (texture_error);
        }
      clutter_actor_queue_redraw (CLUTTER_ACTOR (self));
      g_free(icon_path);
    }
}

static void
mwb_ac_list_new_frame_cb (ClutterTimeline *timeline,
                          guint            msecs,
                          MwbAcList       *self)
{
  MwbAcListPrivate *priv = self->priv;
  priv->anim_progress = clutter_timeline_get_progress (timeline);
  clutter_actor_queue_relayout (CLUTTER_ACTOR (self));
}

static void
mwb_ac_list_completed_cb (ClutterTimeline *timeline,
                          MwbAcList       *self)
{
  MwbAcListPrivate *priv = self->priv;

  g_object_unref (priv->timeline);
  priv->timeline = NULL;

  clutter_actor_queue_relayout (CLUTTER_ACTOR (self));
}

static void
mwb_ac_list_start_transition (MwbAcList *self)
{
  MwbAcListPrivate *priv = self->priv;

  if (priv->timeline)
    return;

  priv->timeline = clutter_timeline_new (100);
  priv->anim_progress = 0.0;

  /* Set a small delay on the timeline to allow new results to come in */
  clutter_timeline_set_delay (priv->timeline, 50);

  g_signal_connect (priv->timeline, "new-frame",
                    G_CALLBACK (mwb_ac_list_new_frame_cb), self);
  g_signal_connect (priv->timeline, "completed",
                    G_CALLBACK (mwb_ac_list_completed_cb), self);
  clutter_timeline_start (priv->timeline);
}

static void
mwb_ac_list_result_exception(void *context,
                             int errno)
{
  //MwbAcList* self = (MwbAcList*)context;
  //MwbAcListPrivate *priv = self->priv;
  return;
}

static void
mwb_ac_list_result_received(void       *context,
                            int         type,
                            const char *url,
                            const char *value,
                            int         favicon_id)
{
  MwbAcList* self = (MwbAcList*)context;
  MwbAcListPrivate *priv = self->priv;

  if (!url || !value) /* No URL */
    return;

  char *result_text = NULL;

  result_text = g_strdup(value);

  if (priv->entries->len < MWB_AC_LIST_MAX_ENTRIES)
    {
      MwbAcListEntry *entry;
      //xxx MwbAcListCachedFavicon *cached_favicon;

      /* Remove the clear timeout */
      if (priv->clear_timeout)
        {
          mwb_ac_list_clear_entries (self);
          mwb_ac_list_add_default_entries (self);
          g_source_remove (priv->clear_timeout);
          priv->clear_timeout = 0;
        }

      g_array_set_size (priv->entries, priv->entries->len + 1);
      entry = &g_array_index (priv->entries, MwbAcListEntry,
                              priv->entries->len - 1);

      /* Prefer to display the comment if the search string matches in
         it */
      if (!mwb_ac_list_stristr (result_text, priv->search_text->str,
                               &entry->match_start, &entry->match_end))
        {
          /* If neither match just display the comment and trust that
             places had some reason to suggest it */
          entry->match_start = entry->match_end = 0;
        }

      entry->label_text = (gchar*)result_text;
      entry->url = g_strdup (url);
      entry->type = favicon_id;

      mwb_ac_list_update_entry (self, entry);
      mwb_ac_list_set_icon (self, entry);

      mwb_ac_list_start_transition (self);

      clutter_actor_queue_relayout (CLUTTER_ACTOR (self));
    }
}

static void
mwb_ac_list_clear_entries (MwbAcList *self)
{
  MwbAcListPrivate *priv = self->priv;
  guint i;

  for (i = 0; i < priv->entries->len; i++)
    {
      MwbAcListEntry *entry
        = &g_array_index (priv->entries, MwbAcListEntry, i);
      if (entry->label_actor)
        clutter_actor_unparent (CLUTTER_ACTOR (entry->label_actor));
      if (entry->highlight_widget)
        {
          g_signal_handler_disconnect (entry->highlight_widget,
                                       entry->highlight_clicked_handler);
          g_signal_handler_disconnect (entry->highlight_widget,
                                       entry->highlight_motion_handler);
          clutter_actor_unparent (CLUTTER_ACTOR (entry->highlight_widget));
        }
      if (entry->label_text)
        g_free (entry->label_text);
      if (entry->url)
        g_free (entry->url);
      if (entry->texture != COGL_INVALID_HANDLE)
        cogl_handle_unref (entry->texture);
    }

  g_array_set_size (priv->entries, 0);

  if (priv->selection >= 0)
    {
      priv->selection = -1;
      g_object_notify (G_OBJECT (self), "selection");
    }

  /* Clear the tallest entry so that adding new entries will reset
     it */
  priv->tallest_entry = MWB_AC_LIST_ICON_SIZE;
}

static void
mwb_ac_list_add_default_entry (MwbAcList *self,
                               const gchar *label_text,
                               const gchar *search_text,
                               gchar *url,
                               CoglHandle icon)
{
  MwbAcListPrivate *priv = self->priv;
  const gchar *marker;
  MwbAcListEntry *entry;

  g_array_set_size (priv->entries, priv->entries->len + 1);
  entry = &g_array_index (priv->entries, MwbAcListEntry,
                          priv->entries->len - 1);

  /* This is done instead of hardcoding the match_end and match_start
     offsets in the hope that it would make translation easier */
  if ((marker = strstr (label_text, "%s")) == NULL)
    {
      entry->label_text = g_strdup (label_text);
      entry->match_start = entry->match_end = -1;
    }
  else
    {
      size_t search_len = strlen (search_text);
      size_t end_len = strlen (marker + 2);
      entry->label_text = (gchar*)g_malloc (marker - label_text
                                    + search_len + end_len + 1);
      memcpy (entry->label_text, label_text, marker - label_text);
      memcpy (entry->label_text + (marker - label_text),
              search_text, search_len);
      memcpy (entry->label_text + (marker - label_text) + search_len,
              marker + 2, end_len + 1);
      entry->match_start = marker - label_text;
      entry->match_end = entry->match_start + search_len;
    }

  entry->url = url;
  if (icon != COGL_INVALID_HANDLE)
    entry->texture = cogl_handle_ref (icon);

  mwb_ac_list_update_entry (self, entry);
}

static void
mwb_ac_list_check_best_tld_suggestion_overlap (gpointer key,
                                               gpointer value,
                                               gpointer user_data)
{
  struct BestTldData *data = (struct BestTldData *) user_data;
  int overlap = -1;
  const gchar *search_str_end = (data->search_string +
                                 strlen (data->search_string));
  const gchar *tail;
  int keylen = strlen ((const char*)key);

  /* Scan from the end of the string to find the most overlap with the
     start of the key */
  for (tail = search_str_end;
       search_str_end - tail <= keylen &&
         tail >= (const gchar *) data->search_string;
       tail--)
    if (g_str_has_prefix ((const char*)key, tail))
      overlap = search_str_end - tail;

  /* Use this key if more of it overlaps or it has a better score */
  if (data->best_overlap_length < overlap ||
      (data->best_overlap_length == overlap &&
       data->best_score < GPOINTER_TO_INT (value)))
    {
      data->best_score = GPOINTER_TO_INT (value);
      data->best_tld = (const char*)key;
      data->best_overlap_length = overlap;
    }
}

static void
mwb_ac_list_complete_domain (MwbAcList *self,
                             const gchar *search_text,
                             gchar **completion,
                             gchar **completion_url)
{
  MwbAcListPrivate *priv = self->priv;
  const gchar *p;
  gboolean has_dot = FALSE;
  struct BestTldData data;

  /* If we don't have any completions then just return the search
     text */
  if (priv->best_tld_suggestion == NULL)
    {
      *completion = g_strdup (search_text);
      *completion_url = g_strdup (search_text);
      return;
    }

  /* Check if the search text contains any characters that don't look
     like part of a domain */
  for (p = search_text; *p; p = g_utf8_next_char (p))
    {
      gunichar ch = g_utf8_get_char (p);

      if (ch == '.')
        has_dot = TRUE;

      if (ch == '/' || ch == ':' || ch == '?' ||
          (!g_unichar_isalnum (g_utf8_get_char_validated (p, -1)) &&
           *p != '-' && *p != '.'))
        {
          *completion = g_strdup (search_text);
          *completion_url = g_strdup (search_text);
          return;
        }
    }

  /* If the search string doesn't contain a dot then just append the
     highest scoring tld */
  if (!has_dot)
    {
      *completion = g_strconcat (search_text,
                                 priv->best_tld_suggestion, NULL);
      *completion_url = g_strconcat ("http://", *completion, "/", NULL);
      return;
    }

  /* Otherwise look for the string with the longest overlap */
  data.best_tld = NULL;
  data.best_score = -1;
  data.best_overlap_length = -1;
  data.search_string = search_text;
  g_hash_table_foreach (priv->tld_suggestions,
                        mwb_ac_list_check_best_tld_suggestion_overlap,
                        &data);
  if (data.best_tld)
    {
      *completion = g_strconcat (search_text,
                                 data.best_tld + data.best_overlap_length,
                                 NULL);
      *completion_url = g_strconcat ("http://", *completion, "/", NULL);
    }
  else
    {
      /* Otherwise we don't have a sensible suggestion */
      *completion = g_strdup (search_text);
      *completion_url = g_strdup (search_text);
    }
}

static void
mwb_ac_list_add_default_entries (MwbAcList *self)
{
  MwbAcListPrivate *priv = self->priv;

  /* Add the two default entries */
  if (priv->search_in_automagic &&
      priv->search_engine_name &&
      priv->search_engine_url)
    {
      char *percent;
      gchar *label;
      GString *search_uri = g_string_new ("");

      /* If the search engine URL contains '%s' then replace that with
         the search text, otherwise just append it */
      if ((percent = strstr (priv->search_engine_url, "%s")))
        {
          g_string_append_len (search_uri, priv->search_engine_url,
                               percent - priv->search_engine_url);
          g_string_append_uri_escaped (search_uri, priv->search_text->str,
                                       NULL, FALSE);
          g_string_append (search_uri, percent + 2);
        }
      else
        {
          g_string_append (search_uri, priv->search_engine_url);
          g_string_append_uri_escaped (search_uri, priv->search_text->str,
                                       NULL, FALSE);
        }

      /* Translators: This string ends up in the autocomplete list and
         is used for searching. '%%s' is the string that the user
         typed in to the entry box and '%s' is the name of the search
         engine provider (such as 'Yahoo!'). It is safe to swap the
         order of these markers if necessary in your language */
      label = g_strdup_printf (_("Search for %%s on %s"),
                               priv->search_engine_name);

      mwb_ac_list_add_default_entry (self,
                                     label,
                                     priv->search_text->str,
                                     search_uri->str,
                                     priv->search_engine_icon);
      /* Above call takes ownership of the URI string */
      g_string_free (search_uri, FALSE);

      g_free (label);
    }
  if (priv->complete_domains)
    {
      gchar *completion, *completion_url;

      mwb_ac_list_complete_domain (self,
                                   priv->search_text->str,
                                   &completion,
                                   &completion_url);

      mwb_ac_list_add_default_entry (self,
                                     _("Go to %s"),
                                     completion,
                                     completion_url,
                                     COGL_INVALID_HANDLE);

      g_free (completion);
    }
}

static gboolean
mwb_ac_list_clear_timeout_cb (MwbAcList *self)
{
  MwbAcListPrivate *priv = self->priv;

  mwb_ac_list_clear_entries (self);
  mwb_ac_list_add_default_entries (self);

  mwb_ac_list_start_transition (self);

  priv->clear_timeout = 0;

  return FALSE;
}

#define AC_LIST_SQL "SELECT DISTINCT url, url||' - '||title as vl, favicon_id "\
                    "FROM ( "\
                          "SELECT url, title, favicon_id, 100 as visit_count "\
                          "FROM bookmarks "\
                          "UNION "\
                          "SELECT url, title, favicon_id, visit_count "\
                          "FROM urls"\
                         ") WHERE vl like ? ORDER BY visit_count DESC"

void
mwb_ac_list_set_search_text (MwbAcList *self,
                             const gchar *search_text)
{
  MwbAcListPrivate *priv = self->priv;

  g_return_if_fail (MWB_IS_AC_LIST (self));

  if (strcmp (priv->search_text->str, search_text) != 0)
    {
      size_t search_text_len = strlen (search_text);

      priv->old_search_length = search_text_len;

      g_string_set_size (priv->search_text, 0);
      g_string_append_len (priv->search_text, search_text, search_text_len);

      /* Only clear after a short timeout, stops us from spawning
       * lots of queries if this gets set frequently, and stops
       * the animation from looking shaky
       */
      if (priv->clear_timeout)
        g_source_remove (priv->clear_timeout);

      priv->clear_timeout = g_timeout_add (100, (GSourceFunc)
                                           mwb_ac_list_clear_timeout_cb,
                                           self);

      g_object_notify (G_OBJECT (self), "search-text");

      if (search_text_len == 0 || !priv->search_stmt)
        return;

      sqlite3_reset(priv->search_stmt);

      gchar param[search_text_len + 3];
      sprintf(param, "%%%s%%", search_text);
      int rc = sqlite3_bind_text(priv->search_stmt, 1, param, search_text_len + 2, SQLITE_TRANSIENT);
      if (rc)
          g_warning("[netpanel] sqlite3_bind_text(): %s", sqlite3_errmsg(priv->dbcon));

      priv->entries->len = 0;
      while (sqlite3_step(priv->search_stmt) == SQLITE_ROW)
        {
          mwb_ac_list_result_received(self,
                  0,
                  (gchar*) sqlite3_column_text(priv->search_stmt, 0), // url
                  (gchar*) sqlite3_column_text(priv->search_stmt, 1), // title
                  sqlite3_column_int(priv->search_stmt, 2));          // favicon_id
        }
    }
}

const gchar *
mwb_ac_list_get_search_text (MwbAcList *self)
{
  MwbAcListPrivate *priv = self->priv;

  g_return_val_if_fail (MWB_IS_AC_LIST (self), NULL);

  return priv->search_text->str;
}

void
mwb_ac_list_set_selection (MwbAcList *self, gint selection)
{
  MwbAcListPrivate *priv = self->priv;

  g_return_if_fail (MWB_IS_AC_LIST (self));
  g_return_if_fail (selection == -1
                    || (selection >= 0 && selection < (gint)priv->entries->len));

  if (priv->selection != selection)
    {
      priv->selection = selection;

      g_object_notify (G_OBJECT (self), "selection");

      if (CLUTTER_ACTOR_IS_MAPPED (CLUTTER_ACTOR (self)))
        clutter_actor_queue_redraw (CLUTTER_ACTOR (self));
    }
}

gint
mwb_ac_list_get_selection (MwbAcList *self)
{
  g_return_val_if_fail (MWB_IS_AC_LIST (self), -1);

  return self->priv->selection;
}

guint
mwb_ac_list_get_n_entries (MwbAcList *self)
{
  g_return_val_if_fail (MWB_IS_AC_LIST (self), 0);

  return self->priv->entries->len;
}

guint
mwb_ac_list_get_n_visible_entries (MwbAcList *self)
{
  MwbAcListPrivate *priv = self->priv;

  g_return_val_if_fail (MWB_IS_AC_LIST (self), 0);

  return MIN (priv->n_visible_entries, priv->entries->len);
}

gchar *
mwb_ac_list_get_entry_url (MwbAcList *self, guint entry)
{
  MwbAcListPrivate *priv;

  g_return_val_if_fail (MWB_IS_AC_LIST (self), NULL);

  priv = self->priv;

  g_return_val_if_fail (entry < priv->entries->len, NULL);

  return g_strdup (g_array_index (priv->entries, MwbAcListEntry, entry).url);
}

GList *
mwb_ac_list_get_tld_suggestions (MwbAcList *self)
{
  MwbAcListPrivate *priv = self->priv;

  return g_hash_table_get_keys (priv->tld_suggestions);
}

void
mwb_ac_list_db_stmt_prepare (MwbAcList *self, void *dbcon)
{
  gint rc;
  MwbAcListPrivate *priv = self->priv;
  priv->dbcon = (sqlite3 *)dbcon;

  if (!priv->dbcon)
    {
      g_warning ("[netpanel] No available database connection");
      return;
    }

  if (!priv->search_stmt)
    {
      rc = sqlite3_prepare_v2 (priv->dbcon,
                               AC_LIST_SQL,
                               -1,
                               &priv->search_stmt,
                               NULL);
    if (rc)
      g_warning("[netpanel] sqlite3_prepare_v2 (): %s",
                sqlite3_errmsg(priv->dbcon));
    }
}

void
mwb_ac_list_db_stmt_finalize (MwbAcList *self)
{
  MwbAcListPrivate *priv = self->priv;

  if (priv->search_stmt)
    sqlite3_finalize(priv->search_stmt);

  priv->search_stmt = NULL;
  priv->dbcon = NULL; /*  let panel to close db */
}
