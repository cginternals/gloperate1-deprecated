#version 330

layout (points) in;
layout (triangle_strip, max_vertices = 4) out;

uniform mat4 viewProjection;
uniform vec2 subPixelOffset;

in vec4 v_tangent[];
in vec4 v_bitangent[];

in vec4 v_uvRect[];

in vec4 v_fontColor[];

out vec2 g_uv;
out vec4 g_fontColor;
//flat out vec3 g_normal;

vec4 subpixelShift(in vec4 position, in vec2 subpixelOffset)
{
    vec4 shiftedPos = position;
    shiftedPos.xy /= shiftedPos.w;
    shiftedPos.xy += subpixelOffset;
    shiftedPos.xy *= shiftedPos.w;
    
    return shiftedPos;
}

void main()
{
    vec4 origin = gl_in[0].gl_Position;
    //vec3 normal = normalize(cross(normalize(v_tangent[0].xyz)
    //    , normalize(v_bitangent[0].xyz)));

    // lower right
    gl_Position = origin + v_tangent[0];
    g_uv = v_uvRect[0].zy;
    //g_normal = normal;
    gl_Position = subpixelShift(viewProjection * gl_Position,subPixelOffset) ;
    g_fontColor = v_fontColor[0];

    EmitVertex();
    
    // upper right
    gl_Position = origin + v_bitangent[0] + v_tangent[0];
    g_uv = v_uvRect[0].zw;
    //g_normal = normal;
    gl_Position = subpixelShift(viewProjection * gl_Position,subPixelOffset) ;
    g_fontColor = v_fontColor[0];

    EmitVertex();
    
    // lower left
    gl_Position = origin;
    g_uv = v_uvRect[0].xy;
    //g_normal = normal;
    gl_Position = subpixelShift(viewProjection * gl_Position,subPixelOffset) ;
    g_fontColor = v_fontColor[0];

    EmitVertex();

    // upper left
    gl_Position = origin + v_bitangent[0];
    g_uv = v_uvRect[0].xw;
    //g_normal = normal;
    gl_Position = subpixelShift(viewProjection * gl_Position,subPixelOffset) ;
    g_fontColor = v_fontColor[0];

    EmitVertex();

    EndPrimitive();
}
