/* edid.h
 *
 * Copyright 2007, 2008, Red Hat, Inc.
 *
 * This file is part of the Gnome Library.
 *
 * The Gnome Library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * The Gnome Library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with the Gnome Library; see the file COPYING.LIB.  If not,
 * write to the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 * Author: Soren Sandmann <sandmann@redhat.com>
 */

#ifndef EDID_H
#define EDID_H

typedef unsigned char uchar;
typedef struct MonitorInfo MonitorInfo;
typedef struct Timing Timing;
typedef struct DetailedTiming DetailedTiming;

typedef enum
{
  UNDEFINED,
  DVI,
  HDMI_A,
  HDMI_B,
  MDDI,
  DISPLAY_PORT
} Interface;

typedef enum
{
  UNDEFINED_COLOR,
  MONOCHROME,
  RGB,
  OTHER_COLOR
} ColorType;

typedef enum
{
  NO_STEREO,
  FIELD_RIGHT,
  FIELD_LEFT,
  TWO_WAY_RIGHT_ON_EVEN,
  TWO_WAY_LEFT_ON_EVEN,
  FOUR_WAY_INTERLEAVED,
  SIDE_BY_SIDE
} StereoType;

struct Timing
{
  int width;
  int height;
  int frequency;
};

struct DetailedTiming
{
  int		pixel_clock;
  int		h_addr;
  int		h_blank;
  int		h_sync;
  int		h_front_porch;
  int		v_addr;
  int		v_blank;
  int		v_sync;
  int		v_front_porch;
  int		width_mm;
  int		height_mm;
  int		right_border;
  int		top_border;
  int		interlaced;
  StereoType	stereo;

  int		digital_sync;
  union
  {
    struct
    {
      int bipolar;
      int serrations;
      int sync_on_green;
    } analog;

    struct
    {
      int composite;
      int serrations;
      int negative_vsync;
      int negative_hsync;
    } digital;
  } connector;
};

struct MonitorInfo
{
  int		checksum;
  char		manufacturer_code[4];
  int		product_code;
  unsigned int	serial_number;

  int		production_week;	/* -1 if not specified */
  int		production_year;	/* -1 if not specified */
  int		model_year;		/* -1 if not specified */

  int		major_version;
  int		minor_version;

  int		is_digital;

  union
  {
    struct
    {
      int	bits_per_primary;
      Interface	interface;
      int	rgb444;
      int	ycrcb444;
      int	ycrcb422;
    } digital;

    struct
    {
      double	video_signal_level;
      double	sync_signal_level;
      double	total_signal_level;

      int	blank_to_black;

      int	separate_hv_sync;
      int	composite_sync_on_h;
      int	composite_sync_on_green;
      int	serration_on_vsync;
      ColorType	color_type;
    } analog;
  } connector;

  int		width_mm;		/* -1 if not specified */
  int		height_mm;		/* -1 if not specified */
  double	aspect_ratio;		/* -1.0 if not specififed */

  double	gamma;			/* -1.0 if not specified */

  int		standby;
  int		suspend;
  int		active_off;

  int		srgb_is_standard;
  int		preferred_timing_includes_native;
  int		continuous_frequency;

  double	red_x;
  double	red_y;
  double	green_x;
  double	green_y;
  double	blue_x;
  double	blue_y;
  double	white_x;
  double	white_y;

  Timing	established[24];	/* Terminated by 0x0x0 */
  Timing	standard[8];

  int		n_detailed_timings;
  DetailedTiming detailed_timings[4];	/* If monitor has a preferred
                                         * mode, it is the first one
                                         * (whether it has, is
                                         * determined by the
                                         * preferred_timing_includes
                                         * bit.
                                         */

  /* Optional product description */
  char		dsc_serial_number[14];
  char		dsc_product_name[14];
  char		dsc_string[14];		/* Unspecified ASCII data */
};

MonitorInfo *decode_edid (const uchar *data);

#endif
