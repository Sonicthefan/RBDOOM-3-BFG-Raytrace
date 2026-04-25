struct PathTraceSmokePayload
{
    uint value;
};

[shader("raygeneration")]
void RayGen()
{
}

[shader("miss")]
void Miss(inout PathTraceSmokePayload payload)
{
    payload.value = 0;
}
