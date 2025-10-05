// Inputs
Texture2D<float4> Input : register(t0);
cbuffer params : register(b0)
{
    float fAlbedoSatPower;
    float fAlbedoNorm;
};
// Outputs
RWTexture2D<float4> Output : register(u0);
// Define the thread group size
[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    // Sample a 4x4 block from the input texture
    const int scaleFactor = 2;
    uint2 baseCoord = id.xy * scaleFactor;

    // Accumulate samples from the 4x4 block
    float4 accumulated = float4(0, 0, 0, 0);

    [unroll]
    for (uint y = 0; y < scaleFactor; y++)
    {
        [unroll]
        for (uint x = 0; x < scaleFactor; x++)
        {
            accumulated += Input.Load(uint3(baseCoord + uint2(x, y), 0));
        }
    }

    // Average the samples
    float4 avgPixel = accumulated / (scaleFactor * scaleFactor);

    // Read input color
    float3 color = avgPixel.rgb;
    float3 albedo = pow(max(0.001, color), fAlbedoSatPower * length(color));  // length(color) suppress saturation of darker colors
    albedo = saturate(lerp(albedo, normalize(albedo), fAlbedoNorm));

    // Write final output
    Output[id.xy] = float4(albedo, avgPixel.a);
}
