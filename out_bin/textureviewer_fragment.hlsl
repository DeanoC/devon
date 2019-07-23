cbuffer uniformBlock : register(b0)
{
    float4x4 ScaleOffsetMatrix;

    float4 colourMask;

    float alphaReplicate;
};

struct FSInput {
    float4 Position : SV_POSITION;
    float2 Uv 	    : TEXCOORD;
    float4 Colour   : COLOR;
};

Texture2D colourTexture : register(t1);
SamplerState pointSampler : register(s0);
SamplerState bilinearSampler : register(s1);

float4 FS_main(FSInput input) : SV_Target
{
    if(alphaReplicate > 0.5) {
        return input.Colour * float4(colourTexture.Sample(pointSampler, input.Uv).aaa, 1.0);
    } else {
        return input.Colour * colourMask * colourTexture.Sample(pointSampler, input.Uv);
    }
}