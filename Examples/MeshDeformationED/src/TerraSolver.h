#pragma once

#include <cassert>

#include "cutil.h"

extern "C" {
#include "Opt.h"
}

struct float9 {
	float array[9];
};
struct float12 {
	float array[12];
};

extern "C" void convertToFloat12(const float3* src0, const float9* src1, float12* target, unsigned int numVars);
extern "C" void convertFromFloat12(const float12* source, float3* tar0, float9* tar1, unsigned int numVars);


template <class type> type* createDeviceBuffer(const std::vector<type>& v) {
	type* d_ptr;
	cutilSafeCall(cudaMalloc(&d_ptr, sizeof(type)*v.size()));

	cutilSafeCall(cudaMemcpy(d_ptr, v.data(), sizeof(type)*v.size(), cudaMemcpyHostToDevice));
	return d_ptr;
}

class TerraSolver {

	int* d_headX;
	int* d_headY;

	int* d_tailX;
	int* d_tailY;

	int edgeCount;

public:
	TerraSolver(unsigned int vertexCount, unsigned int E, const int* d_xCoords, const int* d_offsets, const std::string& terraFile, const std::string& optName) : 
		m_optimizerState(nullptr), m_problem(nullptr), m_plan(nullptr)
	{
		edgeCount = (int)E;
		m_optimizerState = Opt_NewState();
		m_problem = Opt_ProblemDefine(m_optimizerState, terraFile.c_str(), optName.c_str(), NULL);

		std::vector<int> yCoords;

		for (int y = 0; y < (int)edgeCount; ++y) {
			yCoords.push_back(0);
		}

		d_headY = createDeviceBuffer(yCoords);
		d_tailY = createDeviceBuffer(yCoords);

		int* h_offsets = (int*)malloc(sizeof(int)*(vertexCount + 1));
		cutilSafeCall(cudaMemcpy(h_offsets, d_offsets, sizeof(int)*(vertexCount + 1), cudaMemcpyDeviceToHost));

		int* h_xCoords = (int*)malloc(sizeof(int)*(edgeCount + 1));
		cutilSafeCall(cudaMemcpy(h_xCoords, d_xCoords, sizeof(int)*(edgeCount), cudaMemcpyDeviceToHost));
		h_xCoords[edgeCount] = vertexCount;

		// Convert to our edge format
		std::vector<int> h_headX;
		std::vector<int> h_tailX;
		for (int headX = 0; headX < (int)vertexCount; ++headX) {
			for (int j = h_offsets[headX]; j < h_offsets[headX + 1]; ++j) {
				h_headX.push_back(headX);
				h_tailX.push_back(h_xCoords[j]);
			}
		}

		d_headX = createDeviceBuffer(h_headX);
		d_tailX = createDeviceBuffer(h_tailX);



		uint32_t stride = vertexCount * sizeof(float3);
		uint32_t strides[] = { stride*4, stride, stride };
		uint32_t elemsizes[] = { sizeof(float12), sizeof(float3), sizeof(float3) };
		uint32_t dims[] = { vertexCount, 1 };

		m_plan = Opt_ProblemPlan(m_optimizerState, m_problem, dims, elemsizes, strides);

		assert(m_optimizerState);
		assert(m_problem);
		assert(m_plan);


		m_numUnknown = vertexCount;
		cutilSafeCall(cudaMalloc(&d_unknowns, sizeof(float12)*vertexCount));
	}

	~TerraSolver()
	{
		cutilSafeCall(cudaFree(d_headX));
		cutilSafeCall(cudaFree(d_headY));
		cutilSafeCall(cudaFree(d_tailX));
		cutilSafeCall(cudaFree(d_tailY));

		if (m_plan) {
			Opt_PlanFree(m_optimizerState, m_plan);
		}

		if (m_problem) {
			Opt_ProblemDelete(m_optimizerState, m_problem);
		}

		cutilSafeCall(cudaFree(d_unknowns));
	}

	//void solve(float3* d_unknown, float3* d_target, unsigned int nNonLinearIterations, unsigned int nLinearIterations, unsigned int nBlockIterations, float weightFit, float weightReg)
	void solveGN(
		float3* d_vertexPosFloat3,
		float9* d_rotsFloat9,
		float3* d_vertexPosFloat3Urshape,
		float3* d_vertexPosTargetFloat3,
		unsigned int nNonLinearIterations,
		unsigned int nLinearIterations,
		float weightFit,
		float weightReg,
		float weightRot)
	{
		unsigned int nBlockIterations = 1;	//invalid just as a dummy;

		convertToFloat12(d_vertexPosFloat3, d_rotsFloat9, d_unknowns, m_numUnknown);

		void* data[] = { d_unknowns, d_vertexPosFloat3Urshape, d_vertexPosTargetFloat3};
		void* solverParams[] = { &nNonLinearIterations, &nLinearIterations, &nBlockIterations };

		float weightFitSqrt = sqrt(weightFit);
		float weightRegSqrt = sqrt(weightReg);
		float weightRotSqrt = sqrt(weightRot);
		void* problemParams[] = { &weightFitSqrt, &weightRegSqrt, &weightRotSqrt };

		int32_t* xCoords[] = { d_headX, d_tailX };
		int32_t* yCoords[] = { d_headY, d_tailY };
		int32_t edgeCounts[] = { edgeCount };
		Opt_ProblemSolve(m_optimizerState, m_plan, data, edgeCounts, NULL, xCoords, yCoords, problemParams, solverParams);

		convertFromFloat12(d_unknowns, d_vertexPosFloat3, d_rotsFloat9, m_numUnknown);
	}

private:
	OptState*	m_optimizerState;
	Problem*	m_problem;
	Plan*		m_plan;

	unsigned int m_numUnknown;
	float12*		d_unknowns;	//float3 (vertices) + float3 (angles)
};
