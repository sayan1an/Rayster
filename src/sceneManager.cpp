#include "sceneManager.h"
//#include <assimp/Importer.hpp> 
#include <glm/gtc/matrix_transform.hpp>

#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"

//-----------------------------------------------------------------------------
// Extract the directory component from a complete path.
//
#ifdef WIN32
#define CORRECT_PATH_SEP "\\"
#define WRONG_PATH_SEP '/'
#else
#define CORRECT_PATH_SEP "/"
#define WRONG_PATH_SEP '\\'
#endif

struct NamedMaterial : Material 
{
	std::string name;
	NamedMaterial(std::string _name, uint32_t diffIdx, uint32_t specIdx, uint32_t alphaIdx, uint32_t matType)
	{
		name = _name;
		diffuseTextureIdx = diffIdx;
		specularTextureIdx = specIdx;
		alphaIntExtIorTextureIdx = alphaIdx;
		materialType = matType;
	}
};

static std::string get_path(const std::string& file)
{
	std::string dir;
	size_t idx = file.find_last_of("\\/");
	if (idx != std::string::npos)
		dir = file.substr(0, idx);
	if (!dir.empty())
	{
		dir += CORRECT_PATH_SEP;
	}
	return dir;
}

static Mesh* loadMeshTiny(const char* meshPath, bool invertNormal = false)
{	
	std::cout << "Loading Model...";

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

			if (!attrib.normals.empty() && index.normal_index >= 0)
				vertex.normal = {
					attrib.normals[3 * index.normal_index + 0] * (invertNormal ? -1.0f : 1.0f),
					attrib.normals[3 * index.normal_index + 1] * (invertNormal ? -1.0f : 1.0f),
					attrib.normals[3 * index.normal_index + 2] * (invertNormal ? -1.0f : 1.0f)
			};
			else
				vertex.normal = { 0.0f, 1.0f, 0.0f };

			if (!attrib.texcoords.empty() && index.texcoord_index >= 0)
				vertex.texCoord = {
					attrib.texcoords[2 * index.texcoord_index],
					1.0f - attrib.texcoords[2 * index.texcoord_index + 1]
				};
			else 
				vertex.texCoord = glm::vec2(0.5f);

			if (!attrib.colors.empty())
				vertex.color = { 
					attrib.colors[3 * index.vertex_index + 0], 
					attrib.colors[3 * index.vertex_index + 1],
					attrib.colors[3 * index.vertex_index + 2] 
				};
			else
				vertex.color = { 1.0f, 1.0f, 1.0f };

			if (uniqueVertices.count(vertex) == 0) {
				uniqueVertices[vertex] = static_cast<uint32_t>(mesh->vertices.size());
				mesh->vertices.push_back(vertex);
			}

			mesh->indices.push_back(uniqueVertices[vertex]);
		}
	}

	// Compute normal when no normal were provided.
	if (attrib.normals.empty()) {
		for (auto& v : mesh->vertices)
			v.normal = { 0, 0, 0 };

		for (size_t i = 0; i < mesh->indices.size(); i += 3) {
			Vertex& v0 = mesh->vertices[mesh->indices[i + 0]];
			Vertex& v1 = mesh->vertices[mesh->indices[i + 1]];
			Vertex& v2 = mesh->vertices[mesh->indices[i + 2]];

			glm::vec3 n = glm::normalize(glm::cross((v1.pos - v0.pos), (v2.pos - v0.pos)));
			v0.normal += n;
			v1.normal += n;
			v2.normal += n;
		}

		for (auto& v : mesh->vertices)
			v.normal = glm::normalize(v.normal) * (invertNormal ? -1.0f : 1.0f);
	}

	mesh->computeBoundingSphere();
	std::cout << "Done." << std::endl;
	return mesh;
}

static void loadModelTiny(const char* meshPath, const char* materialPath, Model &model, bool normalize = false, float normScale = 1.0f)
{	
	std::cout << "Loading Model...";
	Mesh* mesh = new Mesh();

	tinyobj::attrib_t attrib;
	std::vector<tinyobj::shape_t> shapes;
	std::vector<tinyobj::material_t> materials;
	std::string warn, err;

	if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, meshPath, materialPath)) {
		throw std::runtime_error(warn + err);
	}
	
	uint32_t ldrTexSize = 0;
	uint32_t hdrTexSize = 0;
	if (materials.size() < 1) {
		ldrTexSize = model.addLdrTexture(Image2d(1, 1, glm::vec4(0.5f, 0.5f, 0.5f, 1.0f)));
		hdrTexSize = model.addHdrTexture(Image2d(1, 1, glm::vec4(0.1f, 1.0f, 1.0f, 1.0f), true));
		model.addMaterial(0, 0, 0, 0);
	}

	uint32_t materialSize = 0;

	// Collecting the material in the scene
	for (const auto& material : materials)
	{	
		uint32_t diffuseTexureIdx, specularTextureIdx, alphaIntExtIorIdx;
		if (!material.diffuse_texname.empty())
			diffuseTexureIdx = model.addLdrTexture(Image2d(std::string(materialPath) + material.diffuse_texname));
		else
			diffuseTexureIdx = model.addLdrTexture(Image2d(1, 1, glm::vec4(material.diffuse[0], material.diffuse[1], material.diffuse[2], 1.0f)));

		if (!material.specular_texname.empty())
			specularTextureIdx = model.addLdrTexture(Image2d(materialPath + material.specular_texname));
		else
			specularTextureIdx = model.addLdrTexture(Image2d(1, 1, glm::vec4(material.specular[0], material.specular[1], material.specular[2], 1.0f)));

		if (!material.roughness_texname.empty()) {
			throw std::runtime_error("SceneManager : Roughness texture not yet handled.");
		}
		else
			alphaIntExtIorIdx = model.addHdrTexture(Image2d(1, 1, glm::vec4(std::sqrt(2 / (material.shininess + 2)), material.ior, 1.0f, 1.0f), true));
		
		materialSize = model.addMaterial(diffuseTexureIdx - 1, specularTextureIdx - 1, alphaIntExtIorIdx - 1, 1); // the last 1 corresponds to some-non diffuse material
		ldrTexSize = diffuseTexureIdx + specularTextureIdx;
		hdrTexSize = alphaIntExtIorIdx;
	}

	// Model loader expects multi-layer tex image, add a dummy texture to shut up the model loader 
	if (ldrTexSize < 2)
		model.addLdrTexture(Image2d(1, 1, glm::vec4(0.5f, 0.5f, 0.5f, 1.0f)));
	if (hdrTexSize < 2)
		model.addHdrTexture(Image2d(1, 1, glm::vec4(0.1f, 1.0f, 1.0f, 1.0f), true));
	
	std::unordered_map<Vertex, uint32_t> uniqueVertices = {};
	
	for (const auto& shape : shapes) {
		
		uint32_t faceID = 0;
		int index_cnt = 0;

		for (const auto& index : shape.mesh.indices) {
			Vertex vertex = {};

			vertex.pos = {
				attrib.vertices[3 * index.vertex_index + 0],
				attrib.vertices[3 * index.vertex_index + 1],
				attrib.vertices[3 * index.vertex_index + 2]
			};

			if (!attrib.normals.empty() && index.normal_index >= 0)
				vertex.normal = {
					attrib.normals[3 * index.normal_index + 0],
					attrib.normals[3 * index.normal_index + 1],
					attrib.normals[3 * index.normal_index + 2]
			};
			else
				vertex.normal = { 0.0f, 1.0f, 0.0f };

			if (!attrib.texcoords.empty() && index.texcoord_index >= 0)
				vertex.texCoord = {
					attrib.texcoords[2 * index.texcoord_index],
					1.0f - attrib.texcoords[2 * index.texcoord_index + 1]
			};
			else
				vertex.texCoord = glm::vec2(0.5f);

			if (!attrib.colors.empty())
				vertex.color = {
					attrib.colors[3 * index.vertex_index + 0],
					attrib.colors[3 * index.vertex_index + 1],
					attrib.colors[3 * index.vertex_index + 2]
			};
			else
				vertex.color = { 1.0f, 1.0f, 1.0f };

			vertex.materialIndex = shape.mesh.material_ids[faceID];
			if (vertex.materialIndex < 0 || vertex.materialIndex >= materialSize) {
				vertex.materialIndex = 0;
			}
			
			index_cnt++;
			if (index_cnt >= 3)
			{
				++faceID;
				index_cnt = 0;
			}
			
			if (uniqueVertices.count(vertex) == 0) {
				uniqueVertices[vertex] = static_cast<uint32_t>(mesh->vertices.size());
				mesh->vertices.push_back(vertex);
			}

			mesh->indices.push_back(uniqueVertices[vertex]);
		}
	}

	// Compute normal when no normal were provided.
	if (attrib.normals.empty()) {
		for (auto& v : mesh->vertices)
			v.normal = { 0, 0, 0 };

		for (size_t i = 0; i < mesh->indices.size(); i += 3) {
			Vertex& v0 = mesh->vertices[mesh->indices[i + 0]];
			Vertex& v1 = mesh->vertices[mesh->indices[i + 1]];
			Vertex& v2 = mesh->vertices[mesh->indices[i + 2]];

			glm::vec3 n = glm::normalize(glm::cross((v1.pos - v0.pos), (v2.pos - v0.pos)));
			v0.normal += n;
			v1.normal += n;
			v2.normal += n;
		}

		for (auto& v : mesh->vertices)
			v.normal = glm::normalize(v.normal);
	}

	mesh->computeBoundingSphere();
	if (normalize)
		mesh->normailze(normScale);
	model.addMesh(mesh);
	glm::mat4 tf = glm::identity<glm::mat4>();
	model.addInstance(0, tf);

	std::cout << "Done." << std::endl;
}
/*
static void loadMedievalHouse(Model& model, Camera& cam)
{
	model.addMesh(loadMeshTiny((ROOT +"/models/medievalHouse/medievalHouse.obj").c_str()));
	model.addLdrTexture(Image2d(1, 1, glm::vec4(0.5f, 0.5f, 0.5f, 1.0f)));
	model.addLdrTexture(Image2d(1, 1, glm::vec4(0.5f, 0.5f, 0.5f, 1.0f)));
	model.addHdrTexture(Image2d(1, 1, glm::vec4(0.1f, 1.0f, 1.0f, 1.0f), true));
	model.addHdrTexture(Image2d(1, 1, glm::vec4(0.1f, 1.0f, 1.0f, 1.0f), true));

	model.addMaterial(0, 0, 0, 0);
	glm::mat4 tf = glm::identity<glm::mat4>();
	model.addInstance(0, tf, 0);
}
*/

static void loadMedievalHouse(Model& model, Camera& cam)
{	
	cam.changeKeyFrameFileName(ROOT + "/models/medievalHouse/medievalHouse.bin");
	loadModelTiny((ROOT + "/models/medievalHouse/medievalHouse.obj").c_str(), (ROOT + "/models/medievalHouse/materials/").c_str(), model);
}


static void loadSpaceship(Model& model, Camera& cam)
{
	cam.setCamera({ -0.99069f, 0.007035f, 0.135953f, -0.519664f, 
			9.40074e-010f, 0.998664f, -0.0516768f, 0.817007f, 
			-0.136134f, -0.0511957f, -0.989367f, 3.82439f, 
			0, 0, 0, 1 }, 5, 60);
	cam.setAngleIncrement(0.01f);
	cam.changeKeyFrameFileName(ROOT + "/models/spaceship/spaceship.bin");

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

	Mesh* mesh = loadMeshTiny((ROOT + "/models/spaceship/meshes/quad.obj").c_str(), true);
	//changeTexCoord(mesh);
	uint32_t quadLightIndex = model.addMesh(mesh) - 1;
		
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
	model.addLdrTexture(Image2d(ROOT + "/models/spaceship/light.jpg"));

	// alpha, intIor, extIor texture
	model.addHdrTexture(Image2d(1, 1, glm::vec4(0.1f, 1.0f, 1.0f, 1.0f), true)); // 0
	model.addHdrTexture(Image2d(1, 1, glm::vec4(0.2f, 1.5f, 1.0f, 1.0f), true)); // 1
	model.addHdrTexture(Image2d(1, 1, glm::vec4(0.4f, 1.5f, 1.0f, 1.0f), true)); // 2
	model.addHdrTexture(Image2d(1, 1, glm::vec4(0.01f, 1.5f, 1.0f, 1.0f), true)); // 3
	
	std::vector<NamedMaterial> materials;
	NamedMaterial m = { "RoughAluminium", 0, 2, 0, GGX };
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
	m = { "Glass", 10, 1, 3, DIELECTRIC };
	materials.push_back(m);
	m = { "DarkRubber", 8, 1, 2, GGX };
	materials.push_back(m);
	m = { "Backdrop", 10, 1, 0, DIFFUSE };
	materials.push_back(m);
	m = { "AreaLight", 11, 1, 0, AREA };
	materials.push_back(m);

	for (const auto& material : materials)
		model.addMaterial(material.diffuseTextureIdx, material.specularTextureIdx, material.alphaIntExtIorTextureIdx, material.materialType);

	auto addInstance = [&materials, &model](std::string matName, uint32_t meshIdx, float scale = 1.0f, float translate = 0.0f, uint32_t radiance = 0)
	{	
		uint32_t matIdx = 0;
		for (const auto& material : materials) {
			if (material.name.compare(matName) == 0) {
				glm::mat4 tf = glm::identity<glm::mat4>();
				tf = glm::translate<float>(tf, glm::vec3(0.0, translate, 0.0));
				tf = glm::scale(tf, glm::vec3(scale));
				model.addInstance(meshIdx, tf, matIdx, radiance);
				break;
			}
			matIdx++;
		}
	};
	
	addInstance("Backdrop", 50);
	addInstance("RoughAluminium", 42);
	addInstance("Leather", 44);
	addInstance("RoughAluminium", 38);
	addInstance("RoughAluminium", 40);
	addInstance("RoughAluminium", 72);
	addInstance("RoughSteel", 33);
	//addInstance("AreaLight", 43); // screen inside spaceship
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
	//addInstance("AreaLight", 80, 1.0f, 0.0f, 1); // small headlight
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
	addInstance("AreaLight", quadLightIndex, 0.5f, 2.0f, 1);
}

static void loadDefault(Model &model, Camera &cam)
{	
	cam.changeKeyFrameFileName(ROOT + "/models/default/defailt.bin");

	const std::vector<std::string> MODEL_PATHS = { ROOT + "/models/default/meshes/chalet.obj", ROOT + "/models/default/meshes/deer.obj", ROOT + "/models/default/meshes/cat.obj" };
	const std::vector<std::string> TEXTURE_PATHS = { ROOT + "/models/default/textures/chalet.jpg", ROOT + "/models/default/textures/ubiLogo.jpg" };
	
	for (const auto& texturePath : TEXTURE_PATHS)
		model.addLdrTexture(Image2d(texturePath));

	model.addHdrTexture(Image2d(1, 1, glm::vec4(0.1f, 1.0f, 1.0f, 1.0f), true));
	model.addHdrTexture(Image2d(1, 1, glm::vec4(0.1f, 1.0f, 1.0f, 1.0f), true)); // Need at least two textures, otherwise validation layer may complaint

	Mesh* mesh = loadMeshTiny(MODEL_PATHS[0].c_str());
	mesh->normailze(0.7f);
	model.addMesh(mesh);
	mesh = loadMeshTiny(MODEL_PATHS[1].c_str());
	mesh->normailze(0.7f);
	model.addMesh(mesh);
	mesh = loadMeshTiny(MODEL_PATHS[2].c_str());
	mesh->normailze(0.7f);
	model.addMesh(mesh);

	model.addMaterial(1, 1, 0, 0);
	model.addMaterial(0, 0, 0, 0);
	
	glm::mat4 tf = glm::translate(glm::identity<glm::mat4>(), glm::vec3(0, 0, 2));
	model.addInstance(2, tf, 0);

	tf = glm::translate(glm::identity<glm::mat4>(), glm::vec3(0, -2, 0));
	model.addInstance(0, tf, 1);

	tf = glm::translate(glm::identity<glm::mat4>(), glm::vec3(2, 0, 0));
	model.addInstance(1, tf, 0);

	tf = glm::translate(glm::identity<glm::mat4>(), glm::vec3(0, 2, 0));
	model.addInstance(0, tf, 1);

	tf = glm::translate(glm::identity<glm::mat4>(), glm::vec3(0, 0, -2));
	model.addInstance(2, tf, 0);

	tf = glm::translate(glm::identity<glm::mat4>(), glm::vec3(-2, 0, 0));
	model.addInstance(1, tf, 0);
}

static void loadBasicShapes(Model& model, Camera &cam)
{	
	cam.setCamera({ -0.99069f, 0.007035f, 0.135953f, -0.519664f,
			9.40074e-010f, 0.998664f, -0.0516768f, 0.817007f,
			-0.136134f, -0.0511957f, -0.989367f, 3.82439f,
			0, 0, 0, 1 }, 5, 60);
	cam.changeKeyFrameFileName(ROOT + "/models/modelLibrary/basicShapes.bin");
	cam.setAngleIncrement(0.01f);
	cam.setDistanceIncrement(0.01f);

	model.addLdrTexture(Image2d(1, 1, glm::vec4(0.5f, 0.5f, 0.5f, 1.0f))); // 0
	//model.addLdrTexture(Image2d(1, 1, glm::vec4(0.0f, 0.0f, 0.0f, 1.0f)));
	model.addLdrTexture(Image2d(1, 1, glm::vec4(0.929f, 0.333f, 0.231f, 1.0f))); // 1
	model.addLdrTexture(Image2d(1, 1, glm::vec4(0.125f, 0.388f, 0.608f, 1.0f))); // 2
	model.addLdrTexture(Image2d(1, 1, glm::vec4(0.235f, 0.682f, 0.639f, 1.0f))); // 3
	model.addLdrTexture(Image2d(1, 1, glm::vec4(0.2f, 0.2f, 0.2f, 1.0f))); // 4
	model.addLdrTexture(Image2d(1, 1, glm::vec4(0.0929f, 0.0333f, 0.0231f, 1.0f))); // 5
	//model.addLdrTexture(Image2d(1, 1, glm::vec4(1.f, 1.f, 1.f, 1.0f)));

	model.addHdrTexture(Image2d(1, 1, glm::vec4(0.4f, 1.5f, 1.0f, 1.0f), true)); // 0
	model.addHdrTexture(Image2d(1, 1, glm::vec4(0.05f, 1.5f, 1.0f, 1.0f), true)); // 1

	model.addMaterial(0, 5, 0, GGX); // floor
	model.addMaterial(1, 4, 1, GGX); // urchin
	model.addMaterial(2, 4, 1, GGX); // sphere
	model.addMaterial(3, 4, 1, GGX); // cube
	model.addMaterial(0, 5, 0, AREA); // quadLight

	Mesh* mesh = loadMeshTiny((ROOT + "/models/modelLibrary/groundPlane.obj").c_str());
	mesh->normailze(6.0f);
	model.addMesh(mesh);

	mesh = loadMeshTiny((ROOT + "/models/modelLibrary/basic-shapes/cube/cube.obj").c_str());
	mesh->normailze(0.5f);
	model.addMesh(mesh);

	mesh = loadMeshTiny((ROOT + "/models/modelLibrary/basic-shapes/sphere/sphere.obj").c_str());
	mesh->normailze(0.5f);
	model.addMesh(mesh);

	mesh = loadMeshTiny((ROOT + "/models/modelLibrary/animals/urchin/urchin.obj").c_str());
	mesh->normailze(0.5f);
	model.addMesh(mesh);

	mesh = loadMeshTiny((ROOT + "/models/modelLibrary/quadLight.obj").c_str());
	mesh->normailze(1.25f);
	model.addMesh(mesh);
	
	glm::mat4 tf = glm::identity<glm::mat4>();
	model.addInstance(0, tf, 0);

	tf = glm::translate(glm::identity<glm::mat4>(), glm::vec3(0, 1.0f, 0));
	model.addInstance(1, tf, 3);

	tf = glm::translate(glm::identity<glm::mat4>(), glm::vec3(-1.6f, 1.2f, 0));
	model.addInstance(2, tf, 2);

	tf = glm::translate(glm::identity<glm::mat4>(), glm::vec3(1.6f, 1.2f, 0));
	model.addInstance(3, tf, 1);

	tf = glm::translate(glm::identity<glm::mat4>(), glm::vec3(0.0f, 4.5f, 0));
	model.addInstance(4, tf, 4, 7);

	//tf = glm::translate(glm::identity<glm::mat4>(), glm::vec3(4.5f, 4.5f, 0));
	//model.addInstance(4, tf, 4, 7);
}

static void loadMcMcTest(Model& model, Camera& cam)
{
	cam.setCamera({ -0.99069f, 0.007035f, 0.135953f, -0.519664f,
			9.40074e-010f, 0.998664f, -0.0516768f, 0.817007f,
			-0.136134f, -0.0511957f, -0.989367f, 3.82439f,
			0, 0, 0, 1 }, 5, 60);
	cam.changeKeyFrameFileName(ROOT + "/models/modelLibrary/mcmcTest.bin");
	cam.setAngleIncrement(0.01f);
	cam.setDistanceIncrement(0.01f);

	model.addLdrTexture(Image2d(1, 1, glm::vec4(1.0f, 1.0f, 1.0f, 1.0f))); // 0
	model.addLdrTexture(Image2d(1, 1, glm::vec4(0.929f, 0.333f, 0.231f, 1.0f))); // 1
	model.addLdrTexture(Image2d(1, 1, glm::vec4(0.125f, 0.388f, 0.608f, 1.0f))); // 2
	model.addLdrTexture(Image2d(1, 1, glm::vec4(0.235f, 0.682f, 0.639f, 1.0f))); // 3
	model.addLdrTexture(Image2d(1, 1, glm::vec4(0.0f, 0.0f, 0.0f, 1.0f))); // 4
	model.addLdrTexture(Image2d(1, 1, glm::vec4(0.0f, 0.0f, 0.0f, 1.0f))); // 5
	model.addLdrTexture(Image2d(1, 1, glm::vec4(1.f, 1.f, 1.f, 1.0f))); // 6

	model.addHdrTexture(Image2d(1, 1, glm::vec4(0.01f, 1.5f, 1.0f, 1.0f), true)); // 0
	model.addHdrTexture(Image2d(1, 1, glm::vec4(0.05f, 1.5f, 1.0f, 1.0f), true)); // 1

	model.addMaterial(0, 5, 0, GGX); // floor
	model.addMaterial(1, 4, 1, GGX); // urchin
	model.addMaterial(2, 4, 1, GGX); // sphere
	model.addMaterial(3, 4, 1, GGX); // cube
	model.addMaterial(6, 5, 0, AREA); // quadLight

	Mesh* mesh = loadMeshTiny((ROOT + "/models/modelLibrary/groundPlane.obj").c_str());
	mesh->normailze(6.0f);
	model.addMesh(mesh);

	mesh = loadMeshTiny((ROOT + "/models/modelLibrary/basic-shapes/cube/cube.obj").c_str());
	mesh->normailze(0.5f);
	model.addMesh(mesh);

	mesh = loadMeshTiny((ROOT + "/models/modelLibrary/basic-shapes/sphere/sphere.obj").c_str());
	mesh->normailze(0.5f);
	model.addMesh(mesh);

	mesh = loadMeshTiny((ROOT + "/models/modelLibrary/animals/urchin/urchin.obj").c_str());
	mesh->normailze(0.5f);
	model.addMesh(mesh);

	mesh = loadMeshTiny((ROOT + "/models/modelLibrary/triLight.obj").c_str());
	mesh->normailze(1.25f);
	model.addMesh(mesh);

	glm::mat4 tf = glm::identity<glm::mat4>();
	model.addInstance(0, tf, 0);

	//tf = glm::translate(glm::identity<glm::mat4>(), glm::vec3(0, 1.0f, 0));
	//model.addInstance(1, tf, 3);

	//tf = glm::translate(glm::identity<glm::mat4>(), glm::vec3(-1.6f, 1.2f, 0));
	//model.addInstance(2, tf, 2);

	//tf = glm::translate(glm::identity<glm::mat4>(), glm::vec3(1.6f, 1.2f, 0));
	//model.addInstance(3, tf, 1);

	tf = glm::translate(glm::identity<glm::mat4>(), glm::vec3(0.0f, 4.5f, 0));
	model.addInstance(4, tf, 4, 7);

	tf = glm::scale(glm::translate(glm::identity<glm::mat4>(), glm::vec3(-3.5f, 4.5f, 0)), glm::vec3(0.5f, 0.5f, 0.5f));
	model.addInstance(4, tf, 4, 28);

	//tf = glm::translate(glm::identity<glm::mat4>(), glm::vec3(4.5f, 4.5f, 0));
	//model.addInstance(4, tf, 4, 7);
}

extern void loadScene(Model& model, Camera& cam, const std::string& name)
{	
	//loadMedievalHouse(model, cam);
	//loadBasicShapes(model, cam);
	loadSpaceship(model, cam);
	//loadMcMcTest(model, cam);
	//loadDefault(model, cam);

	/*if (name.compare("default") == 0)
		loadDefault(model, cam);
	if (name.compare("spaceship") == 0)
		loadSpaceship(model, cam);
	else
		throw std::runtime_error("Model not found");
*/}