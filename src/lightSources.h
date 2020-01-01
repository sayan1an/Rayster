#pragma once

#include <vector>
#include "model.hpp"

class DiscretePdf
{
public:
	void add(float value, uint32_t triangleIdx)
	{
		triangleIdxs.push_back(triangleIdx);

		float cumSum = dCdf[dCdf.size() - 1] + value;
		dCdf.push_back(cumSum);

		dCdfNormalized.clear();

		for (const float entry : dCdf)
			dCdfNormalized.push_back(entry / cumSum);
	}

	DiscretePdf()
	{
		dCdf.reserve(100);
		dCdfNormalized.reserve(100);
		dCdf.push_back(0.0f);
		dCdfNormalized.push_back(0.0f);
	}
	
private:
	std::vector<float> dCdf;
	std::vector<float> dCdfNormalized;
	std::vector<uint32_t> triangleIdxs; // MSB 16 bit - instance index, LSB 16 bit primitive index
};

class AreaLightSources
{
public:
	void init(const Model *model)
	{
		CHECK(model->instanceData_static.size() > 0, "AreaLightSources::init() - Model (static instances) are not initialized");
		CHECK(model->materials.size() > 0, "AreaLightSources::init() - Model (Materials) are not initialized");

		uint32_t globalInstanceIdx = 0;
		for (const auto& instance : model->instanceData_static) {
			if (instance.data.x >= 0xffffffff) {
				CHECK(false, "AreaLightSources::init() - Not implemented");
			}
			else {
				uint32_t materialIdx = instance.data.x;
				Material mat = model->materials[materialIdx];

				if (mat.materialType == AREA) {
					uint32_t meshIdx = model->meshPointers[globalInstanceIdx];
					const Mesh* mesh = model->meshes[meshIdx];

					std::cout << "Found one" << std::endl;

				}
			}

			globalInstanceIdx++;
		}

	}
private:
	Model* model;
	DiscretePdf dPdf;
};