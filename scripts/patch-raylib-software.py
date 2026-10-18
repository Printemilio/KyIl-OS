"""Durable, checked adaptation of the vendored RLSW 1.5 renderer copy.
Never writes the original Suchu repository. See pixel differential tests.
"""
import pathlib,sys
root=pathlib.Path(__file__).resolve().parents[1];path=pathlib.Path(sys.argv[1])
s=path.read_text()
def replace(old,new):
    global s
    assert s.count(old)==1,(str(path),old[:70],s.count(old))
    s=s.replace(old,new)
if 'sw_primitive_samples_white' in s:raise SystemExit('RLSW adaptation already applied')
replace('''    int x = u*tex->width;
    int y = v*tex->height;''','''    int x = u*tex->width;
    int y = v*tex->height;
    if (x >= tex->width) x = tex->width - 1;
    if (y >= tex->height) y = tex->height - 1;''')
replace('''    int x0 = (int)xf;
    int y0 = (int)yf;''','''    int x0 = (int)floorf(xf);
    int y0 = (int)floorf(yf);''')
for axis,limit in [('x','wMinus1'),('y','hMinus1')]:
    for index in ('0','1'):
        old=f'        {axis}{index} = ({axis}{index} > tex->{limit})? tex->{limit} : {axis}{index};'
        new=f'        {axis}{index} = {axis}{index} < 0 ? 0 : ({axis}{index} > tex->{limit})? tex->{limit} : {axis}{index};'
        replace(old,new)
replace('static void sw_immediate_push_vertex(const float position[4])',
        (root/'userspace/raylib/rlsw_solid_white.inc').read_text()+'\nstatic void sw_immediate_push_vertex(const float position[4])')
replace('uint32_t state = RLSW.rasterState;',
        'uint32_t state = RLSW.rasterState;\n'
        '        if ((state & SW_STATE_TEXTURE_2D) && sw_primitive_samples_white()) state &= ~SW_STATE_TEXTURE_2D;')
# Alpha dependency alone does not imply that blending opaque vertices is a
# no-op (e.g. SRC_ALPHA,ONE is additive). Only the standard pair permits it.
replace('else if ((RLSW.blendFlags & SW_BLEND_FLAG_NEEDS_ALPHA) && (!RLSW.primitive.hasColorAlpha))',
        'else if (RLSW.srcFactor == SW_SRC_ALPHA && RLSW.dstFactor == SW_ONE_MINUS_SRC_ALPHA && (!RLSW.primitive.hasColorAlpha))')
replace('#define SW_AFFINE_BLOCK 16', '''#if !defined(SW_ENABLE_TEXTURE) && !defined(SW_ENABLE_DEPTH_TEST) && !defined(SW_ENABLE_BLEND) && !defined(RLSW_VEX_DISABLE_OPTIMIZATIONS) && !defined(RLSW_VEX_DISABLE_FLAT_SPANS)
    /* Same coverage and pixel conversion, one conversion for a flat span. */
    if (SW_FRAMEBUFFER_COLOR_SIZE == 4 && dWdx == 0.0f && w != 0.0f && isfinite(w) &&
        dCdx[0] == 0.0f && dCdx[1] == 0.0f && dCdx[2] == 0.0f && dCdx[3] == 0.0f)
    {
        float reciprocal=1.0f/w;
        float src[4]={color[0]*reciprocal,color[1]*reciprocal,color[2]*reciprocal,color[3]*reciprocal};
        uint32_t packed;SW_FRAMEBUFFER_COLOR_SET(&packed,src,0);
        uint32_t *dst=(uint32_t *)cPtr;
        for(int i=0;i<xEnd-xStart;i++)dst[i]=packed;
        return;
    }
#endif
#define SW_AFFINE_BLOCK 16''')
replace('''        float color[4] = { cRow[0], cRow[1], cRow[2], cRow[3] };

        for (int x = xMin; x < xMax; x++)''','''        float color[4] = { cRow[0], cRow[1], cRow[2], cRow[3] };

#if !defined(SW_ENABLE_TEXTURE) && !defined(SW_ENABLE_DEPTH_TEST) && !defined(SW_ENABLE_BLEND) && !defined(RLSW_VEX_DISABLE_OPTIMIZATIONS) && !defined(RLSW_VEX_DISABLE_FLAT_SPANS)
        if (SW_FRAMEBUFFER_COLOR_SIZE == 4 && dCdx[0]==0.0f && dCdx[1]==0.0f && dCdx[2]==0.0f && dCdx[3]==0.0f)
        {
            uint32_t packed;SW_FRAMEBUFFER_COLOR_SET(&packed,color,0);
            uint32_t *dst=(uint32_t *)cPtr;
            for(int i=0;i<xMax-xMin;i++)dst[i]=packed;
        }
        else
#endif
        for (int x = xMin; x < xMax; x++)''')
# A subimage says nothing about alpha elsewhere in the texture. The same-format
# shortcut must also invalidate the original opaque classification.
start=s.index('void swTexSubImage2D(',s.index('void swTexSubImage2D(')+1)
end=s.index('\nvoid swTexParameteri(',start)
body=s[start:end]
assert body.count('    bool alphaFound = false;')==1
body=body.replace('    bool alphaFound = false;\n','')
body=body.replace('            alphaFound |= (color[3] < 1.0f);\n','')
body=body.replace('    RLSW.boundTexture->alpha = alphaFound? SW_PIXELFORMAT_ALPHA[srcPixelFormat] : SW_PIXEL_ALPHA_NONE;\n','')
body=body.replace('    if (srcPixelFormat == RLSW.boundTexture->format)',
    '    RLSW.boundTexture->alpha = SW_PIXELFORMAT_ALPHA[RLSW.boundTexture->format];\n\n'
    '    if (srcPixelFormat == RLSW.boundTexture->format)')
s=s[:start]+body+s[end:]
path.write_text(s)
