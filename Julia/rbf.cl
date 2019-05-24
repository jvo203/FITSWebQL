inline void atomicAdd_l_f(volatile __local float *addr, float val)
{
    union {
        unsigned int u32;
        float f32;
    } next, expected, current;
    current.f32 = *addr;
    do
    {
        expected.f32 = current.f32;
        next.f32 = expected.f32 + val;
        current.u32 = atomic_cmpxchg((volatile __local unsigned int *)addr,
                                     expected.u32, next.u32);
    } while (current.u32 != expected.u32);
}

inline void atomicAdd_g_f(volatile __global float *addr, float val)
{
    union {
        unsigned int u32;
        float f32;
    } next, expected, current;
    current.f32 = *addr;
    do
    {
        expected.f32 = current.f32;
        next.f32 = expected.f32 + val;
        current.u32 = atomic_cmpxchg((volatile __global unsigned int *)addr,
                                     expected.u32, next.u32);
    } while (current.u32 != expected.u32);
}

__kernel void rbf_gradient_pass(__global float *_x1, __global float *_x2, __global float *_y, __global float *_data, __global float *_e, __constant float *c1, __constant float *c2, __constant float *p0, __constant float *p1, __constant float *p2, __constant float *w, __global float *_grad_c1, __global float *_grad_c2, __global float *_grad_p0, __global float *_grad_p1, __global float *_grad_p2, __global float *_grad_w)
{

    __local float tid_grad_c1[NCLUST];
    __local float tid_grad_c2[NCLUST];
    __local float tid_grad_p0[NCLUST];
    __local float tid_grad_p1[NCLUST];
    __local float tid_grad_p2[NCLUST];
    __local float tid_grad_w[NCLUST + 1];

    float grad_c1[NCLUST];
    float grad_c2[NCLUST];
    float grad_p0[NCLUST];
    float grad_p1[NCLUST];
    float grad_p2[NCLUST];
    float grad_w[NCLUST + 1];

    size_t local_index = get_local_id(0);

    if (local_index == 0)
    {
        for (int i = 0; i < NCLUST; i++)
        {
            tid_grad_c1[i] = 0.0;
            tid_grad_c2[i] = 0.0;
            tid_grad_p0[i] = 0.0;
            tid_grad_p1[i] = 0.0;
            tid_grad_p2[i] = 0.0;
        }

        for (int i = 0; i < NCLUST + 1; i++)
            tid_grad_w[i] = 0.0;
    };
    barrier(CLK_LOCAL_MEM_FENCE);

    size_t index = get_global_id(0);

    float x1 = _x1[index];
    float x2 = _x2[index];

    float tmp = w[NCLUST];
    grad_w[NCLUST] = 1.0; //bias

    for (int i = 0; i < NCLUST; i++)
    {
        float a = native_exp(p0[i]);
        float b = p1[i];
        float c = native_exp(p2[i]);

        float tmp1 = (x1 - c1[i]);
        float tmp2 = (x2 - c2[i]);
        float dist = a * tmp1 * tmp1 - 2.0f * b * tmp1 * tmp2 + c * tmp2 * tmp2;
        float act = native_exp(-dist);
        tmp += w[i] * act;

        //gradients
        grad_w[i] = act;
        grad_c1[i] = 2.0 * w[i] * act * (a * tmp1 - b * tmp2);
        grad_c2[i] = 2.0 * w[i] * act * (c * tmp2 - b * tmp1);
        grad_p0[i] = w[i] * act * a * (-tmp1 * tmp1);
        grad_p1[i] = 2.0 * w[i] * act * tmp1 * tmp2;
        grad_p2[i] = w[i] * act * c * (-tmp2 * tmp2);
    }

    float e = tmp - _data[index];
    _y[index] = tmp;
    _e[index] = e;

    //gradients
    for (int i = 0; i < NCLUST + 1; i++)
        atomicAdd_l_f(&(tid_grad_w[i]), e * grad_w[i]);

    for (int i = 0; i < NCLUST; i++)
    {
        atomicAdd_l_f(&(tid_grad_c1[i]), e * grad_c1[i]);
        atomicAdd_l_f(&(tid_grad_c2[i]), e * grad_c2[i]);
        atomicAdd_l_f(&(tid_grad_p0[i]), e * grad_p0[i]);
        atomicAdd_l_f(&(tid_grad_p1[i]), e * grad_p1[i]);
        atomicAdd_l_f(&(tid_grad_p2[i]), e * grad_p2[i]);
    }

    barrier(CLK_LOCAL_MEM_FENCE);
    if (local_index == (get_local_size(0) - 1))
    {
        for (int i = 0; i < NCLUST + 1; i++)
            atomicAdd_g_f(&(_grad_w[i]), tid_grad_w[i]);

        for (int i = 0; i < NCLUST; i++)
        {
            atomicAdd_g_f(&(_grad_c1[i]), tid_grad_c1[i]);
            atomicAdd_g_f(&(_grad_c2[i]), tid_grad_c2[i]);
            atomicAdd_g_f(&(_grad_p0[i]), tid_grad_p0[i]);
            atomicAdd_g_f(&(_grad_p1[i]), tid_grad_p1[i]);
            atomicAdd_g_f(&(_grad_p2[i]), tid_grad_p2[i]);
        }
    }
}