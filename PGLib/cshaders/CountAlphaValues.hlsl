// Global Registers
Texture2D<float4> Input : register(t0);
RWStructuredBuffer<uint> Output : register(u0);

// Define the thread group size
[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    float4 pixel = Input.Load(uint3(id.xy, 0)) * 255;

    if (pixel.r >= 4) {
        InterlockedAdd(Output[0], 1);
    }

    if (pixel.g >= 4) {
        InterlockedAdd(Output[1], 1);
    }

    if (pixel.b >= 4) {
        InterlockedAdd(Output[2], 1);
    }

    if (pixel.a > 254) {
        InterlockedAdd(Output[3], 1);
    }
}
