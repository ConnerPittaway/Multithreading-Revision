#pragma once

constexpr bool timingMeasurementEnabled = true;
constexpr size_t WorkerCount = 4;
constexpr size_t ChunkSize = 8'000;
constexpr size_t ChunkCount = 100;
constexpr size_t SubsetSize = ChunkSize / WorkerCount;
constexpr size_t LightIterations = 100;
constexpr size_t HeavyIterations = 1'000;

constexpr double ProbabilityHeavy = .05;

static_assert(ChunkSize >= WorkerCount);
static_assert(ChunkSize% WorkerCount == 0);
