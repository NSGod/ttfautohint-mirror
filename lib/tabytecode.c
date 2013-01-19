/* tabytecode.c */

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


#include "ta.h"
#include <string.h>


#undef MISSING
#define MISSING (FT_UShort)~0


#define DEBUGGING


#ifdef TA_DEBUG
int _ta_debug = 0;
int _ta_debug_disable_horz_hints;
int _ta_debug_disable_vert_hints;
int _ta_debug_disable_blue_hints;
void* _ta_debug_hints;
#endif


typedef struct Hints_Record_
{
  FT_UInt size;
  FT_UInt num_actions;
  FT_Byte* buf;
  FT_UInt buf_len;
} Hints_Record;

typedef struct Recorder_
{
  FONT* font;
  GLYPH* glyph; /* the current glyph */
  Hints_Record hints_record;

  /* some segments can `wrap around' */
  /* a contour's start point like 24-25-26-0-1-2 */
  /* (there can be at most one such segment per contour); */
  /* later on we append additional records */
  /* to split them into 24-26 and 0-2 */
  FT_UShort* wrap_around_segments;
  FT_UShort num_wrap_around_segments;

  FT_UShort num_stack_elements; /* the necessary stack depth so far */

  /* data necessary for strong point interpolation */
  FT_UShort* ip_before_points;
  FT_UShort* ip_after_points;
  FT_UShort* ip_on_point_array;
  FT_UShort* ip_between_point_array;

  FT_UShort num_strong_points;
  FT_UShort num_segments;
} Recorder;


/* this is the bytecode of the `.ttfautohint' glyph */

FT_Byte ttfautohint_glyph_bytecode[7] =
{

  /* increment `cvtl_is_subglyph' counter */
  PUSHB_3,
    cvtl_is_subglyph,
    1,
    cvtl_is_subglyph,
  RCVT,
  ADD,
  WCVTP,

};


/*
 * convert array `args' into a sequence of NPUSHB, NPUSHW, PUSHB_X, and
 * PUSHW_X instructions to be stored in `bufp' (the latter two instructions
 * only if `optimize' is not set); if `need_words' is set, NPUSHW and
 * PUSHW_X gets used
 */

FT_Byte*
TA_build_push(FT_Byte* bufp,
              FT_UInt* args,
              FT_UInt num_args,
              FT_Bool need_words,
              FT_Bool optimize)
{
  FT_UInt* arg = args;
  FT_UInt i, j, nargs;


  if (need_words)
  {
    for (i = 0; i < num_args; i += 255)
    {
      nargs = (num_args - i > 255) ? 255 : num_args - i;

      if (optimize && nargs <= 8)
        BCI(PUSHW_1 - 1 + nargs);
      else
      {
        BCI(NPUSHW);
        BCI(nargs);
      }
      for (j = 0; j < nargs; j++)
      {
        BCI(HIGH(*arg));
        BCI(LOW(*arg));
        arg++;
      }
    }
  }
  else
  {
    for (i = 0; i < num_args; i += 255)
    {
      nargs = (num_args - i > 255) ? 255 : num_args - i;

      if (optimize && nargs <= 8)
        BCI(PUSHB_1 - 1 + nargs);
      else
      {
        BCI(NPUSHB);
        BCI(nargs);
      }
      for (j = 0; j < nargs; j++)
      {
        BCI(*arg);
        arg++;
      }
    }
  }

  return bufp;
}


/* We add a subglyph for each composite glyph. */
/* Since subglyphs must contain at least one point, */
/* we have to adjust all point indices accordingly. */
/* Using the `pointsums' array of the `GLYPH' structure */
/* it is straightforward to do that: */
/* Assuming that point with index x is in the interval */
/* pointsums[n] <= x < pointsums[n + 1], */
/* the new point index is x + n. */

static FT_UInt
TA_adjust_point_index(Recorder* recorder,
                      FT_UInt idx)
{
  FONT* font = recorder->font;
  GLYPH* glyph = recorder->glyph;
  FT_UShort i;


  if (!glyph->num_components || !font->hint_with_components)
    return idx; /* not a composite glyph */

  for (i = 0; i < glyph->num_pointsums; i++)
    if (idx < glyph->pointsums[i])
      break;

  return idx + i;
}


/* we store the segments in the storage area; */
/* each segment record consists of the first and last point */

static FT_Byte*
TA_sfnt_build_glyph_segments(SFNT* sfnt,
                             Recorder* recorder,
                             FT_Byte* bufp,
                             FT_Bool optimize)
{
  FONT* font = recorder->font;
  TA_GlyphHints hints = &font->loader->hints;
  TA_AxisHints axis = &hints->axis[TA_DIMENSION_VERT];
  TA_Point points = hints->points;
  TA_Segment segments = axis->segments;
  TA_Segment seg;
  TA_Segment seg_limit;

  FT_Outline outline = font->loader->gloader->base.outline;

  FT_UInt* args;
  FT_UInt* arg;
  FT_UInt num_args;
  FT_UShort num_segments;

  FT_Bool need_words = 0;

  FT_Int n;
  FT_UInt base;
  FT_UShort num_packed_segments;
  FT_UShort num_storage;
  FT_UShort num_stack_elements;
  FT_UShort num_twilight_points;


  seg_limit = segments + axis->num_segments;
  num_segments = axis->num_segments;

  /* to pack the data in the bytecode more tightly, */
  /* we store up to the first nine segments in nibbles if possible, */
  /* using delta values */
  base = 0;
  num_packed_segments = 0;
  for (seg = segments; seg < seg_limit; seg++)
  {
    FT_UInt first = seg->first - points;
    FT_UInt last = seg->last - points;


    first = TA_adjust_point_index(recorder, first);
    last = TA_adjust_point_index(recorder, last);

    if (first - base >= 16)
      break;
    if (first > last || last - first >= 16)
      break;
    if (num_packed_segments == 9)
      break;
    num_packed_segments++;
    base = last;
  }

  /* also handle wrap-around segments */
  num_segments += recorder->num_wrap_around_segments;

  /* wrap-around segments are pushed with four arguments; */
  /* a segment stored in nibbles needs only one byte instead of two */
  num_args = num_packed_segments
             + 2 * (num_segments - num_packed_segments)
             + 2 * recorder->num_wrap_around_segments
             + 2;

  /* collect all arguments temporarily in an array (in reverse order) */
  /* so that we can easily split into chunks of 255 args */
  /* as needed by NPUSHB and NPUSHW, respectively */
  args = (FT_UInt*)malloc(num_args * sizeof (FT_UInt));
  if (!args)
    return NULL;

  arg = args + num_args - 1;

  if (num_segments > 0xFF)
    need_words = 1;

  /* the number of packed segments is indicated by the function number */
  if (recorder->glyph->num_components && font->hint_with_components)
    *(arg--) = bci_create_segments_composite_0 + num_packed_segments;
  else
    *(arg--) = bci_create_segments_0 + num_packed_segments;
  *(arg--) = num_segments;

  base = 0;
  for (seg = segments; seg < segments + num_packed_segments; seg++)
  {
    FT_UInt first = seg->first - points;
    FT_UInt last = seg->last - points;
    FT_UInt low_nibble;
    FT_UInt high_nibble;


    first = TA_adjust_point_index(recorder, first);
    last = TA_adjust_point_index(recorder, last);

    low_nibble = first - base;
    high_nibble = last - first;

    *(arg--) = 16 * high_nibble + low_nibble;

    base = last;
  }

  for (seg = segments + num_packed_segments; seg < seg_limit; seg++)
  {
    FT_UInt first = seg->first - points;
    FT_UInt last = seg->last - points;


    *(arg--) = TA_adjust_point_index(recorder, first);
    *(arg--) = TA_adjust_point_index(recorder, last);

    /* we push the last and first contour point */
    /* as a third and fourth argument in wrap-around segments */
    if (first > last)
    {
      for (n = 0; n < outline.n_contours; n++)
      {
        FT_UInt end = (FT_UInt)outline.contours[n];


        if (first <= end)
        {
          *(arg--) = TA_adjust_point_index(recorder, end);
          if (end > 0xFF)
            need_words = 1;

          if (n == 0)
            *(arg--) = TA_adjust_point_index(recorder, 0);
          else
            *(arg--) = TA_adjust_point_index(recorder,
                         (FT_UInt)outline.contours[n - 1] + 1);
          break;
        }
      }
    }

    if (last > 0xFF)
      need_words = 1;
  }

  /* emit the second part of wrap-around segments as separate segments */
  /* so that edges can easily link to them */
  for (seg = segments; seg < seg_limit; seg++)
  {
    FT_UInt first = seg->first - points;
    FT_UInt last = seg->last - points;


    if (first > last)
    {
      for (n = 0; n < outline.n_contours; n++)
      {
        if (first <= (FT_UInt)outline.contours[n])
        {
          if (n == 0)
            *(arg--) = TA_adjust_point_index(recorder, 0);
          else
            *(arg--) = TA_adjust_point_index(recorder,
                         (FT_UInt)outline.contours[n - 1] + 1);
          break;
        }
      }

      *(arg--) = TA_adjust_point_index(recorder, last);
    }
  }

  /* with most fonts it is very rare */
  /* that any of the pushed arguments is larger than 0xFF, */
  /* thus we refrain from further optimizing this case */
  bufp = TA_build_push(bufp, args, num_args, need_words, optimize);

  BCI(CALL);

  num_storage = sal_segment_offset + num_segments * 2;
  if (num_storage > sfnt->max_storage)
    sfnt->max_storage = num_storage;

  num_twilight_points = num_segments * 2;
  if (num_twilight_points > sfnt->max_twilight_points)
    sfnt->max_twilight_points = num_twilight_points;

  /* both this function and `TA_emit_hints_record' */
  /* push data onto the stack */
  num_stack_elements = ADDITIONAL_STACK_ELEMENTS
                       + recorder->num_stack_elements + num_args;
  if (num_stack_elements > sfnt->max_stack_elements)
    sfnt->max_stack_elements = num_stack_elements;

  free(args);

  return bufp;
}


static FT_Byte*
TA_sfnt_build_glyph_scaler(SFNT* sfnt,
                           Recorder* recorder,
                           FT_Byte* bufp)
{
  FONT* font = recorder->font;
  FT_GlyphSlot glyph = sfnt->face->glyph;
  FT_Vector* points = glyph->outline.points;
  FT_Int num_contours = glyph->outline.n_contours;

  FT_UInt* args;
  FT_UInt* arg;
  FT_UInt num_args;

  FT_Bool need_words = 0;
  FT_Int p, q;
  FT_Int start, end;
  FT_UShort num_stack_elements;


  num_args = 2 * num_contours + 2;

  /* collect all arguments temporarily in an array (in reverse order) */
  /* so that we can easily split into chunks of 255 args */
  /* as needed by NPUSHB and NPUSHW, respectively */
  args = (FT_UInt*)malloc(num_args * sizeof (FT_UInt));
  if (!args)
    return NULL;

  arg = args + num_args - 1;

  if (num_args > 0xFF)
    need_words = 1;

  if (recorder->glyph->num_components && font->hint_with_components)
    *(arg--) = bci_scale_composite_glyph;
  else
    *(arg--) = bci_scale_glyph;
  *(arg--) = num_contours;

  start = 0;
  end = 0;

  for (p = 0; p < num_contours; p++)
  {
    FT_Int max = start;
    FT_Int min = start;


    end = glyph->outline.contours[p];

    for (q = start; q <= end; q++)
    {
      if (points[q].y < points[min].y)
        min = q;
      if (points[q].y > points[max].y)
        max = q;
    }

    if (min > max)
    {
      *(arg--) = TA_adjust_point_index(recorder, max);
      *(arg--) = TA_adjust_point_index(recorder, min);
    }
    else
    {
      *(arg--) = TA_adjust_point_index(recorder, min);
      *(arg--) = TA_adjust_point_index(recorder, max);
    }

    start = end + 1;
  }

  if (end > 0xFF)
    need_words = 1;

  /* with most fonts it is very rare */
  /* that any of the pushed arguments is larger than 0xFF, */
  /* thus we refrain from further optimizing this case */
  bufp = TA_build_push(bufp, args, num_args, need_words, 1);

  BCI(CALL);

  num_stack_elements = ADDITIONAL_STACK_ELEMENTS + num_args;
  if (num_stack_elements > sfnt->max_stack_elements)
    sfnt->max_stack_elements = num_stack_elements;

  free(args);

  return bufp;
}


static FT_Byte*
TA_font_build_subglyph_shifter(FONT* font,
                               FT_Byte* bufp)
{
  FT_Face face = font->loader->face;
  FT_GlyphSlot glyph = face->glyph;

  TA_GlyphLoader gloader = font->loader->gloader;

  TA_SubGlyph subglyphs = gloader->base.subglyphs;
  TA_SubGlyph subglyph_limit = subglyphs + gloader->base.num_subglyphs;
  TA_SubGlyph subglyph;

  FT_Int curr_contour = 0;


  for (subglyph = subglyphs; subglyph < subglyph_limit; subglyph++)
  {
    FT_Error error;

    FT_UShort flags = subglyph->flags;
    FT_Pos y_offset = subglyph->arg2;

    FT_Int num_contours;


    /* load subglyph to get the number of contours */
    error = FT_Load_Glyph(face, subglyph->index, FT_LOAD_NO_SCALE);
    if (error)
      return NULL;
    num_contours = glyph->outline.n_contours;

    /* nothing to do if there is a point-to-point alignment */
    if (!(flags & FT_SUBGLYPH_FLAG_ARGS_ARE_XY_VALUES))
      goto End;

    /* nothing to do if y offset is zero */
    if (!y_offset)
      goto End;

    /* nothing to do if there are no contours */
    if (!num_contours)
      goto End;

    /* note that calling `FT_Load_Glyph' without FT_LOAD_NO_RECURSE */
    /* ensures that composite subglyphs are represented as simple glyphs */

    if (num_contours > 0xFF
        || curr_contour > 0xFF)
    {
      BCI(PUSHW_2);
      BCI(HIGH(curr_contour));
      BCI(LOW(curr_contour));
      BCI(HIGH(num_contours));
      BCI(LOW(num_contours));
    }
    else
    {
      BCI(PUSHB_2);
      BCI(curr_contour);
      BCI(num_contours);
    }

    /* there are high chances that this value needs PUSHW, */
    /* thus we handle it separately */
    if (y_offset > 0xFF || y_offset < 0)
    {
      BCI(PUSHW_1);
      BCI(HIGH(y_offset));
      BCI(LOW(y_offset));
    }
    else
    {
      BCI(PUSHB_1);
      BCI(y_offset);
    }

    BCI(PUSHB_1);
    BCI(bci_shift_subglyph);
    BCI(CALL);

  End:
    curr_contour += num_contours;
  }

  return bufp;
}


/*
 * The four `ta_ip_*' actions in the `TA_hints_recorder' callback store its
 * data in four arrays (which are simple but waste a lot of memory).  The
 * function below converts them into bytecode.
 *
 * For `ta_ip_before' and `ta_ip_after', the collected points are emitted
 * together with the edge they correspond to.
 *
 * For both `ta_ip_on' and `ta_ip_between', an outer loop is constructed to
 * loop over the edge or edge pairs, respectively, and each edge or edge
 * pair contains an inner loop to emit the correponding points.
 */

static void
TA_build_point_hints(Recorder* recorder,
                     TA_GlyphHints hints)
{
  TA_AxisHints axis = &hints->axis[TA_DIMENSION_VERT];
  TA_Segment segments = axis->segments;
  TA_Edge edges = axis->edges;

  TA_Edge edge;
  TA_Edge before;
  TA_Edge after;

  FT_Byte* p = recorder->hints_record.buf;
  FT_UShort num_edges = axis->num_edges;
  FT_UShort num_strong_points = recorder->num_strong_points;

  FT_UShort i;
  FT_UShort j;
  FT_UShort k;

  FT_UShort* ip;
  FT_UShort* iq;
  FT_UShort* ir;
  FT_UShort* ip_limit;
  FT_UShort* iq_limit;
  FT_UShort* ir_limit;


  /* we store everything as 16bit numbers; */
  /* the function numbers (`ta_ip_before', etc.) */
  /* reflect the order in the TA_Action enumeration */

  /* ip_before_points */

  i = 0;
  ip = recorder->ip_before_points;
  ip_limit = ip + num_strong_points;
  for (; ip < ip_limit; ip++)
  {
    if (*ip != MISSING)
      i++;
    else
      break;
  }

  if (i)
  {
    recorder->hints_record.num_actions++;

    edge = edges;

    *(p++) = 0;
    *(p++) = (FT_Byte)ta_ip_before + ACTION_OFFSET;
    *(p++) = HIGH(edge->first - segments);
    *(p++) = LOW(edge->first - segments);
    *(p++) = HIGH(i);
    *(p++) = LOW(i);

    ip = recorder->ip_before_points;
    ip_limit = ip + i;
    for (; ip < ip_limit; ip++)
    {
      FT_UInt point = TA_adjust_point_index(recorder, *ip);


      *(p++) = HIGH(point);
      *(p++) = LOW(point);
    }
  }

  /* ip_after_points */

  i = 0;
  ip = recorder->ip_after_points;
  ip_limit = ip + num_strong_points;
  for (; ip < ip_limit; ip++)
  {
    if (*ip != MISSING)
      i++;
    else
      break;
  }

  if (i)
  {
    recorder->hints_record.num_actions++;

    edge = edges + axis->num_edges - 1;

    *(p++) = 0;
    *(p++) = (FT_Byte)ta_ip_after + ACTION_OFFSET;
    *(p++) = HIGH(edge->first - segments);
    *(p++) = LOW(edge->first - segments);
    *(p++) = HIGH(i);
    *(p++) = LOW(i);

    ip = recorder->ip_after_points;
    ip_limit = ip + i;
    for (; ip < ip_limit; ip++)
    {
      FT_UInt point = TA_adjust_point_index(recorder, *ip);


      *(p++) = HIGH(point);
      *(p++) = LOW(point);
    }
  }

  /* ip_on_point_array */

  i = 0;
  ip = recorder->ip_on_point_array;
  ip_limit = ip + num_edges * num_strong_points;
  for (; ip < ip_limit; ip += num_strong_points)
    if (*ip != MISSING)
      i++;

  if (i)
  {
    recorder->hints_record.num_actions++;

    *(p++) = 0;
    *(p++) = (FT_Byte)ta_ip_on + ACTION_OFFSET;
    *(p++) = HIGH(i);
    *(p++) = LOW(i);

    i = 0;
    ip = recorder->ip_on_point_array;
    ip_limit = ip + num_edges * num_strong_points;
    for (; ip < ip_limit; ip += num_strong_points, i++)
    {
      if (*ip == MISSING)
        continue;

      edge = edges + i;

      *(p++) = HIGH(edge->first - segments);
      *(p++) = LOW(edge->first - segments);

      j = 0;
      iq = ip;
      iq_limit = iq + num_strong_points;
      for (; iq < iq_limit; iq++)
      {
        if (*iq != MISSING)
          j++;
        else
          break;
      }

      *(p++) = HIGH(j);
      *(p++) = LOW(j);

      iq = ip;
      iq_limit = iq + j;
      for (; iq < iq_limit; iq++)
      {
        FT_UInt point = TA_adjust_point_index(recorder, *iq);


        *(p++) = HIGH(point);
        *(p++) = LOW(point);
      }
    }
  }

  /* ip_between_point_array */

  i = 0;
  ip = recorder->ip_between_point_array;
  ip_limit = ip + num_edges * num_edges * num_strong_points;
  for (; ip < ip_limit; ip += num_strong_points)
    if (*ip != MISSING)
      i++;

  if (i)
  {
    recorder->hints_record.num_actions++;

    *(p++) = 0;
    *(p++) = (FT_Byte)ta_ip_between + ACTION_OFFSET;
    *(p++) = HIGH(i);
    *(p++) = LOW(i);

    i = 0;
    ip = recorder->ip_between_point_array;
    ip_limit = ip + num_edges * num_edges * num_strong_points;
    for (;
         ip < ip_limit;
         ip += num_edges * num_strong_points, i++)
    {
      before = edges + i;

      j = 0;
      iq = ip;
      iq_limit = iq + num_edges * num_strong_points;
      for (; iq < iq_limit; iq += num_strong_points, j++)
      {
        if (*iq == MISSING)
          continue;

        after = edges + j;

        *(p++) = HIGH(after->first - segments);
        *(p++) = LOW(after->first - segments);
        *(p++) = HIGH(before->first - segments);
        *(p++) = LOW(before->first - segments);

        k = 0;
        ir = iq;
        ir_limit = ir + num_strong_points;
        for (; ir < ir_limit; ir++)
        {
          if (*ir != MISSING)
            k++;
          else
            break;
        }

        *(p++) = HIGH(k);
        *(p++) = LOW(k);

        ir = iq;
        ir_limit = ir + k;
        for (; ir < ir_limit; ir++)
        {
          FT_UInt point = TA_adjust_point_index(recorder, *ir);


          *(p++) = HIGH(point);
          *(p++) = LOW(point);
        }
      }
    }
  }

  recorder->hints_record.buf = p;
}


static FT_Bool
TA_hints_record_is_different(Hints_Record* hints_records,
                             FT_UInt num_hints_records,
                             FT_Byte* start,
                             FT_Byte* end)
{
  Hints_Record last_hints_record;


  if (!hints_records)
    return 1;

  /* we only need to compare with the last hints record */
  last_hints_record = hints_records[num_hints_records - 1];

  if ((FT_UInt)(end - start) != last_hints_record.buf_len)
    return 1;

  if (memcmp(start, last_hints_record.buf, last_hints_record.buf_len))
    return 1;

  return 0;
}


static FT_Error
TA_add_hints_record(Hints_Record** hints_records,
                    FT_UInt* num_hints_records,
                    FT_Byte* start,
                    Hints_Record hints_record)
{
  Hints_Record* hints_records_new;
  FT_UInt buf_len;
  /* at this point, `hints_record.buf' still points into `ins_buf' */
  FT_Byte* end = hints_record.buf;


  buf_len = (FT_UInt)(end - start);

  /* now fill the structure completely */
  hints_record.buf_len = buf_len;
  hints_record.buf = (FT_Byte*)malloc(buf_len);
  if (!hints_record.buf)
    return FT_Err_Out_Of_Memory;

  memcpy(hints_record.buf, start, buf_len);

  (*num_hints_records)++;
  hints_records_new =
    (Hints_Record*)realloc(*hints_records, *num_hints_records
                                           * sizeof (Hints_Record));
  if (!hints_records_new)
  {
    free(hints_record.buf);
    (*num_hints_records)--;
    return FT_Err_Out_Of_Memory;
  }
  else
    *hints_records = hints_records_new;

  (*hints_records)[*num_hints_records - 1] = hints_record;

  return FT_Err_Ok;
}


static FT_Byte*
TA_emit_hints_record(Recorder* recorder,
                     Hints_Record* hints_record,
                     FT_Byte* bufp,
                     FT_Bool optimize)
{
  FT_Byte* p;
  FT_Byte* endp;
  FT_Bool need_words = 0;

  FT_UInt i, j;
  FT_UInt num_arguments;
  FT_UInt num_args;


  /* check whether any argument is larger than 0xFF */
  endp = hints_record->buf + hints_record->buf_len;
  for (p = hints_record->buf; p < endp; p += 2)
    if (*p)
      need_words = 1;

  /* with most fonts it is very rare */
  /* that any of the pushed arguments is larger than 0xFF, */
  /* thus we refrain from further optimizing this case */

  num_arguments = hints_record->buf_len / 2;
  p = endp - 2;

  if (need_words)
  {
    for (i = 0; i < num_arguments; i += 255)
    {
      num_args = (num_arguments - i > 255) ? 255 : (num_arguments - i);

      if (optimize && num_args <= 8)
        BCI(PUSHW_1 - 1 + num_args);
      else
      {
        BCI(NPUSHW);
        BCI(num_args);
      }
      for (j = 0; j < num_args; j++)
      {
        BCI(*p);
        BCI(*(p + 1));
        p -= 2;
      }
    }
  }
  else
  {
    /* we only need the lower bytes */
    p++;

    for (i = 0; i < num_arguments; i += 255)
    {
      num_args = (num_arguments - i > 255) ? 255 : (num_arguments - i);

      if (optimize && num_args <= 8)
        BCI(PUSHB_1 - 1 + num_args);
      else
      {
        BCI(NPUSHB);
        BCI(num_args);
      }
      for (j = 0; j < num_args; j++)
      {
        BCI(*p);
        p -= 2;
      }
    }
  }

  /* collect stack depth data */
  if (num_arguments > recorder->num_stack_elements)
    recorder->num_stack_elements = num_arguments;

  return bufp;
}


static FT_Byte*
TA_emit_hints_records(Recorder* recorder,
                      Hints_Record* hints_records,
                      FT_UInt num_hints_records,
                      FT_Byte* bufp,
                      FT_Bool optimize)
{
  FT_UInt i;
  Hints_Record* hints_record;


  hints_record = hints_records;

  for (i = 0; i < num_hints_records - 1; i++)
  {
    BCI(MPPEM);
    if (hints_record->size > 0xFF)
    {
      BCI(PUSHW_1);
      BCI(HIGH((hints_record + 1)->size));
      BCI(LOW((hints_record + 1)->size));
    }
    else
    {
      BCI(PUSHB_1);
      BCI((hints_record + 1)->size);
    }
    BCI(LT);
    BCI(IF);
    bufp = TA_emit_hints_record(recorder, hints_record, bufp, optimize);
    BCI(ELSE);

    hints_record++;
  }

  bufp = TA_emit_hints_record(recorder, hints_record, bufp, optimize);

  for (i = 0; i < num_hints_records - 1; i++)
    BCI(EIF);

  return bufp;
}


static void
TA_free_hints_records(Hints_Record* hints_records,
                      FT_UInt num_hints_records)
{
  FT_UInt i;


  for (i = 0; i < num_hints_records; i++)
    free(hints_records[i].buf);

  free(hints_records);
}


static FT_Byte*
TA_hints_recorder_handle_segments(FT_Byte* bufp,
                                  TA_AxisHints axis,
                                  TA_Edge edge,
                                  FT_UShort* wraps)
{
  TA_Segment segments = axis->segments;
  TA_Segment seg;
  FT_UShort seg_idx;
  FT_UShort num_segs = 0;
  FT_UShort* wrap;


  seg_idx = edge->first - segments;

  /* we store everything as 16bit numbers */
  *(bufp++) = HIGH(seg_idx);
  *(bufp++) = LOW(seg_idx);

  /* wrap-around segments are stored as two segments */
  if (edge->first->first > edge->first->last)
    num_segs++;

  seg = edge->first->edge_next;
  while (seg != edge->first)
  {
    num_segs++;

    if (seg->first > seg->last)
      num_segs++;

    seg = seg->edge_next;
  }

  *(bufp++) = HIGH(num_segs);
  *(bufp++) = LOW(num_segs);

  if (edge->first->first > edge->first->last)
  {
    /* emit second part of wrap-around segment; */
    /* the bytecode positions such segments after `normal' ones */
    wrap = wraps;
    for (;;)
    {
      if (seg_idx == *wrap)
        break;
      wrap++;
    }

    *(bufp++) = HIGH(axis->num_segments + (wrap - wraps));
    *(bufp++) = LOW(axis->num_segments + (wrap - wraps));
  }

  seg = edge->first->edge_next;
  while (seg != edge->first)
  {
    seg_idx = seg - segments;

    *(bufp++) = HIGH(seg_idx);
    *(bufp++) = LOW(seg_idx);

    if (seg->first > seg->last)
    {
      wrap = wraps;
      for (;;)
      {
        if (seg_idx == *wrap)
          break;
        wrap++;
      }

      *(bufp++) = HIGH(axis->num_segments + (wrap - wraps));
      *(bufp++) = LOW(axis->num_segments + (wrap - wraps));
    }

    seg = seg->edge_next;
  }

  return bufp;
}


static void
TA_hints_recorder(TA_Action action,
                  TA_GlyphHints hints,
                  TA_Dimension dim,
                  void* arg1,
                  TA_Edge arg2,
                  TA_Edge arg3,
                  TA_Edge lower_bound,
                  TA_Edge upper_bound)
{
  TA_AxisHints axis = &hints->axis[dim];
  TA_Edge edges = axis->edges;
  TA_Segment segments = axis->segments;
  TA_Point points = hints->points;

  Recorder* recorder = (Recorder*)hints->user;
  FONT* font = recorder->font;
  FT_UShort* wraps = recorder->wrap_around_segments;
  FT_Byte* p = recorder->hints_record.buf;

  FT_UShort* ip;
  FT_UShort* limit;


  if (dim == TA_DIMENSION_HORZ)
    return;

  /* we collect point hints for later processing */
  switch (action)
  {
  case ta_ip_before:
    {
      TA_Point point = (TA_Point)arg1;


      ip = recorder->ip_before_points;
      limit = ip + recorder->num_strong_points;
      for (; ip < limit; ip++)
      {
        if (*ip == MISSING)
        {
          *ip = point - points;
          break;
        }
      }
    }
    return;

  case ta_ip_after:
    {
      TA_Point point = (TA_Point)arg1;


      ip = recorder->ip_after_points;
      limit = ip + recorder->num_strong_points;
      for (; ip < limit; ip++)
      {
        if (*ip == MISSING)
        {
          *ip = point - points;
          break;
        }
      }
    }
    return;

  case ta_ip_on:
    {
      TA_Point point = (TA_Point)arg1;
      TA_Edge edge = arg2;


      ip = recorder->ip_on_point_array
           + recorder->num_strong_points
             * (edge - edges);
      limit = ip + recorder->num_strong_points;
      for (; ip < limit; ip++)
      {
        if (*ip == MISSING)
        {
          *ip = point - points;
          break;
        }
      }
    }
    return;

  case ta_ip_between:
    {
      TA_Point point = (TA_Point)arg1;
      TA_Edge before = arg2;
      TA_Edge after = arg3;


      /* note that `recorder->num_segments' has been used for allocation, */
      /* but `axis->num_edges' is used for accessing this array */
      ip = recorder->ip_between_point_array
           + recorder->num_strong_points * axis->num_edges
             * (before - edges)
           + recorder->num_strong_points
             * (after - edges);
      limit = ip + recorder->num_strong_points;
      for (; ip < limit; ip++)
      {
        if (*ip == MISSING)
        {
          *ip = point - points;
          break;
        }
      }
    }
    return;

  case ta_bound:
    /* we ignore the BOUND action since we signal this information */
    /* with the proper function number */
    return;

  default:
    break;
  }

  /* some enum values correspond to four or eight bytecode functions; */
  /* if the value is n, the function numbers are n, ..., n+7, */
  /* to be differentiated with flags */

  switch (action)
  {
  case ta_link:
    {
      TA_Edge base_edge = (TA_Edge)arg1;
      TA_Edge stem_edge = arg2;


      *(p++) = 0;
      *(p++) = (FT_Byte)action + ACTION_OFFSET
               + ((stem_edge->flags & TA_EDGE_SERIF) != 0)
               + 2 * ((base_edge->flags & TA_EDGE_ROUND) != 0);

      *(p++) = HIGH(base_edge->first - segments);
      *(p++) = LOW(base_edge->first - segments);
      *(p++) = HIGH(stem_edge->first - segments);
      *(p++) = LOW(stem_edge->first - segments);

      p = TA_hints_recorder_handle_segments(p, axis, stem_edge, wraps);
    }
    break;

  case ta_anchor:
    {
      TA_Edge edge = (TA_Edge)arg1;
      TA_Edge edge2 = arg2;


      *(p++) = 0;
      *(p++) = (FT_Byte)action + ACTION_OFFSET
               + ((edge2->flags & TA_EDGE_SERIF) != 0)
               + 2 * ((edge->flags & TA_EDGE_ROUND) != 0);

      *(p++) = HIGH(edge->first - segments);
      *(p++) = LOW(edge->first - segments);
      *(p++) = HIGH(edge2->first - segments);
      *(p++) = LOW(edge2->first - segments);

      p = TA_hints_recorder_handle_segments(p, axis, edge, wraps);
    }
    break;

  case ta_adjust:
    {
      TA_Edge edge = (TA_Edge)arg1;
      TA_Edge edge2 = arg2;
      TA_Edge edge_minus_one = lower_bound;


      *(p++) = 0;
      *(p++) = (FT_Byte)action + ACTION_OFFSET
               + ((edge2->flags & TA_EDGE_SERIF) != 0)
               + 2 * ((edge->flags & TA_EDGE_ROUND) != 0)
               + 4 * (edge_minus_one != NULL);

      *(p++) = HIGH(edge->first - segments);
      *(p++) = LOW(edge->first - segments);
      *(p++) = HIGH(edge2->first - segments);
      *(p++) = LOW(edge2->first - segments);

      if (edge_minus_one)
      {
        *(p++) = HIGH(edge_minus_one->first - segments);
        *(p++) = LOW(edge_minus_one->first - segments);
      }

      p = TA_hints_recorder_handle_segments(p, axis, edge, wraps);
    }
    break;

  case ta_blue_anchor:
    {
      TA_Edge edge = (TA_Edge)arg1;
      TA_Edge blue = arg2;


      *(p++) = 0;
      *(p++) = (FT_Byte)action + ACTION_OFFSET;

      *(p++) = HIGH(blue->first - segments);
      *(p++) = LOW(blue->first - segments);

      if (edge->best_blue_is_shoot)
      {
        *(p++) = HIGH(CVT_BLUE_SHOOTS_OFFSET(font) + edge->best_blue_idx);
        *(p++) = LOW(CVT_BLUE_SHOOTS_OFFSET(font) + edge->best_blue_idx);
      }
      else
      {
        *(p++) = HIGH(CVT_BLUE_REFS_OFFSET(font) + edge->best_blue_idx);
        *(p++) = LOW(CVT_BLUE_REFS_OFFSET(font) + edge->best_blue_idx);
      }

      *(p++) = HIGH(edge->first - segments);
      *(p++) = LOW(edge->first - segments);

      p = TA_hints_recorder_handle_segments(p, axis, edge, wraps);
    }
    break;

  case ta_stem:
    {
      TA_Edge edge = (TA_Edge)arg1;
      TA_Edge edge2 = arg2;
      TA_Edge edge_minus_one = lower_bound;


      *(p++) = 0;
      *(p++) = (FT_Byte)action + ACTION_OFFSET
               + ((edge2->flags & TA_EDGE_SERIF) != 0)
               + 2 * ((edge->flags & TA_EDGE_ROUND) != 0)
               + 4 * (edge_minus_one != NULL);

      *(p++) = HIGH(edge->first - segments);
      *(p++) = LOW(edge->first - segments);
      *(p++) = HIGH(edge2->first - segments);
      *(p++) = LOW(edge2->first - segments);

      if (edge_minus_one)
      {
        *(p++) = HIGH(edge_minus_one->first - segments);
        *(p++) = LOW(edge_minus_one->first - segments);
      }

      p = TA_hints_recorder_handle_segments(p, axis, edge, wraps);
      p = TA_hints_recorder_handle_segments(p, axis, edge2, wraps);
    }
    break;

  case ta_blue:
    {
      TA_Edge edge = (TA_Edge)arg1;


      *(p++) = 0;
      *(p++) = (FT_Byte)action + ACTION_OFFSET;

      if (edge->best_blue_is_shoot)
      {
        *(p++) = HIGH(CVT_BLUE_SHOOTS_OFFSET(font) + edge->best_blue_idx);
        *(p++) = LOW(CVT_BLUE_SHOOTS_OFFSET(font) + edge->best_blue_idx);
      }
      else
      {
        *(p++) = HIGH(CVT_BLUE_REFS_OFFSET(font) + edge->best_blue_idx);
        *(p++) = LOW(CVT_BLUE_REFS_OFFSET(font) + edge->best_blue_idx);
      }

      *(p++) = HIGH(edge->first - segments);
      *(p++) = LOW(edge->first - segments);

      p = TA_hints_recorder_handle_segments(p, axis, edge, wraps);
    }
    break;

  case ta_serif:
    {
      TA_Edge serif = (TA_Edge)arg1;
      TA_Edge base = serif->serif;


      *(p++) = 0;
      *(p++) = (FT_Byte)action + ACTION_OFFSET
               + (lower_bound != NULL)
               + 2 * (upper_bound != NULL);

      *(p++) = HIGH(serif->first - segments);
      *(p++) = LOW(serif->first - segments);
      *(p++) = HIGH(base->first - segments);
      *(p++) = LOW(base->first - segments);

      if (lower_bound)
      {
        *(p++) = HIGH(lower_bound->first - segments);
        *(p++) = LOW(lower_bound->first - segments);
      }
      if (upper_bound)
      {
        *(p++) = HIGH(upper_bound->first - segments);
        *(p++) = LOW(upper_bound->first - segments);
      }

      p = TA_hints_recorder_handle_segments(p, axis, serif, wraps);
    }
    break;

  case ta_serif_anchor:
  case ta_serif_link2:
    {
      TA_Edge edge = (TA_Edge)arg1;


      *(p++) = 0;
      *(p++) = (FT_Byte)action + ACTION_OFFSET
               + (lower_bound != NULL)
               + 2 * (upper_bound != NULL);

      *(p++) = HIGH(edge->first - segments);
      *(p++) = LOW(edge->first - segments);

      if (lower_bound)
      {
        *(p++) = HIGH(lower_bound->first - segments);
        *(p++) = LOW(lower_bound->first - segments);
      }
      if (upper_bound)
      {
        *(p++) = HIGH(upper_bound->first - segments);
        *(p++) = LOW(upper_bound->first - segments);
      }

      p = TA_hints_recorder_handle_segments(p, axis, edge, wraps);
    }
    break;

  case ta_serif_link1:
    {
      TA_Edge edge = (TA_Edge)arg1;
      TA_Edge before = arg2;
      TA_Edge after = arg3;


      *(p++) = 0;
      *(p++) = (FT_Byte)action + ACTION_OFFSET
               + (lower_bound != NULL)
               + 2 * (upper_bound != NULL);

      *(p++) = HIGH(before->first - segments);
      *(p++) = LOW(before->first - segments);
      *(p++) = HIGH(edge->first - segments);
      *(p++) = LOW(edge->first - segments);
      *(p++) = HIGH(after->first - segments);
      *(p++) = LOW(after->first - segments);

      if (lower_bound)
      {
        *(p++) = HIGH(lower_bound->first - segments);
        *(p++) = LOW(lower_bound->first - segments);
      }
      if (upper_bound)
      {
        *(p++) = HIGH(upper_bound->first - segments);
        *(p++) = LOW(upper_bound->first - segments);
      }

      p = TA_hints_recorder_handle_segments(p, axis, edge, wraps);
    }
    break;

  default:
    /* there are more cases in the enumeration */
    /* which are handled with flags */
    break;
  }

  recorder->hints_record.num_actions++;
  recorder->hints_record.buf = p;
}


static FT_Error
TA_init_recorder(Recorder* recorder,
                 FONT* font,
                 GLYPH* glyph,
                 TA_GlyphHints hints)
{
  TA_AxisHints axis = &hints->axis[TA_DIMENSION_VERT];
  TA_Point points = hints->points;
  TA_Point point_limit = points + hints->num_points;
  TA_Point point;

  TA_Segment segments = axis->segments;
  TA_Segment seg_limit = segments + axis->num_segments;
  TA_Segment seg;

  FT_UShort num_strong_points = 0;
  FT_UShort* wrap_around_segment;

  recorder->font = font;
  recorder->glyph = glyph;
  recorder->num_segments = axis->num_segments;

  recorder->ip_before_points = NULL;
  recorder->ip_after_points = NULL;
  recorder->ip_on_point_array = NULL;
  recorder->ip_between_point_array = NULL;

  recorder->num_stack_elements = 0;

  /* no need to clean up allocated arrays in case of error; */
  /* this is handled later by `TA_free_recorder' */

  recorder->num_wrap_around_segments = 0;
  for (seg = segments; seg < seg_limit; seg++)
    if (seg->first > seg->last)
      recorder->num_wrap_around_segments++;

  recorder->wrap_around_segments =
    (FT_UShort*)malloc(recorder->num_wrap_around_segments
                       * sizeof (FT_UShort));
  if (!recorder->wrap_around_segments)
    return FT_Err_Out_Of_Memory;

  wrap_around_segment = recorder->wrap_around_segments;
  for (seg = segments; seg < seg_limit; seg++)
    if (seg->first > seg->last)
      *(wrap_around_segment++) = seg - segments;

  /* get number of strong points */
  for (point = points; point < point_limit; point++)
  {
    /* actually, we need to test `TA_FLAG_TOUCH_Y' also; */
    /* however, this value isn't known yet */
    /* (or rather, it can vary between different pixel sizes) */
    if (point->flags & TA_FLAG_WEAK_INTERPOLATION)
      continue;

    num_strong_points++;
  }

  recorder->num_strong_points = num_strong_points;

  recorder->ip_before_points =
    (FT_UShort*)malloc(num_strong_points * sizeof (FT_UShort));
  if (!recorder->ip_before_points)
    return FT_Err_Out_Of_Memory;

  recorder->ip_after_points =
    (FT_UShort*)malloc(num_strong_points * sizeof (FT_UShort));
  if (!recorder->ip_after_points)
    return FT_Err_Out_Of_Memory;

  /* actually, we need `hints->num_edges' for the array sizes; */
  /* however, this value isn't known yet */
  /* (or rather, it can vary between different pixel sizes) */
  recorder->ip_on_point_array =
    (FT_UShort*)malloc(axis->num_segments
                       * num_strong_points * sizeof (FT_UShort));
  if (!recorder->ip_on_point_array)
    return FT_Err_Out_Of_Memory;

  recorder->ip_between_point_array =
    (FT_UShort*)malloc(axis->num_segments * axis->num_segments
                       * num_strong_points * sizeof (FT_UShort));
  if (!recorder->ip_between_point_array)
    return FT_Err_Out_Of_Memory;

  return FT_Err_Ok;
}


static void
TA_reset_recorder(Recorder* recorder,
                  FT_Byte* bufp)
{
  recorder->hints_record.buf = bufp;
  recorder->hints_record.num_actions = 0;
}


static void
TA_rewind_recorder(Recorder* recorder,
                   FT_Byte* bufp,
                   FT_UInt size)
{
  TA_reset_recorder(recorder, bufp);

  recorder->hints_record.size = size;

  /* We later check with MISSING (which expands to 0xFF bytes) */

  memset(recorder->ip_before_points, 0xFF,
         recorder->num_strong_points * sizeof (FT_UShort));
  memset(recorder->ip_after_points, 0xFF,
         recorder->num_strong_points * sizeof (FT_UShort));

  memset(recorder->ip_on_point_array, 0xFF,
         recorder->num_segments
         * recorder->num_strong_points * sizeof (FT_UShort));
  memset(recorder->ip_between_point_array, 0xFF,
         recorder->num_segments * recorder->num_segments
         * recorder->num_strong_points * sizeof (FT_UShort));
}


static void
TA_free_recorder(Recorder* recorder)
{
  free(recorder->wrap_around_segments);

  free(recorder->ip_before_points);
  free(recorder->ip_after_points);
  free(recorder->ip_on_point_array);
  free(recorder->ip_between_point_array);
}


FT_Error
TA_sfnt_build_glyph_instructions(SFNT* sfnt,
                                 FONT* font,
                                 FT_Long idx)
{
  FT_Face face = sfnt->face;
  FT_Error error;

  FT_Byte* ins_buf;
  FT_UInt ins_len;
  FT_Byte* bufp;
  FT_Byte* p;

  SFNT_Table* glyf_table = &font->tables[sfnt->glyf_idx];
  glyf_Data* data = (glyf_Data*)glyf_table->data;
  /* `idx' is never negative */
  GLYPH* glyph = &data->glyphs[idx];

  TA_GlyphHints hints;

  FT_UInt num_action_hints_records = 0;
  FT_UInt num_point_hints_records = 0;
  Hints_Record* action_hints_records = NULL;
  Hints_Record* point_hints_records = NULL;

  Recorder recorder;
  FT_UShort num_stack_elements;
  FT_Bool optimize = 0;

  FT_Int32 load_flags;
  FT_UInt size;

  FT_Byte* pos[3];

#ifdef TA_DEBUG
  int _ta_debug_save;
#endif


  /* XXX: right now, we abuse this flag to control */
  /*      the global behaviour of the auto-hinter */
  load_flags = 1 << 29; /* vertical hinting only */
  if (!font->pre_hinting)
  {
    if (font->hint_with_components)
      load_flags |= FT_LOAD_NO_SCALE;
    else
      load_flags |= FT_LOAD_NO_RECURSE;
  }

  /* computing the segments is resolution independent, */
  /* thus the pixel size in this call is arbitrary */
  error = FT_Set_Pixel_Sizes(face, 20, 20);
  if (error)
    return error;

#ifdef TA_DEBUG
  /* temporarily disable debugging output */
  /* to avoid getting the information twice */
  _ta_debug_save = _ta_debug;
  _ta_debug = 0;
#endif

  ta_loader_register_hints_recorder(font->loader, NULL, NULL);
  error = ta_loader_load_glyph(font, face, (FT_UInt)idx, load_flags);

#ifdef TA_DEBUG
  _ta_debug = _ta_debug_save;
#endif

  if (error)
    return error;

  /* do nothing if we have an empty glyph */
  if (!face->glyph->outline.n_contours)
    return FT_Err_Ok;

  hints = &font->loader->hints;

  /* do nothing if the setup delivered the dummy module only */
  if (!hints->num_points)
    return FT_Err_Ok;

  /* we allocate a buffer which is certainly large enough */
  /* to hold all of the created bytecode instructions; */
  /* later on it gets reallocated to its real size */
  ins_len = hints->num_points * 1000;
  ins_buf = (FT_Byte*)malloc(ins_len);
  if (!ins_buf)
    return FT_Err_Out_Of_Memory;

  /* initialize array with an invalid bytecode */
  /* so that we can easily find the array length at reallocation time */
  memset(ins_buf, INS_A0, ins_len);

  /* handle composite glyph */
  if (font->loader->gloader->base.num_subglyphs)
  {
    bufp = TA_font_build_subglyph_shifter(font, ins_buf);
    if (!bufp)
    {
      error = FT_Err_Out_Of_Memory;
      goto Err;
    }

    goto Done1;
  }

  /* only scale the glyph if the dummy hinter has been used */
  if (font->loader->metrics->clazz == &ta_dummy_script_class)
  {
    /* since `TA_init_recorder' hasn't been called yet, */
    /* we manually initialize the `font' and `glyph' fields */
    recorder.font = font;
    recorder.glyph = glyph;

    bufp = TA_sfnt_build_glyph_scaler(sfnt, &recorder, ins_buf);
    if (!bufp)
    {
      error = FT_Err_Out_Of_Memory;
      goto Err;
    }

    goto Done1;
  }

  error = TA_init_recorder(&recorder, font, glyph, hints);
  if (error)
    goto Err;

  /* loop over a large range of pixel sizes */
  /* to find hints records which get pushed onto the bytecode stack */

#ifdef DEBUGGING
  if (font->debug)
  {
    int num_chars, i;
    char buf[256];


    (void)FT_Get_Glyph_Name(face, idx, buf, 256);

    num_chars = fprintf(stderr, "glyph %ld", idx);
    if (*buf)
      num_chars += fprintf(stderr, " (%s)", buf);
    fprintf(stderr, "\n");
    for (i = 0; i < num_chars; i++)
      putc('=', stderr);
    fprintf(stderr, "\n\n");

  }
#endif

  /* we temporarily use `ins_buf' to record the current glyph hints */
  ta_loader_register_hints_recorder(font->loader,
                                    TA_hints_recorder,
                                    (void*)&recorder);

  for (size = font->hinting_range_min;
       size <= font->hinting_range_max;
       size++)
  {
#ifdef DEBUGGING
    int have_dumps = 0;
#endif


    TA_rewind_recorder(&recorder, ins_buf, size);

    error = FT_Set_Pixel_Sizes(face, size, size);
    if (error)
      goto Err;

#ifdef DEBUGGING
    if (font->debug)
    {
      int num_chars, i;


      num_chars = fprintf(stderr, "size %d\n", size);
      for (i = 0; i < num_chars - 1; i++)
        putc('-', stderr);
      fprintf(stderr, "\n\n");
    }
#endif

    /* calling `ta_loader_load_glyph' uses the */
    /* `TA_hints_recorder' function as a callback, */
    /* modifying `hints_record' */
    error = ta_loader_load_glyph(font, face, idx, load_flags);
    if (error)
      goto Err;

    if (TA_hints_record_is_different(action_hints_records,
                                     num_action_hints_records,
                                     ins_buf, recorder.hints_record.buf))
    {
#ifdef DEBUGGING
      if (font->debug)
      {
        have_dumps = 1;

        ta_glyph_hints_dump_edges(_ta_debug_hints);
        ta_glyph_hints_dump_segments(_ta_debug_hints);
        ta_glyph_hints_dump_points(_ta_debug_hints);

        fprintf(stderr, "action hints record:\n");
        if (ins_buf == recorder.hints_record.buf)
          fprintf(stderr, "  (none)");
        else
        {
          fprintf(stderr, "  ");
          for (p = ins_buf; p < recorder.hints_record.buf; p += 2)
            fprintf(stderr, " %2d", *p * 256 + *(p + 1));
        }
        fprintf(stderr, "\n");
      }
#endif

      error = TA_add_hints_record(&action_hints_records,
                                  &num_action_hints_records,
                                  ins_buf, recorder.hints_record);
      if (error)
        goto Err;
    }

    /* now handle point records */

    TA_reset_recorder(&recorder, ins_buf);

    /* use the point hints data collected in `TA_hints_recorder' */
    TA_build_point_hints(&recorder, hints);

    if (TA_hints_record_is_different(point_hints_records,
                                     num_point_hints_records,
                                     ins_buf, recorder.hints_record.buf))
    {
#ifdef DEBUGGING
      if (font->debug)
      {
        if (!have_dumps)
        {
          int num_chars, i;


          num_chars = fprintf(stderr, "size %d\n", size);
          for (i = 0; i < num_chars - 1; i++)
            putc('-', stderr);
          fprintf(stderr, "\n\n");

          ta_glyph_hints_dump_edges(_ta_debug_hints);
          ta_glyph_hints_dump_segments(_ta_debug_hints);
          ta_glyph_hints_dump_points(_ta_debug_hints);
        }

        fprintf(stderr, "point hints record:\n");
        if (ins_buf == recorder.hints_record.buf)
          fprintf(stderr, "  (none)");
        else
        {
          fprintf(stderr, "  ");
          for (p = ins_buf; p < recorder.hints_record.buf; p += 2)
            fprintf(stderr, " %2d", *p * 256 + *(p + 1));
        }
        fprintf(stderr, "\n\n");
      }
#endif

      error = TA_add_hints_record(&point_hints_records,
                                  &num_point_hints_records,
                                  ins_buf, recorder.hints_record);
      if (error)
        goto Err;
    }
  }

  if (num_action_hints_records == 1 && !action_hints_records[0].num_actions)
  {
    /* since we only have a single empty record we just scale the glyph */
    bufp = TA_sfnt_build_glyph_scaler(sfnt, &recorder, ins_buf);
    if (!bufp)
    {
      error = FT_Err_Out_Of_Memory;
      goto Err;
    }

    /* clear the rest of the temporarily used part of `ins_buf' */
    p = bufp;
    while (*p != INS_A0)
      *(p++) = INS_A0;

    goto Done;
  }

  /* if there is only a single record, */
  /* we do a global optimization later on */
  if (num_action_hints_records > 1)
    optimize = 1;

  /* store the hints records and handle stack depth */
  pos[0] = ins_buf;
  bufp = TA_emit_hints_records(&recorder,
                               point_hints_records,
                               num_point_hints_records,
                               ins_buf,
                               optimize);

  num_stack_elements = recorder.num_stack_elements;
  recorder.num_stack_elements = 0;

  pos[1] = bufp;
  bufp = TA_emit_hints_records(&recorder,
                               action_hints_records,
                               num_action_hints_records,
                               bufp,
                               optimize);

  recorder.num_stack_elements += num_stack_elements;

  pos[2] = bufp;
  bufp = TA_sfnt_build_glyph_segments(sfnt, &recorder, bufp, optimize);
  if (!bufp)
  {
    error = FT_Err_Out_Of_Memory;
    goto Err;
  }

  /* XXX improve handling of NPUSHW */
  if (num_action_hints_records == 1
      && *(pos[0]) != NPUSHW && *(pos[1]) != NPUSHW && *(pos[2]) != NPUSHW)
  {
    /*
     * we optimize two common cases, replacing
     *
     *   NPUSHB A ... NPUSHB B [... NPUSHB C] ... CALL
     *
     * with
     *
     *   NPUSHB (A+B[+C]) ... CALL
     *
     * if possible
     */
    FT_Byte sizes[3];
    FT_Byte new_size1;
    FT_Byte new_size2;

    FT_UInt sum;
    FT_UInt i;
    FT_UInt pos_idx;


    /* the point hints records block can be missing */
    if (pos[0] == pos[1])
    {
      pos[1] = pos[2];
      pos[2] = NULL;
    }

    /* there are at least two NPUSHB instructions */
    /* (one of them directly at the start) */
    sizes[0] = *(pos[0] + 1);
    sizes[1] = *(pos[1] + 1);
    sizes[2] = pos[2] ? *(pos[2] + 1) : 0;

    sum = sizes[0] + sizes[1] + sizes[2];

    if (sum > 2 * 0xFF)
      goto Done2; /* nothing to do since we need three NPUSHB */
    else if (!sizes[2] && (sum > 0xFF))
      goto Done2; /* nothing to do since we need two NPUSHB */

    if (sum > 0xFF)
    {
      /* reduce three NPUSHB to two */
      new_size1 = 0xFF;
      new_size2 = sum - 0xFF;
    }
    else
    {
      /* reduce two or three NPUSHB to one */
      new_size1 = sum;
      new_size2 = 0;
    }

    /* pack data */
    p = ins_buf;
    bufp = ins_buf;
    pos_idx = 0;

    if (new_size1 <= 8)
      BCI(PUSHB_1 - 1 + new_size1);
    else
    {
      BCI(NPUSHB);
      BCI(new_size1);
    }
    for (i = 0; i < new_size1; i++)
    {
      if (p == pos[pos_idx])
      {
        pos_idx++;
        p += 2; /* skip old NPUSHB */
      }
      *(bufp++) = *(p++);
    }

    if (new_size2)
    {
      if (new_size2 <= 8)
        BCI(PUSHB_1 - 1 + new_size2);
      else
      {
        BCI(NPUSHB);
        BCI(new_size2);
      }
      for (i = 0; i < new_size2; i++)
      {
        if (p == pos[pos_idx])
        {
          pos_idx++;
          p += 2;
        }
        *(bufp++) = *(p++);
      }
    }

    BCI(CALL);
  }

Done2:
  /* clear the rest of the temporarily used part of `ins_buf' */
  p = bufp;
  while (*p != INS_A0)
    *(p++) = INS_A0;

Done:
  TA_free_hints_records(action_hints_records, num_action_hints_records);
  TA_free_hints_records(point_hints_records, num_point_hints_records);
  TA_free_recorder(&recorder);

  /* we are done, so reallocate the instruction array to its real size */
  if (*bufp == INS_A0)
  {
    /* search backwards */
    while (*bufp == INS_A0)
      bufp--;
    bufp++;
  }
  else
  {
    /* search forwards */
    while (*bufp != INS_A0)
      bufp++;
  }

Done1:
  ins_len = bufp - ins_buf;

  if (ins_len > sfnt->max_instructions)
    sfnt->max_instructions = ins_len;

  glyph->ins_buf = (FT_Byte*)realloc(ins_buf, ins_len);
  glyph->ins_len = ins_len;

  return FT_Err_Ok;

Err:
  TA_free_hints_records(action_hints_records, num_action_hints_records);
  TA_free_hints_records(point_hints_records, num_point_hints_records);
  TA_free_recorder(&recorder);
  free(ins_buf);

  return error;
}


/* end of tabytecode.c */
