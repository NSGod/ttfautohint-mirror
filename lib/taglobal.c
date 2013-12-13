/* taglobal.c */

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


/* originally file `afglobal.c' (2011-Mar-28) from FreeType */

/* heavily modified 2011 by Werner Lemberg <wl@gnu.org> */

#include <stdlib.h>

#include "taglobal.h"

/* get writing system specific header files */
#undef WRITING_SYSTEM
#define WRITING_SYSTEM(ws, WS) /* empty */
#include "tawrtsys.h"


#undef WRITING_SYSTEM
#define WRITING_SYSTEM(ws, WS) \
          &ta_ ## ws ## _writing_system_class,

TA_WritingSystemClass const ta_writing_system_classes[] =
{

#include "tawrtsys.h"

  NULL /* do not remove */
};


#undef SCRIPT
#define SCRIPT(s, S, d) \
          &ta_ ## s ## _script_class,

TA_ScriptClass const ta_script_classes[] =
{

#include <ttfautohint-scripts.h>

  NULL  /* do not remove */
};


#ifdef TA_DEBUG

#undef SCRIPT
#define SCRIPT(s, S, d) #s,

const char* ta_script_names[] =
{

#include <ttfautohint-scripts.h>

};

#endif /* TA_DEBUG */


/* Compute the script index of each glyph within a given face. */

static FT_Error
ta_face_globals_compute_script_coverage(TA_FaceGlobals globals)
{
  FT_Error error;
  FT_Face face = globals->face;
  FT_CharMap old_charmap = face->charmap;
  FT_Byte* gscripts = globals->glyph_scripts;
  FT_UInt ss;
  FT_UInt i;


  /* the value TA_SCRIPT_UNASSIGNED means `uncovered glyph' */
  memset(globals->glyph_scripts, TA_SCRIPT_UNASSIGNED, globals->glyph_count);

  error = FT_Select_Charmap(face, FT_ENCODING_UNICODE);
  if (error)
  {
    /* ignore this error; we simply use the fallback script */
    /* XXX: Shouldn't we rather disable hinting? */
    error = FT_Err_Ok;
    goto Exit;
  }

  /* scan each script in a Unicode charmap */
  for (ss = 0; ta_script_classes[ss]; ss++)
  {
    TA_ScriptClass script_class = ta_script_classes[ss];
    TA_Script_UniRange range;


    if (script_class->script_uni_ranges == NULL)
      continue;

    /* scan all Unicode points in the range and */
    /* set the corresponding glyph script index */
    for (range = script_class->script_uni_ranges; range->first != 0; range++)
    {
      FT_ULong charcode = range->first;
      FT_UInt gindex;


      gindex = FT_Get_Char_Index(face, charcode);

      if (gindex != 0
          && gindex < (FT_ULong)globals->glyph_count
          && gscripts[gindex] == TA_SCRIPT_UNASSIGNED)
        gscripts[gindex] = (FT_Byte)ss;

      for (;;)
      {
        charcode = FT_Get_Next_Char(face, charcode, &gindex);

        if (gindex == 0 || charcode > range->last)
          break;

        if (gindex < (FT_ULong)globals->glyph_count
            && gscripts[gindex] == TA_SCRIPT_UNASSIGNED)
          gscripts[gindex] = (FT_Byte)ss;
      }
    }
  }

  /* mark ASCII digits */
  for (i = 0x30; i <= 0x39; i++)
  {
    FT_UInt gindex = FT_Get_Char_Index(face, i);


    if (gindex != 0
        && gindex < (FT_ULong)globals->glyph_count)
      gscripts[gindex] |= TA_DIGIT;
  }

Exit:
  /* by default, all uncovered glyphs are set to the fallback script */
  /* XXX: Shouldn't we disable hinting or do something similar? */
  if (globals->font->fallback_script != TA_SCRIPT_UNASSIGNED)
  {
    FT_Long nn;


    for (nn = 0; nn < globals->glyph_count; nn++)
    {
      if ((gscripts[nn] & ~TA_DIGIT) == TA_SCRIPT_UNASSIGNED)
      {
        gscripts[nn] &= ~TA_SCRIPT_UNASSIGNED;
        gscripts[nn] |= globals->font->fallback_script;
      }
    }
  }

  FT_Set_Charmap(face, old_charmap);
  return error;
}


FT_Error
ta_face_globals_new(FT_Face face,
                    TA_FaceGlobals *aglobals,
                    FONT* font)
{
  FT_Error error;
  TA_FaceGlobals globals;


  globals = (TA_FaceGlobals)calloc(1, sizeof (TA_FaceGlobalsRec) +
                                      face->num_glyphs * sizeof (FT_Byte));
  if (!globals)
  {
    error = FT_Err_Out_Of_Memory;
    goto Err;
  }

  globals->face = face;
  globals->glyph_count = face->num_glyphs;
  globals->glyph_scripts = (FT_Byte*)(globals + 1);
  globals->font = font;

  error = ta_face_globals_compute_script_coverage(globals);
  if (error)
  {
    ta_face_globals_free(globals);
    globals = NULL;
  }

  globals->increase_x_height = TA_PROP_INCREASE_X_HEIGHT_MAX;

Err:
  *aglobals = globals;
  return error;
}


void
ta_face_globals_free(TA_FaceGlobals globals)
{
  if (globals)
  {
    FT_UInt nn;


    for (nn = 0; nn < TA_SCRIPT_MAX; nn++)
    {
      if (globals->metrics[nn])
      {
        TA_ScriptClass script_class =
          ta_script_classes[nn];
        TA_WritingSystemClass writing_system_class =
          ta_writing_system_classes[script_class->writing_system];


        if (writing_system_class->script_metrics_done)
          writing_system_class->script_metrics_done(globals->metrics[nn]);

        free(globals->metrics[nn]);
        globals->metrics[nn] = NULL;
      }
    }

    globals->glyph_count = 0;
    globals->glyph_scripts = NULL; /* no need to free this one! */
    globals->face = NULL;

    free(globals);
    globals = NULL;
  }
}


FT_Error
ta_face_globals_get_metrics(TA_FaceGlobals globals,
                            FT_UInt gindex,
                            FT_UInt options,
                            TA_ScriptMetrics *ametrics)
{
  TA_ScriptMetrics metrics = NULL;
  TA_Script script = (TA_Script)(options & 15);
  TA_ScriptClass script_class;
  TA_WritingSystemClass writing_system_class;
  FT_Error error = FT_Err_Ok;


  if (gindex >= (FT_ULong)globals->glyph_count)
  {
    error = FT_Err_Invalid_Argument;
    goto Exit;
  }

  /* if we have a forced script (via `options'), use it, */
  /* otherwise look into `glyph_scripts' array */
  if (script == TA_SCRIPT_NONE || script + 1 >= TA_SCRIPT_MAX)
    script = (TA_Script)(globals->glyph_scripts[gindex]
                         & TA_SCRIPT_UNASSIGNED);

  script_class =
    ta_script_classes[script];
  writing_system_class =
    ta_writing_system_classes[script_class->writing_system];

  metrics = globals->metrics[script];
  if (metrics == NULL)
  {
    /* create the global metrics object if necessary */
    metrics = (TA_ScriptMetrics)
                calloc(1, writing_system_class->script_metrics_size);
    if (!metrics)
    {
      error = FT_Err_Out_Of_Memory;
      goto Exit;
    }

    metrics->script_class = script_class;
    metrics->globals = globals;

    if (writing_system_class->script_metrics_init)
    {
      error = writing_system_class->script_metrics_init(metrics,
                                                        globals->face);
      if (error)
      {
        if (writing_system_class->script_metrics_done)
          writing_system_class->script_metrics_done(metrics);

        free(metrics);
        metrics = NULL;
        goto Exit;
      }
    }

    globals->metrics[script] = metrics;
  }

Exit:
  *ametrics = metrics;

  return error;
}


FT_Bool
ta_face_globals_is_digit(TA_FaceGlobals globals,
                         FT_UInt gindex)
{
  if (gindex < (FT_ULong)globals->glyph_count)
    return (FT_Bool)(globals->glyph_scripts[gindex] & TA_DIGIT);

  return (FT_Bool)0;
}

/* end of taglobal.c */
