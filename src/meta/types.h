/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2008 Iain Holmes
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef META_TYPES_H
#define META_TYPES_H

/**
 * MetaCompositor: (skip)
 *
 */
typedef struct _MetaBackend     MetaBackend;
typedef struct _MetaCompositor  MetaCompositor;
typedef struct _MetaDisplay     MetaDisplay;
typedef struct _MetaX11Display  MetaX11Display;
typedef struct _MetaFrame       MetaFrame;
typedef struct _MetaWindow      MetaWindow;
typedef struct _MetaWorkspace   MetaWorkspace;
/**
 * MetaGroup: (skip)
 *
 */
typedef struct _MetaGroup       MetaGroup;
typedef struct _MetaKeyBinding  MetaKeyBinding;
typedef struct _MetaCursorTracker MetaCursorTracker;

typedef struct _MetaDnd         MetaDnd;
typedef struct _MetaSettings    MetaSettings;

typedef struct _MetaWorkspaceManager MetaWorkspaceManager;
typedef struct _MetaSelection   MetaSelection;

#endif
