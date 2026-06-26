#pragma once

struct RtPathTraceEntityFeedStats
{
    int frameIndex = 0;
    int reachableAreas = 0;
    int candidatesS0 = 0;
    int candidatesS1 = 0;
    int candidatesS2 = 0;
    int candidatesS3 = 0;
    int candidatesS4 = 0;
    int admitted = 0;
    int droppedBudget = 0;
};

void DumpEntityFeedStats(const RtPathTraceEntityFeedStats& s);
