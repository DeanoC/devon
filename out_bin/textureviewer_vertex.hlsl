cbuffer uniformBlock : register(b0)
{
    float4x4 ScaleOffsetMatrix;
};

struct VSInput
{
    float2 Position : POSITION;
    float2 Uv 			 : TEXCOORD0;
    float4 Colour   : COLOR;
};

struct VSOutput {
    float4 Position : SV_POSITION;
    float2 Uv 			 : TEXCOORD0;
    float4 Colour   : COLOR;
};

VSOutput VS_main(VSInput input)
{
    VSOutput result;
    result.Position = mul(ScaleOffsetMatrix, float4(input.Position, 0.f, 1.f));
    result.Uv = input.Uv;
    result.Colour = input.Colour;
    return result;
}