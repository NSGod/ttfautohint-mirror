/* talatin.c */

/*
 * Copyright (C) 2011-2013 by Werner Lemberg.
 *
 * This file is part of the ttfautohint library, and may only be used,
 * modified, and distributed under the terms given in `COPYING'.  By
 * continuing to use, modify, or distribute this file you indicate that you
 * have read `COPYING' and understand and accept it fully.
 *
 * The file `COPYING' mentioned in the previous paragraph is distributed
 * with the ttfautohint library.
 */


/* originally file `aflatin.c' (2011-Mar-28) from FreeType */

/* heavily modified 2011 by Werner Lemberg <wl@gnu.org> */

#include <string.h>

#include <ft2build.h>
#include FT_ADVANCES_H
#include FT_TRUETYPE_TABLES_H

#include "taglobal.h"
#include "talatin.h"
#include "tasort.h"


#ifdef TA_CONFIG_OPTION_USE_WARPER
#include "tawarp.h"
#endif

#include <numberset.h>


/* find segments and links, compute all stem widths, and initialize */
/* standard width and height for the glyph with given charcode */

void
ta_latin_metrics_init_widths(TA_LatinMetrics metrics,
                             FT_Face face)
{
  /* scan the array of segments in each direction */
  TA_GlyphHintsRec hints[1];


  TA_LOG(("standard widths computation\n"
          "===========================\n\n"));

  ta_glyph_hints_init(hints);

  metrics->axis[TA_DIMENSION_HORZ].width_count = 0;
  metrics->axis[TA_DIMENSION_VERT].width_count = 0;

  {
    FT_Error error;
    FT_UInt glyph_index;
    int dim;
    TA_LatinMetricsRec dummy[1];
    TA_Scaler scaler = &dummy->root.scaler;


    glyph_index = FT_Get_Char_Index(
                    face,
                    metrics->root.script_class->standard_char);
    if (glyph_index == 0)
      goto Exit;

    TA_LOG(("standard character: 0x%X (glyph index %d)\n",
            metrics->root.script_class->standard_char, glyph_index));

    error = FT_Load_Glyph(face, glyph_index, FT_LOAD_NO_SCALE);
    if (error || face->glyph->outline.n_points <= 0)
      goto Exit;

    memset(dummy, 0, sizeof (TA_LatinMetricsRec));

    dummy->units_per_em = metrics->units_per_em;

    scaler->x_scale = 0x10000L;
    scaler->y_scale = 0x10000L;
    scaler->x_delta = 0;
    scaler->y_delta = 0;

    scaler->face = face;
    scaler->render_mode = FT_RENDER_MODE_NORMAL;
    scaler->flags = 0;

    ta_glyph_hints_rescale(hints, (TA_ScriptMetrics)dummy);

    error = ta_glyph_hints_reload(hints, &face->glyph->outline);
    if (error)
      goto Exit;

    for (dim = 0; dim < TA_DIMENSION_MAX; dim++)
    {
      TA_LatinAxis axis = &metrics->axis[dim];
      TA_AxisHints axhints = &hints->axis[dim];

      TA_Segment seg, limit, link;
      FT_UInt num_widths = 0;


      error = ta_latin_hints_compute_segments(hints, (TA_Dimension)dim);
      if (error)
        goto Exit;

      ta_latin_hints_link_segments(hints, (TA_Dimension)dim);

      seg = axhints->segments;
      limit = seg + axhints->num_segments;

      for (; seg < limit; seg++)
      {
        link = seg->link;

        /* we only consider stem segments there! */
        if (link
            && link->link == seg
            && link > seg)
        {
          FT_Pos dist;


          dist = seg->pos - link->pos;
          if (dist < 0)
            dist = -dist;

          if (num_widths < TA_LATIN_MAX_WIDTHS)
            axis->widths[num_widths++].org = dist;
        }
      }

      /* this also replaces multiple almost identical stem widths */
      /* with a single one (the value 100 is heuristic) */
      ta_sort_and_quantize_widths(&num_widths, axis->widths,
                                  dummy->units_per_em / 100);
      axis->width_count = num_widths;
    }

Exit:
    for (dim = 0; dim < TA_DIMENSION_MAX; dim++)
    {
      TA_LatinAxis axis = &metrics->axis[dim];
      FT_Pos stdw;


      stdw = (axis->width_count > 0) ? axis->widths[0].org
                                     : TA_LATIN_CONSTANT(metrics, 50);

      /* let's try 20% of the smallest width */
      axis->edge_distance_threshold = stdw / 5;
      axis->standard_width = stdw;
      axis->extra_light = 0;

#ifdef TA_DEBUG
      {
        FT_UInt i;


        TA_LOG(("%s widths:\n",
                dim == TA_DIMENSION_VERT ? "horizontal"
                                         : "vertical"));

        TA_LOG(("  %d (standard)", axis->standard_width));
        for (i = 1; i < axis->width_count; i++)
          TA_LOG((" %d", axis->widths[i].org));

        TA_LOG(("\n"));
      }
#endif
    }
  }

  TA_LOG(("\n"));

  ta_glyph_hints_done(hints);
}


#define TA_LATIN_MAX_TEST_CHARACTERS 12


static const char ta_latin_blue_chars[TA_LATIN_MAX_BLUES]
                                     [TA_LATIN_MAX_TEST_CHARACTERS + 1] =
{
  "THEZOCQS",
  "HEZLOCUS",
  "fijkdbh",
  "xzroesc",
  "xzroesc",
  "pqgjy"
};


/* find all blue zones; flat segments give the reference points, */
/* round segments the overshoot positions */

static void
ta_latin_metrics_init_blues(TA_LatinMetrics metrics,
                            FT_Face face)
{
  FT_Pos flats[TA_LATIN_MAX_TEST_CHARACTERS];
  FT_Pos rounds[TA_LATIN_MAX_TEST_CHARACTERS];
  FT_Int num_flats;
  FT_Int num_rounds;

  FT_Int bb;
  TA_LatinBlue blue;
  FT_Error error;
  TA_LatinAxis axis = &metrics->axis[TA_DIMENSION_VERT];
  FT_Outline outline;


  /* we compute the blues simply by loading each character from the */
  /* `ta_latin_blue_chars[blues]' string, then finding its top-most or */
  /* bottom-most points (depending on `TA_IS_TOP_BLUE') */

  TA_LOG(("blue zones computation\n"
          "======================\n\n"));

  for (bb = 0; bb < TA_LATIN_BLUE_MAX; bb++)
  {
    const char* p = ta_latin_blue_chars[bb];
    const char* limit = p + TA_LATIN_MAX_TEST_CHARACTERS;
    FT_Pos* blue_ref;
    FT_Pos* blue_shoot;


    TA_LOG(("blue zone %d:\n", bb));

    num_flats = 0;
    num_rounds = 0;

    for (; p < limit && *p; p++)
    {
      FT_UInt glyph_index;
      FT_Pos best_y; /* same as points.y */
      FT_Int best_point, best_contour_first, best_contour_last;
      FT_Vector* points;
      FT_Bool round = 0;


      /* load the character in the face -- skip unknown or empty ones */
      glyph_index = FT_Get_Char_Index(face, (FT_UInt)*p);
      if (glyph_index == 0)
        continue;

      error = FT_Load_Glyph(face, glyph_index, FT_LOAD_NO_SCALE);
      outline = face->glyph->outline;
      if (error || outline.n_points <= 0)
        continue;

      /* now compute min or max point indices and coordinates */
      points = outline.points;
      best_point = -1;
      best_y = 0; /* make compiler happy */
      best_contour_first = 0; /* ditto */
      best_contour_last = 0; /* ditto */

      {
        FT_Int nn;
        FT_Int first = 0;
        FT_Int last = -1;


        for (nn = 0; nn < outline.n_contours; first = last + 1, nn++)
        {
          FT_Int old_best_point = best_point;
          FT_Int pp;


          last = outline.contours[nn];

          /* avoid single-point contours since they are never rasterized; */
          /* in some fonts, they correspond to mark attachment points */
          /* which are way outside of the glyph's real outline */
          if (last <= first)
            continue;

          if (TA_LATIN_IS_TOP_BLUE(bb))
          {
            for (pp = first; pp <= last; pp++)
              if (best_point < 0
                  || points[pp].y > best_y)
              {
                best_point = pp;
                best_y = points[pp].y;
              }
          }
          else
          {
            for (pp = first; pp <= last; pp++)
              if (best_point < 0
                  || points[pp].y < best_y)
              {
                best_point = pp;
                best_y = points[pp].y;
              }
          }

          if (best_point != old_best_point)
          {
            best_contour_first = first;
            best_contour_last = last;
          }
        }
        TA_LOG(("  %c  %ld", *p, best_y));
      }

      /* now check whether the point belongs to a straight or round */
      /* segment; we first need to find in which contour the extremum */
      /* lies, then inspect its previous and next points */
      if (best_point >= 0)
      {
        FT_Pos best_x = points[best_point].x;
        FT_Int prev, next;
        FT_Int best_segment_first, best_segment_last;
        FT_Int best_on_point_first, best_on_point_last;
        FT_Pos dist;


        best_segment_first = best_point;
        best_segment_last = best_point;

        if (FT_CURVE_TAG(outline.tags[best_point]) == FT_CURVE_TAG_ON)
        {
          best_on_point_first = best_point;
          best_on_point_last = best_point;
        }
        else
        {
          best_on_point_first = -1;
          best_on_point_last = -1;
        }

        /* look for the previous and next points on the contour */
        /* that are not on the same Y coordinate, then threshold */
        /* the `closeness'... */
        prev = best_point;
        next = prev;

        do
        {
          if (prev > best_contour_first)
            prev--;
          else
            prev = best_contour_last;

          dist = TA_ABS(points[prev].y - best_y);
          /* accept a small distance or a small angle (both values are */
          /* heuristic; value 20 corresponds to approx. 2.9 degrees) */
          if (dist > 5)
            if (TA_ABS(points[prev].x - best_x) <= 20 * dist)
              break;

          best_segment_first = prev;

          if (FT_CURVE_TAG(outline.tags[prev]) == FT_CURVE_TAG_ON)
          {
            best_on_point_first = prev;
            if (best_on_point_last < 0)
              best_on_point_last = prev;
          }

        } while (prev != best_point);

        do
        {
          if (next < best_contour_last)
            next++;
          else
            next = best_contour_first;

          dist = TA_ABS(points[next].y - best_y);
          if (dist > 5)
            if (TA_ABS(points[next].x - best_x) <= 20 * dist)
              break;

          best_segment_last = next;

          if (FT_CURVE_TAG(outline.tags[next]) == FT_CURVE_TAG_ON)
          {
            best_on_point_last = next;
            if (best_on_point_first < 0)
              best_on_point_first = next;
          }

        } while (next != best_point);

        /*
         * now set the `round' flag depending on the segment's kind:
         *
         * - if the horizontal distance between the first and last
         *   `on' point is larger than upem/8 (value 8 is heuristic)
         *   we have a flat segment
         * - if either the first or the last point of the segment is
         *   an `off' point, the segment is round, otherwise it is
         *   flat
         */
        if (best_on_point_first >= 0
            && best_on_point_last >= 0
            && (FT_UInt)(TA_ABS(points[best_on_point_last].x
                                - points[best_on_point_first].x))
                 > metrics->units_per_em / 8)
          round = 0;
        else
          round = FT_BOOL(FT_CURVE_TAG(outline.tags[best_segment_first])
                            != FT_CURVE_TAG_ON
                          || FT_CURVE_TAG(outline.tags[best_segment_last])
                               != FT_CURVE_TAG_ON);

        TA_LOG((" (%s)\n", round ? "round" : "flat"));
      }

      if (round)
        rounds[num_rounds++] = best_y;
      else
        flats[num_flats++] = best_y;
    }

    if (num_flats == 0 && num_rounds == 0)
    {
      /* we couldn't find a single glyph to compute this blue zone, */
      /* we will simply ignore it then */
      TA_LOG(("  empty\n"));
      continue;
    }

    /* we have computed the contents of the `rounds' and `flats' tables, */
    /* now determine the reference and overshoot position of the blue -- */
    /* we simply take the median value after a simple sort */
    ta_sort_pos(num_rounds, rounds);
    ta_sort_pos(num_flats, flats);

    blue = &axis->blues[axis->blue_count];
    blue_ref = &blue->ref.org;
    blue_shoot = &blue->shoot.org;

    axis->blue_count++;

    if (num_flats == 0)
    {
      *blue_ref =
      *blue_shoot = rounds[num_rounds / 2];
    }
    else if (num_rounds == 0)
    {
      *blue_ref =
      *blue_shoot = flats[num_flats / 2];
    }
    else
    {
      *blue_ref = flats[num_flats / 2];
      *blue_shoot = rounds[num_rounds / 2];
    }

    /* there are sometimes problems if the overshoot position of top */
    /* zones is under its reference position, or the opposite for bottom */
    /* zones; we must thus check everything there and correct the errors */
    if (*blue_shoot != *blue_ref)
    {
      FT_Pos ref = *blue_ref;
      FT_Pos shoot = *blue_shoot;
      FT_Bool over_ref = FT_BOOL(shoot > ref);


      if (TA_LATIN_IS_TOP_BLUE(bb) ^ over_ref)
      {
        *blue_ref =
        *blue_shoot = (shoot + ref) / 2;

        TA_LOG(("  [overshoot smaller than reference,"
                " taking mean value]\n"));
      }
    }

    blue->flags = 0;
    if (TA_LATIN_IS_TOP_BLUE(bb))
      blue->flags |= TA_LATIN_BLUE_TOP;

    /* the following flag is used later to adjust the y and x scales */
    /* in order to optimize the pixel grid alignment */
    /* of the top of small letters */
    if (bb == TA_LATIN_BLUE_SMALL_TOP)
      blue->flags |= TA_LATIN_BLUE_ADJUSTMENT;

    TA_LOG(("    -> reference = %ld\n"
            "       overshoot = %ld\n",
            *blue_ref, *blue_shoot));
  }

  /* add two blue zones for usWinAscent and usWinDescent */
  /* just in case the above algorithm has missed them -- */
  /* Windows cuts off everything outside of those two values */
  {
    TT_OS2* os2;


    os2 = (TT_OS2*)FT_Get_Sfnt_Table(face, ft_sfnt_os2);

    if (os2)
    {
      blue = &axis->blues[axis->blue_count];
      blue->flags = TA_LATIN_BLUE_TOP | TA_LATIN_BLUE_ACTIVE;
      blue->ref.org =
      blue->shoot.org = os2->usWinAscent;

      TA_LOG(("artificial blue zone for usWinAscent:\n"
              "    -> reference = %ld\n"
              "       overshoot = %ld\n",
              blue->ref.org, blue->shoot.org));

      blue = &axis->blues[axis->blue_count + 1];
      blue->flags = TA_LATIN_BLUE_ACTIVE;
      blue->ref.org =
      blue->shoot.org = -os2->usWinDescent;

      TA_LOG(("artificial blue zone for usWinDescent:\n"
              "    -> reference = %ld\n"
              "       overshoot = %ld\n",
              blue->ref.org, blue->shoot.org));
    }
    else
    {
      blue = &axis->blues[axis->blue_count];
      blue->flags =
      blue->ref.org =
      blue->shoot.org = 0;

      blue = &axis->blues[axis->blue_count + 1];
      blue->flags =
      blue->ref.org =
      blue->shoot.org = 0;
    }
  }

  TA_LOG(("\n"));

  return;
}


/* check whether all ASCII digits have the same advance width */

void
ta_latin_metrics_check_digits(TA_LatinMetrics metrics,
                              FT_Face face)
{
  FT_UInt i;
  FT_Bool started = 0, same_width = 1;
  FT_Fixed advance, old_advance = 0;


  /* digit `0' is 0x30 in all supported charmaps */
  for (i = 0x30; i <= 0x39; i++)
  {
    FT_UInt glyph_index;


    glyph_index = FT_Get_Char_Index(face, i);
    if (glyph_index == 0)
      continue;

    if (FT_Get_Advance(face, glyph_index,
                       FT_LOAD_NO_SCALE
                       | FT_LOAD_NO_HINTING
                       | FT_LOAD_IGNORE_TRANSFORM,
                       &advance))
      continue;

    if (started)
    {
      if (advance != old_advance)
      {
        same_width = 0;
        break;
      }
    }
    else
    {
      old_advance = advance;
      started = 1;
    }
  }

  metrics->root.digits_have_same_width = same_width;
}


/* initialize global metrics */

FT_Error
ta_latin_metrics_init(TA_LatinMetrics metrics,
                      FT_Face face)
{
  FT_CharMap oldmap = face->charmap;


  metrics->units_per_em = face->units_per_EM;

  if (!FT_Select_Charmap(face, FT_ENCODING_UNICODE))
  {
    ta_latin_metrics_init_widths(metrics, face);
    ta_latin_metrics_init_blues(metrics, face);
    ta_latin_metrics_check_digits(metrics, face);
  }

  FT_Set_Charmap(face, oldmap);
  return FT_Err_Ok;
}


/* adjust scaling value, then scale and shift widths */
/* and blue zones (if applicable) for given dimension */

static void
ta_latin_metrics_scale_dim(TA_LatinMetrics metrics,
                           TA_Scaler scaler,
                           TA_Dimension dim)
{
  FT_Fixed scale;
  FT_Pos delta;
  TA_LatinAxis axis;
  FT_UInt ppem;
  FT_UInt nn;


  ppem = metrics->root.scaler.face->size->metrics.x_ppem;

  if (dim == TA_DIMENSION_HORZ)
  {
    scale = scaler->x_scale;
    delta = scaler->x_delta;
  }
  else
  {
    scale = scaler->y_scale;
    delta = scaler->y_delta;
  }

  axis = &metrics->axis[dim];

  if (axis->org_scale == scale && axis->org_delta == delta)
    return;

  axis->org_scale = scale;
  axis->org_delta = delta;

  /* correct X and Y scale to optimize the alignment of the top of */
  /* small letters to the pixel grid */
  /* (if we do x-height snapping for this ppem value) */
  if (!number_set_is_element(
        metrics->root.globals->font->x_height_snapping_exceptions,
        ppem))
  {
    TA_LatinAxis Axis = &metrics->axis[TA_DIMENSION_VERT];
    TA_LatinBlue blue = NULL;


    for (nn = 0; nn < Axis->blue_count; nn++)
    {
      if (Axis->blues[nn].flags & TA_LATIN_BLUE_ADJUSTMENT)
      {
        blue = &Axis->blues[nn];
        break;
      }
    }

    if (blue)
    {
      FT_Pos scaled;
      FT_Pos threshold;
      FT_Pos fitted;
      FT_UInt limit;


      scaled = FT_MulFix(blue->shoot.org, scaler->y_scale);
      limit = metrics->root.globals->increase_x_height;
      threshold = 40;

      /* if the `increase-x-height' property is active, */
      /* we round up much more often                    */
      if (limit
          && ppem <= limit
          && ppem >= TA_PROP_INCREASE_X_HEIGHT_MIN)
        threshold = 52;

      fitted = (scaled + threshold) & ~63;

      if (scaled != fitted)
      {
        if (dim == TA_DIMENSION_VERT)
          scale = FT_MulDiv(scale, fitted, scaled);
      }
    }
  }

  axis->scale = scale;
  axis->delta = delta;

  if (dim == TA_DIMENSION_HORZ)
  {
    metrics->root.scaler.x_scale = scale;
    metrics->root.scaler.x_delta = delta;
  }
  else
  {
    metrics->root.scaler.y_scale = scale;
    metrics->root.scaler.y_delta = delta;
  }

  /* scale the widths */
  for (nn = 0; nn < axis->width_count; nn++)
  {
    TA_Width width = axis->widths + nn;


    width->cur = FT_MulFix(width->org, scale);
    width->fit = width->cur;
  }

  /* an extra-light axis corresponds to a standard width that is */
  /* smaller than 5/8 pixels */
  axis->extra_light =
    (FT_Bool)(FT_MulFix(axis->standard_width, scale) < 32 + 8);

  if (dim == TA_DIMENSION_VERT)
  {
    /* scale the blue zones */
    for (nn = 0; nn < axis->blue_count; nn++)
    {
      TA_LatinBlue blue = &axis->blues[nn];
      FT_Pos dist;


      blue->ref.cur = FT_MulFix(blue->ref.org, scale) + delta;
      blue->ref.fit = blue->ref.cur;
      blue->shoot.cur = FT_MulFix(blue->shoot.org, scale) + delta;
      blue->shoot.fit = blue->shoot.cur;
      blue->flags &= ~TA_LATIN_BLUE_ACTIVE;

      /* a blue zone is only active if it is less than 3/4 pixels tall */
      dist = FT_MulFix(blue->ref.org - blue->shoot.org, scale);
      if (dist <= 48 && dist >= -48)
      {
#if 0
        FT_Pos delta1;
#endif
        FT_Pos delta2;


        /* use discrete values for blue zone widths */

#if 0
        /* generic, original code */
        delta1 = blue->shoot.org - blue->ref.org;
        delta2 = delta1;
        if (delta1 < 0)
          delta2 = -delta2;

        delta2 = FT_MulFix(delta2, scale);

        if (delta2 < 32)
          delta2 = 0;
        else if (delta2 < 64)
          delta2 = 32 + (((delta2 - 32) + 16) & ~31);
        else
          delta2 = TA_PIX_ROUND(delta2);

        if (delta1 < 0)
          delta2 = -delta2;

        blue->ref.fit = TA_PIX_ROUND(blue->ref.cur);
        blue->shoot.fit = blue->ref.fit + delta2;
#else
        /* simplified version due to abs(dist) <= 48 */
        delta2 = dist;
        if (dist < 0)
          delta2 = -delta2;

        if (delta2 < 32)
          delta2 = 0;
        else if (delta2 < 48)
          delta2 = 32;
        else
          delta2 = 64;

        if (dist < 0)
          delta2 = -delta2;

        blue->ref.fit = TA_PIX_ROUND(blue->ref.cur);
        blue->shoot.fit = blue->ref.fit - delta2;
#endif

        blue->flags |= TA_LATIN_BLUE_ACTIVE;
      }
    }

    /* the last two artificial blue zones are to be scaled */
    /* with uncorrected scaling values */
    {
      TA_LatinAxis a = &metrics->axis[TA_DIMENSION_VERT];
      TA_LatinBlue b;


      b = &a->blues[a->blue_count];
      b->ref.cur =
      b->ref.fit =
      b->shoot.cur =
      b->shoot.fit = FT_MulFix(b->ref.org, a->org_scale) + delta;

      b = &a->blues[a->blue_count + 1];
      b->ref.cur =
      b->ref.fit =
      b->shoot.cur =
      b->shoot.fit = FT_MulFix(b->ref.org, a->org_scale) + delta;
    }
  }
}


/* scale global values in both directions */

void
ta_latin_metrics_scale(TA_LatinMetrics metrics,
                       TA_Scaler scaler)
{
  metrics->root.scaler.render_mode = scaler->render_mode;
  metrics->root.scaler.face = scaler->face;
  metrics->root.scaler.flags = scaler->flags;

  ta_latin_metrics_scale_dim(metrics, scaler, TA_DIMENSION_HORZ);
  ta_latin_metrics_scale_dim(metrics, scaler, TA_DIMENSION_VERT);
}


/* walk over all contours and compute its segments */

FT_Error
ta_latin_hints_compute_segments(TA_GlyphHints hints,
                                TA_Dimension dim)
{
  TA_AxisHints axis = &hints->axis[dim];
  FT_Error error = FT_Err_Ok;

  TA_Segment segment = NULL;
  TA_SegmentRec seg0;

  TA_Point* contour = hints->contours;
  TA_Point* contour_limit = contour + hints->num_contours;
  TA_Direction major_dir, segment_dir;


  memset(&seg0, 0, sizeof (TA_SegmentRec));
  seg0.score = 32000;
  seg0.flags = TA_EDGE_NORMAL;

  major_dir = (TA_Direction)TA_ABS(axis->major_dir);
  segment_dir = major_dir;

  axis->num_segments = 0;

  /* set up (u,v) in each point */
  if (dim == TA_DIMENSION_HORZ)
  {
    TA_Point point = hints->points;
    TA_Point limit = point + hints->num_points;


    for (; point < limit; point++)
    {
      point->u = point->fx;
      point->v = point->fy;
    }
  }
  else
  {
    TA_Point point = hints->points;
    TA_Point limit = point + hints->num_points;


    for (; point < limit; point++)
    {
      point->u = point->fy;
      point->v = point->fx;
    }
  }

  /* do each contour separately */
  for (; contour < contour_limit; contour++)
  {
    TA_Point point = contour[0];
    TA_Point last = point->prev;

    int on_edge = 0;

    FT_Pos min_pos = 32000; /* minimum segment pos != min_coord */
    FT_Pos max_pos = -32000; /* maximum segment pos != max_coord */
    FT_Bool passed;


    if (point == last) /* skip singletons -- just in case */
      continue;

    if (TA_ABS(last->out_dir) == major_dir
        && TA_ABS(point->out_dir) == major_dir)
    {
      /* we are already on an edge, try to locate its start */
      last = point;

      for (;;)
      {
        point = point->prev;
        if (TA_ABS(point->out_dir) != major_dir)
        {
          point = point->next;
          break;
        }
        if (point == last)
          break;
      }
    }

    last = point;
    passed = 0;

    for (;;)
    {
      FT_Pos u, v;


      if (on_edge)
      {
        u = point->u;
        if (u < min_pos)
          min_pos = u;
        if (u > max_pos)
          max_pos = u;

        if (point->out_dir != segment_dir
            || point == last)
        {
          /* we are just leaving an edge; record a new segment! */
          segment->last = point;
          segment->pos = (FT_Short)((min_pos + max_pos) >> 1);

          /* a segment is round if either its first or last point */
          /* is a control point */
          if ((segment->first->flags | point->flags) & TA_FLAG_CONTROL)
            segment->flags |= TA_EDGE_ROUND;

          /* compute segment size */
          min_pos = max_pos = point->v;

          v = segment->first->v;
          if (v < min_pos)
            min_pos = v;
          if (v > max_pos)
            max_pos = v;

          segment->min_coord = (FT_Short)min_pos;
          segment->max_coord = (FT_Short)max_pos;
          segment->height = (FT_Short)(segment->max_coord -
                                         segment->min_coord);

          on_edge = 0;
          segment = NULL;
          /* fall through */
        }
      }

      /* now exit if we are at the start/end point */
      if (point == last)
      {
        if (passed)
          break;
        passed = 1;
      }

      if (!on_edge
          && TA_ABS(point->out_dir) == major_dir)
      {
        /* this is the start of a new segment! */
        segment_dir = (TA_Direction)point->out_dir;

        /* clear all segment fields */
        error = ta_axis_hints_new_segment(axis, &segment);
        if (error)
          goto Exit;

        segment[0] = seg0;
        segment->dir = (FT_Char)segment_dir;
        min_pos = max_pos = point->u;
        segment->first = point;
        segment->last = point;
        on_edge = 1;
      }

      point = point->next;
    }
  } /* contours */


  /* now slightly increase the height of segments if this makes sense -- */
  /* this is used to better detect and ignore serifs */
  {
    TA_Segment segments = axis->segments;
    TA_Segment segments_end = segments + axis->num_segments;


    for (segment = segments; segment < segments_end; segment++)
    {
      TA_Point first = segment->first;
      TA_Point last = segment->last;

      FT_Pos first_v = first->v;
      FT_Pos last_v = last->v;


      if (first == last)
        continue;

      if (first_v < last_v)
      {
        TA_Point p;


        p = first->prev;
        if (p->v < first_v)
          segment->height = (FT_Short)(segment->height +
                                        ((first_v - p->v) >> 1));

        p = last->next;
        if (p->v > last_v)
          segment->height = (FT_Short)(segment->height +
                                        ((p->v - last_v) >> 1));
      }
      else
      {
        TA_Point p;


        p = first->prev;
        if (p->v > first_v)
          segment->height = (FT_Short)(segment->height +
                                        ((p->v - first_v) >> 1));

        p = last->next;
        if (p->v < last_v)
          segment->height = (FT_Short)(segment->height +
                                        ((last_v - p->v) >> 1));
      }
    }
  }

Exit:
  return error;
}


/* link segments to form stems and serifs */

void
ta_latin_hints_link_segments(TA_GlyphHints hints,
                             TA_Dimension dim)
{
  TA_AxisHints axis = &hints->axis[dim];

  TA_Segment segments = axis->segments;
  TA_Segment segment_limit = segments + axis->num_segments;

  FT_Pos len_threshold, len_score;
  TA_Segment seg1, seg2;


  len_threshold = TA_LATIN_CONSTANT(hints->metrics, 8);
  if (len_threshold == 0)
    len_threshold = 1;

  len_score = TA_LATIN_CONSTANT(hints->metrics, 6000);

  /* now compare each segment to the others */
  for (seg1 = segments; seg1 < segment_limit; seg1++)
  {
    /* the fake segments are introduced to hint the metrics -- */
    /* we must never link them to anything */
    if (seg1->dir != axis->major_dir
        || seg1->first == seg1->last)
      continue;

    /* search for stems having opposite directions, */
    /* with seg1 to the `left' of seg2 */
    for (seg2 = segments; seg2 < segment_limit; seg2++)
    {
      FT_Pos pos1 = seg1->pos;
      FT_Pos pos2 = seg2->pos;


      if (seg1->dir + seg2->dir == 0
          && pos2 > pos1)
      {
        /* compute distance between the two segments */
        FT_Pos dist = pos2 - pos1;
        FT_Pos min = seg1->min_coord;
        FT_Pos max = seg1->max_coord;
        FT_Pos len, score;


        if (min < seg2->min_coord)
          min = seg2->min_coord;
        if (max > seg2->max_coord)
          max = seg2->max_coord;

        /* compute maximum coordinate difference of the two segments */
        len = max - min;
        if (len >= len_threshold)
        {
          /* small coordinate differences cause a higher score, and */
          /* segments with a greater distance cause a higher score also */
          score = dist + len_score / len;

          /* and we search for the smallest score */
          /* of the sum of the two values */
          if (score < seg1->score)
          {
            seg1->score = score;
            seg1->link = seg2;
          }

          if (score < seg2->score)
          {
            seg2->score = score;
            seg2->link = seg1;
          }
        }
      }
    }
  }

  /* now compute the `serif' segments, cf. explanations in `tahints.h' */
  for (seg1 = segments; seg1 < segment_limit; seg1++)
  {
    seg2 = seg1->link;

    if (seg2)
    {
      if (seg2->link != seg1)
      {
        seg1->link = 0;
        seg1->serif = seg2->link;
      }
    }
  }
}


/* link segments to edges, using feature analysis for selection */

FT_Error
ta_latin_hints_compute_edges(TA_GlyphHints hints,
                             TA_Dimension dim)
{
  TA_AxisHints axis = &hints->axis[dim];
  FT_Error error = FT_Err_Ok;
  TA_LatinAxis laxis = &((TA_LatinMetrics)hints->metrics)->axis[dim];

  TA_Segment segments = axis->segments;
  TA_Segment segment_limit = segments + axis->num_segments;
  TA_Segment seg;

#if 0
  TA_Direction up_dir;
#endif
  FT_Fixed scale;
  FT_Pos edge_distance_threshold;
  FT_Pos segment_length_threshold;


  axis->num_edges = 0;

  scale = (dim == TA_DIMENSION_HORZ) ? hints->x_scale
                                     : hints->y_scale;

#if 0
  up_dir = (dim == TA_DIMENSION_HORZ) ? TA_DIR_UP
                                      : TA_DIR_RIGHT;
#endif

  /* we ignore all segments that are less than 1 pixel in length */
  /* to avoid many problems with serif fonts */
  /* (the corresponding threshold is computed in font units) */
  if (dim == TA_DIMENSION_HORZ)
    segment_length_threshold = FT_DivFix(64, hints->y_scale);
  else
    segment_length_threshold = 0;

  /********************************************************************/
  /*                                                                  */
  /* We begin by generating a sorted table of edges for the current   */
  /* direction. To do so, we simply scan each segment and try to find */
  /* an edge in our table that corresponds to its position.           */
  /*                                                                  */
  /* If no edge is found, we create and insert a new edge in the      */
  /* sorted table. Otherwise, we simply add the segment to the edge's */
  /* list which gets processed in the second step to compute the      */
  /* edge's properties.                                               */
  /*                                                                  */
  /* Note that the table of edges is sorted along the segment/edge    */
  /* position.                                                        */
  /*                                                                  */
  /********************************************************************/

  /* assure that edge distance threshold is at most 0.25px */
  edge_distance_threshold = FT_MulFix(laxis->edge_distance_threshold,
                                      scale);
  if (edge_distance_threshold > 64 / 4)
    edge_distance_threshold = 64 / 4;

  edge_distance_threshold = FT_DivFix(edge_distance_threshold,
                                      scale);

  for (seg = segments; seg < segment_limit; seg++)
  {
    TA_Edge found = NULL;
    FT_Int ee;


    if (seg->height < segment_length_threshold)
      continue;

    /* a special case for serif edges: */
    /* if they are smaller than 1.5 pixels we ignore them */
    if (seg->serif
        && 2 * seg->height < 3 * segment_length_threshold)
      continue;

    /* look for an edge corresponding to the segment */
    for (ee = 0; ee < axis->num_edges; ee++)
    {
      TA_Edge edge = axis->edges + ee;
      FT_Pos dist;


      dist = seg->pos - edge->fpos;
      if (dist < 0)
        dist = -dist;

      if (dist < edge_distance_threshold && edge->dir == seg->dir)
      {
        found = edge;
        break;
      }
    }

    if (!found)
    {
      TA_Edge edge;


      /* insert a new edge in the list and sort according to the position */
      error = ta_axis_hints_new_edge(axis, seg->pos,
                                     (TA_Direction)seg->dir,
                                     &edge);
      if (error)
        goto Exit;

      /* add the segment to the new edge's list */
      memset(edge, 0, sizeof (TA_EdgeRec));
      edge->first = seg;
      edge->last = seg;
      edge->dir = seg->dir;
      edge->fpos = seg->pos;
      edge->opos = FT_MulFix(seg->pos, scale);
      edge->pos = edge->opos;
      seg->edge_next = seg;
    }
    else
    {
      /* if an edge was found, simply add the segment to the edge's list */
      seg->edge_next = found->first;
      found->last->edge_next = seg;
      found->last = seg;
    }
  }

  /*****************************************************************/
  /*                                                               */
  /* Good, we now compute each edge's properties according to      */
  /* the segments found on its position.  Basically, these are     */
  /*                                                               */
  /* - the edge's main direction                                   */
  /* - stem edge, serif edge or both (which defaults to stem then) */
  /* - rounded edge, straight or both (which defaults to straight) */
  /* - link for edge                                               */
  /*                                                               */
  /*****************************************************************/

  /* first of all, set the `edge' field in each segment -- this is */
  /* required in order to compute edge links */

  /* note that removing this loop and setting the `edge' field of each */
  /* segment directly in the code above slows down execution speed for */
  /* some reasons on platforms like the Sun */
  {
    TA_Edge edges = axis->edges;
    TA_Edge edge_limit = edges + axis->num_edges;
    TA_Edge edge;


    for (edge = edges; edge < edge_limit; edge++)
    {
      seg = edge->first;
      if (seg)
        do
        {
          seg->edge = edge;
          seg = seg->edge_next;
        } while (seg != edge->first);
    }

    /* now compute each edge properties */
    for (edge = edges; edge < edge_limit; edge++)
    {
      FT_Int is_round = 0; /* does it contain round segments? */
      FT_Int is_straight = 0; /* does it contain straight segments? */
#if 0
      FT_Pos ups = 0; /* number of upwards segments */
      FT_Pos downs = 0; /* number of downwards segments */
#endif


      seg = edge->first;

      do
      {
        FT_Bool is_serif;


        /* check for roundness of segment */
        if (seg->flags & TA_EDGE_ROUND)
          is_round++;
        else
          is_straight++;

#if 0
        /* check for segment direction */
        if (seg->dir == up_dir)
          ups += seg->max_coord - seg->min_coord;
        else
          downs += seg->max_coord - seg->min_coord;
#endif

        /* check for links -- */
        /* if seg->serif is set, then seg->link must be ignored */
        is_serif = (FT_Bool)(seg->serif
                             && seg->serif->edge
                             && seg->serif->edge != edge);

        if ((seg->link && seg->link->edge != NULL)
            || is_serif)
        {
          TA_Edge edge2;
          TA_Segment seg2;


          edge2 = edge->link;
          seg2 = seg->link;

          if (is_serif)
          {
            seg2 = seg->serif;
            edge2 = edge->serif;
          }

          if (edge2)
          {
            FT_Pos edge_delta;
            FT_Pos seg_delta;


            edge_delta = edge->fpos - edge2->fpos;
            if (edge_delta < 0)
              edge_delta = -edge_delta;

            seg_delta = seg->pos - seg2->pos;
            if (seg_delta < 0)
              seg_delta = -seg_delta;

            if (seg_delta < edge_delta)
              edge2 = seg2->edge;
          }
          else
            edge2 = seg2->edge;

          if (is_serif)
          {
            edge->serif = edge2;
            edge2->flags |= TA_EDGE_SERIF;
          }
          else
            edge->link = edge2;
        }

        seg = seg->edge_next;
      } while (seg != edge->first);

      /* set the round/straight flags */
      edge->flags = TA_EDGE_NORMAL;

      if (is_round > 0
          && is_round >= is_straight)
        edge->flags |= TA_EDGE_ROUND;

#if 0
      /* set the edge's main direction */
      edge->dir = TA_DIR_NONE;

      if (ups > downs)
        edge->dir = (FT_Char)up_dir;

      else if (ups < downs)
        edge->dir = (FT_Char)-up_dir;

      else if (ups == downs)
        edge->dir = 0; /* both up and down! */
#endif

      /* get rid of serifs if link is set */
      /* XXX: this gets rid of many unpleasant artefacts! */
      /* example: the `c' in cour.pfa at size 13 */

      if (edge->serif && edge->link)
        edge->serif = 0;
    }
  }

Exit:
  return error;
}


/* detect segments and edges for given dimension */

FT_Error
ta_latin_hints_detect_features(TA_GlyphHints hints,
                               TA_Dimension dim)
{
  FT_Error error;


  error = ta_latin_hints_compute_segments(hints, dim);
  if (!error)
  {
    ta_latin_hints_link_segments(hints, dim);

    error = ta_latin_hints_compute_edges(hints, dim);
  }

  return error;
}


/* compute all edges which lie within blue zones */

void
ta_latin_hints_compute_blue_edges(TA_GlyphHints hints,
                                  TA_LatinMetrics metrics)
{
  TA_AxisHints axis = &hints->axis[TA_DIMENSION_VERT];

  TA_Edge edge = axis->edges;
  TA_Edge edge_limit = edge + axis->num_edges;

  TA_LatinAxis latin = &metrics->axis[TA_DIMENSION_VERT];
  FT_Fixed scale = latin->scale;


  /* compute which blue zones are active, */
  /* i.e. have their scaled size < 3/4 pixels */

  /* for each horizontal edge search the blue zone which is closest */
  for (; edge < edge_limit; edge++)
  {
    FT_UInt bb;
    TA_Width best_blue = NULL;
    FT_Pos best_dist; /* initial threshold */

    FT_UInt best_blue_idx = 0;
    FT_Bool best_blue_is_shoot = 0;


    /* compute the initial threshold as a fraction of the EM size */
    /* (the value 40 is heuristic) */
    best_dist = FT_MulFix(metrics->units_per_em / 40, scale);

    /* assure a minimum distance of 0.5px */
    if (best_dist > 64 / 2)
      best_dist = 64 / 2;

    /* this loop also handles the two extra blue zones */
    /* for usWinAscent and usWinDescent */
    /* if option `windows-compatibility' is set */
    for (bb = 0;
         bb < latin->blue_count
              + (metrics->root.globals->font->windows_compatibility ? 2 : 0);
         bb++)
    {
      TA_LatinBlue blue = latin->blues + bb;
      FT_Bool is_top_blue, is_major_dir;


      /* skip inactive blue zones (i.e., those that are too large) */
      if (!(blue->flags & TA_LATIN_BLUE_ACTIVE))
        continue;

      /* if it is a top zone, check for right edges -- */
      /* if it is a bottom zone, check for left edges */
      is_top_blue = (FT_Byte)((blue->flags & TA_LATIN_BLUE_TOP) != 0);
      is_major_dir = FT_BOOL(edge->dir == axis->major_dir);

      /* if it is a top zone, the edge must be against the major */
      /* direction; if it is a bottom zone, it must be in the major */
      /* direction */
      if (is_top_blue ^ is_major_dir)
      {
        FT_Pos dist;


        /* first of all, compare it to the reference position */
        dist = edge->fpos - blue->ref.org;
        if (dist < 0)
          dist = -dist;

        dist = FT_MulFix(dist, scale);
        if (dist < best_dist)
        {
          best_dist = dist;
          best_blue = &blue->ref;

          best_blue_idx = bb;
          best_blue_is_shoot = 0;
        }

        /* now compare it to the overshoot position and check whether */
        /* the edge is rounded, and whether the edge is over the */
        /* reference position of a top zone, or under the reference */
        /* position of a bottom zone */
        if (edge->flags & TA_EDGE_ROUND
            && dist != 0)
        {
          FT_Bool is_under_ref = FT_BOOL(edge->fpos < blue->ref.org);


          if (is_top_blue ^ is_under_ref)
          {
            dist = edge->fpos - blue->shoot.org;
            if (dist < 0)
              dist = -dist;

            dist = FT_MulFix(dist, scale);
            if (dist < best_dist)
            {
              best_dist = dist;
              best_blue = &blue->shoot;

              best_blue_idx = bb;
              best_blue_is_shoot = 1;
            }
          }
        }
      }
    }

    if (best_blue)
    {
      edge->blue_edge = best_blue;
      edge->best_blue_idx = best_blue_idx;
      edge->best_blue_is_shoot = best_blue_is_shoot;
    }
  }
}


/* initalize hinting engine */

static FT_Error
ta_latin_hints_init(TA_GlyphHints hints,
                    TA_LatinMetrics metrics)
{
  FT_Render_Mode mode;
  FT_UInt32 scaler_flags, other_flags;
  FT_Face face = metrics->root.scaler.face;


  ta_glyph_hints_rescale(hints, (TA_ScriptMetrics)metrics);

  /* correct x_scale and y_scale if needed, since they may have */
  /* been modified by `ta_latin_metrics_scale_dim' above */
  hints->x_scale = metrics->axis[TA_DIMENSION_HORZ].scale;
  hints->x_delta = metrics->axis[TA_DIMENSION_HORZ].delta;
  hints->y_scale = metrics->axis[TA_DIMENSION_VERT].scale;
  hints->y_delta = metrics->axis[TA_DIMENSION_VERT].delta;

  /* compute flags depending on render mode, etc. */
  mode = metrics->root.scaler.render_mode;

#if 0 /* #ifdef TA_CONFIG_OPTION_USE_WARPER */
  if (mode == FT_RENDER_MODE_LCD
      || mode == FT_RENDER_MODE_LCD_V)
    metrics->root.scaler.render_mode =
    mode = FT_RENDER_MODE_NORMAL;
#endif

  scaler_flags = hints->scaler_flags;
  other_flags = 0;

  /* we snap the width of vertical stems for the monochrome */
  /* and horizontal LCD rendering targets only */
  if (mode == FT_RENDER_MODE_MONO
      || mode == FT_RENDER_MODE_LCD)
    other_flags |= TA_LATIN_HINTS_HORZ_SNAP;

  /* we snap the width of horizontal stems for the monochrome */
  /* and vertical LCD rendering targets only */
  if (mode == FT_RENDER_MODE_MONO
      || mode == FT_RENDER_MODE_LCD_V)
    other_flags |= TA_LATIN_HINTS_VERT_SNAP;

  /* we adjust stems to full pixels only if we don't use the `light' mode */
  if (mode != FT_RENDER_MODE_LIGHT)
    other_flags |= TA_LATIN_HINTS_STEM_ADJUST;

  if (mode == FT_RENDER_MODE_MONO)
    other_flags |= TA_LATIN_HINTS_MONO;

  /* in `light' hinting mode we disable horizontal hinting completely; */
  /* we also do it if the face is italic */
  if (mode == FT_RENDER_MODE_LIGHT
      || (face->style_flags & FT_STYLE_FLAG_ITALIC) != 0)
    scaler_flags |= TA_SCALER_FLAG_NO_HORIZONTAL;

  hints->scaler_flags = scaler_flags;
  hints->other_flags = other_flags;

  return FT_Err_Ok;
}


/* snap a given width in scaled coordinates to */
/* one of the current standard widths */

static FT_Pos
ta_latin_snap_width(TA_Width widths,
                    FT_Int count,
                    FT_Pos width)
{
  int n;
  FT_Pos best = 64 + 32 + 2;
  FT_Pos reference = width;
  FT_Pos scaled;


  for (n = 0; n < count; n++)
  {
    FT_Pos w;
    FT_Pos dist;


    w = widths[n].cur;
    dist = width - w;
    if (dist < 0)
      dist = -dist;
    if (dist < best)
    {
      best = dist;
      reference = w;
    }
  }

  scaled = TA_PIX_ROUND(reference);

  if (width >= reference)
  {
    if (width < scaled + 48)
      width = reference;
  }
  else
  {
    if (width > scaled - 48)
      width = reference;
  }

   return width;
}


/* compute the snapped width of a given stem, ignoring very thin ones */

/* there is a lot of voodoo in this function; changing the hard-coded */
/* parameters influence the whole hinting process */

static FT_Pos
ta_latin_compute_stem_width(TA_GlyphHints hints,
                            TA_Dimension dim,
                            FT_Pos width,
                            FT_Byte base_flags,
                            FT_Byte stem_flags)
{
  TA_LatinMetrics metrics = (TA_LatinMetrics) hints->metrics;
  TA_LatinAxis axis = &metrics->axis[dim];

  FT_Pos dist = width;
  FT_Int sign = 0;
  FT_Int vertical = (dim == TA_DIMENSION_VERT);


  if (!TA_LATIN_HINTS_DO_STEM_ADJUST(hints)
      || axis->extra_light)
    return width;

  if (dist < 0)
  {
    dist = -width;
    sign = 1;
  }

  if ((vertical && !TA_LATIN_HINTS_DO_VERT_SNAP(hints))
      || (!vertical && !TA_LATIN_HINTS_DO_HORZ_SNAP(hints)))
  {
    /* smooth hinting process: very lightly quantize the stem width */

    /* leave the widths of serifs alone */
    if ((stem_flags & TA_EDGE_SERIF)
        && vertical
        && (dist < 3 * 64))
      goto Done_Width;
    else if (base_flags & TA_EDGE_ROUND)
    {
      if (dist < 80)
        dist = 64;
    }
    else if (dist < 56)
      dist = 56;

    if (axis->width_count > 0)
    {
      FT_Pos delta;


      /* compare to standard width */
      delta = dist - axis->widths[0].cur;

      if (delta < 0)
        delta = -delta;

      if (delta < 40)
      {
        dist = axis->widths[0].cur;
        if (dist < 48)
          dist = 48;

        goto Done_Width;
      }

      if (dist < 3 * 64)
      {
        delta = dist & 63;
        dist &= -64;

        if (delta < 10)
          dist += delta;
        else if (delta < 32)
          dist += 10;
        else if (delta < 54)
          dist += 54;
        else
          dist += delta;
      }
      else
        dist = (dist + 32) & ~63;
    }
  }
  else
  {
    /* strong hinting process: snap the stem width to integer pixels */

    FT_Pos org_dist = dist;


    dist = ta_latin_snap_width(axis->widths, axis->width_count, dist);

    if (vertical)
    {
      /* in the case of vertical hinting, */
      /* always round the stem heights to integer pixels */

      if (dist >= 64)
        dist = (dist + 16) & ~63;
      else
        dist = 64;
    }
    else
    {
      if (TA_LATIN_HINTS_DO_MONO(hints))
      {
        /* monochrome horizontal hinting: */
        /* snap widths to integer pixels with a different threshold */

        if (dist < 64)
          dist = 64;
        else
          dist = (dist + 32) & ~63;
      }
      else
      {
        /* for horizontal anti-aliased hinting, we adopt a more subtle */
        /* approach: we strengthen small stems, round stems whose size */
        /* is between 1 and 2 pixels to an integer, otherwise nothing */

        if (dist < 48)
          dist = (dist + 64) >> 1;

        else if (dist < 128)
        {
          /* we only round to an integer width if the corresponding */
          /* distortion is less than 1/4 pixel -- otherwise, this */
          /* makes everything worse since the diagonals, which are */
          /* not hinted, appear a lot bolder or thinner than the */
          /* vertical stems */

          FT_Pos delta;


          dist = (dist + 22) & ~63;
          delta = dist - org_dist;
          if (delta < 0)
            delta = -delta;

          if (delta >= 16)
          {
            dist = org_dist;
            if (dist < 48)
              dist = (dist + 64) >> 1;
          }
        }
        else
          /* round otherwise to prevent color fringes in LCD mode */
          dist = (dist + 32) & ~63;
      }
    }
  }

Done_Width:
  if (sign)
    dist = -dist;

  return dist;
}


/* align one stem edge relative to the previous stem edge */

static void
ta_latin_align_linked_edge(TA_GlyphHints hints,
                           TA_Dimension dim,
                           TA_Edge base_edge,
                           TA_Edge stem_edge)
{
  FT_Pos dist = stem_edge->opos - base_edge->opos;

  FT_Pos fitted_width = ta_latin_compute_stem_width(
                           hints, dim, dist,
                           base_edge->flags,
                           stem_edge->flags);


  stem_edge->pos = base_edge->pos + fitted_width;

  TA_LOG(("  LINK: edge %d (opos=%.2f) linked to %.2f,"
            " dist was %.2f, now %.2f\n",
          stem_edge - hints->axis[dim].edges, stem_edge->opos / 64.0,
          stem_edge->pos / 64.0, dist / 64.0, fitted_width / 64.0));

  if (hints->recorder)
    hints->recorder(ta_link, hints, dim,
                    base_edge, stem_edge, NULL, NULL, NULL);
}


/* shift the coordinates of the `serif' edge by the same amount */
/* as the corresponding `base' edge has been moved already */

static void
ta_latin_align_serif_edge(TA_GlyphHints hints,
                          TA_Edge base,
                          TA_Edge serif)
{
  FT_UNUSED(hints);

  serif->pos = base->pos + (serif->opos - base->opos);
}


/* the main grid-fitting routine */

void
ta_latin_hint_edges(TA_GlyphHints hints,
                    TA_Dimension dim)
{
  TA_AxisHints axis = &hints->axis[dim];

  TA_Edge edges = axis->edges;
  TA_Edge edge_limit = edges + axis->num_edges;
  FT_PtrDist n_edges;
  TA_Edge edge;

  TA_Edge anchor = NULL;
  FT_Int has_serifs = 0;

#ifdef TA_DEBUG
  FT_UInt num_actions = 0;
#endif

  TA_LOG(("%s edge hinting\n", dim == TA_DIMENSION_VERT ? "horizontal"
                                                        : "vertical"));

  /* we begin by aligning all stems relative to the blue zone if needed -- */
  /* that's only for horizontal edges */

  if (dim == TA_DIMENSION_VERT
      && TA_HINTS_DO_BLUES(hints))
  {
    for (edge = edges; edge < edge_limit; edge++)
    {
      TA_Width blue;
      TA_Edge edge1, edge2; /* these edges form the stem to check */


      if (edge->flags & TA_EDGE_DONE)
        continue;

      blue = edge->blue_edge;
      edge1 = NULL;
      edge2 = edge->link;

      if (blue)
        edge1 = edge;

      /* flip edges if the other stem is aligned to a blue zone */
      else if (edge2 && edge2->blue_edge)
      {
        blue = edge2->blue_edge;
        edge1 = edge2;
        edge2 = edge;
      }

      if (!edge1)
        continue;

#ifdef TA_DEBUG
      if (!anchor)
        TA_LOG(("  BLUE_ANCHOR: edge %d (opos=%.2f) snapped to %.2f,"
                  " was %.2f (anchor=edge %d)\n",
                edge1 - edges, edge1->opos / 64.0, blue->fit / 64.0,
                edge1->pos / 64.0, edge - edges));
      else
        TA_LOG(("  BLUE: edge %d (opos=%.2f) snapped to %.2f, was %.2f\n",
                edge1 - edges, edge1->opos / 64.0, blue->fit / 64.0,
                edge1->pos / 64.0));

      num_actions++;
#endif

      edge1->pos = blue->fit;
      edge1->flags |= TA_EDGE_DONE;

      if (hints->recorder)
      {
        if (!anchor)
          hints->recorder(ta_blue_anchor, hints, dim,
                          edge1, edge, NULL, NULL, NULL);
        else
          hints->recorder(ta_blue, hints, dim,
                          edge1, NULL, NULL, NULL, NULL);
      }

      if (edge2 && !edge2->blue_edge)
      {
        ta_latin_align_linked_edge(hints, dim, edge1, edge2);
        edge2->flags |= TA_EDGE_DONE;

#ifdef TA_DEBUG
        num_actions++;
#endif
      }

      if (!anchor)
        anchor = edge;
    }
  }

  /* now we align all other stem edges, */
  /* trying to maintain the relative order of stems in the glyph */
  for (edge = edges; edge < edge_limit; edge++)
  {
    TA_Edge edge2;


    if (edge->flags & TA_EDGE_DONE)
      continue;

    /* skip all non-stem edges */
    edge2 = edge->link;
    if (!edge2)
    {
      has_serifs++;
      continue;
    }

    /* now align the stem */

    /* this should not happen, but it's better to be safe */
    if (edge2->blue_edge)
    {
      TA_LOG(("  ASSERTION FAILED for edge %d\n", edge2-edges));

      ta_latin_align_linked_edge(hints, dim, edge2, edge);
      edge->flags |= TA_EDGE_DONE;

#ifdef TA_DEBUG
      num_actions++;
#endif
      continue;
    }

    if (!anchor)
    {
      /* if we reach this if clause, no stem has been aligned yet */

      FT_Pos org_len, org_center, cur_len;
      FT_Pos cur_pos1, error1, error2, u_off, d_off;


      org_len = edge2->opos - edge->opos;
      cur_len = ta_latin_compute_stem_width(hints, dim, org_len,
                                            edge->flags, edge2->flags);

      /* some voodoo to specially round edges for small stem widths; */
      /* the idea is to align the center of a stem, */
      /* then shifting the stem edges to suitable positions */
      if (cur_len <= 64)
      {
        /* width <= 1px */
        u_off = 32;
        d_off = 32;
      }
      else
      {
        /* 1px < width < 1.5px */
        u_off = 38;
        d_off = 26;
      }

      if (cur_len < 96)
      {
        org_center = edge->opos + (org_len >> 1);
        cur_pos1 = TA_PIX_ROUND(org_center);

        error1 = org_center - (cur_pos1 - u_off);
        if (error1 < 0)
          error1 = -error1;

        error2 = org_center - (cur_pos1 + d_off);
        if (error2 < 0)
          error2 = -error2;

        if (error1 < error2)
          cur_pos1 -= u_off;
        else
          cur_pos1 += d_off;

        edge->pos = cur_pos1 - cur_len / 2;
        edge2->pos = edge->pos + cur_len;
      }
      else
        edge->pos = TA_PIX_ROUND(edge->opos);

      anchor = edge;
      edge->flags |= TA_EDGE_DONE;

      TA_LOG(("  ANCHOR: edge %d (opos=%.2f) and %d (opos=%.2f)"
                " snapped to %.2f and %.2f\n",
              edge - edges, edge->opos / 64.0,
              edge2 - edges, edge2->opos / 64.0,
              edge->pos / 64.0, edge2->pos / 64.0));

      if (hints->recorder)
        hints->recorder(ta_anchor, hints, dim,
                        edge, edge2, NULL, NULL, NULL);

      ta_latin_align_linked_edge(hints, dim, edge, edge2);

#ifdef TA_DEBUG
      num_actions += 2;
#endif
    }
    else
    {
      FT_Pos org_pos, org_len, org_center, cur_len;
      FT_Pos cur_pos1, cur_pos2, delta1, delta2;


      org_pos = anchor->pos + (edge->opos - anchor->opos);
      org_len = edge2->opos - edge->opos;
      org_center = org_pos + (org_len >> 1);

      cur_len = ta_latin_compute_stem_width(hints, dim, org_len,
                                            edge->flags, edge2->flags);

      if (edge2->flags & TA_EDGE_DONE)
      {
        TA_LOG(("  ADJUST: edge %d (pos=%.2f) moved to %.2f\n",
                edge - edges, edge->pos / 64.0,
                (edge2->pos - cur_len) / 64.0));

        edge->pos = edge2->pos - cur_len;

        if (hints->recorder)
        {
          TA_Edge bound = NULL;


          if (edge > edges)
            bound = &edge[-1];

          hints->recorder(ta_adjust, hints, dim,
                          edge, edge2, NULL, bound, NULL);
        }
      }

      else if (cur_len < 96)
      {
        FT_Pos u_off, d_off;


        cur_pos1 = TA_PIX_ROUND(org_center);

        if (cur_len <= 64)
        {
          u_off = 32;
          d_off = 32;
        }
        else
        {
          u_off = 38;
          d_off = 26;
        }

        delta1 = org_center - (cur_pos1 - u_off);
        if (delta1 < 0)
          delta1 = -delta1;

        delta2 = org_center - (cur_pos1 + d_off);
        if (delta2 < 0)
          delta2 = -delta2;

        if (delta1 < delta2)
          cur_pos1 -= u_off;
        else
          cur_pos1 += d_off;

        edge->pos = cur_pos1 - cur_len / 2;
        edge2->pos = cur_pos1 + cur_len / 2;

        TA_LOG(("  STEM: edge %d (opos=%.2f) linked to %d (opos=%.2f)"
                  " snapped to %.2f and %.2f\n",
                edge - edges, edge->opos / 64.0,
                edge2 - edges, edge2->opos / 64.0,
                edge->pos / 64.0, edge2->pos / 64.0));

        if (hints->recorder)
        {
          TA_Edge bound = NULL;


          if (edge > edges)
            bound = &edge[-1];

          hints->recorder(ta_stem, hints, dim,
                          edge, edge2, NULL, bound, NULL);
        }
      }

      else
      {
        org_pos = anchor->pos + (edge->opos - anchor->opos);
        org_len = edge2->opos - edge->opos;
        org_center = org_pos + (org_len >> 1);

        cur_len = ta_latin_compute_stem_width(hints, dim, org_len,
                                              edge->flags, edge2->flags);

        cur_pos1 = TA_PIX_ROUND(org_pos);
        delta1 = cur_pos1 + (cur_len >> 1) - org_center;
        if (delta1 < 0)
          delta1 = -delta1;

        cur_pos2 = TA_PIX_ROUND(org_pos + org_len) - cur_len;
        delta2 = cur_pos2 + (cur_len >> 1) - org_center;
        if (delta2 < 0)
          delta2 = -delta2;

        edge->pos = (delta1 < delta2) ? cur_pos1 : cur_pos2;
        edge2->pos = edge->pos + cur_len;

        TA_LOG(("  STEM: edge %d (opos=%.2f) linked to %d (opos=%.2f)"
                  " snapped to %.2f and %.2f\n",
                edge - edges, edge->opos / 64.0,
                edge2 - edges, edge2->opos / 64.0,
                edge->pos / 64.0, edge2->pos / 64.0));

        if (hints->recorder)
        {
          TA_Edge bound = NULL;


          if (edge > edges)
            bound = &edge[-1];

          hints->recorder(ta_stem, hints, dim,
                          edge, edge2, NULL, bound, NULL);
        }
      }

#ifdef TA_DEBUG
      num_actions++;
#endif

      edge->flags |= TA_EDGE_DONE;
      edge2->flags |= TA_EDGE_DONE;

      if (edge > edges
          && edge->pos < edge[-1].pos)
      {
#ifdef TA_DEBUG
        TA_LOG(("  BOUND: edge %d (pos=%.2f) moved to %.2f\n",
                edge - edges, edge->pos / 64.0, edge[-1].pos / 64.0));

        num_actions++;
#endif

        edge->pos = edge[-1].pos;

        if (hints->recorder)
          hints->recorder(ta_bound, hints, dim,
                          edge, &edge[-1], NULL, NULL, NULL);
      }
    }
  }

  /* make sure that lowercase m's maintain their symmetry */

  /* In general, lowercase m's have six vertical edges if they are sans */
  /* serif, or twelve if they are with serifs.  This implementation is  */
  /* based on that assumption, and seems to work very well with most    */
  /* faces.  However, if for a certain face this assumption is not      */
  /* true, the m is just rendered like before.  In addition, any stem   */
  /* correction will only be applied to symmetrical glyphs (even if the */
  /* glyph is not an m), so the potential for unwanted distortion is    */
  /* relatively low.                                                    */

  /* we don't handle horizontal edges since we can't easily assure that */
  /* the third (lowest) stem aligns with the base line; it might end up */
  /* one pixel higher or lower */

  n_edges = edge_limit - edges;
  if (dim == TA_DIMENSION_HORZ
      && (n_edges == 6 || n_edges == 12))
  {
    TA_Edge edge1, edge2, edge3;
    FT_Pos dist1, dist2, span, delta;


    if (n_edges == 6)
    {
      edge1 = edges;
      edge2 = edges + 2;
      edge3 = edges + 4;
    }
    else
    {
      edge1 = edges + 1;
      edge2 = edges + 5;
      edge3 = edges + 9;
    }

    dist1 = edge2->opos - edge1->opos;
    dist2 = edge3->opos - edge2->opos;

    span = dist1 - dist2;
    if (span < 0)
      span = -span;

    if (span < 8)
    {
      delta = edge3->pos - (2 * edge2->pos - edge1->pos);
      edge3->pos -= delta;
      if (edge3->link)
        edge3->link->pos -= delta;

      /* move the serifs along with the stem */
      if (n_edges == 12)
      {
        (edges + 8)->pos -= delta;
        (edges + 11)->pos -= delta;
      }

      edge3->flags |= TA_EDGE_DONE;
      if (edge3->link)
        edge3->link->flags |= TA_EDGE_DONE;
    }
  }

  if (has_serifs || !anchor)
  {
    /* now hint the remaining edges (serifs and single) */
    /* in order to complete our processing */
    for (edge = edges; edge < edge_limit; edge++)
    {
      TA_Edge lower_bound = NULL;
      TA_Edge upper_bound = NULL;

      FT_Pos delta;


      if (edge->flags & TA_EDGE_DONE)
        continue;

      delta = 1000;

      if (edge->serif)
      {
        delta = edge->serif->opos - edge->opos;
        if (delta < 0)
          delta = -delta;
      }

      if (edge > edges)
        lower_bound = &edge[-1];

      if (edge + 1 < edge_limit
          && edge[1].flags & TA_EDGE_DONE)
        upper_bound = &edge[1];


      if (delta < 64 + 16)
      {
        ta_latin_align_serif_edge(hints, edge->serif, edge);

        TA_LOG(("  SERIF: edge %d (opos=%.2f) serif to %d (opos=%.2f)"
                  " aligned to %.2f\n",
                edge - edges, edge->opos / 64.0,
                edge->serif - edges, edge->serif->opos / 64.0,
                edge->pos / 64.0));

        if (hints->recorder)
          hints->recorder(ta_serif, hints, dim,
                          edge, NULL, NULL, lower_bound, upper_bound);
      }
      else if (!anchor)
      {
        edge->pos = TA_PIX_ROUND(edge->opos);
        anchor = edge;

        TA_LOG(("  SERIF_ANCHOR: edge %d (opos=%.2f) snapped to %.2f\n",
                edge - edges, edge->opos / 64.0, edge->pos / 64.0));

        if (hints->recorder)
          hints->recorder(ta_serif_anchor, hints, dim,
                          edge, NULL, NULL, lower_bound, upper_bound);
      }
      else
      {
        TA_Edge before, after;


        for (before = edge - 1; before >= edges; before--)
          if (before->flags & TA_EDGE_DONE)
            break;

        for (after = edge + 1; after < edge_limit; after++)
          if (after->flags & TA_EDGE_DONE)
            break;

        if (before >= edges && before < edge
            && after < edge_limit && after > edge)
        {
          if (after->opos == before->opos)
            edge->pos = before->pos;
          else
            edge->pos = before->pos + FT_MulDiv(edge->opos - before->opos,
                                                after->pos - before->pos,
                                                after->opos - before->opos);

          TA_LOG(("  SERIF_LINK1: edge %d (opos=%.2f) snapped to %.2f"
                    " from %d (opos=%.2f)\n",
                  edge - edges, edge->opos / 64.0,
                  edge->pos / 64.0,
                  before - edges, before->opos / 64.0));

          if (hints->recorder)
            hints->recorder(ta_serif_link1, hints, dim,
                            edge, before, after, lower_bound, upper_bound);
        }
        else
        {
          edge->pos = anchor->pos + ((edge->opos - anchor->opos + 16) & ~31);
          TA_LOG(("  SERIF_LINK2: edge %d (opos=%.2f) snapped to %.2f\n",
                  edge - edges, edge->opos / 64.0, edge->pos / 64.0));

          if (hints->recorder)
            hints->recorder(ta_serif_link2, hints, dim,
                            edge, NULL, NULL, lower_bound, upper_bound);
        }
      }

#ifdef TA_DEBUG
      num_actions++;
#endif
      edge->flags |= TA_EDGE_DONE;

      if (edge > edges
          && edge->pos < edge[-1].pos)
      {
#ifdef TA_DEBUG
        TA_LOG(("  BOUND: edge %d (pos=%.2f) moved to %.2f\n",
                edge - edges, edge->pos / 64.0, edge[-1].pos / 64.0));
        num_actions++;
#endif

        edge->pos = edge[-1].pos;

        if (hints->recorder)
          hints->recorder(ta_bound, hints, dim,
                          edge, &edge[-1], NULL, NULL, NULL);
      }

      if (edge + 1 < edge_limit
          && edge[1].flags & TA_EDGE_DONE
          && edge->pos > edge[1].pos)
      {
#ifdef TA_DEBUG
        TA_LOG(("  BOUND: edge %d (pos=%.2f) moved to %.2f\n",
                edge - edges, edge->pos / 64.0, edge[1].pos / 64.0));

        num_actions++;
#endif

        edge->pos = edge[1].pos;

        if (hints->recorder)
          hints->recorder(ta_bound, hints, dim,
                          edge, &edge[1], NULL, NULL, NULL);
      }
    }
  }

#ifdef TA_DEBUG
  if (!num_actions)
    TA_LOG(("  (none)\n"));
  TA_LOG(("\n"));
#endif
}


/* apply the complete hinting algorithm to a latin glyph */

static FT_Error
ta_latin_hints_apply(TA_GlyphHints hints,
                     FT_Outline* outline,
                     TA_LatinMetrics metrics)
{
  FT_Error error;
  int dim;


  error = ta_glyph_hints_reload(hints, outline);
  if (error)
    goto Exit;

  /* analyze glyph outline */
#ifdef TA_CONFIG_OPTION_USE_WARPER
  if (metrics->root.scaler.render_mode == FT_RENDER_MODE_LIGHT
      || TA_HINTS_DO_HORIZONTAL(hints))
#else
  if (TA_HINTS_DO_HORIZONTAL(hints))
#endif
  {
    error = ta_latin_hints_detect_features(hints, TA_DIMENSION_HORZ);
    if (error)
      goto Exit;
  }

  if (TA_HINTS_DO_VERTICAL(hints))
  {
    error = ta_latin_hints_detect_features(hints, TA_DIMENSION_VERT);
    if (error)
      goto Exit;

    ta_latin_hints_compute_blue_edges(hints, metrics);
  }

  /* grid-fit the outline */
  for (dim = 0; dim < TA_DIMENSION_MAX; dim++)
  {
#ifdef TA_CONFIG_OPTION_USE_WARPER
    if (dim == TA_DIMENSION_HORZ
        && metrics->root.scaler.render_mode == FT_RENDER_MODE_LIGHT)
    {
      TA_WarperRec warper;
      FT_Fixed scale;
      FT_Pos delta;


      ta_warper_compute(&warper, hints, (TA_Dimension)dim, &scale, &delta);
      ta_glyph_hints_scale_dim(hints, (TA_Dimension)dim, scale, delta);

      continue;
    }
#endif

    if ((dim == TA_DIMENSION_HORZ && TA_HINTS_DO_HORIZONTAL(hints))
        || (dim == TA_DIMENSION_VERT && TA_HINTS_DO_VERTICAL(hints)))
    {
      ta_latin_hint_edges(hints, (TA_Dimension)dim);
      ta_glyph_hints_align_edge_points(hints, (TA_Dimension)dim);
      ta_glyph_hints_align_strong_points(hints, (TA_Dimension)dim);
      ta_glyph_hints_align_weak_points(hints, (TA_Dimension)dim);
    }
  }
  ta_glyph_hints_save(hints, outline);

Exit:
  return error;
}


/* XXX: this should probably fine tuned to differentiate better between */
/* scripts... */

static const TA_Script_UniRangeRec ta_latin_uniranges[] =
{
  TA_UNIRANGE_REC(0x0020UL, 0x007FUL), /* Basic Latin (no control chars) */
  TA_UNIRANGE_REC(0x00A0UL, 0x00FFUL), /* Latin-1 Supplement (no control chars) */
  TA_UNIRANGE_REC(0x0100UL, 0x017FUL), /* Latin Extended-A */
  TA_UNIRANGE_REC(0x0180UL, 0x024FUL), /* Latin Extended-B */
  TA_UNIRANGE_REC(0x0250UL, 0x02AFUL), /* IPA Extensions */
  TA_UNIRANGE_REC(0x02B0UL, 0x02FFUL), /* Spacing Modifier Letters */
  TA_UNIRANGE_REC(0x0300UL, 0x036FUL), /* Combining Diacritical Marks */
  TA_UNIRANGE_REC(0x0370UL, 0x03FFUL), /* Greek and Coptic */
  TA_UNIRANGE_REC(0x0400UL, 0x04FFUL), /* Cyrillic */
  TA_UNIRANGE_REC(0x0500UL, 0x052FUL), /* Cyrillic Supplement */
  TA_UNIRANGE_REC(0x1D00UL, 0x1D7FUL), /* Phonetic Extensions */
  TA_UNIRANGE_REC(0x1D80UL, 0x1DBFUL), /* Phonetic Extensions Supplement */
  TA_UNIRANGE_REC(0x1DC0UL, 0x1DFFUL), /* Combining Diacritical Marks Supplement */
  TA_UNIRANGE_REC(0x1E00UL, 0x1EFFUL), /* Latin Extended Additional */
  TA_UNIRANGE_REC(0x1F00UL, 0x1FFFUL), /* Greek Extended */
  TA_UNIRANGE_REC(0x2000UL, 0x206FUL), /* General Punctuation */
  TA_UNIRANGE_REC(0x2070UL, 0x209FUL), /* Superscripts and Subscripts */
  TA_UNIRANGE_REC(0x20A0UL, 0x20CFUL), /* Currency Symbols */
  TA_UNIRANGE_REC(0x2150UL, 0x218FUL), /* Number Forms */
  TA_UNIRANGE_REC(0x2460UL, 0x24FFUL), /* Enclosed Alphanumerics */
  TA_UNIRANGE_REC(0x2C60UL, 0x2C7FUL), /* Latin Extended-C */
  TA_UNIRANGE_REC(0x2DE0UL, 0x2DFFUL), /* Cyrillic Extended-A */
  TA_UNIRANGE_REC(0x2E00UL, 0x2E7FUL), /* Supplemental Punctuation */
  TA_UNIRANGE_REC(0xA640UL, 0xA69FUL), /* Cyrillic Extended-B */
  TA_UNIRANGE_REC(0xA720UL, 0xA7FFUL), /* Latin Extended-D */
  TA_UNIRANGE_REC(0xFB00UL, 0xFB06UL), /* Alphab. Present. Forms (Latin Ligs) */
  TA_UNIRANGE_REC(0x1D400UL, 0x1D7FFUL), /* Mathematical Alphanumeric Symbols */
  TA_UNIRANGE_REC(0x1F100UL, 0x1F1FFUL), /* Enclosed Alphanumeric Supplement */
  TA_UNIRANGE_REC(0UL, 0UL)
};


const TA_ScriptClassRec ta_latin_script_class =
{
  TA_SCRIPT_LATIN,
  ta_latin_uniranges,
  'o',

  sizeof (TA_LatinMetricsRec),

  (TA_Script_InitMetricsFunc)ta_latin_metrics_init,
  (TA_Script_ScaleMetricsFunc)ta_latin_metrics_scale,
  (TA_Script_DoneMetricsFunc)NULL,

  (TA_Script_InitHintsFunc)ta_latin_hints_init,
  (TA_Script_ApplyHintsFunc)ta_latin_hints_apply
};

/* end of talatin.c */
