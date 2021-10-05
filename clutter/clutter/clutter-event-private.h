#ifndef __CLUTTER_EVENT_PRIVATE_H__
#define __CLUTTER_EVENT_PRIVATE_H__

#include <clutter/clutter-event.h>

G_BEGIN_DECLS

CLUTTER_EXPORT
void            _clutter_event_set_pointer_emulated     (ClutterEvent       *event,
                                                         gboolean            is_emulated);

/* Reinjecting queued events for processing */
CLUTTER_EXPORT
void            _clutter_process_event                  (ClutterEvent       *event);

CLUTTER_EXPORT
gboolean        _clutter_event_process_filters          (ClutterEvent       *event);

/* clears the event queue inside the main context */
void            _clutter_clear_events_queue             (void);
void            _clutter_clear_events_queue_for_stage   (ClutterStage       *stage);

CLUTTER_EXPORT
void            _clutter_event_set_platform_data        (ClutterEvent       *event,
                                                         gpointer            data);
CLUTTER_EXPORT
gpointer        _clutter_event_get_platform_data        (const ClutterEvent *event);

CLUTTER_EXPORT
void            _clutter_event_set_state_full           (ClutterEvent        *event,
							 ClutterModifierType  button_state,
							 ClutterModifierType  base_state,
							 ClutterModifierType  latched_state,
							 ClutterModifierType  locked_state,
							 ClutterModifierType  effective_state);

CLUTTER_EXPORT
void            _clutter_event_push                     (const ClutterEvent *event,
                                                         gboolean            do_copy);

G_END_DECLS

#endif /* __CLUTTER_EVENT_PRIVATE_H__ */
