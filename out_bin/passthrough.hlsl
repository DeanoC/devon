struct VSInput
{
    float3 Position : POSITION;
    float4 Colour : COLOR;
};

struct VSOutput {
	float4 Position : SV_POSITION;
    float4 Colour : COLOR;
};

VSOutput main(VSInput input)
{
    VSOutput result;

    result.Position = float4(input.Position, 1);
    result.Colour = input.Colour;
    return result;
}