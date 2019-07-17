struct PSInput {
	float4 Position : SV_POSITION;
    float4 Colour : COLOR;
};

float4 PS_main(PSInput input) : SV_TARGET
{
    return input.Colour;
}