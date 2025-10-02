// Inputs
Texture2D<float4> Input : register(t0);
cbuffer params : register(b0)
{
    float luminanceMultiplier;
};

// Outputs
RWTexture2D<float4> Output : register(u0);

// Define the thread group size
[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    float4 newPixel = Input.Load(uint3(id.xy, 0));

    // luminance
    newPixel.rgb *= luminanceMultiplier;

    Output[id.xy] = newPixel;
}
