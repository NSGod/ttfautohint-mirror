/* ttfautohint.c */

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


/* This file needs FreeType 2.4.5 or newer. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "ta.h"


#define COMPARE(str) \
          (len == (sizeof (str) - 1) \
           && !strncmp(start, str, sizeof (str) - 1))
#define DUMPVAL(str, arg) \
          fprintf(stderr, "%33s = %ld\n", \
                          (str), \
                          (FT_Long)(arg))
#define DUMPSTR(str, arg) \
          fprintf(stderr, "%33s = %s\n", \
                          (str), \
                          (arg))


void
TA_sfnt_set_properties(SFNT* sfnt,
                       FONT* font)
{
  TA_FaceGlobals  globals = (TA_FaceGlobals)sfnt->face->autohint.data;


  globals->increase_x_height = font->increase_x_height;
}


TA_Error
TTF_autohint(const char* options,
             ...)
{
  va_list ap;

  FONT* font;
  FT_Error error;
  FT_Long i;

  FILE* in_file = NULL;
  FILE* out_file = NULL;

  const char* in_buf = NULL;
  size_t in_len = 0;
  char** out_bufp = NULL;
  size_t* out_lenp = NULL;

  const unsigned char** error_stringp = NULL;

  FT_Long hinting_range_min = -1;
  FT_Long hinting_range_max = -1;
  FT_Long hinting_limit = -1;
  FT_Long increase_x_height = -1;

  const char* x_height_snapping_exceptions_string = NULL;
  number_range* x_height_snapping_exceptions = NULL;

  FT_Bool gray_strong_stem_width = 0;
  FT_Bool gdi_cleartype_strong_stem_width = 1;
  FT_Bool dw_cleartype_strong_stem_width = 0;

  TA_Progress_Func progress;
  void* progress_data;
  TA_Info_Func info;
  void* info_data;

  FT_Bool windows_compatibility = 0;
  FT_Bool ignore_restrictions = 0;
  FT_Bool pre_hinting = 0;
  FT_Bool hint_with_components = 0;
  FT_UInt fallback_script = TA_SCRIPT_FALLBACK;
  FT_Bool symbol = 0;

  FT_Bool dehint = 0;

  FT_Bool debug = 0;

  const char* op;


  if (!options || !*options)
  {
    error = FT_Err_Invalid_Argument;
    goto Err1;
  }

  /* XXX */
  va_start(ap, options);

  op = options;

  for(;;)
  {
    const char* start;
    size_t len;


    start = op;

    /* search comma */
    while (*op && *op != ',')
      op++;

    /* remove leading whitespace */
    while (isspace(*start))
      start++;

    /* check for empty option */
    if (start == op)
      goto End;

    len = op - start;

    /* the `COMPARE' macro uses `len' and `start' */

    /* handle options -- don't forget to update parameter dump below! */
    if (COMPARE("debug"))
      debug = (FT_Bool)va_arg(ap, FT_Int);
    else if (COMPARE("dehint"))
      dehint = (FT_Bool)va_arg(ap, FT_Int);
    else if (COMPARE("dw-cleartype-strong-stem-width"))
      dw_cleartype_strong_stem_width = (FT_Bool)va_arg(ap, FT_Int);
    else if (COMPARE("error-string"))
      error_stringp = va_arg(ap, const unsigned char**);
    else if (COMPARE("fallback-script"))
      fallback_script = va_arg(ap, FT_UInt);
    else if (COMPARE("gdi-cleartype-strong-stem-width"))
      gdi_cleartype_strong_stem_width = (FT_Bool)va_arg(ap, FT_Int);
    else if (COMPARE("gray-strong-stem-width"))
      gray_strong_stem_width = (FT_Bool)va_arg(ap, FT_Int);
    else if (COMPARE("hinting-limit"))
      hinting_limit = (FT_Long)va_arg(ap, FT_UInt);
    else if (COMPARE("hinting-range-max"))
      hinting_range_max = (FT_Long)va_arg(ap, FT_UInt);
    else if (COMPARE("hinting-range-min"))
      hinting_range_min = (FT_Long)va_arg(ap, FT_UInt);
    else if (COMPARE("hint-with-components"))
      hint_with_components = (FT_Bool)va_arg(ap, FT_Int);
    else if (COMPARE("ignore-restrictions"))
      ignore_restrictions = (FT_Bool)va_arg(ap, FT_Int);
    else if (COMPARE("in-buffer"))
    {
      in_file = NULL;
      in_buf = va_arg(ap, const char*);
    }
    else if (COMPARE("in-buffer-len"))
    {
      in_file = NULL;
      in_len = va_arg(ap, size_t);
    }
    else if (COMPARE("in-file"))
    {
      in_file = va_arg(ap, FILE*);
      in_buf = NULL;
      in_len = 0;
    }
    else if (COMPARE("increase-x-height"))
      increase_x_height = (FT_Long)va_arg(ap, FT_UInt);
    else if (COMPARE("info-callback"))
      info = va_arg(ap, TA_Info_Func);
    else if (COMPARE("info-callback-data"))
      info_data = va_arg(ap, void*);
    else if (COMPARE("out-buffer"))
    {
      out_file = NULL;
      out_bufp = va_arg(ap, char**);
    }
    else if (COMPARE("out-buffer-len"))
    {
      out_file = NULL;
      out_lenp = va_arg(ap, size_t*);
    }
    else if (COMPARE("out-file"))
    {
      out_file = va_arg(ap, FILE*);
      out_bufp = NULL;
      out_lenp = NULL;
    }
    else if (COMPARE("pre-hinting"))
      pre_hinting = (FT_Bool)va_arg(ap, FT_Int);
    else if (COMPARE("progress-callback"))
      progress = va_arg(ap, TA_Progress_Func);
    else if (COMPARE("progress-callback-data"))
      progress_data = va_arg(ap, void*);
    else if (COMPARE("symbol"))
      symbol = (FT_Bool)va_arg(ap, FT_Int);
    else if (COMPARE("windows-compatibility"))
      windows_compatibility = (FT_Bool)va_arg(ap, FT_Int);
    else if (COMPARE("x-height-snapping-exceptions"))
      x_height_snapping_exceptions_string = va_arg(ap, const char*);
    else
    {
      error = TA_Err_Unknown_Argument;
      goto Err1;
    }

  End:
    if (!*op)
      break;
    op++;
  }

  va_end(ap);

  /* check options */

  if (!(in_file
        || (in_buf && in_len)))
  {
    error = FT_Err_Invalid_Argument;
    goto Err1;
  }

  if (!(out_file
        || (out_bufp && out_lenp)))
  {
    error = FT_Err_Invalid_Argument;
    goto Err1;
  }

  font = (FONT*)calloc(1, sizeof (FONT));
  if (!font)
  {
    error = FT_Err_Out_Of_Memory;
    goto Err1;
  }

  if (dehint)
    goto No_check;

  if (hinting_range_min >= 0 && hinting_range_min < 2)
  {
    error = FT_Err_Invalid_Argument;
    goto Err1;
  }
  if (hinting_range_min < 0)
    hinting_range_min = TA_HINTING_RANGE_MIN;

  if (hinting_range_max >= 0 && hinting_range_max < hinting_range_min)
  {
    error = FT_Err_Invalid_Argument;
    goto Err1;
  }
  if (hinting_range_max < 0)
    hinting_range_max = TA_HINTING_RANGE_MAX;

  /* value 0 is valid */
  if (hinting_limit > 0 && hinting_limit < hinting_range_max)
  {
    error = FT_Err_Invalid_Argument;
    goto Err1;
  }
  if (hinting_limit < 0)
    hinting_limit = TA_HINTING_LIMIT;

  if (increase_x_height > 0
      && increase_x_height < TA_PROP_INCREASE_X_HEIGHT_MIN)
  {
    error = FT_Err_Invalid_Argument;
    goto Err1;
  }
  if (increase_x_height < 0)
    increase_x_height = TA_INCREASE_X_HEIGHT;

  if (x_height_snapping_exceptions_string)
  {
    const char* s = number_set_parse(x_height_snapping_exceptions_string,
                                     &x_height_snapping_exceptions,
                                     TA_PROP_INCREASE_X_HEIGHT_MIN,
                                     0x7FFF);
    if (*s)
    {
      error = FT_Err_Invalid_Argument;
      goto Err1;
    }
  }

  font->hinting_range_min = (FT_UInt)hinting_range_min;
  font->hinting_range_max = (FT_UInt)hinting_range_max;
  font->hinting_limit = (FT_UInt)hinting_limit;
  font->increase_x_height = increase_x_height;
  font->x_height_snapping_exceptions = x_height_snapping_exceptions;

  font->gray_strong_stem_width = gray_strong_stem_width;
  font->gdi_cleartype_strong_stem_width = gdi_cleartype_strong_stem_width;
  font->dw_cleartype_strong_stem_width = dw_cleartype_strong_stem_width;

  font->windows_compatibility = windows_compatibility;
  font->ignore_restrictions = ignore_restrictions;
  font->pre_hinting = pre_hinting;
  font->hint_with_components = hint_with_components;
  font->fallback_script = fallback_script;
  font->symbol = symbol;

No_check:
  font->progress = progress;
  font->progress_data = progress_data;
  font->info = info;
  font->info_data = info_data;

  font->debug = debug;
  font->dehint = dehint;

  font->gasp_idx = MISSING;

  /* dump parameters */
  if (debug)
  {
    char *s;


    fprintf(stderr, "TTF_autohint parameters\n"
                    "=======================\n"
                    "\n");

    if (dehint)
      DUMPVAL("dehint",
              font->dehint);
    else
    {
      DUMPVAL("dw-cleartype-strong-stem-width",
              font->dw_cleartype_strong_stem_width);
      DUMPVAL("fallback-script",
              font->fallback_script);
      DUMPVAL("gdi-cleartype-strong-stem-width",
              font->gdi_cleartype_strong_stem_width);
      DUMPVAL("gray-strong-stem-width",
              font->gray_strong_stem_width);
      DUMPVAL("hinting-limit",
              font->hinting_limit);
      DUMPVAL("hinting-range-max",
              font->hinting_range_max);
      DUMPVAL("hinting-range-min",
              font->hinting_range_min);
      DUMPVAL("hint-with-components",
              font->hint_with_components);
      DUMPVAL("ignore-restrictions",
              font->ignore_restrictions);
      DUMPVAL("increase-x-height",
              font->increase_x_height);
      DUMPVAL("pre-hinting",
              font->pre_hinting);
      DUMPVAL("symbol",
              font->symbol);
      DUMPVAL("windows-compatibility",
              font->windows_compatibility);

      s = number_set_show(font->x_height_snapping_exceptions,
                          TA_PROP_INCREASE_X_HEIGHT_MIN, 0x7FFF);
      DUMPSTR("x-height-snapping-exceptions", s);
      free(s);
    }

    fprintf(stderr, "\n");
  }

  /* now start with processing the data */

  if (in_file)
  {
    error = TA_font_file_read(font, in_file);
    if (error)
      goto Err;
  }
  else
  {
    /* a valid TTF can never be that small */
    if (in_len < 100)
    {
      error = TA_Err_Invalid_Font_Type;
      goto Err1;
    }
    font->in_buf = (FT_Byte*)in_buf;
    font->in_len = in_len;
  }

  error = TA_font_init(font);
  if (error)
    goto Err;

  if (font->debug)
    _ta_debug = 1;

  /* we do some loops over all subfonts */
  for (i = 0; i < font->num_sfnts; i++)
  {
    SFNT* sfnt = &font->sfnts[i];
    FT_UInt idx;


    error = FT_New_Memory_Face(font->lib, font->in_buf, font->in_len,
                               i, &sfnt->face);

    /* assure that the font hasn't been already processed by ttfautohint; */
    /* another, more thorough check is done in TA_glyph_parse_simple */
    idx = FT_Get_Name_Index(sfnt->face, TTFAUTOHINT_GLYPH);
    if (idx)
    {
      error = TA_Err_Already_Processed;
      goto Err;
    }

    if (error)
      goto Err;

    error = TA_sfnt_split_into_SFNT_tables(sfnt, font);
    if (error)
      goto Err;

    /* check permission */
    if (sfnt->OS2_idx != MISSING)
    {
      SFNT_Table* OS2_table = &font->tables[sfnt->OS2_idx];


      /* check lower byte of the `fsType' field */
      if (OS2_table->buf[OS2_FSTYPE_OFFSET + 1] == 0x02
          && !font->ignore_restrictions)
      {
        error = TA_Err_Missing_Legal_Permission;
        goto Err;
      }
    }

    if (font->dehint)
    {
      error = TA_sfnt_split_glyf_table(sfnt, font);
      if (error)
        goto Err;
    }
    else
    {
      if (font->pre_hinting)
        error = TA_sfnt_create_glyf_data(sfnt, font);
      else
        error = TA_sfnt_split_glyf_table(sfnt, font);
      if (error)
        goto Err;

      /* this call creates a `globals' object... */
      error = TA_sfnt_handle_coverage(sfnt, font);
      if (error)
        goto Err;

      /* ... so that we now can initialize its properties */
      TA_sfnt_set_properties(sfnt, font);
    }
  }

  if (!font->dehint)
  {
    for (i = 0; i < font->num_sfnts; i++)
    {
      SFNT* sfnt = &font->sfnts[i];


      TA_sfnt_adjust_master_coverage(sfnt, font);
    }
  }

#if 0
  /* this code is here for completeness -- */
  /* right now, `glyf' tables get hinted only once, */
  /* and referring subfonts simply reuse it, */
  /* but this might change in the future */

  if (!font->dehint)
  {
    for (i = 0; i < font->num_sfnts; i++)
    {
      SFNT* sfnt = &font->sfnts[i];


      TA_sfnt_copy_master_coverage(sfnt, font);
    }
  }
#endif

  /* loop again over subfonts */
  for (i = 0; i < font->num_sfnts; i++)
  {
    SFNT* sfnt = &font->sfnts[i];


    error = ta_loader_init(font);
    if (error)
      goto Err;

    error = TA_sfnt_build_gasp_table(sfnt, font);
    if (error)
      goto Err;
    if (!font->dehint)
    {
      error = TA_sfnt_build_cvt_table(sfnt, font);
      if (error)
        goto Err;
      error = TA_sfnt_build_fpgm_table(sfnt, font);
      if (error)
        goto Err;
      error = TA_sfnt_build_prep_table(sfnt, font);
      if (error)
        goto Err;
    }
    error = TA_sfnt_build_glyf_table(sfnt, font);
    if (error)
      goto Err;
    error = TA_sfnt_build_loca_table(sfnt, font);
    if (error)
      goto Err;

    if (font->loader)
      ta_loader_done(font);
  }

  for (i = 0; i < font->num_sfnts; i++)
  {
    SFNT* sfnt = &font->sfnts[i];


    error = TA_sfnt_update_maxp_table(sfnt, font);
    if (error)
      goto Err;

    if (!font->dehint)
    {
      /* we add one glyph for composites */
      if (sfnt->max_components
          && !font->pre_hinting
          && font->hint_with_components)
      {
        error = TA_sfnt_update_hmtx_table(sfnt, font);
        if (error)
          goto Err;
        error = TA_sfnt_update_post_table(sfnt, font);
        if (error)
          goto Err;
        error = TA_sfnt_update_GPOS_table(sfnt, font);
        if (error)
          goto Err;
      }
    }

    if (font->info)
    {
      /* add info about ttfautohint to the version string */
      error = TA_sfnt_update_name_table(sfnt, font);
      if (error)
        goto Err;
    }
  }

  if (font->num_sfnts == 1)
    error = TA_font_build_TTF(font);
  else
    error = TA_font_build_TTC(font);
  if (error)
    goto Err;

  if (out_file)
  {
    error = TA_font_file_write(font, out_file);
    if (error)
      goto Err;
  }
  else
  {
    *out_bufp = (char*)font->out_buf;
    *out_lenp = font->out_len;
  }

  error = TA_Err_Ok;

Err:
  TA_font_unload(font, in_buf, out_bufp);

Err1:
  if (error_stringp)
    *error_stringp = (const unsigned char*)TA_get_error_message(error);

  return error;
}

/* end of ttfautohint.c */
