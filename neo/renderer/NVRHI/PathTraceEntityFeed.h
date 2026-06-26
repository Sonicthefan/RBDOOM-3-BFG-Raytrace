#pragma once

#include <vector>

struct viewDef_t;

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
std::vector<bool> BuildEntityFeedReachableAreas(const viewDef_t* viewDef, int maxDepth, float maxDistance);
void DumpEntityFeedSingleBoneDiagnostics(const viewDef_t* viewDef);
void DumpEntityFeedJointAdvanceProbe(const viewDef_t* viewDef);
void DumpEntityFeedReachableCandidateStats(const viewDef_t* viewDef);
