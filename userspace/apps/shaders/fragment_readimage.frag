#version 450
layout(set=0,binding=0,rgba8) readonly uniform image2D source;
layout(location=0) out vec4 color;
void main() { color=imageLoad(source,ivec2(gl_FragCoord.xy)); }
