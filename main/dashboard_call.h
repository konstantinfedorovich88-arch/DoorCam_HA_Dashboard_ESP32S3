#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    DASH_CALL_IDLE = 0,
    DASH_CALL_RINGING,
    DASH_CALL_ACTIVE,
} dash_call_state_t;

typedef struct {
    dash_call_state_t state;
    uint32_t session_id;
} dash_call_t;

void dash_call_init(dash_call_t *call);
bool dash_call_ring(dash_call_t *call, uint32_t session_id);
bool dash_call_accept(dash_call_t *call, uint32_t session_id);
bool dash_call_reject(dash_call_t *call, uint32_t session_id);
bool dash_call_hangup(dash_call_t *call, uint32_t session_id);
