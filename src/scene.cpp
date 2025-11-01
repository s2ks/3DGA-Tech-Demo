#include <fmt/format.h>
#include <assert.h>

#include "scene.h"
#include "mesh.h"

Scene::Scene() {
	glGenBuffers(1, &this->tri_ubo);
	glGenBuffers(1, &this->bvh_ubo);

	//this->quadMesh = GPUMesh::createFullscreenQuad();

	// Define a fullscreen quad
	this->quadMesh.vertices = {
		// positions        // normals       // texCoords
		{{-1.0f, -1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
		{{ 1.0f, -1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
		{{ 1.0f,  1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},
		{{-1.0f,  1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}},
	};
	this->quadMesh.triangles = {
		{0, 1, 2},
		{2, 3, 0}
	};
	this->quadMesh.material = Material(); // Default material

    // Create VAO and bind it so subsequent creations of VBO and IBO are bound to this VAO
    glGenVertexArrays(1, &this->vao);
    glBindVertexArray(this->vao);

    // Create vertex buffer object (VBO)
    glGenBuffers(1, &this->vbo);
    glBindBuffer(GL_ARRAY_BUFFER, this->vbo);
    glBufferData(
		GL_ARRAY_BUFFER,
		static_cast<GLsizeiptr>(this->quadMesh.vertices.size() *
			sizeof(decltype(this->quadMesh.vertices)::value_type)),
		this->quadMesh.vertices.data(),
		GL_STATIC_DRAW);

    // Create index buffer object (IBO)
    glGenBuffers(1, &this->ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, this->ibo);
    glBufferData(
		GL_ELEMENT_ARRAY_BUFFER,
		static_cast<GLsizeiptr>(this->quadMesh.triangles.size() *
			sizeof(decltype(this->quadMesh.triangles)::value_type)),
		this->quadMesh.triangles.data(),
		GL_STATIC_DRAW);

    // Tell OpenGL that we will be using vertex attributes 0, 1 and 2.
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    // We tell OpenGL what each vertex looks like and how they are mapped to the shader (location = ...).
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, position));
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, normal));
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, texCoord));
    // Reuse all attributes for each instance
    glVertexAttribDivisor(0, 0);
    glVertexAttribDivisor(1, 0);
    glVertexAttribDivisor(2, 0);

    // Each triangle has 3 vertices.
    this->quadNumIndices = static_cast<GLsizei>(3 * this->quadMesh.triangles.size());
}

Scene::~Scene() {
	glDeleteBuffers(1, &this->tri_ubo);
	glDeleteBuffers(1, &this->bvh_ubo);

	glDeleteBuffers(1, &this->vbo);
	glDeleteBuffers(1, &this->ibo);
	glDeleteVertexArrays(1, &this->vao);
}

std::vector<int> Scene::addMesh(
		std::filesystem::path filePath,
		const glm::mat4 &transform,
		bool normalize) {

    if (!std::filesystem::exists(filePath)) {
        throw MeshLoadingException(fmt::format("File {} does not exist", filePath.string().c_str()));
	}

	std::vector<Mesh> subMeshes = loadMesh(filePath, { .normalizeVertexPositions = normalize });
	std::vector<int> meshIndices;

	for (auto& mesh : subMeshes) {
		this->meshes.emplace_back(std::move(mesh));
		this->meshTransforms.emplace_back(glm::mat4(1.0f));
		meshIndices.emplace_back((int) this->meshes.size() - 1);

		for(auto& triangle : mesh.triangles) {
			Triangle tri;
			tri.v0 = mesh.vertices[triangle.x];
			tri.v1 = mesh.vertices[triangle.y];
			tri.v2 = mesh.vertices[triangle.z];
			tri.mat = mesh.material;
		}
	}

	this->updateTriUBO();

	return meshIndices;
}

size_t Scene::buildBVH(int start, int end, int maxTris = 4) {
	std::span<Triangle> triangleSpan(this->triangles.data() + start, (unsigned) (end - start));

	glm::vec3 minBounds( std::numeric_limits<float>::max());
	glm::vec3 maxBounds(-std::numeric_limits<float>::max());
	for (const auto& tri : triangleSpan) {
		minBounds = 
			glm::min(minBounds, glm::min(tri.v0.position, glm::min(tri.v1.position, tri.v2.position)));
		maxBounds = 
			glm::max(maxBounds, glm::max(tri.v0.position, glm::max(tri.v1.position, tri.v2.position)));
	}

	BVHNode node;

	node.min = minBounds;
	node.max = maxBounds;

	if (end - start <= maxTris) {
		node.left = -1;
		node.right = -1;
		node.first = start;
		node.count = (end - start);

		this->bvh.push_back(node);

		return this->bvh.size() - 1;
	}

	/* TODO (optimisation): order the vertices in such a way that the bounding box overlap is minimal */
	int mid = (start + (end - start) / 2);

	this->bvh.push_back(node);
	size_t node_idx = this->bvh.size() - 1;

	size_t leftChild = buildBVH(start, mid, maxTris);
	size_t rightChild = buildBVH(mid, end, maxTris);

	this->bvh[node_idx].left = (int) leftChild;
	this->bvh[node_idx].right = (int) rightChild;

	return node_idx;
}

void Scene::buildBVH(int maxTris) {
	this->bvh.clear();
	size_t root = buildBVH(0, (int) this->triangles.size(), maxTris);

	this->updateBVHUBO();

	assert(root == 0);
}

void Scene::updateTriUBO() {
	glBindBuffer(GL_UNIFORM_BUFFER, this->tri_ubo);
	glBufferData(
		GL_UNIFORM_BUFFER,
		(GLsizeiptr) (this->triangles.size() * sizeof(Triangle)),
		this->triangles.data(),
		GL_STATIC_DRAW
	);

	glBindBuffer(GL_UNIFORM_BUFFER, 0);
}

void Scene::updateBVHUBO() {
	glBindBuffer(GL_UNIFORM_BUFFER, this->bvh_ubo);
	glBufferData(
		GL_UNIFORM_BUFFER,
		(GLsizeiptr) (this->bvh.size() * sizeof(BVHNode)),
		this->bvh.data(),
		GL_STATIC_DRAW
	);

	glBindBuffer(GL_UNIFORM_BUFFER, 0);
}

void Scene::draw(const Shader &shader) {

    //drawingShader.bindUniformBlock("Material", 0, m_uboMaterial);
	shader.bindUniformBlock("Triangles", 1, this->tri_ubo);
	shader.bindUniformBlock("BVH", 2, this->bvh_ubo);
    

    // Draw the quad mesh's triangles
    glBindVertexArray(this->vao);
    glDrawElements(GL_TRIANGLES, this->quadNumIndices, GL_UNSIGNED_INT, nullptr);
}
