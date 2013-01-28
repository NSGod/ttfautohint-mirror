// maingui.h

// Copyright (C) 2012-2013 by Werner Lemberg.
//
// This file is part of the ttfautohint library, and may only be used,
// modified, and distributed under the terms given in `COPYING'.  By
// continuing to use, modify, or distribute this file you indicate that you
// have read `COPYING' and understand and accept it fully.
//
// The file `COPYING' mentioned in the previous paragraph is distributed
// with the ttfautohint library.


#ifndef __MAINGUI_H__
#define __MAINGUI_H__

#include <config.h>

#include <QtGui>
#include "ddlineedit.h"
#include "ttlineedit.h"

#include <stdio.h>
#include <ttfautohint.h>
#include <numberset.h>

class QAction;
class QButtonGroup;
class QCheckBox;
class QComboBox;
class QFile;
class QLabel;
class QLocale;
class QMenu;
class QPushButton;
class QSpinBox;

class Drag_Drop_Line_Edit;
class Tooltip_Line_Edit;

class Main_GUI
: public QMainWindow
{
  Q_OBJECT

public:
  Main_GUI(int, int, int,
           bool, bool, bool,
           int, const char*,
           bool, bool, bool,
           bool, bool, int, bool,
           bool);
  ~Main_GUI();

protected:
  void closeEvent(QCloseEvent*);

private slots:
  void about();
  void browse_input();
  void browse_output();
  void check_min();
  void check_max();
  void check_limit();
  void check_dehint();
  void check_no_limit();
  void check_no_increase();
  void absolute_input();
  void absolute_output();
  void check_number_set();
  void clear_status_bar();
  void check_run();
  void run();

private:
  int hinting_range_min;
  int hinting_range_max;
  int hinting_limit;
  int gray_strong_stem_width;
  int gdi_cleartype_strong_stem_width;
  int dw_cleartype_strong_stem_width;
  int increase_x_height;
  QString x_height_snapping_exceptions_string;
  number_range* x_height_snapping_exceptions;
  int ignore_restrictions;
  int windows_compatibility;
  int pre_hinting;
  int hint_with_components;
  int no_info;
  int latin_fallback;
  int symbol;
  int dehint;

  void create_layout();

  void create_connections();
  void create_actions();
  void create_menus();
  void create_status_bar();
  void set_defaults();
  void read_settings();
  void write_settings();

  int check_filenames(const QString&, const QString&);
  int open_files(const QString&, FILE**, const QString&, FILE**);
  int handle_error(TA_Error, const unsigned char*, QString);

  QMenu* file_menu;
  QMenu* help_menu;

  Drag_Drop_Line_Edit* input_line;
  QPushButton* input_button;

  Drag_Drop_Line_Edit* output_line;
  QPushButton* output_button;

  QLabel* min_label;
  QSpinBox* min_box;
  QLabel* max_label;
  QSpinBox* max_box;

  QLabel* stem_label;
  QCheckBox* gray_box;
  QCheckBox* gdi_box;
  QCheckBox* dw_box;

  QLabel* fallback_label;
  QComboBox* fallback_box;

  QLabel* limit_label;
  QSpinBox* limit_box;
  QCheckBox* no_limit_box;

  QLabel* increase_label;
  QSpinBox* increase_box;
  QCheckBox* no_increase_box;

  QLabel* snapping_label;
  Tooltip_Line_Edit* snapping_line;

  QCheckBox* wincomp_box;
  QCheckBox* pre_box;
  QCheckBox* hint_box;
  QCheckBox* symbol_box;
  QCheckBox* dehint_box;
  QCheckBox* info_box;

  QPushButton* run_button;

  QAction* exit_act;
  QAction* about_act;
  QAction* about_Qt_act;

  QLocale* locale;
};

#endif // __MAINGUI_H__

// end of maingui.h
