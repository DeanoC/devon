cbuffer uniformBlock : register(b0)
{
    float4x4 ScaleOffsetMatrix;

    float4 colourMask;

    float alphaReplicate;

    int forceMipLevel;
};

struct FSInput {
    float4 Position : SV_POSITION;
    float2 Uv 	    : TEXCOORD;
    float4 Colour   : COLOR;
};

Texture2D colourTexture : register(t1);
SamplerState pointSampler : register(s0);
SamplerState bilinearSampler : register(s1);

float4 SampleTexture(float2 uv) {
    float4 texSample;

    texSample = colourTexture.SampleLevel(pointSampler, uv, forceMipLevel);

    if(alphaReplicate > 0.5) {
        return float4(texSample.aaa, 1.0);
    } else {
        return colourMask * texSample;
    }
}

float4 FS_main(FSInput input) : SV_Target
{
    float4 texSample = SampleTexture(input.Uv);
    return input.Colour * texSample;
}