/* talatin.h */

/*
 * Copyright (C) 2011-2014 by Werner Lemberg.
 *
 * This file is part of the ttfautohint library, and may only be used,
 * modified, and distributed under the terms given in `COPYING'.  By
 * continuing to use, modify, or distribute this file you indicate that you
 * have read `COPYING' and understand and accept it fully.
 *
 * The file `COPYING' mentioned in the previous paragraph is distributed
 * with the ttfautohint library.
 */


/* originally file `aflatin.h' (2011-Mar-28) from FreeType */

/* heavily modified 2011 by Werner Lemberg <wl@gnu.org> */

#ifndef __TALATIN_H__
#define __TALATIN_H__

#include "tatypes.h"
#include "tahints.h"


/* the `latin' writing system */

extern const TA_WritingSystemClassRec ta_latin_writing_system_class;

/* the latin-specific script classes */

extern const TA_ScriptClassRec ta_cyrl_script_class;
extern const TA_ScriptClassRec ta_grek_script_class;
extern const TA_ScriptClassRec ta_hebr_script_class;
extern const TA_ScriptClassRec ta_latn_script_class;
#if 0
extern const TA_ScriptClassRec ta_armn_script_class;
#endif


/* constants are given with units_per_em == 2048 in mind */
#define TA_LATIN_CONSTANT(metrics, c) \
          (((c) * (FT_Long)((TA_LatinMetrics)(metrics))->units_per_em) / 2048)


/* Latin (global) metrics management */

#define TA_LATIN_IS_TOP_BLUE(b) \
          ((b)->properties & TA_BLUE_PROPERTY_LATIN_TOP)
#define TA_LATIN_IS_X_HEIGHT_BLUE(b) \
          ((b)->properties & TA_BLUE_PROPERTY_LATIN_X_HEIGHT)
#define TA_LATIN_IS_LONG_BLUE(b) \
          ((b)->properties &TA_BLUE_PROPERTY_LATIN_LONG)

#define TA_LATIN_MAX_WIDTHS 16


#define TA_LATIN_BLUE_ACTIVE  (1 << 0) /* set if zone height is <= 3/4px */
#define TA_LATIN_BLUE_TOP (1 << 1) /* result of TA_LATIN_IS_TOP_BLUE */
#define TA_LATIN_BLUE_ADJUSTMENT (1 << 2) /* used for scale adjustment */
                                          /* optimization */


typedef struct TA_LatinBlueRec_
{
  TA_WidthRec ref;
  TA_WidthRec shoot;
  FT_UInt flags;
} TA_LatinBlueRec, *TA_LatinBlue;


typedef struct TA_LatinAxisRec_
{
  FT_Fixed scale;
  FT_Pos delta;

  FT_UInt width_count; /* number of used widths */
  TA_WidthRec widths[TA_LATIN_MAX_WIDTHS]; /* widths array */
  FT_Pos edge_distance_threshold; /* used for creating edges */
  FT_Pos standard_width; /* the default stem thickness */
  FT_Bool extra_light; /* is standard width very light? */

  /* ignored for horizontal metrics */
  FT_UInt blue_count;
  /* we add two blue zones for usWinAscent and usWinDescent */
  TA_LatinBlueRec blues[TA_BLUE_STRINGSET_MAX + 2];

  FT_Fixed org_scale;
  FT_Pos org_delta;
} TA_LatinAxisRec, *TA_LatinAxis;


typedef struct TA_LatinMetricsRec_
{
  TA_StyleMetricsRec root;
  FT_UInt units_per_em;
  TA_LatinAxisRec axis[TA_DIMENSION_MAX];
} TA_LatinMetricsRec, *TA_LatinMetrics;


FT_Error
ta_latin_metrics_init(TA_LatinMetrics metrics,
                      FT_Face face);

void
ta_latin_metrics_scale(TA_LatinMetrics metrics,
                       TA_Scaler scaler);

void
ta_latin_metrics_init_widths(TA_LatinMetrics metrics,
                             FT_Face face);

void
ta_latin_metrics_check_digits(TA_LatinMetrics metrics,
                              FT_Face face);


#define TA_LATIN_HINTS_HORZ_SNAP (1 << 0) /* enable stem width snapping */
#define TA_LATIN_HINTS_VERT_SNAP (1 << 1) /* enable stem height snapping */
#define TA_LATIN_HINTS_STEM_ADJUST (1 << 2) /* enable stem width/height */
                                            /* adjustment */
#define TA_LATIN_HINTS_MONO (1 << 3) /* indicate monochrome rendering */


#define TA_LATIN_HINTS_DO_HORZ_SNAP(h) \
          TA_HINTS_TEST_OTHER(h, TA_LATIN_HINTS_HORZ_SNAP)
#define TA_LATIN_HINTS_DO_VERT_SNAP(h) \
          TA_HINTS_TEST_OTHER(h, TA_LATIN_HINTS_VERT_SNAP)
#define TA_LATIN_HINTS_DO_STEM_ADJUST(h) \
          TA_HINTS_TEST_OTHER(h, TA_LATIN_HINTS_STEM_ADJUST)
#define TA_LATIN_HINTS_DO_MONO(h) \
          TA_HINTS_TEST_OTHER(h, TA_LATIN_HINTS_MONO)


/* the next functions shouldn't normally be exported; */
/* however, other scripts might like to use these functions as-is */

FT_Error
ta_latin_hints_compute_segments(TA_GlyphHints hints,
                                TA_Dimension dim);
void
ta_latin_hints_link_segments(TA_GlyphHints hints,
                             TA_Dimension dim);
FT_Error
ta_latin_hints_compute_edges(TA_GlyphHints hints,
                             TA_Dimension dim);
FT_Error
ta_latin_hints_detect_features(TA_GlyphHints hints,
                               TA_Dimension dim);

#endif /* __TALATIN_H__ */

/* end of talatin.h */
