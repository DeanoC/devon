cbuffer uniformBlock : register(b0)
{
    float4x4 ScaleOffsetMatrix;

    float4 colourMask;

    float alphaReplicate;

    int forceMipLevel;

    uint sliceToView;
    uint numSlices;
};

struct FSInput {
    float4 Position : SV_POSITION;
    float2 Uv 	    : TEXCOORD;
    float4 Colour   : COLOR;
};

Texture2D colourTexture : register(t1);
Texture2DArray colourTextureArray : register(t2);

SamplerState pointSampler : register(s0);
SamplerState bilinearSampler : register(s1);

float4 SampleTexture(float2 uv) {
    float4 texSample;

    if(sliceToView > 0 )
    {
        texSample = colourTextureArray.SampleLevel(pointSampler, float3(uv, sliceToView), (float)forceMipLevel);
    } else {
        texSample = colourTexture.SampleLevel(pointSampler, uv, (float)forceMipLevel);
    }

    if(alphaReplicate > 0.5) {
        return float4(texSample.aaa, 1.0);
    } else {
        // if viewing rgba multiple in alpha otherwise just show rgb
        if(colourMask.a > 0.5f) {
            texSample.rgb = texSample.rgb * texSample.a;
        }
        return colourMask * texSample;
    }
}

float4 FS_main(FSInput input) : SV_Target
{
    float4 texSample = SampleTexture(input.Uv);
    return input.Colour * texSample;
}