#pragma once
#include <array>
#include <span>
#include <format>
#include <fstream>

#include "Constants.h"

struct ChunkTimeInfo
{
	std::array<float, WorkerCount> timeSpentWorkingPerThread;
	std::array<size_t, WorkerCount> numberOfHeavyPerThread;
	float totalChunkTime;
};

void WriteCSV(const std::span<const ChunkTimeInfo> timings)
{
	//Output CSV of timings
// work-time, idle-time, number of heavies x workers + total time, total heavies
	std::ofstream csv{ "timings.csv", std::ios_base::trunc };
	for (size_t i = 0; i < WorkerCount; i++)
	{
		csv << std::format("work_{0:}, idle_{0:}, heavy_{0:}, ", i);
	}
	csv << "chunk_time, total_idle, total_heavy\n";

	for (const auto& chunk : timings)
	{
		float totalIdle = 0.f;
		size_t totalHeavy = 0;
		for (size_t i = 0; i < WorkerCount; i++)
		{
			const auto idle = chunk.totalChunkTime - chunk.timeSpentWorkingPerThread[i];
			const auto heavy = chunk.numberOfHeavyPerThread[i];
			csv << std::format("{},{},{},", chunk.timeSpentWorkingPerThread[i], idle, heavy);
			totalIdle += idle;
			totalHeavy += heavy;
		}
		csv << std::format("{},{},{}\n", chunk.totalChunkTime, totalIdle, totalHeavy);
	}
}