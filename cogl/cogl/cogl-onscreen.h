/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2011,2012,2013 Intel Corporation.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 *
 *
 * Authors:
 *   Robert Bragg <robert@linux.intel.com>
 */

#if !defined(__COGL_H_INSIDE__) && !defined(COGL_COMPILATION)
#error "Only <cogl/cogl.h> can be included directly."
#endif

#ifndef __COGL_ONSCREEN_H
#define __COGL_ONSCREEN_H

#include <cogl/cogl-context.h>
#include <cogl/cogl-framebuffer.h>
#include <cogl/cogl-frame-info.h>
#include <cogl/cogl-object.h>

#include <glib-object.h>

G_BEGIN_DECLS

typedef struct _CoglOnscreen CoglOnscreen;
#define COGL_ONSCREEN(X) ((CoglOnscreen *)(X))

typedef struct _CoglScanout CoglScanout;

/**
 * cogl_onscreen_get_gtype:
 *
 * Returns: a #GType that can be used with the GLib type system.
 */
COGL_EXPORT
GType cogl_onscreen_get_gtype (void);

/**
 * cogl_onscreen_new: (constructor) (skip)
 * @context: A #CoglContext
 * @width: The desired framebuffer width
 * @height: The desired framebuffer height
 *
 * Instantiates an "unallocated" #CoglOnscreen framebuffer that may be
 * configured before later being allocated, either implicitly when
 * it is first used or explicitly via cogl_framebuffer_allocate().
 *
 * Return value: (transfer full): A newly instantiated #CoglOnscreen framebuffer
 * Since: 1.8
 * Stability: unstable
 */
COGL_EXPORT CoglOnscreen *
cogl_onscreen_new (CoglContext *context, int width, int height);

#ifdef COGL_HAS_X11
/**
 * cogl_x11_onscreen_get_window_xid:
 * @onscreen: A #CoglOnscreen framebuffer
 *
 * Assuming you know the given @onscreen framebuffer is based on an x11 window
 * this queries the XID of that window. If
 * cogl_x11_onscreen_set_foreign_window_xid() was previously called then it
 * will return that same XID otherwise it will be the XID of a window Cogl
 * created internally. If the window has not been allocated yet and a foreign
 * xid has not been set then it's undefined what value will be returned.
 *
 * It's undefined what this function does if called when not using an x11 based
 * renderer.
 *
 * Since: 1.10
 * Stability: unstable
 */
COGL_EXPORT uint32_t
cogl_x11_onscreen_get_window_xid (CoglOnscreen *onscreen);

#endif /* COGL_HAS_X11 */

/**
 * cogl_onscreen_show:
 * @onscreen: The onscreen framebuffer to make visible
 *
 * This requests to make @onscreen visible to the user.
 *
 * Actually the precise semantics of this function depend on the
 * window system currently in use, and if you don't have a
 * multi-windowining system this function may in-fact do nothing.
 *
 * This function will implicitly allocate the given @onscreen
 * framebuffer before showing it if it hasn't already been allocated.
 *
 * When using the Wayland winsys calling this will set the surface to
 * a toplevel type which will make it appear. If the application wants
 * to set a different type for the surface, it can avoid calling
 * cogl_onscreen_show() and set its own type directly with the Wayland
 * client API via cogl_wayland_onscreen_get_surface().
 *
 * <note>Since Cogl doesn't explicitly track the visibility status of
 * onscreen framebuffers it wont try to avoid redundant window system
 * requests e.g. to show an already visible window. This also means
 * that it's acceptable to alternatively use native APIs to show and
 * hide windows without confusing Cogl.</note>
 *
 * Since: 2.0
 * Stability: Unstable
 */
COGL_EXPORT void
cogl_onscreen_show (CoglOnscreen *onscreen);

/**
 * cogl_onscreen_hide:
 * @onscreen: The onscreen framebuffer to make invisible
 *
 * This requests to make @onscreen invisible to the user.
 *
 * Actually the precise semantics of this function depend on the
 * window system currently in use, and if you don't have a
 * multi-windowining system this function may in-fact do nothing.
 *
 * This function does not implicitly allocate the given @onscreen
 * framebuffer before hiding it.
 *
 * <note>Since Cogl doesn't explicitly track the visibility status of
 * onscreen framebuffers it wont try to avoid redundant window system
 * requests e.g. to show an already visible window. This also means
 * that it's acceptable to alternatively use native APIs to show and
 * hide windows without confusing Cogl.</note>
 *
 * Since: 2.0
 * Stability: Unstable
 */
COGL_EXPORT void
cogl_onscreen_hide (CoglOnscreen *onscreen);

/**
 * cogl_onscreen_swap_buffers:
 * @onscreen: A #CoglOnscreen framebuffer
 *
 * Swaps the current back buffer being rendered too, to the front for display.
 *
 * This function also implicitly discards the contents of the color, depth and
 * stencil buffers as if cogl_framebuffer_discard_buffers() were used. The
 * significance of the discard is that you should not expect to be able to
 * start a new frame that incrementally builds on the contents of the previous
 * frame.
 *
 * <note>It is highly recommended that applications use
 * cogl_onscreen_swap_buffers_with_damage() instead whenever possible
 * and also use the cogl_onscreen_get_buffer_age() api so they can
 * perform incremental updates to older buffers instead of having to
 * render a full buffer for every frame.</note>
 *
 * Since: 1.10
 * Stability: unstable
 */
COGL_EXPORT void
cogl_onscreen_swap_buffers (CoglOnscreen *onscreen);


/**
 * cogl_onscreen_get_buffer_age:
 * @onscreen: A #CoglOnscreen framebuffer
 *
 * Gets the current age of the buffer contents.
 *
 * This function allows applications to query the age of the current
 * back buffer contents for a #CoglOnscreen as the number of frames
 * elapsed since the contents were most recently defined.
 *
 * These age values exposes enough information to applications about
 * how Cogl internally manages back buffers to allow applications to
 * re-use the contents of old frames and minimize how much must be
 * redrawn for the next frame.
 *
 * The back buffer contents can either be reported as invalid (has an
 * age of 0) or it may be reported to be the same contents as from n
 * frames prior to the current frame.
 *
 * The queried value remains valid until the next buffer swap.
 *
 * <note>One caveat is that under X11 the buffer age does not reflect
 * changes to buffer contents caused by the window systems. X11
 * applications must track Expose events to determine what buffer
 * regions need to additionally be repaired each frame.</note>
 *
 * The recommended way to take advantage of this buffer age api is to
 * build up a circular buffer of length 3 for tracking damage regions
 * over the last 3 frames and when starting a new frame look at the
 * age of the buffer and combine the damage regions for the current
 * frame with the damage regions of previous @age frames so you know
 * everything that must be redrawn to update the old contents for the
 * new frame.
 *
 * <note>If the system doesn't not support being able to track the age
 * of back buffers then this function will always return 0 which
 * implies that the contents are undefined.</note>
 *
 * <note>The %COGL_FEATURE_ID_BUFFER_AGE feature can optionally be
 * explicitly checked to determine if Cogl is currently tracking the
 * age of #CoglOnscreen back buffer contents. If this feature is
 * missing then this function will always return 0.</note>
 *
 * Return value: The age of the buffer contents or 0 when the buffer
 *               contents are undefined.
 *
 * Since: 1.14
 * Stability: stable
 */
COGL_EXPORT int
cogl_onscreen_get_buffer_age (CoglOnscreen *onscreen);

/**
 * cogl_onscreen_swap_buffers_with_damage:
 * @onscreen: A #CoglOnscreen framebuffer
 * @rectangles: An array of integer 4-tuples representing damaged
 *              rectangles as (x, y, width, height) tuples.
 * @n_rectangles: The number of 4-tuples to be read from @rectangles
 *
 * Swaps the current back buffer being rendered too, to the front for
 * display and provides information to any system compositor about
 * what regions of the buffer have changed (damage) with respect to
 * the last swapped buffer.
 *
 * This function has the same semantics as
 * cogl_framebuffer_swap_buffers() except that it additionally allows
 * applications to pass a list of damaged rectangles which may be
 * passed on to a compositor so that it can minimize how much of the
 * screen is redrawn in response to this applications newly swapped
 * front buffer.
 *
 * For example if your application is only animating a small object in
 * the corner of the screen and everything else is remaining static
 * then it can help the compositor to know that only the bottom right
 * corner of your newly swapped buffer has really changed with respect
 * to your previously swapped front buffer.
 *
 * If @n_rectangles is 0 then the whole buffer will implicitly be
 * reported as damaged as if cogl_onscreen_swap_buffers() had been
 * called.
 *
 * This function also implicitly discards the contents of the color,
 * depth and stencil buffers as if cogl_framebuffer_discard_buffers()
 * were used. The significance of the discard is that you should not
 * expect to be able to start a new frame that incrementally builds on
 * the contents of the previous frame. If you want to perform
 * incremental updates to older back buffers then please refer to the
 * cogl_onscreen_get_buffer_age() api.
 *
 * Whenever possible it is recommended that applications use this
 * function instead of cogl_onscreen_swap_buffers() to improve
 * performance when running under a compositor.
 *
 * <note>It is highly recommended to use this API in conjunction with
 * the cogl_onscreen_get_buffer_age() api so that your application can
 * perform incremental rendering based on old back buffers.</note>
 *
 * Since: 1.16
 * Stability: unstable
 */
COGL_EXPORT void
cogl_onscreen_swap_buffers_with_damage (CoglOnscreen *onscreen,
                                        const int *rectangles,
                                        int n_rectangles);

/**
 * cogl_onscreen_direct_scanout: (skip)
 */
COGL_EXPORT void
cogl_onscreen_direct_scanout (CoglOnscreen *onscreen,
                              CoglScanout  *scanout);

/**
 * cogl_onscreen_swap_region:
 * @onscreen: A #CoglOnscreen framebuffer
 * @rectangles: An array of integer 4-tuples representing rectangles as
 *              (x, y, width, height) tuples.
 * @n_rectangles: The number of 4-tuples to be read from @rectangles
 *
 * Swaps a region of the back buffer being rendered too, to the front for
 * display.  @rectangles represents the region as array of @n_rectangles each
 * defined by 4 sequential (x, y, width, height) integers.
 *
 * This function also implicitly discards the contents of the color, depth and
 * stencil buffers as if cogl_framebuffer_discard_buffers() were used. The
 * significance of the discard is that you should not expect to be able to
 * start a new frame that incrementally builds on the contents of the previous
 * frame.
 *
 * Since: 1.10
 * Stability: unstable
 */
COGL_EXPORT void
cogl_onscreen_swap_region (CoglOnscreen *onscreen,
                           const int *rectangles,
                           int n_rectangles);

/**
 * CoglFrameEvent:
 * @COGL_FRAME_EVENT_SYNC: Notifies that the system compositor has
 *                         acknowledged a frame and is ready for a
 *                         new frame to be created.
 * @COGL_FRAME_EVENT_COMPLETE: Notifies that a frame has ended. This
 *                             is a good time for applications to
 *                             collect statistics about the frame
 *                             since the #CoglFrameInfo should hold
 *                             the most data at this point. No other
 *                             events should be expected after a
 *                             @COGL_FRAME_EVENT_COMPLETE event.
 *
 * Identifiers that are passed to #CoglFrameCallback functions
 * (registered using cogl_onscreen_add_frame_callback()) that
 * mark the progression of a frame in some way which usually
 * means that new information will have been accumulated in the
 * frame's corresponding #CoglFrameInfo object.
 *
 * The last event that will be sent for a frame will be a
 * @COGL_FRAME_EVENT_COMPLETE event and so these are a good
 * opportunity to collect statistics about a frame since the
 * #CoglFrameInfo should hold the most data at this point.
 *
 * <note>A frame may not be completed before the next frame can start
 * so applications should avoid needing to collect all statistics for
 * a particular frame before they can start a new frame.</note>
 *
 * Since: 1.14
 * Stability: unstable
 */
typedef enum _CoglFrameEvent
{
  COGL_FRAME_EVENT_SYNC = 1,
  COGL_FRAME_EVENT_COMPLETE
} CoglFrameEvent;

/**
 * CoglFrameCallback:
 * @onscreen: The onscreen that the frame is associated with
 * @event: A #CoglFrameEvent notifying how the frame has progressed
 * @info: The meta information, such as timing information, about
 *        the frame that has progressed.
 * @user_data: The user pointer passed to
 *             cogl_onscreen_add_frame_callback()
 *
 * Is a callback that can be registered via
 * cogl_onscreen_add_frame_callback() to be called when a frame
 * progresses in some notable way.
 *
 * Please see the documentation for #CoglFrameEvent and
 * cogl_onscreen_add_frame_callback() for more details about what
 * events can be notified.
 *
 * Since: 1.14
 * Stability: unstable
 */
typedef void (*CoglFrameCallback) (CoglOnscreen *onscreen,
                                   CoglFrameEvent event,
                                   CoglFrameInfo *info,
                                   void *user_data);

/**
 * CoglFrameClosure:
 *
 * An opaque type that tracks a #CoglFrameCallback and associated user
 * data. A #CoglFrameClosure pointer will be returned from
 * cogl_onscreen_add_frame_callback() and it allows you to remove a
 * callback later using cogl_onscreen_remove_frame_callback().
 *
 * Since: 1.14
 * Stability: unstable
 */
typedef struct _CoglClosure CoglFrameClosure;

/**
 * cogl_frame_closure_get_gtype:
 *
 * Returns: a #GType that can be used with the GLib type system.
 */
COGL_EXPORT
GType cogl_frame_closure_get_gtype (void);

/**
 * cogl_onscreen_add_frame_callback:
 * @onscreen: A #CoglOnscreen framebuffer
 * @callback: (scope notified) (closure user_data): A callback function to call for frame events
 * @user_data: A private pointer to be passed to @callback
 * @destroy: (allow-none): An optional callback to destroy @user_data
 *           when the @callback is removed or @onscreen is freed.
 *
 * Installs a @callback function that will be called for significant
 * events relating to the given @onscreen framebuffer.
 *
 * The @callback will be used to notify when the system compositor is
 * ready for this application to render a new frame. In this case
 * %COGL_FRAME_EVENT_SYNC will be passed as the event argument to the
 * given @callback in addition to the #CoglFrameInfo corresponding to
 * the frame beeing acknowledged by the compositor.
 *
 * The @callback will also be called to notify when the frame has
 * ended. In this case %COGL_FRAME_EVENT_COMPLETE will be passed as
 * the event argument to the given @callback in addition to the
 * #CoglFrameInfo corresponding to the newly presented frame.  The
 * meaning of "ended" here simply means that no more timing
 * information will be collected within the corresponding
 * #CoglFrameInfo and so this is a good opportunity to analyse the
 * given info. It does not necessarily mean that the GPU has finished
 * rendering the corresponding frame.
 *
 * We highly recommend throttling your application according to
 * %COGL_FRAME_EVENT_SYNC events so that your application can avoid
 * wasting resources, drawing more frames than your system compositor
 * can display.
 *
 * Return value: a #CoglFrameClosure pointer that can be used to
 *               remove the callback and associated @user_data later.
 * Since: 1.14
 * Stability: unstable
 */
COGL_EXPORT CoglFrameClosure *
cogl_onscreen_add_frame_callback (CoglOnscreen *onscreen,
                                  CoglFrameCallback callback,
                                  void *user_data,
                                  CoglUserDataDestroyCallback destroy);

/**
 * cogl_onscreen_remove_frame_callback:
 * @onscreen: A #CoglOnscreen
 * @closure: A #CoglFrameClosure returned from
 *           cogl_onscreen_add_frame_callback()
 *
 * Removes a callback and associated user data that were previously
 * registered using cogl_onscreen_add_frame_callback().
 *
 * If a destroy callback was passed to
 * cogl_onscreen_add_frame_callback() to destroy the user data then
 * this will get called.
 *
 * Since: 1.14
 * Stability: unstable
 */
COGL_EXPORT void
cogl_onscreen_remove_frame_callback (CoglOnscreen *onscreen,
                                     CoglFrameClosure *closure);

/**
 * cogl_onscreen_set_resizable:
 * @onscreen: A #CoglOnscreen framebuffer
 *
 * Lets you request Cogl to mark an @onscreen framebuffer as
 * resizable or not.
 *
 * By default, if possible, a @onscreen will be created by Cogl
 * as non resizable, but it is not guaranteed that this is always
 * possible for all window systems.
 *
 * <note>Cogl does not know whether marking the @onscreen framebuffer
 * is truly meaningful for your current window system (consider
 * applications being run fullscreen on a phone or TV) so this
 * function may not have any useful effect. If you are running on a
 * multi windowing system such as X11 or Win32 or OSX then Cogl will
 * request to the window system that users be allowed to resize the
 * @onscreen, although it's still possible that some other window
 * management policy will block this possibility.</note>
 *
 * <note>Whenever an @onscreen framebuffer is resized the viewport
 * will be automatically updated to match the new size of the
 * framebuffer with an origin of (0,0). If your application needs more
 * specialized control of the viewport it will need to register a
 * resize handler using cogl_onscreen_add_resize_callback() so that it
 * can track when the viewport has been changed automatically.</note>
 *
 * Since: 2.0
 */
COGL_EXPORT void
cogl_onscreen_set_resizable (CoglOnscreen *onscreen,
                             gboolean resizable);

/**
 * cogl_onscreen_get_resizable:
 * @onscreen: A #CoglOnscreen framebuffer
 *
 * Lets you query whether @onscreen has been marked as resizable via
 * the cogl_onscreen_set_resizable() api.
 *
 * By default, if possible, a @onscreen will be created by Cogl
 * as non resizable, but it is not guaranteed that this is always
 * possible for all window systems.
 *
 * <note>If cogl_onscreen_set_resizable(@onscreen, %TRUE) has been
 * previously called then this function will return %TRUE, but it's
 * possible that the current windowing system being used does not
 * support window resizing (consider fullscreen windows on a phone or
 * a TV). This function is not aware of whether resizing is truly
 * meaningful with your window system, only whether the @onscreen has
 * been marked as resizable.</note>
 *
 * Return value: Returns whether @onscreen has been marked as
 *               resizable or not.
 * Since: 2.0
 */
COGL_EXPORT gboolean
cogl_onscreen_get_resizable (CoglOnscreen *onscreen);

/**
 * CoglOnscreenResizeCallback:
 * @onscreen: A #CoglOnscreen framebuffer that was resized
 * @width: The new width of @onscreen
 * @height: The new height of @onscreen
 * @user_data: The private passed to
 *             cogl_onscreen_add_resize_callback()
 *
 * Is a callback type used with the
 * cogl_onscreen_add_resize_callback() allowing applications to be
 * notified whenever an @onscreen framebuffer is resized.
 *
 * <note>Cogl automatically updates the viewport of an @onscreen
 * framebuffer that is resized so this callback is also an indication
 * that the viewport has been modified too</note>
 *
 * <note>A resize callback will only ever be called while dispatching
 * Cogl events from the system mainloop; so for example during
 * cogl_poll_renderer_dispatch(). This is so that callbacks shouldn't
 * occur while an application might have arbitrary locks held for
 * example.</note>
 *
 * Since: 2.0
 */
typedef void (*CoglOnscreenResizeCallback) (CoglOnscreen *onscreen,
                                            int width,
                                            int height,
                                            void *user_data);

/**
 * CoglOnscreenResizeClosure:
 *
 * An opaque type that tracks a #CoglOnscreenResizeCallback and
 * associated user data. A #CoglOnscreenResizeClosure pointer will be
 * returned from cogl_onscreen_add_resize_callback() and it allows you
 * to remove a callback later using
 * cogl_onscreen_remove_resize_callback().
 *
 * Since: 2.0
 * Stability: unstable
 */
typedef struct _CoglClosure CoglOnscreenResizeClosure;

/**
 * cogl_onscreen_resize_closure_get_gtype:
 *
 * Returns: a #GType that can be used with the GLib type system.
 */
COGL_EXPORT
GType cogl_onscreen_resize_closure_get_gtype (void);

/**
 * cogl_onscreen_add_resize_callback:
 * @onscreen: A #CoglOnscreen framebuffer
 * @callback: (scope notified) (closure user_data): A #CoglOnscreenResizeCallback to call when
 *            the @onscreen changes size.
 * @user_data: Private data to be passed to @callback.
 * @destroy: (allow-none): An optional callback to destroy @user_data
 *           when the @callback is removed or @onscreen is freed.
 *
 * Registers a @callback with @onscreen that will be called whenever
 * the @onscreen framebuffer changes size.
 *
 * The @callback can be removed using
 * cogl_onscreen_remove_resize_callback() passing the returned closure
 * pointer.
 *
 * <note>Since Cogl automatically updates the viewport of an @onscreen
 * framebuffer that is resized, a resize callback can also be used to
 * track when the viewport has been changed automatically by Cogl in
 * case your application needs more specialized control over the
 * viewport.</note>
 *
 * <note>A resize callback will only ever be called while dispatching
 * Cogl events from the system mainloop; so for example during
 * cogl_poll_renderer_dispatch(). This is so that callbacks shouldn't
 * occur while an application might have arbitrary locks held for
 * example.</note>
 *
 * Return value: a #CoglOnscreenResizeClosure pointer that can be used to
 *               remove the callback and associated @user_data later.
 * Since: 2.0
 */
COGL_EXPORT CoglOnscreenResizeClosure *
cogl_onscreen_add_resize_callback (CoglOnscreen *onscreen,
                                   CoglOnscreenResizeCallback callback,
                                   void *user_data,
                                   CoglUserDataDestroyCallback destroy);

/**
 * cogl_onscreen_remove_resize_callback:
 * @onscreen: A #CoglOnscreen framebuffer
 * @closure: An identifier returned from cogl_onscreen_add_resize_callback()
 *
 * Removes a resize @callback and @user_data pair that were previously
 * associated with @onscreen via cogl_onscreen_add_resize_callback().
 *
 * Since: 2.0
 */
COGL_EXPORT void
cogl_onscreen_remove_resize_callback (CoglOnscreen *onscreen,
                                      CoglOnscreenResizeClosure *closure);

/**
 * CoglOnscreenDirtyInfo:
 * @x: Left edge of the dirty rectangle
 * @y: Top edge of the dirty rectangle, measured from the top of the window
 * @width: Width of the dirty rectangle
 * @height: Height of the dirty rectangle
 *
 * A structure passed to callbacks registered using
 * cogl_onscreen_add_dirty_callback(). The members describe a
 * rectangle within the onscreen buffer that should be redrawn.
 *
 * Since: 1.16
 * Stability: unstable
 */
typedef struct _CoglOnscreenDirtyInfo CoglOnscreenDirtyInfo;

struct _CoglOnscreenDirtyInfo
{
  int x, y;
  int width, height;
};

/**
 * CoglOnscreenDirtyCallback:
 * @onscreen: The onscreen that the frame is associated with
 * @info: A #CoglOnscreenDirtyInfo struct containing the details of the
 *   dirty area
 * @user_data: The user pointer passed to
 *             cogl_onscreen_add_frame_callback()
 *
 * Is a callback that can be registered via
 * cogl_onscreen_add_dirty_callback() to be called when the windowing
 * system determines that a region of the onscreen window has been
 * lost and the application should redraw it.
 *
 * Since: 1.16
 * Stability: unstable
 */
typedef void (*CoglOnscreenDirtyCallback) (CoglOnscreen *onscreen,
                                           const CoglOnscreenDirtyInfo *info,
                                           void *user_data);

/**
 * CoglOnscreenDirtyClosure:
 *
 * An opaque type that tracks a #CoglOnscreenDirtyCallback and associated
 * user data. A #CoglOnscreenDirtyClosure pointer will be returned from
 * cogl_onscreen_add_dirty_callback() and it allows you to remove a
 * callback later using cogl_onscreen_remove_dirty_callback().
 *
 * Since: 1.16
 * Stability: unstable
 */
typedef struct _CoglClosure CoglOnscreenDirtyClosure;

/**
 * cogl_onscreen_dirty_closure_get_gtype:
 *
 * Returns: a #GType that can be used with the GLib type system.
 */
COGL_EXPORT
GType cogl_onscreen_dirty_closure_get_gtype (void);

/**
 * cogl_onscreen_add_dirty_callback:
 * @onscreen: A #CoglOnscreen framebuffer
 * @callback: (scope notified) (closure user_data): A callback function to call for dirty events
 * @user_data: A private pointer to be passed to @callback
 * @destroy: (allow-none): An optional callback to destroy @user_data when the
 *           @callback is removed or @onscreen is freed.
 *
 * Installs a @callback function that will be called whenever the
 * window system has lost the contents of a region of the onscreen
 * buffer and the application should redraw it to repair the buffer.
 * For example this may happen in a window system without a compositor
 * if a window that was previously covering up the onscreen window has
 * been moved causing a region of the onscreen to be exposed.
 *
 * The @callback will be passed a #CoglOnscreenDirtyInfo struct which
 * decribes a rectangle containing the newly dirtied region. Note that
 * this may be called multiple times to describe a non-rectangular
 * region composed of multiple smaller rectangles.
 *
 * The dirty events are separate from %COGL_FRAME_EVENT_SYNC events so
 * the application should also listen for this event before rendering
 * the dirty region to ensure that the framebuffer is actually ready
 * for rendering.
 *
 * Return value: a #CoglOnscreenDirtyClosure pointer that can be used to
 *               remove the callback and associated @user_data later.
 * Since: 1.16
 * Stability: unstable
 */
COGL_EXPORT CoglOnscreenDirtyClosure *
cogl_onscreen_add_dirty_callback (CoglOnscreen *onscreen,
                                  CoglOnscreenDirtyCallback callback,
                                  void *user_data,
                                  CoglUserDataDestroyCallback destroy);

/**
 * cogl_onscreen_remove_dirty_callback:
 * @onscreen: A #CoglOnscreen
 * @closure: A #CoglOnscreenDirtyClosure returned from
 *           cogl_onscreen_add_dirty_callback()
 *
 * Removes a callback and associated user data that were previously
 * registered using cogl_onscreen_add_dirty_callback().
 *
 * If a destroy callback was passed to
 * cogl_onscreen_add_dirty_callback() to destroy the user data then
 * this will also get called.
 *
 * Since: 1.16
 * Stability: unstable
 */
COGL_EXPORT void
cogl_onscreen_remove_dirty_callback (CoglOnscreen *onscreen,
                                     CoglOnscreenDirtyClosure *closure);

/**
 * cogl_is_onscreen:
 * @object: A #CoglObject pointer
 *
 * Gets whether the given object references a #CoglOnscreen.
 *
 * Return value: %TRUE if the object references a #CoglOnscreen
 *   and %FALSE otherwise.
 * Since: 1.10
 * Stability: unstable
 */
COGL_EXPORT gboolean
cogl_is_onscreen (void *object);

/**
 * cogl_onscreen_get_frame_counter:
 *
 * Gets the value of the framebuffers frame counter. This is
 * a counter that increases by one each time
 * cogl_onscreen_swap_buffers() or cogl_onscreen_swap_region()
 * is called.
 *
 * Return value: the current frame counter value
 * Since: 1.14
 * Stability: unstable
 */
COGL_EXPORT int64_t
cogl_onscreen_get_frame_counter (CoglOnscreen *onscreen);

G_END_DECLS

#endif /* __COGL_ONSCREEN_H */
