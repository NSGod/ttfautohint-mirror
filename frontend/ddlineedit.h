// ddlineedit.h

// Copyright (C) 2012-2013 by Werner Lemberg.
//
// This file is part of the ttfautohint library, and may only be used,
// modified, and distributed under the terms given in `COPYING'.  By
// continuing to use, modify, or distribute this file you indicate that you
// have read `COPYING' and understand and accept it fully.
//
// The file `COPYING' mentioned in the previous paragraph is distributed
// with the ttfautohint library.


#ifndef __DDLINEEDIT_H__
#define __DDLINEEDIT_H__

#include <config.h>
#include "ttlineedit.h"

#include <QtGui>

class Drag_Drop_Line_Edit
: public Tooltip_Line_Edit
{
  Q_OBJECT

public:
  Drag_Drop_Line_Edit(QWidget* = 0);

  void dragEnterEvent(QDragEnterEvent*);
  void dropEvent(QDropEvent*);
};


#endif // __DDLINEEDIT_H__

// end of ddlineedit.h
