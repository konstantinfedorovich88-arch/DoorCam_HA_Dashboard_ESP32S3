#include "dashboard_call.h"

void dash_call_init(dash_call_t *call)
{
    if (!call) return;
    call->state = DASH_CALL_IDLE;
    call->session_id = 0;
}

bool dash_call_ring(dash_call_t *call, uint32_t id)
{
    if (!call || !id || call->state != DASH_CALL_IDLE) return false;
    call->session_id = id;
    call->state = DASH_CALL_RINGING;
    return true;
}

bool dash_call_accept(dash_call_t *call, uint32_t id)
{
    if (!call || call->state != DASH_CALL_RINGING || call->session_id != id) return false;
    call->state = DASH_CALL_ACTIVE;
    return true;
}

bool dash_call_reject(dash_call_t *call, uint32_t id)
{
    if (!call || call->state != DASH_CALL_RINGING || call->session_id != id) return false;
    dash_call_init(call);
    return true;
}

bool dash_call_hangup(dash_call_t *call, uint32_t id)
{
    if (!call || call->state == DASH_CALL_IDLE || call->session_id != id) return false;
    dash_call_init(call);
    return true;
}
