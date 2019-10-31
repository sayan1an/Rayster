#include "sceneManager.h"

static const std::vector<std::string> MODEL_PATHS = { ROOT + "/models/chalet.obj", ROOT + "/models/deer.obj", ROOT + "/models/cat.obj" };
static const std::vector<std::string> TEXTURE_PATHS = { ROOT + "/textures/chalet.jpg", ROOT + "/textures/ubiLogo.jpg" };


static Mesh* loadMesh(const char* meshPath)
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
				attrib.vertices[3 * (int)index.vertex_index + 0],
				attrib.vertices[3 * (int)index.vertex_index + 1],
				attrib.vertices[3 * (int)index.vertex_index + 2]
			};

			/*
			vertex.normal = {
				attrib.normals[3 * (int)index.normal_index + 0],
				attrib.normals[3 * (int)index.normal_index + 1],
				attrib.normals[3 * (int)index.normal_index + 2]
			};
			*/

			vertex.normal = { 0, 1, 0 };

			vertex.texCoord = {
				attrib.texcoords[2 * (int)index.texcoord_index + 0],
				1.0f - attrib.texcoords[2 * (int)index.texcoord_index + 1]
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

	for (int i = 0; i < 88; i++) {
		std::string meshFile = ROOT + "/models/spaceship/meshes/Mesh0" + std::string((i < 10) ? "0" : "") + std::to_string(i) + ".obj";
		model.addMesh(loadMesh(meshFile.c_str()));
	}

	model.addTexture(Image2d());
	model.addTexture(Image2d());

	for (int i = 0; i < 88; i++) {
		glm::mat4 tf = glm::identity<glm::mat4>();
		model.addInstance(i, 0, tf);
	}
}

static void loadDefault(Model &model, Camera &cam)
{
	for (const auto& texturePath : TEXTURE_PATHS)
		model.addTexture(Image2d(texturePath));

	Mesh* mesh = loadMesh(MODEL_PATHS[2].c_str());
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
}

extern void loadScene(Model& model, Camera& cam, const std::string& name)
{	
	if (name.compare("default") == 0)
		loadDefault(model, cam);
	if (name.compare("spaceship") == 0)
		loadSpaceship(model, cam);
	else
		throw std::runtime_error("Model not found");
}