#include "sceneManager.h"
#include <assimp/Importer.hpp> 

#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"

static const std::vector<std::string> MODEL_PATHS = { ROOT + "/models/chalet.obj", ROOT + "/models/deer.obj", ROOT + "/models/cat.obj" };
static const std::vector<std::string> TEXTURE_PATHS = { ROOT + "/textures/chalet.jpg", ROOT + "/textures/ubiLogo.jpg" };

struct Material
{
	std::string name;
	uint32_t diffuseTextureIdx;
	uint32_t specularTextureIdx;
	uint32_t alphaIntExtIorTextureIdx;
	uint32_t brdfType;
};


static Mesh* loadMeshTiny(const char* meshPath)
{	
	Mesh* mesh = new Mesh();

	tinyobj::attrib_t attrib;
	std::vector<tinyobj::shape_t> shapes;
	std::vector<tinyobj::material_t> materials;
	std::string warn, err;

	if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, meshPath)) {
		throw std::runtime_error(warn + err);
	}

	std::unordered_map<Vertex, uint32_t> uniqueVertices = {};

	for (const auto& shape : shapes) {
		for (const auto& index : shape.mesh.indices) {
			Vertex vertex = {};

			vertex.pos = {
				attrib.vertices[3 * index.vertex_index + 0],
				attrib.vertices[3 * index.vertex_index + 1],
				attrib.vertices[3 * index.vertex_index + 2]
			};

			vertex.normal = {
				attrib.normals[3 * index.normal_index + 0],
				attrib.normals[3 * index.normal_index + 1],
				attrib.normals[3 * index.normal_index + 2]
			};
			
			vertex.texCoord = {
				attrib.texcoords[2 * index.texcoord_index + 0],
				1.0f - attrib.texcoords[2 * index.texcoord_index + 1]
			};

			vertex.color = { 1.0f, 1.0f, 1.0f };

			if (uniqueVertices.count(vertex) == 0) {
				uniqueVertices[vertex] = static_cast<uint32_t>(mesh->vertices.size());
				mesh->vertices.push_back(vertex);
			}

			mesh->indices.push_back(uniqueVertices[vertex]);
		}
	}

	return mesh;
}

static void loadSpaceship(Model& model, Camera& cam)
{
	cam.setCamera({ -0.99069f, 0.007035f, 0.135953f, -0.519664f, 9.40074e-010f, 0.998664f, -0.0516768f, 0.817007f, -0.136134f, -0.0511957f, -0.989367f, 3.82439f, 0, 0, 0, 1 }, 5, 60);

	auto changeTexCoord = [](Mesh* mesh)
	{
		for (auto& vertex : mesh->vertices)
			vertex.texCoord = glm::vec2(0.5f);
	};

	for (int i = 0; i < 88; i++) {
		std::string meshFile = ROOT + "/models/spaceship/meshes/Mesh0" + std::string((i < 10) ? "0" : "") + std::to_string(i) + ".obj";
		Mesh* mesh = loadMeshTiny(meshFile.c_str());
		changeTexCoord(mesh);
		model.addMesh(mesh);
	}
	
	// color textures
	model.addTexture(Image2d(1, 1, glm::vec4(0.5f, 0.5f, 0.5f, 1.0f))); // default diffuse color , 0
	model.addTexture(Image2d(1, 1, glm::vec4(1.0f, 1.0f, 1.0f, 1.0f))); // default specular color, 1
	model.addTexture(Image2d(1, 1, glm::vec4(0.578596f, 0.578596f, 0.578596f, 1.0f)));// 2
	model.addTexture(Image2d(1, 1, glm::vec4(0.01f, 0.01f, 0.01f, 1.0f))); // 3
	model.addTexture(Image2d(1, 1, glm::vec4(0.256f, 0.013f, 0.08f, 1.0f))); // 4
	model.addTexture(Image2d(1, 1, glm::vec4(0.034f, 0.014f, 0.008f, 1.0f))); // 5
	model.addTexture(Image2d(1, 1, glm::vec4(0.163f, 0.03f, 0.037f, 1.0f))); // 6
	model.addTexture(Image2d(1, 1, glm::vec4(0.772f, 0.175f, 0.262f, 1.0f))); // 7
	model.addTexture(Image2d(1, 1, glm::vec4(0.025f, 0.025f, 0.025f, 1.0f))); // 8

	// alpha, intIor, extIor
	model.addTexture(Image2d(1, 1, glm::vec4(0.1f, 1.0f, 1.0f, 1.0f))); // 9
	model.addTexture(Image2d(1, 1, glm::vec4(0.2f, 1.5f, 1.0f, 1.0f))); // 10
	model.addTexture(Image2d(1, 1, glm::vec4(0.4f, 1.5f, 1.0f, 1.0f))); // 11
	model.addTexture(Image2d(1, 1, glm::vec4(0.01f, 1.5f, 1.0f, 1.0f))); // 12

	enum BRDF_TYPE { BECKMANN, GGX, DIELECTRIC };

	std::vector<Material> materials;
	Material m = { "RoughAluminium", 0, 2, 9, GGX };
	materials.push_back(m);
	m = { "RoughSteel", 0, 1, 9, GGX };
	materials.push_back(m);
	m = { "DarkPlastic", 3, 1, 10, BECKMANN };
	materials.push_back(m);
	m = { "PinkLeather", 4, 1, 11, BECKMANN };
	materials.push_back(m);
	m = { "Leather", 5, 1, 11, BECKMANN };
	materials.push_back(m);
	m = { "BrightPinkLeather", 7, 1, 11, BECKMANN };
	materials.push_back(m);
	m = { "Glass", 0, 1, 12, DIELECTRIC };
	materials.push_back(m);
	m = { "DarkRubber", 8, 1, 11, GGX };
	materials.push_back(m);

	for (int i = 0; i < 88; i++) {
		glm::mat4 tf = glm::identity<glm::mat4>();
		model.addInstance(i, i % 9, tf);
	}
}

/*
static void loadDefault(Model &model, Camera &cam)
{
	for (const auto& texturePath : TEXTURE_PATHS)
		model.addTexture(Image2d(texturePath));

	Mesh* mesh = loadMeshTiny(MODEL_PATHS[2].c_str());
	mesh->normailze(0.7f);
	model.addMesh(mesh);
	mesh = loadMesh(MODEL_PATHS[0].c_str());
	mesh->normailze(0.7f);
	model.addMesh(mesh);
	mesh = loadMesh(MODEL_PATHS[1].c_str());
	mesh->normailze(0.7f);
	model.addMesh(mesh);
	
	glm::mat4 tf = glm::translate(glm::identity<glm::mat4>(), glm::vec3(0, 0, 2));
	model.addInstance(2, 0, tf);

	tf = glm::translate(glm::identity<glm::mat4>(), glm::vec3(0, -2, 0));
	model.addInstance(0, 1, tf);

	tf = glm::translate(glm::identity<glm::mat4>(), glm::vec3(2, 0, 0));
	model.addInstance(1, 1, tf);

	tf = glm::translate(glm::identity<glm::mat4>(), glm::vec3(0, 2, 0));
	model.addInstance(0, 0, tf);

	tf = glm::translate(glm::identity<glm::mat4>(), glm::vec3(0, 0, -2));
	model.addInstance(2, 1, tf);

	tf = glm::translate(glm::identity<glm::mat4>(), glm::vec3(-2, 0, 0));
	model.addInstance(1, 0, tf);
}*/

extern void loadScene(Model& model, Camera& cam, const std::string& name)
{	
	loadSpaceship(model, cam);

	/*if (name.compare("default") == 0)
		loadDefault(model, cam);
	if (name.compare("spaceship") == 0)
		loadSpaceship(model, cam);
	else
		throw std::runtime_error("Model not found");
*/}