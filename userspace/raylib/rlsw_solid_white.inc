/* General fixed-function identity-texture specialization. Check the complete
 * nearest/linear footprint, not just one sample. No cached verdict survives a
 * texture mutation. A failed/expensive check uses the ordinary rasterizer. */
static bool sw_primitive_samples_white(void)
{
#ifdef RLSW_VEX_DISABLE_OPTIMIZATIONS
    return false;
#else
    const sw_texture_t *tex = RLSW.boundTexture;
    if (!tex || !tex->pixels || !tex->readColor || tex->width <= 0 || tex->height <= 0) return false;
    if ((tex->minFilter != SW_NEAREST && tex->minFilter != SW_LINEAR) ||
        (tex->magFilter != SW_NEAREST && tex->magFilter != SW_LINEAR)) return false;
    float u0=INFINITY,v0=INFINITY,u1=-INFINITY,v1=-INFINITY;
    for (int i=0;i<RLSW.primitive.vertexCount;i++) {
        const sw_vertex_t *vertex=&RLSW.primitive.buffer[i];
        float u=vertex->texcoord[0],v=vertex->texcoord[1];
        if (!isfinite(u) || !isfinite(v) || !(vertex->position[3]>0.0f) ||
            vertex->position[3]!=RLSW.primitive.buffer[0].position[3]) return false;
        if(u<u0)u0=u;if(u>u1)u1=u;if(v<v0)v0=v;if(v>v1)v1=v;
    }
    bool linear=tex->minFilter==SW_LINEAR || tex->magFilter==SW_LINEAR;
    float minx=floorf(u0*tex->width-(linear?0.5f:0.0f));
    float miny=floorf(v0*tex->height-(linear?0.5f:0.0f));
    float maxx=floorf(u1*tex->width+(linear?0.5f:0.0f));
    float maxy=floorf(v1*tex->height+(linear?0.5f:0.0f));
    /* In-range coordinates avoid depending on wrap/clamp edge behavior. */
    if (!(minx>=0 && miny>=0 && maxx<tex->width && maxy<tex->height)) return false;
    if ((maxx-minx+1)*(maxy-miny+1)>64) return false;
    for(int y=(int)miny;y<=(int)maxy;y++)for(int x=(int)minx;x<=(int)maxx;x++) {
        float color[4];tex->readColor(color,tex->pixels,(uint32_t)y*tex->width+(uint32_t)x);
        if(color[0]!=1.0f || color[1]!=1.0f || color[2]!=1.0f || color[3]!=1.0f)return false;
    }
    return true;
#endif
}
