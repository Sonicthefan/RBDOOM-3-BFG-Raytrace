#pragma once

#include <vector>

struct viewDef_t;
class RtPathTraceInstanceUniverse;
class RtSmokeGeometryUniverse;
struct RtSmokeMaterialStats;

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
    int residencyVisited = 0;
    int residencyDerived = 0;
    int residencyCacheHits = 0;
    int residencyCacheMisses = 0;
    int residencyResidentRecords = 0;
    int residencyEvictedRecords = 0;
};

void DumpEntityFeedStats(const RtPathTraceEntityFeedStats& s);
std::vector<bool> BuildEntityFeedReachableAreas(const viewDef_t* viewDef, int maxDepth, float maxDistance);
void DumpEntityFeedSingleBoneDiagnostics(const viewDef_t* viewDef);
void DumpEntityFeedJointAdvanceProbe(const viewDef_t* viewDef);
void DumpEntityFeedReachableCandidateStats(const viewDef_t* viewDef);
void ProduceEntityFeedRigidEntities(const viewDef_t* viewDef, RtSmokeGeometryUniverse& geometryUniverse, RtPathTraceInstanceUniverse& instanceUniverse, RtSmokeMaterialStats& materialStats);
