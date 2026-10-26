#include "gpu/gpu_timer.h"

#include "gpu/gl.h"

void GpuTimerInit(GpuTimer *timer)
{
    *timer = (GpuTimer){ 0 };
    gl.GenQueries(GPU_TIMER_RING, timer->queries);
}

void GpuTimerShutdown(GpuTimer *timer)
{
    if (timer->queries[0] != 0) gl.DeleteQueries(GPU_TIMER_RING, timer->queries);
    *timer = (GpuTimer){ 0 };
}

void GpuTimerBegin(GpuTimer *timer)
{
    // Ring full: skip timing this frame rather than block on the oldest query.
    if (timer->head - timer->tail >= GPU_TIMER_RING) return;
    gl.BeginQuery(GL_TIME_ELAPSED, timer->queries[timer->head%GPU_TIMER_RING]);
    timer->active = true;
}

void GpuTimerEnd(GpuTimer *timer)
{
    if (!timer->active) return;
    gl.EndQuery(GL_TIME_ELAPSED);
    timer->head++;
    timer->active = false;
}

int GpuTimerCollect(GpuTimer *timer, uint64_t *out, int capacity)
{
    int count = 0;
    while ((timer->tail != timer->head) && (count < capacity))
    {
        const GLuint id = timer->queries[timer->tail%GPU_TIMER_RING];
        GLuint available = 0;
        gl.GetQueryObjectuiv(id, GL_QUERY_RESULT_AVAILABLE, &available);
        if (!available) break;

        gl.GetQueryObjectui64v(id, GL_QUERY_RESULT, &out[count++]);
        timer->tail++;
    }
    return count;
}
