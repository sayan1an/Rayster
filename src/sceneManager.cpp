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
	cam.setCamera({ -0.99069f, 0.007035f, 0.135953f, -0.519664f, 
			9.40074e-010f, 0.998664f, -0.0516768f, 0.817007f, 
			-0.136134f, -0.0511957f, -0.989367f, 3.82439f, 
			0, 0, 0, 1 }, 5, 60);
	cam.setAngleIncrement(0.01f);

	auto changeTexCoord = [](Mesh* mesh)
	{
		for (auto& vertex : mesh->vertices)
			vertex.texCoord = glm::vec2(0.5f);
	};

	for (int i = 0; i < 88; i++) {
		std::string meshFile = ROOT + "/models/spaceship/meshes/Mesh0" 
			+ std::string((i < 10) ? "0" : "") + std::to_string(i) + ".obj";
		Mesh* mesh = loadMeshTiny(meshFile.c_str());
		changeTexCoord(mesh);
		model.addMesh(mesh);
	}
	
	// color textures
	model.addLdrTexture(Image2d(1, 1, glm::vec4(0.5f, 0.5f, 0.5f, 1.0f))); // default diffuse color , 0
	model.addLdrTexture(Image2d(1, 1, glm::vec4(1.0f, 1.0f, 1.0f, 1.0f))); // default specular color, 1
	model.addLdrTexture(Image2d(1, 1, glm::vec4(0.578596f, 0.578596f, 0.578596f, 1.0f)));// 2
	model.addLdrTexture(Image2d(1, 1, glm::vec4(0.01f, 0.01f, 0.01f, 1.0f))); // 3
	model.addLdrTexture(Image2d(1, 1, glm::vec4(0.256f, 0.013f, 0.08f, 1.0f))); // 4
	model.addLdrTexture(Image2d(1, 1, glm::vec4(0.034f, 0.014f, 0.008f, 1.0f))); // 5
	model.addLdrTexture(Image2d(1, 1, glm::vec4(0.163f, 0.03f, 0.037f, 1.0f))); // 6
	model.addLdrTexture(Image2d(1, 1, glm::vec4(0.772f, 0.175f, 0.262f, 1.0f))); // 7
	model.addLdrTexture(Image2d(1, 1, glm::vec4(0.025f, 0.025f, 0.025f, 1.0f))); // 8
	model.addLdrTexture(Image2d(1, 1, glm::vec4(0.0f, 0.0f, 0.0f, 1.0f))); // 9
	model.addLdrTexture(Image2d(1, 1, glm::vec4(0.1f, 0.1f, 0.1f, 1.0f))); // 10

	// alpha, intIor, extIor texture
	model.addHdrTexture(Image2d(1, 1, glm::vec4(0.1f, 1.0f, 1.0f, 1.0f), true)); // 0
	model.addHdrTexture(Image2d(1, 1, glm::vec4(0.2f, 1.5f, 1.0f, 1.0f), true)); // 1
	model.addHdrTexture(Image2d(1, 1, glm::vec4(0.4f, 1.5f, 1.0f, 1.0f), true)); // 2
	model.addHdrTexture(Image2d(1, 1, glm::vec4(0.01f, 1.5f, 1.0f, 1.0f), true)); // 3

	
	enum BRDF_TYPE { DIFFUSE, BECKMANN, GGX, DIELECTRIC, AREA };

	std::vector<Material> materials;
	Material m = { "RoughAluminium", 0, 2, 0, GGX };
	materials.push_back(m);
	m = { "RoughSteel", 0, 1, 0, GGX };
	materials.push_back(m);
	m = { "DarkPlastic", 3, 1, 1, BECKMANN };
	materials.push_back(m);
	m = { "PinkLeather", 4, 1, 2, BECKMANN };
	materials.push_back(m);
	m = { "Leather", 5, 1, 2, BECKMANN };
	materials.push_back(m);
	m = { "BrightPinkLeather", 7, 1, 2, BECKMANN };
	materials.push_back(m);
	m = { "Glass", 0, 1, 3, DIELECTRIC };
	materials.push_back(m);
	m = { "DarkRubber", 8, 1, 2, GGX };
	materials.push_back(m);
	m = { "Backdrop", 10, 1, 0, DIFFUSE };
	materials.push_back(m);

	auto addInstance = [&materials, &model](std::string matName, uint32_t meshIdx)
	{
		for (const auto& material : materials) {
			if (material.name.compare(matName) == 0) {
				glm::mat4 tf = glm::identity<glm::mat4>();
				model.addInstance(meshIdx, 
					material.diffuseTextureIdx, material.specularTextureIdx, 
					material.alphaIntExtIorTextureIdx, material.brdfType, tf);
				break;
			}
		}
	};

	addInstance("Backdrop", 50);
	addInstance("RoughAluminium", 42);
	addInstance("Leather", 44);
	addInstance("RoughAluminium", 38);
	addInstance("RoughAluminium", 40);
	addInstance("RoughAluminium", 72);
	addInstance("RoughSteel", 33);
	addInstance("Black", 43);
	addInstance("Leather", 28);
	addInstance("RedLeather", 55);
	addInstance("DarkPlastic", 53);
	addInstance("RedLeather", 36);
	addInstance("PinkLeather", 35);
	addInstance("RedLeather", 31);
	addInstance("PinkLeather", 30);
	addInstance("RoughAluminium", 27);
	addInstance("RoughSteel", 64);
	addInstance("RoughSteel", 58);
	addInstance("Black", 80);
	addInstance("RoughAluminium", 67);
	addInstance("RoughAluminium", 60);
	addInstance("RoughSteel", 26);
	addInstance("RoughSteel", 47);
	addInstance("DarkPlastic", 61);
	addInstance("DarkRubber", 63);
	addInstance("RoughAluminium", 65);
	addInstance("RoughAluminium", 48);
	addInstance("RoughAluminium", 66);
	addInstance("DarkRubber", 68);
	addInstance("RoughSteel", 71);
	addInstance("RoughAluminium", 46);
	addInstance("RoughAluminium", 76);
	addInstance("RoughAluminium", 59);
	addInstance("RoughAluminium", 57);
	addInstance("RoughAluminium", 62);
	addInstance("RoughAluminium", 74);
	addInstance("RoughAluminium", 75);
	addInstance("RoughAluminium", 78);
	addInstance("RoughAluminium", 81);
	addInstance("RoughAluminium", 34);
	addInstance("RoughAluminium", 84);
	addInstance("RoughAluminium", 85);
	addInstance("RoughAluminium", 73);
	addInstance("RoughAluminium", 77);
	addInstance("RoughAluminium", 87);
	addInstance("RoughAluminium", 52);
	addInstance("RoughAluminium", 25);
	addInstance("RoughAluminium", 24);
	addInstance("RoughAluminium", 86);
	addInstance("RoughAluminium", 23);
	addInstance("RoughAluminium", 21);
	addInstance("RoughAluminium", 39);
	addInstance("RoughAluminium", 20);
	addInstance("RoughAluminium", 32);
	addInstance("RoughSteel", 19);
	addInstance("RoughAluminium", 18);
	addInstance("RoughAluminium", 70);
	addInstance("RoughAluminium", 16);
	addInstance("RoughAluminium", 15);
	addInstance("RoughAluminium", 54);
	addInstance("RoughAluminium", 13);
	addInstance("RoughAluminium", 79);
	addInstance("RoughAluminium", 41);
	addInstance("RoughAluminium", 12);
	addInstance("RoughAluminium", 11);
	addInstance("RoughAluminium", 83);
	addInstance("RoughAluminium", 10);
	addInstance("RoughAluminium", 69);
	addInstance("RoughAluminium", 9);
	addInstance("RoughAluminium", 7);
	addInstance("RoughAluminium", 17);
	addInstance("RoughSteel", 6);
	addInstance("RoughAluminium", 37);
	addInstance("RoughSteel", 8);
	addInstance("RoughSteel", 45);
	addInstance("RoughAluminium", 5);
	addInstance("RoughAluminium", 4);
	addInstance("RoughSteel", 49);
	addInstance("RoughSteel", 82);
	addInstance("RoughSteel", 14);
	addInstance("RoughSteel", 3);
	addInstance("RoughAluminium", 2);
	addInstance("RoughSteel", 51);
	addInstance("Glass", 1);
	addInstance("RoughAluminium", 56);
	addInstance("BrightPinkLeather", 0);
	addInstance("RedLeather", 22);
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