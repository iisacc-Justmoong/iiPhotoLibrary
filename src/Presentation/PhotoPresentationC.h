#pragma once
#include "PresentationExport.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct iiphoto_presentation iiphoto_presentation;
typedef struct iiphoto_model_lease iiphoto_model_lease;
typedef struct iiphoto_entry {
    const char *id, *gallery_key, *name, *preview, *preview_revision, *created;
    uint64_t bytes;
    int video;
} iiphoto_entry;
typedef struct iiphoto_flags {
    int active, busy, can_share;
    const char *access, *status;
} iiphoto_flags;
typedef void (*iiphoto_observer)(void *context, int property, const char *row_id);
typedef void (*iiphoto_action)(void *context, int action, const char *const *values, size_t count);

enum iiphoto_property {
    IIPHOTO_ROWS, IIPHOTO_ACTIVE, IIPHOTO_BUSY, IIPHOTO_CAN_SHARE, IIPHOTO_INFORMATION_OPEN,
    IIPHOTO_EMPTY, IIPHOTO_MOTION_ENABLED, IIPHOTO_COMPACT_LAYOUT, IIPHOTO_SELECTED_VIDEO,
    IIPHOTO_SELECTED_BYTES, IIPHOTO_REVEAL_REVISION, IIPHOTO_THUMBNAIL_SIZE,
    IIPHOTO_VIEWPORT_WIDTH, IIPHOTO_VIEWPORT_HEIGHT, IIPHOTO_ACCESS, IIPHOTO_STATUS,
    IIPHOTO_SELECTED_ID, IIPHOTO_SELECTED_NAME, IIPHOTO_SELECTED_PREVIEW,
    IIPHOTO_SELECTED_CREATED, IIPHOTO_REVEAL_KEY,
    IIPHOTO_ROW_GALLERY_KEY, IIPHOTO_ROW_NAME, IIPHOTO_ROW_PREVIEW,
    IIPHOTO_ROW_PREVIEW_REVISION, IIPHOTO_ROW_CREATED, IIPHOTO_ROW_BYTES,
    IIPHOTO_ROW_VIDEO, IIPHOTO_ROW_SELECTED, IIPHOTO_ROW_ID
};
enum iiphoto_action_kind {
    IIPHOTO_ACTION_CONNECT, IIPHOTO_ACTION_PICK_FILES, IIPHOTO_ACTION_REFRESH,
    IIPHOTO_ACTION_ADD_FILES, IIPHOTO_ACTION_OPEN, IIPHOTO_ACTION_COPY,
    IIPHOTO_ACTION_DUPLICATE, IIPHOTO_ACTION_SHARE, IIPHOTO_ACTION_TRASH,
    IIPHOTO_ACTION_PRIORITIZE, IIPHOTO_ACTION_PREVIEW_CHANGED
};
enum iiphoto_command {
    IIPHOTO_CONNECT, IIPHOTO_PICK_FILES, IIPHOTO_REFRESH, IIPHOTO_CLOSE_INFORMATION,
    IIPHOTO_OPEN, IIPHOTO_COPY, IIPHOTO_DUPLICATE, IIPHOTO_SHARE, IIPHOTO_TRASH,
    IIPHOTO_HIDE, IIPHOTO_SHOW
};

/* All calls and model leases require the creating thread. Callback strings and
 * the model pointer are borrowed. Callbacks must not throw across this ABI.
 * Release a model lease only after its native view and active queries retire. */
IIPHOTO_PRESENTATION_EXPORT const char *iiphoto_last_error(void);
IIPHOTO_PRESENTATION_EXPORT iiphoto_presentation *iiphoto_create(void);
IIPHOTO_PRESENTATION_EXPORT int iiphoto_destroy(iiphoto_presentation *);
IIPHOTO_PRESENTATION_EXPORT int iiphoto_observe(iiphoto_presentation *, iiphoto_observer, void *);
/* Row property notifications are opt-in by stable ID. Structural and model
 * notifications continue to use the observer without a row watch. */
IIPHOTO_PRESENTATION_EXPORT int iiphoto_watch_rows(iiphoto_presentation *, const char *const *, size_t);
IIPHOTO_PRESENTATION_EXPORT int iiphoto_set_actions(iiphoto_presentation *, iiphoto_action, void *);
IIPHOTO_PRESENTATION_EXPORT int iiphoto_update(iiphoto_presentation *, const iiphoto_flags *, const iiphoto_entry *, size_t, int *);
IIPHOTO_PRESENTATION_EXPORT int iiphoto_update_flags(iiphoto_presentation *, const iiphoto_flags *, int *);
IIPHOTO_PRESENTATION_EXPORT int iiphoto_change(iiphoto_presentation *, const char *id, const iiphoto_entry *, int *);
IIPHOTO_PRESENTATION_EXPORT int iiphoto_reorder(iiphoto_presentation *, const char *const *, size_t, int *);
IIPHOTO_PRESENTATION_EXPORT int iiphoto_visible_ids(iiphoto_presentation *, const char *const *, size_t);
IIPHOTO_PRESENTATION_EXPORT int iiphoto_drop_files(iiphoto_presentation *, const char *const *, size_t, int *);
IIPHOTO_PRESENTATION_EXPORT int iiphoto_select(iiphoto_presentation *, const char *id, int information);
IIPHOTO_PRESENTATION_EXPORT int iiphoto_command(iiphoto_presentation *, int command);
IIPHOTO_PRESENTATION_EXPORT int iiphoto_resize(iiphoto_presentation *, double width, double height);
IIPHOTO_PRESENTATION_EXPORT int iiphoto_pinch(iiphoto_presentation *, double scale, int *);
IIPHOTO_PRESENTATION_EXPORT int iiphoto_count(iiphoto_presentation *, size_t *);
IIPHOTO_PRESENTATION_EXPORT int iiphoto_text(iiphoto_presentation *, int property, char *, size_t, size_t *);
IIPHOTO_PRESENTATION_EXPORT int iiphoto_boolean(iiphoto_presentation *, int property, int *);
IIPHOTO_PRESENTATION_EXPORT int iiphoto_unsigned(iiphoto_presentation *, int property, uint64_t *);
IIPHOTO_PRESENTATION_EXPORT int iiphoto_number(iiphoto_presentation *, int property, double *);
IIPHOTO_PRESENTATION_EXPORT int iiphoto_row_text(iiphoto_presentation *, size_t row, int property, char *, size_t, size_t *);
IIPHOTO_PRESENTATION_EXPORT int iiphoto_row_boolean(iiphoto_presentation *, size_t row, int property, int *);
IIPHOTO_PRESENTATION_EXPORT int iiphoto_row_unsigned(iiphoto_presentation *, size_t row, int property, uint64_t *);
IIPHOTO_PRESENTATION_EXPORT int iiphoto_acquire_model(iiphoto_presentation *, iiphoto_model_lease **);
IIPHOTO_PRESENTATION_EXPORT int iiphoto_lease_model(iiphoto_model_lease *, void **);
IIPHOTO_PRESENTATION_EXPORT int iiphoto_release_model(iiphoto_model_lease *);

#ifdef __cplusplus
}
#endif
